#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <lfrfid/lfrfid_worker.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/iso14443_3b/iso14443_3b_poller.h>
#include <nfc/protocols/iso15693_3/iso15693_3_poller.h>
#include <nfc/protocols/felica/felica_poller.h>
#include <nfc/protocols/st25tb/st25tb_poller.h>
#include <stdio.h>
#include <string.h>

#define ZONES 4
#define STEPS 4
#define KEY_LEN 56
#define PENALTY_MS 5000
#define DEDUP_MS 1500
#define FLASH_MS 1200
#define LF_SLICE_MS 300
#define NFC_SLICE_MS 300
#define NFC_POLL_MS 400

#define ZONES_FILE APP_DATA_PATH("zones.txt")
#define BEST_FILE APP_DATA_PATH("best.txt")

static const char* const challenges[] = {
    "Reverse into bay %c",
    "Parallel park at %c",
    "Nose-in park at %c",
    "Figure-8, then stop at %c",
    "Three-point turn, stop at %c",
    "Slalom through the cones, park at %c",
    "Reverse around the corner into %c",
    "Stop with front wheels on %c",
    "Drive in a full circle, then park at %c",
    "Reverse in a straight line to %c",
    "Back in at an angle to %c",
    "U-turn, then nose-in park at %c",
    "Weave between the cones, stop at %c",
    "Spin 180 degrees and park at %c",
};
#define CHALLENGE_COUNT (sizeof(challenges) / sizeof(challenges[0]))

typedef enum {
    ScreenMenu,
    ScreenLearn,
    ScreenPlay,
    ScreenResult,
} Screen;

typedef enum {
    EventInput,
    EventTick,
    EventTag,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
    char key[KEY_LEN];
} AppEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    FuriTimer* timer;
    NotificationApp* notifications;
    Storage* storage;

    FuriThread* scan_thread;
    volatile bool scan_run;
    volatile bool scan_enabled;

    Screen screen;
    uint8_t menu_sel;

    char zones[ZONES][KEY_LEN];
    uint8_t learn_idx;
    char learn_msg[24];
    uint32_t learn_msg_until;

    uint8_t step_challenge[STEPS];
    uint8_t step_zone[STEPS];
    uint8_t step;
    uint32_t start_tick;
    uint32_t end_ms;
    uint8_t misses;
    char miss_msg[24];
    uint32_t miss_until;
    uint32_t best_ms;
    uint32_t session_best_ms;
    bool new_best;

    char last_key[KEY_LEN];
    uint32_t last_tick;
} App;

typedef struct {
    volatile bool found;
    NfcProtocol protocol;
    volatile bool ready;
    NfcPoller* poller;
    NfcDevice* device;
} NfcCtx;

typedef struct {
    volatile ProtocolId id;
} LfCtx;

static bool zones_complete(const App* app) {
    for(uint8_t i = 0; i < ZONES; i++) {
        if(app->zones[i][0] == '\0') return false;
    }
    return true;
}

