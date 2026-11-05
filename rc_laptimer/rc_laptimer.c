#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdio.h>

#define BEAM_PIN (&gpio_ext_pc3)
#define BEAM_GLITCH_MS 20
#define MIN_LAP_DEFAULT_MS 1000
#define MIN_LAP_STEP_MS 500
#define MIN_LAP_MIN_MS 500
#define MIN_LAP_MAX_MS 5000
#define RECENT_LAPS 3

typedef enum {
    EventKey,
    EventBeam,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
    uint32_t tick;
} AppEvent;

typedef enum {
    StateReady,
    StateRunning,
} SessionState;

typedef struct {
    FuriMutex* mutex;
    SessionState state;
    uint32_t lap_start;
    uint32_t min_lap_ms;
    uint32_t last_ms;
    uint32_t best_ms;
    uint32_t laps[RECENT_LAPS];
    uint16_t count;
    FuriMessageQueue* queue;
    uint32_t last_isr_tick;
} App;

static void format_time(char* buf, size_t size, uint32_t ms) {
    uint32_t minutes = ms / 60000;
    uint32_t rest = ms % 60000;
    if(minutes > 0) {
        snprintf(buf, size, "%lu:%02lu.%02lu", minutes, rest / 1000, (rest % 1000) / 10);
    } else {
        snprintf(buf, size, "%lu.%02lu", rest / 1000, (rest % 1000) / 10);
    }
}

static void beam_isr(void* ctx) {
    App* app = ctx;
    uint32_t now = furi_get_tick();
    if(now - app->last_isr_tick < BEAM_GLITCH_MS) return;
    app->last_isr_tick = now;
    AppEvent event = {.type = EventBeam, .tick = now};
    furi_message_queue_put(app->queue, &event, 0);
}

static void app_reset(App* app) {
    app->state = StateReady;
    app->last_ms = 0;
    app->best_ms = 0;
    app->count = 0;
    for(size_t i = 0; i < RECENT_LAPS; i++) app->laps[i] = 0;
}

static void app_trigger(App* app, uint32_t now) {
    if(app->state == StateReady) {
        app->state = StateRunning;
        app->lap_start = now;
        return;
    }
    uint32_t lap = now - app->lap_start;
    if(lap < app->min_lap_ms) return;
    app->lap_start = now;
    app->last_ms = lap;
    if(app->best_ms == 0 || lap < app->best_ms) app->best_ms = lap;
    app->count++;
    for(size_t i = RECENT_LAPS - 1; i > 0; i--) app->laps[i] = app->laps[i - 1];
    app->laps[0] = lap;
}

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    char a[16];
    char b[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    uint32_t current = app->state == StateRunning ? furi_get_tick() - app->lap_start : 0;
    format_time(a, sizeof(a), current);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 0, 20, a);

    canvas_set_font(canvas, FontSecondary);
    snprintf(b, sizeof(b), "Laps %u", app->count);
    canvas_draw_str_aligned(canvas, 127, 2, AlignRight, AlignTop, b);
    snprintf(b, sizeof(b), "min %lu.%lus", app->min_lap_ms / 1000, (app->min_lap_ms % 1000) / 100);
    canvas_draw_str_aligned(canvas, 127, 12, AlignRight, AlignTop, b);

    if(app->state == StateReady) {
        canvas_draw_str(canvas, 0, 34, "Waiting for first pass");
    } else {
        char l[16];
        char bs[16];
        format_time(l, sizeof(l), app->last_ms);
        format_time(bs, sizeof(bs), app->best_ms);
        snprintf(b, sizeof(b), "Last %s  Best %s", app->count ? l : "-", app->count ? bs : "-");
        canvas_draw_str(canvas, 0, 34, b);
    }

    canvas_draw_line(canvas, 0, 38, 127, 38);
    for(size_t i = 0; i < RECENT_LAPS; i++) {
        if(i < app->count) {
            format_time(a, sizeof(a), app->laps[i]);
            snprintf(b, sizeof(b), "#%u  %s", (unsigned)(app->count - i), a);
            canvas_draw_str(canvas, 0, 48 + i * 8, b);
        }
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input, void* ctx) {
    App* app = ctx;
    AppEvent event = {.type = EventKey, .input = *input};
    furi_message_queue_put(app->queue, &event, 0);
}

int32_t rc_laptimer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(16, sizeof(AppEvent));
    app->min_lap_ms = MIN_LAP_DEFAULT_MS;

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notifications, &sequence_display_backlight_enforce_on);

    furi_hal_gpio_init(BEAM_PIN, GpioModeInterruptFall, GpioPullUp, GpioSpeedLow);
    furi_hal_gpio_add_int_callback(BEAM_PIN, beam_isr, app);

    bool running = true;
    AppEvent event;
    while(running) {
        FuriStatus status = furi_message_queue_get(app->queue, &event, 50);
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(status == FuriStatusOk) {
            if(event.type == EventBeam) {
                app_trigger(app, event.tick);
            } else if(event.input.type == InputTypeShort || event.input.type == InputTypeRepeat) {
                switch(event.input.key) {
                case InputKeyOk:
                    if(event.input.type == InputTypeShort) app_trigger(app, furi_get_tick());
                    break;
                case InputKeyLeft:
                    if(app->min_lap_ms > MIN_LAP_MIN_MS) app->min_lap_ms -= MIN_LAP_STEP_MS;
                    break;
                case InputKeyRight:
                    if(app->min_lap_ms < MIN_LAP_MAX_MS) app->min_lap_ms += MIN_LAP_STEP_MS;
                    break;
                case InputKeyUp:
                    if(event.input.type == InputTypeShort) app_reset(app);
                    break;
                case InputKeyBack:
                    running = false;
                    break;
                default:
                    break;
                }
            }
        }
        furi_mutex_release(app->mutex);
        view_port_update(view_port);
    }

    furi_hal_gpio_remove_int_callback(BEAM_PIN);
    furi_hal_gpio_init(BEAM_PIN, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

    notification_message(notifications, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
