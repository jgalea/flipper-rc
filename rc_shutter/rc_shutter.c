#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>
#include <storage/storage.h>
#include <hid_usage_consumer.h>
#include <stdio.h>

#define KEYS_FILE APP_DATA_PATH(".bt_shutter.keys")

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Bt* bt;
    FuriHalBleProfileBase* profile;
    FuriMutex* mutex;
    BtStatus status;
    uint32_t shots;
    bool running;
} RcShutter;

static void draw_cb(Canvas* canvas, void* ctx) {
    RcShutter* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    char buf[24];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "RC Shutter");
    canvas_set_font(canvas, FontSecondary);
    const char* st = app->status == BtStatusConnected ? "Connected" : "Advertising...";
    canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, st);
    snprintf(buf, sizeof(buf), "Shots: %lu", (unsigned long)app->shots);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignTop, "OK shutter  Up/Dn vol");
    furi_mutex_release(app->mutex);
}

static void tap(RcShutter* app, uint16_t key) {
    ble_profile_hid_consumer_key_press(app->profile, key);
    ble_profile_hid_consumer_key_release(app->profile, key);
}

static void input_cb(InputEvent* event, void* ctx) {
    RcShutter* app = ctx;
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyBack) {
            app->running = false;
        } else if(app->status == BtStatusConnected) {
            if(event->key == InputKeyOk) {
                tap(app, HID_CONSUMER_VOLUME_INCREMENT);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->shots++;
                furi_mutex_release(app->mutex);
            } else if(event->key == InputKeyUp) {
                tap(app, HID_CONSUMER_VOLUME_INCREMENT);
            } else if(event->key == InputKeyDown) {
                tap(app, HID_CONSUMER_VOLUME_DECREMENT);
            }
        }
    }
    view_port_update(app->view_port);
}

static void status_cb(BtStatus status, void* ctx) {
    RcShutter* app = ctx;
    app->status = status;
    view_port_update(app->view_port);
}

int32_t rc_shutter_app(void* p) {
    UNUSED(p);
    RcShutter* app = malloc(sizeof(RcShutter));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->shots = 0;
    app->running = true;
    app->status = BtStatusAdvertising;

    app->bt = furi_record_open(RECORD_BT);
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app->bt, KEYS_FILE);

    BleProfileHidParams params = {.device_name_prefix = "Shutter", .mac_xor = 0x0001};
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