static void save_zones(App* app) {
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, ZONES_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* out = furi_string_alloc();
        for(uint8_t i = 0; i < ZONES; i++) {
            furi_string_cat_printf(out, "%c=%s\n", 'A' + i, app->zones[i]);
        }
        storage_file_write(file, furi_string_get_cstr(out), furi_string_size(out));
        furi_string_free(out);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void load_zones(App* app) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, ZONES_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[ZONES * (KEY_LEN + 4) + 1];
        size_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[n] = '\0';
        char* line = buf;
        while(line && *line) {
            char* next = strchr(line, '\n');
            if(next) *next++ = '\0';
            if(line[0] >= 'A' && line[0] < 'A' + ZONES && line[1] == '=') {
                strlcpy(app->zones[line[0] - 'A'], line + 2, KEY_LEN);
            }
            line = next;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void save_best(App* app) {
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, BEST_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%lu\n", (unsigned long)app->best_ms);
        storage_file_write(file, buf, len);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void load_best(App* app) {
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, BEST_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[16];
        size_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[n] = '\0';
        app->best_ms = strtoul(buf, NULL, 10);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void format_time(char* out, size_t size, uint32_t ms) {
    snprintf(out, size, "%lu.%lus", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
}

static void post_tag(App* app, const char* key) {
    AppEvent event = {.type = EventTag};
    strlcpy(event.key, key, KEY_LEN);
    furi_message_queue_put(app->queue, &event, 0);
}

static void lf_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    LfCtx* ctx = context;
    if(result == LFRFIDWorkerReadDone && protocol != PROTOCOL_NO) ctx->id = protocol;
}

static void scan_lf(App* app, LFRFIDWorker* worker, ProtocolDict* dict) {
    LfCtx ctx = {.id = PROTOCOL_NO};
    lfrfid_worker_read_start(worker, LFRFIDWorkerReadTypeAuto, lf_callback, &ctx);
    for(uint32_t t = 0; t < LF_SLICE_MS && ctx.id == PROTOCOL_NO && app->scan_run; t += 10) {
        furi_delay_ms(10);
    }
    lfrfid_worker_stop(worker);
    if(ctx.id == PROTOCOL_NO) return;

    size_t size = protocol_dict_get_data_size(dict, ctx.id);
    uint8_t data[32] = {0};
    if(size == 0 || size > sizeof(data)) return;
    protocol_dict_get_data(dict, ctx.id, data, size);

    char key[KEY_LEN];
    int len = snprintf(key, sizeof(key), "LF:%s:", protocol_dict_get_name(dict, ctx.id));
    for(size_t i = 0; i < size && len < (int)sizeof(key) - 3; i++) {
        len += snprintf(key + len, sizeof(key) - len, "%02X", data[i]);
    }
    post_tag(app, key);
}

static void nfc_scanner_callback(NfcScannerEvent event, void* context) {
    NfcCtx* ctx = context;
    if(event.type != NfcScannerEventTypeDetected || ctx->found) return;
    for(size_t i = 0; i < event.data.protocol_num; i++) {
        NfcProtocol p = event.data.protocols[i];
        if(nfc_protocol_get_parent(p) == NfcProtocolInvalid) {
            ctx->protocol = p;
            ctx->found = true;
            return;
        }
    }
}

static NfcCommand nfc_poller_callback(NfcGenericEvent event, void* context) {
    NfcCtx* ctx = context;
    bool ready = false;
    switch(event.protocol) {
    case NfcProtocolIso14443_3a:
        ready = ((Iso14443_3aPollerEvent*)event.event_data)->type == Iso14443_3aPollerEventTypeReady;
        break;
    case NfcProtocolIso14443_3b:
        ready = ((Iso14443_3bPollerEvent*)event.event_data)->type == Iso14443_3bPollerEventTypeReady;
        break;
    case NfcProtocolIso15693_3:
        ready = ((Iso15693_3PollerEvent*)event.event_data)->type == Iso15693_3PollerEventTypeReady;
        break;
    case NfcProtocolFelica:
        ready = ((FelicaPollerEvent*)event.event_data)->type == FelicaPollerEventTypeReady;
        break;
    case NfcProtocolSt25tb:
        ready = ((St25tbPollerEvent*)event.event_data)->type == St25tbPollerEventTypeReady;
        break;
    default:
        break;
    }
    if(!ready) return NfcCommandContinue;
    nfc_device_set_data(ctx->device, ctx->protocol, nfc_poller_get_data(ctx->poller));
    ctx->ready = true;
    return NfcCommandStop;
}

static void scan_nfc(App* app, Nfc* nfc, NfcScanner* scanner, NfcDevice* device) {
    NfcCtx ctx = {.found = false, .protocol = NfcProtocolInvalid, .device = device};
    nfc_scanner_start(scanner, nfc_scanner_callback, &ctx);
    for(uint32_t t = 0; t < NFC_SLICE_MS && !ctx.found && app->scan_run; t += 10) {
        furi_delay_ms(10);
    }
    nfc_scanner_stop(scanner);
    if(!ctx.found) return;

    ctx.poller = nfc_poller_alloc(nfc, ctx.protocol);
    nfc_poller_start(ctx.poller, nfc_poller_callback, &ctx);
    for(uint32_t t = 0; t < NFC_POLL_MS && !ctx.ready && app->scan_run; t += 10) {
        furi_delay_ms(10);
    }
    nfc_poller_stop(ctx.poller);
    nfc_poller_free(ctx.poller);
    if(!ctx.ready) return;

    size_t uid_len = 0;
    const uint8_t* uid = nfc_device_get_uid(device, &uid_len);
    if(!uid || uid_len == 0) return;

    char key[KEY_LEN];
    int len = snprintf(key, sizeof(key), "NFC:");
    for(size_t i = 0; i < uid_len && len < (int)sizeof(key) - 3; i++) {
        len += snprintf(key + len, sizeof(key) - len, "%02X", uid[i]);
    }
    post_tag(app, key);
}

static int32_t scan_thread_fn(void* context) {
    App* app = context;
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    LFRFIDWorker* worker = lfrfid_worker_alloc(dict);
    lfrfid_worker_start_thread(worker);
    Nfc* nfc = nfc_alloc();
    NfcScanner* scanner = nfc_scanner_alloc(nfc);
    NfcDevice* device = nfc_device_alloc();

    while(app->scan_run) {
        if(!app->scan_enabled) {
            furi_delay_ms(50);
            continue;
        }
        scan_lf(app, worker, dict);
        if(!app->scan_run || !app->scan_enabled) continue;
        scan_nfc(app, nfc, scanner, device);
    }

    nfc_device_free(device);
    nfc_scanner_free(scanner);
    nfc_free(nfc);
    lfrfid_worker_stop_thread(worker);
    lfrfid_worker_free(worker);
    protocol_dict_free(dict);
    return 0;
}

static void start_game(App* app) {
    uint8_t used[STEPS];
    for(uint8_t i = 0; i < STEPS; i++) {
        uint8_t c;
        bool dup;
        do {
            c = furi_hal_random_get() % CHALLENGE_COUNT;
            dup = false;
            for(uint8_t j = 0; j < i; j++) dup |= (used[j] == c);
        } while(dup);
        used[i] = c;
        app->step_challenge[i] = c;

        uint8_t z;
        do {
            z = furi_hal_random_get() % ZONES;
        } while(i > 0 && z == app->step_zone[i - 1]);
        app->step_zone[i] = z;
    }
    app->step = 0;
    app->misses = 0;
    app->miss_until = 0;
    app->start_tick = furi_get_tick();
    app->screen = ScreenPlay;
    app->scan_enabled = true;
}

static void finish_game(App* app) {
    app->scan_enabled = false;
    uint32_t elapsed = furi_get_tick() - app->start_tick;
    app->end_ms = elapsed + (uint32_t)app->misses * PENALTY_MS;
    if(app->session_best_ms == 0 || app->end_ms < app->session_best_ms) {
        app->session_best_ms = app->end_ms;
    }
    app->new_best = false;
    if(app->best_ms == 0 || app->end_ms < app->best_ms) {
        app->best_ms = app->end_ms;
        app->new_best = true;
        save_best(app);
    }
    app->screen = ScreenResult;
}

static int find_zone(const App* app, const char* key) {
    for(uint8_t i = 0; i < ZONES; i++) {
        if(strcmp(app->zones[i], key) == 0) return i;
    }
    return -1;
}

static void handle_tag(App* app, const char* key) {
    uint32_t now = furi_get_tick();
    if(strcmp(key, app->last_key) == 0 && now - app->last_tick < DEDUP_MS) {
        app->last_tick = now;
        return;
    }
    strlcpy(app->last_key, key, KEY_LEN);
    app->last_tick = now;

    if(app->screen == ScreenLearn) {
        int existing = find_zone(app, key);
        if(existing >= 0 && existing != app->learn_idx) {
            snprintf(app->learn_msg, sizeof(app->learn_msg), "Already zone %c", 'A' + existing);
            app->learn_msg_until = now + 2000;
            notification_message(app->notifications, &sequence_error);
            return;
        }
        strlcpy(app->zones[app->learn_idx], key, KEY_LEN);
        notification_message(app->notifications, &sequence_success);
        app->learn_idx++;
        app->learn_msg[0] = '\0';
        if(app->learn_idx >= ZONES) {
            save_zones(app);
            app->scan_enabled = false;
            app->screen = ScreenMenu;
        }
    } else if(app->screen == ScreenPlay) {
        int zone = find_zone(app, key);
        if(zone == app->step_zone[app->step]) {
            notification_message(app->notifications, &sequence_success);
            app->step++;
            if(app->step >= STEPS) finish_game(app);
        } else {
            app->misses++;
            if(zone >= 0) {
                snprintf(app->miss_msg, sizeof(app->miss_msg), "Miss: that is zone %c", 'A' + zone);
            } else {
                snprintf(app->miss_msg, sizeof(app->miss_msg), "Miss: unknown tag");
            }
            app->miss_until = now + FLASH_MS;
            notification_message(app->notifications, &sequence_error);
        }
    }
}

static void draw_wrapped(Canvas* canvas, const char* text, int x, int y, int cols, int max_lines) {
    char line[32];
    size_t len = strlen(text);
    size_t pos = 0;
    for(int row = 0; row < max_lines && pos < len; row++) {
        size_t end = pos + cols;
        if(end >= len) {
            end = len;
        } else {
            size_t brk = end;
            while(brk > pos && text[brk] != ' ') brk--;
            if(brk > pos) end = brk;
        }
        size_t n = end - pos;
        if(n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, text + pos, n);
        line[n] = '\0';
        canvas_draw_str(canvas, x, y + row * 11, line);
        pos = end;
        while(pos < len && text[pos] == ' ') pos++;
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    App* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    char buf[64];
    uint32_t now = furi_get_tick();

    if(app->screen == ScreenMenu) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 11, "RC Parking");
        canvas_set_font(canvas, FontSecondary);
        if(app->best_ms) {
            char t[16];
            format_time(t, sizeof(t), app->best_ms);
            snprintf(buf, sizeof(buf), "Best %s", t);
            canvas_draw_str_aligned(canvas, 126, 3, AlignRight, AlignTop, buf);
        }
        const char* items[2] = {"Play", "Learn zone tags"};
        for(uint8_t i = 0; i < 2; i++) {
            int y = 22 + i * 14;
            if(i == app->menu_sel) {
                canvas_draw_box(canvas, 0, y - 1, 128, 13);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 6, y + 9, items[i]);
            canvas_set_color(canvas, ColorBlack);
        }
        if(!zones_complete(app)) {
            canvas_draw_str(canvas, 2, 62, "Learn all 4 zones to play");
        } else {
            canvas_draw_str(canvas, 2, 62, "Zones A-D ready");
        }
    } else if(app->screen == ScreenLearn) {
        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "Learn zone %c", 'A' + app->learn_idx);
        canvas_draw_str(canvas, 2, 11, buf);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 26, "Hold the tag against the");
        canvas_draw_str(canvas, 2, 36, "Flipper back (LF or NFC)");
        if(app->learn_msg[0] && now < app->learn_msg_until) {
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, 2, 52, app->learn_msg);
        } else {
            snprintf(buf, sizeof(buf), "Learned %d of %d", app->learn_idx, ZONES);
            canvas_draw_str(canvas, 2, 52, buf);
        }
    } else if(app->screen == ScreenPlay) {
        uint32_t elapsed = now - app->start_tick + (uint32_t)app->misses * PENALTY_MS;
        char t[16];
        format_time(t, sizeof(t), elapsed);
        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "Step %d/%d", app->step + 1, STEPS);
        canvas_draw_str(canvas, 2, 10, buf);
        canvas_draw_str_aligned(canvas, 126, 0, AlignRight, AlignTop, t);
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "Misses: %d", app->misses);
        canvas_draw_str(canvas, 2, 20, buf);

        char text[64];
        snprintf(
            text,
            sizeof(text),
            challenges[app->step_challenge[app->step]],
            'A' + app->step_zone[app->step]);
        draw_wrapped(canvas, text, 2, 31, 21, 3);

        if(now < app->miss_until) {
            canvas_draw_box(canvas, 0, 52, 128, 12);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, app->miss_msg);
            canvas_set_color(canvas, ColorBlack);
        } else {
            snprintf(buf, sizeof(buf), "Scan zone %c", 'A' + app->step_zone[app->step]);
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, buf);
        }
    } else {
        char t[16];
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 11, app->new_best ? "Done - new best!" : "Done");
        canvas_set_font(canvas, FontSecondary);
        uint32_t base = app->end_ms - (uint32_t)app->misses * PENALTY_MS;
        format_time(t, sizeof(t), base);
        snprintf(buf, sizeof(buf), "Time: %s", t);
        canvas_draw_str(canvas, 2, 24, buf);
        snprintf(
            buf, sizeof(buf), "Penalties: %d x %ds", app->misses, PENALTY_MS / 1000);
        canvas_draw_str(canvas, 2, 34, buf);
        format_time(t, sizeof(t), app->end_ms);
        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "Total: %s", t);
        canvas_draw_str(canvas, 2, 47, buf);
        canvas_set_font(canvas, FontSecondary);
        format_time(t, sizeof(t), app->session_best_ms);
        char t2[16];
        format_time(t2, sizeof(t2), app->best_ms);
        snprintf(buf, sizeof(buf), "Session %s  All-time %s", t, t2);
        canvas_draw_str(canvas, 2, 57, buf);
        canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, "OK: again");
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input, void* context) {
    App* app = context;
    AppEvent event = {.type = EventInput, .input = *input};
    furi_message_queue_put(app->queue, &event, 0);
}

