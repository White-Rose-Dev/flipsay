#include "flipsay.h"

#include <momentum/momentum.h>
#include <string.h>

#define TAG "FlipSay"

static void input_callback(InputEvent* event, void* ctx) {
    FsApp* app = ctx;
    FsEvent ev = {.type = FsEvInput, .input = *event};
    furi_message_queue_put(app->queue, &ev, 0);
}

static void boot_step(FsApp* app, int pct, const char* msg) {
    app->boot_pct = (uint8_t)pct;
    app->boot_msg = msg;
    fs_trace(app, msg);
    view_port_update(app->vp);
    furi_delay_ms(24);
}

int32_t flipsay_app(void* p) {
    UNUSED(p);

    FsApp* app = malloc(sizeof(FsApp));
    memset(app, 0, sizeof(FsApp));
    app->main_id = furi_thread_get_current_id();

    app->mtx = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(16, sizeof(FsEvent));
    fs_ui_init(app);
    app->boot_msg = "STARTING";
    app->boot_pct = 2;

    app->vp = view_port_alloc();
    view_port_draw_callback_set(app->vp, fs_ui_draw, app);
    view_port_input_callback_set(app->vp, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->vp, GuiLayerFullscreen);

    boot_step(app, 6, "MOUNTING STORAGE");
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notif = furi_record_open(RECORD_NOTIFICATION);
    fs_io_ensure_dirs(app->storage);
    fs_trace_reset(app);

    boot_step(app, 14, "LOADING SETTINGS");
    fs_cfg_load(app);

    boot_step(app, 20, "LOADING BOOKMARKS");
    fs_bm_load(app);

    boot_step(app, 28, "ALLOCATING BUFFERS");
    if(!app->cfg.lite && memmgr_get_free_heap() < 48 * 1024) {
        app->cfg.lite = 1;
        FURI_LOG_W(TAG, "low heap, lite mode forced");
    }
    app->ch[0] = fs_chan_alloc(app->cfg.lite ? 24 : FS_WF_ROWS);
    fs_geo_compute(
        &app->cfg,
        app->cfg.source == FsSrcNrf ? FsRadioNrf : FsRadioNone,
        0,
        false,
        &app->ch[0]->geo);
    fs_ui_set_view(app, (FsView)app->cfg.view);

    boot_step(app, 34, "CONFIGURING PINS");
    app->orig_spi_cc = momentum_settings.spi_cc1101_handle == SpiExtra ? 1 : 0;
    app->orig_spi_nrf = momentum_settings.spi_nrf24_handle == SpiExtra ? 1 : 0;
    if(!app->cfg.pad[1]) {
        app->cfg.pin_cc = app->orig_spi_cc;
        app->cfg.pin_nrf = app->orig_spi_nrf;
        app->cfg.pad[1] = 1;
    }
    fs_apply_pins(app);

    boot_step(app, 42, "LOADING RADIO DRIVERS");
    subghz_devices_init();

    if(app->cfg.backlight) {
        notification_message(app->notif, &sequence_display_backlight_enforce_on);
    }

    boot_step(app, 50, "STARTING RADIO SERVICE");
    app->running = true;
    fs_worker_start(app);

    FsEvent ev;
    while(app->running) {
        FuriStatus s = furi_message_queue_get(app->queue, &ev, 33);
        if(s == FuriStatusOk && ev.type == FsEvInput) {
            if(!fs_ui_input(app, &ev.input)) break;
        }
        if(fs_ui_tick(app)) view_port_update(app->vp);
    }

    FURI_LOG_I(TAG, "exit requested");
    app->ui.mode = FsUiExiting;
    app->ui.exit_tick = furi_get_tick();
    view_port_update(app->vp);
    furi_delay_ms(340);

    gui_remove_view_port(app->gui, app->vp);
    view_port_free(app->vp);
    furi_record_close(RECORD_GUI);

    FURI_LOG_I(TAG, "exit: stopping radio worker");
    fs_worker_stop(app);
    FURI_LOG_I(TAG, "exit: worker stopped");
    subghz_devices_deinit();

    fs_restore_pins(app);
    fs_cfg_save(app);

    notification_message(app->notif, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);

    fs_saved_free(app);
    if(app->scope) free(app->scope);
    if(app->snap_buf) free(app->snap_buf);
    fs_chan_free(app->ch[0]);
    fs_chan_free(app->ch[1]);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mtx);
    free(app);
    FURI_LOG_I(TAG, "exit: done");
    return 0;
}
