#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
#include <storage/storage.h>
#include <hid_usage_keyboard.h>
#include <stdio.h>

#define KEYS_FILE APP_DATA_PATH(".bt_lights.keys")

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Bt* bt;
    FuriHalBleProfileBase* profile;
    FuriMutex* mutex;
    BtStatus status;
    char last[16];
    bool running;
} RcLights;

static const char* const labels[] = {"On", "Off", "Dim", "Bright", "Warm", "Red", "Green", "Blue"};
static const uint16_t keys[] = {
    HID_KEYBOARD_F13,
    HID_KEYBOARD_F14,
    HID_KEYBOARD_F15,
    HID_KEYBOARD_F16,
    HID_KEYBOARD_F17,
    HID_KEYBOARD_F18,
    HID_KEYBOARD_F19,
    HID_KEYBOARD_F20,
};

static void draw_cb(Canvas* canvas, void* ctx) {
    RcLights* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "RC Lights");
    canvas_set_font(canvas, FontSecondary);
    const char* st = app->status == BtStatusConnected ? "Connected" : "Advertising...";
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, st);
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, "Up On  Down Off");
    canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "L Dim  R Bright  OK Warm");
    canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "Hold: U Red D Green L Blue");
    if(app->last[0]) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, app->last);
    }
    furi_mutex_release(app->mutex);
}

static void send(RcLights* app, size_t index) {
    ble_profile_hid_kb_press(app->profile, keys[index]);
    ble_profile_hid_kb_release(app->profile, keys[index]);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    snprintf(app->last, sizeof(app->last), "%s", labels[index]);
    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    RcLights* app = ctx;
    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort) app->running = false;
    } else if(app->status == BtStatusConnected) {
        if(event->type == InputTypeShort) {
            switch(event->key) {
            case InputKeyUp:
                send(app, 0);
                break;
            case InputKeyDown:
                send(app, 1);
                break;
            case InputKeyLeft:
                send(app, 2);
                break;
            case InputKeyRight:
                send(app, 3);
                break;
            case InputKeyOk:
                send(app, 4);
                break;
            default:
                break;
            }
        } else if(event->type == InputTypeLong) {
            switch(event->key) {
            case InputKeyUp:
                send(app, 5);
                break;
            case InputKeyDown:
                send(app, 6);
                break;
            case InputKeyLeft:
                send(app, 7);
                break;
            default:
                break;
            }
        }
    }
    view_port_update(app->view_port);
}

static void status_cb(BtStatus status, void* ctx) {
    RcLights* app = ctx;
    app->status = status;
    view_port_update(app->view_port);
}

int32_t rc_lights_app(void* p) {
    UNUSED(p);
    RcLights* app = malloc(sizeof(RcLights));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->last[0] = '\0';
    app->running = true;
    app->status = BtStatusAdvertising;

    app->bt = furi_record_open(RECORD_BT);
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app->bt, KEYS_FILE);

    BleProfileHidParams params = {.device_name_prefix = "Lights", .mac_xor = 0x0002};
    app->profile = bt_profile_start(app->bt, ble_profile_hid, &params);
    furi_check(app->profile);
    bt_set_status_changed_callback(app->bt, status_cb, app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    while(app->running) {
        furi_delay_ms(50);
    }

    bt_set_status_changed_callback(app->bt, NULL, NULL);
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));
    furi_record_close(RECORD_BT);

    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