static void tick_callback(void* context) {
    App* app = context;
    AppEvent event = {.type = EventTick};
    furi_message_queue_put(app->queue, &event, 0);
}

static bool handle_input(App* app, const InputEvent* input) {
    if(input->type != InputTypeShort) return true;
    if(input->key == InputKeyBack) {
        if(app->screen == ScreenMenu) return false;
        app->scan_enabled = false;
        app->screen = ScreenMenu;
        return true;
    }
    if(app->screen == ScreenMenu) {
        if(input->key == InputKeyUp || input->key == InputKeyDown) {
            app->menu_sel = !app->menu_sel;
        } else if(input->key == InputKeyOk) {
            if(app->menu_sel == 0) {
                if(zones_complete(app)) start_game(app);
            } else {
                for(uint8_t i = 0; i < ZONES; i++) app->zones[i][0] = '\0';
                app->learn_idx = 0;
                app->learn_msg[0] = '\0';
                app->screen = ScreenLearn;
                app->scan_enabled = true;
            }
        }
    } else if(app->screen == ScreenResult && input->key == InputKeyOk) {
        start_game(app);
    }
    return true;
}

int32_t rc_parking_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    load_zones(app);
    load_best(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->scan_run = true;
    app->scan_enabled = false;
    app->scan_thread = furi_thread_alloc_ex("RcParkingScan", 4096, scan_thread_fn, app);
    furi_thread_start(app->scan_thread);

    app->timer = furi_timer_alloc(tick_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, 100);

    AppEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event.type == EventInput) {
            running = handle_input(app, &event.input);
        } else if(event.type == EventTag) {
            handle_tag(app, event.key);
        }
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    app->scan_enabled = false;
    app->scan_run = false;
    furi_thread_join(app->scan_thread);
    furi_thread_free(app->scan_thread);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
