#include "flipsay.h"

#include <momentum/momentum.h>
#include <stdio.h>
#include <string.h>

void fs_io_ensure_dirs(Storage* st) {
    storage_simply_mkdir(st, EXT_PATH("apps_data"));
    storage_simply_mkdir(st, FS_DIR);
    storage_simply_mkdir(st, EXT_PATH("subghz"));
    storage_simply_mkdir(st, FS_SAVED_DIR);
}

void fs_apply_pins(FsApp* app) {
    momentum_settings.spi_cc1101_handle = app->cfg.pin_cc ? SpiExtra : SpiDefault;
    momentum_settings.spi_nrf24_handle = app->cfg.pin_nrf ? SpiExtra : SpiDefault;
}

void fs_restore_pins(FsApp* app) {
    momentum_settings.spi_cc1101_handle = app->orig_spi_cc ? SpiExtra : SpiDefault;
    momentum_settings.spi_nrf24_handle = app->orig_spi_nrf ? SpiExtra : SpiDefault;
}

void fs_cfg_defaults(FsConfig* c) {
    memset(c, 0, sizeof(FsConfig));
    c->magic = FS_CFG_MAGIC;
    c->version = 3;
    c->size = sizeof(FsConfig);
    c->source = FsSrcAuto;
    c->view = FsViewCombo;
    c->speed = 1;
    c->avg = 2;
    c->hold = 2;
    c->overlay = 1;
    c->grid = 1;
    c->alerts = 1;
    c->backlight = 1;
    c->otg = 1;
    c->scope_gap = 1;
    c->nrf_rate = 1;
    c->track = 1;
    c->step_idx = 2;
    c->wave = 1;
    c->notify = 1;
    c->dec_sens = 1;
    c->pg_mode = 0;
    c->mu_nrf_speed = 1;
    c->mu_layout = 2;
    c->mu_slot[0] = FsSlotExt;
    c->mu_slot[1] = FsSlotInt;
    c->mu_slot[2] = FsSlotNrfPkt;
    c->mu_slot[3] = FsSlotWifi;
    c->preset[0] = 4;
    c->center_khz[0] = 433920;
    c->span_khz[0] = 2000;
    c->lock_khz[0] = 433920;
    c->center_khz[1] = 2463500;
    c->span_khz[1] = 127000;
    c->lock_khz[1] = 2437000;
    c->int_center_khz = 868350;
    c->int_span_khz = 4000;
}

static void cfg_sanitize(FsConfig* c) {
    if(c->source >= FsSrcCount) c->source = FsSrcAuto;
    if(c->view >= FsViewCount) c->view = FsViewCombo;
    if(c->res > 1) c->res = 0;
    if(c->speed > 3) c->speed = 1;
    if(c->bw > 6) c->bw = 0;
    if(c->avg > 4) c->avg = 2;
    if(c->hold > 4) c->hold = 2;
    if(c->scale > 6) c->scale = 0;
    if(c->style > 3) c->style = 0;
    if(c->overlay > 3) c->overlay = 1;
    if(c->wf_pal > 3) c->wf_pal = 0;
    if(c->wf_rate > 3) c->wf_rate = 0;
    if(c->trig_act > 3) c->trig_act = 0;
    if(c->alerts > 3) c->alerts = 1;
    if(c->mod > 6) c->mod = 0;
    if(c->scope_gap > 4) c->scope_gap = 1;
    if(c->squelch > 15) c->squelch = 0;
    if(c->step_idx > 5) c->step_idx = 2;
    if(c->ch_page > 2) c->ch_page = 0;
    if(c->wave > 2) c->wave = 1;
    if(c->gain_lna > 8) c->gain_lna = 0;
    if(c->gain_dvga > 4) c->gain_dvga = 0;
    if(c->gain_tgt > 8) c->gain_tgt = 0;
    if(c->pin_cc > 1) c->pin_cc = 0;
    if(c->pin_nrf > 1) c->pin_nrf = 0;
    if(c->auto_decode > 1) c->auto_decode = 0;
    if(c->notify > 1) c->notify = 1;
    if(c->dec_sens > 2) c->dec_sens = 1;
    if(c->pg_mode > 1) c->pg_mode = 0;
    if(c->pg_combine > 1) c->pg_combine = 0;
    if(c->mu_nrf_speed > 2) c->mu_nrf_speed = 1;
    if(c->mu_layout > 4) c->mu_layout = 2;
    for(int i = 0; i < 4; i++)
        if(c->mu_slot[i] >= FsSlotCount) c->mu_slot[i] = FsSlotOff;
    if(c->wifi_mode > 3) c->wifi_mode = 0;
    if(c->wifi_view > 3) c->wifi_view = 0;
    if(c->tut_done > 1) c->tut_done = 0;
    if(c->lite > 1) c->lite = 0;
    if(c->intro > 1) c->intro = 0;
    if(c->rssi_off < -20 || c->rssi_off > 20) c->rssi_off = 0;
    if(c->off_int < -20 || c->off_int > 20) c->off_int = 0;
    if(c->off_ext < -20 || c->off_ext > 20) c->off_ext = 0;
    fs_geo_normalize(c, 0);
    fs_geo_normalize(c, 1);
    fs_geo_normalize_int(c);
    if(c->lock_khz[0] < 281000 || c->lock_khz[0] > 962000) c->lock_khz[0] = 433920;
    if(c->lock_khz[1] < 2400000 || c->lock_khz[1] > 2527000) c->lock_khz[1] = 2437000;
}

void fs_cfg_load(FsApp* app) {
    fs_cfg_defaults(&app->cfg);
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FS_SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FsConfig* tmp = malloc(sizeof(FsConfig));
        size_t r = storage_file_read(f, tmp, sizeof(FsConfig));
        if(r == sizeof(FsConfig) && tmp->magic == FS_CFG_MAGIC && tmp->size == sizeof(FsConfig) &&
           tmp->version == 3) {
            app->cfg = *tmp;
        }
        free(tmp);
        storage_file_close(f);
    }
    storage_file_free(f);
    cfg_sanitize(&app->cfg);
}

void fs_cfg_save(FsApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FS_SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, &app->cfg, sizeof(FsConfig));
        storage_file_close(f);
    }
    storage_file_free(f);
}

void fs_bm_load(FsApp* app) {
    memset(&app->bm, 0, sizeof(FsBookmarks));
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FS_BM_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FsBookmarks* tmp = malloc(sizeof(FsBookmarks));
        size_t r = storage_file_read(f, tmp, sizeof(FsBookmarks));
        if(r == sizeof(FsBookmarks) && tmp->magic == FS_CFG_MAGIC && tmp->count <= FS_MAX_BM) {
            app->bm = *tmp;
        }
        free(tmp);
        storage_file_close(f);
    }
    storage_file_free(f);
    app->bm.magic = FS_CFG_MAGIC;
}

void fs_bm_save(FsApp* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FS_BM_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, &app->bm, sizeof(FsBookmarks));
        storage_file_close(f);
    }
    storage_file_free(f);
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool fs_snapshot_write(FsApp* app) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char path[80];
    snprintf(
        path,
        sizeof(path),
        FS_DIR "/snap_%04u%02u%02u_%02u%02u%02u.bmp",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);

    uint8_t hdr[62];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    put32(&hdr[2], 62 + 1024);
    put32(&hdr[10], 62);
    put32(&hdr[14], 40);
    put32(&hdr[18], 128);
    put32(&hdr[22], 64);
    hdr[26] = 1;
    hdr[28] = 1;
    put32(&hdr[34], 1024);
    put32(&hdr[38], 2835);
    put32(&hdr[42], 2835);
    put32(&hdr[46], 2);
    put32(&hdr[50], 2);
    hdr[54] = 0x00;
    hdr[55] = 0x82;
    hdr[56] = 0xFF;

    uint8_t* img = malloc(1024);
    memset(img, 0, 1024);
    for(int y = 0; y < 64; y++) {
        int row = 63 - y;
        for(int x = 0; x < 128; x++) {
            if(app->snap_buf[(y >> 3) * 128 + x] & (1 << (y & 7))) {
                img[row * 16 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
            }
        }
    }

    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, hdr, sizeof(hdr));
        storage_file_write(f, img, 1024);
        storage_file_close(f);
        ok = true;
    }
    storage_file_free(f);
    free(img);
    return ok;
}

/* ---------- saved decodes ---------- */

void fs_saved_free(FsApp* app) {
    if(app->saved_names) {
        free(app->saved_names);
        app->saved_names = NULL;
    }
    app->saved_n = 0;
}

void fs_saved_load(FsApp* app) {
    fs_saved_free(app);
    app->saved_names = malloc(FS_SAVED_MAX * FS_SAVED_NAME);
    memset(app->saved_names, 0, FS_SAVED_MAX * FS_SAVED_NAME);
    storage_simply_mkdir(app->storage, FS_SAVED_DIR);
    File* d = storage_file_alloc(app->storage);
    if(storage_dir_open(d, FS_SAVED_DIR)) {
        FileInfo fi;
        char name[48];
        while(app->saved_n < FS_SAVED_MAX && storage_dir_read(d, &fi, name, sizeof(name))) {
            if(file_info_is_dir(&fi)) continue;
            if(strlen(name) >= FS_SAVED_NAME) continue;
            strncpy(app->saved_names[app->saved_n], name, FS_SAVED_NAME - 1);
            app->saved_n++;
        }
        storage_dir_close(d);
    }
    storage_file_free(d);
}

bool fs_saved_info(FsApp* app, const char* name) {
    char path[96];
    snprintf(path, sizeof(path), FS_SAVED_DIR "/%s", name);
    for(int i = 0; i < 4; i++) app->saved_info[i][0] = 0;
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* s = furi_string_alloc();
    bool ok = false;
    if(flipper_format_file_open_existing(ff, path)) {
        uint32_t freq = 0, bits = 0;
        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Protocol", s))
            snprintf(app->saved_info[0], 32, "PROTO %s", furi_string_get_cstr(s));
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Frequency", &freq, 1))
            snprintf(
                app->saved_info[1], 32, "FREQ %lu.%03lu MHZ", freq / 1000000, (freq / 1000) % 1000);
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Bit", &bits, 1))
            snprintf(app->saved_info[2], 32, "BITS %lu", bits);
        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Key", s)) {
            const char* k = furi_string_get_cstr(s);
            size_t n = strlen(k);
            const char* t = n > 26 ? k + (n - 26) : k;
            snprintf(app->saved_info[3], 32, "KEY %s", t);
        }
        ok = true;
    }
    furi_string_free(s);
    flipper_format_free(ff);
    return ok;
}

bool fs_saved_delete(FsApp* app, const char* name) {
    char path[96];
    snprintf(path, sizeof(path), FS_SAVED_DIR "/%s", name);
    return storage_common_remove(app->storage, path) == FSE_OK;
}

void fs_saved_delete_all(FsApp* app) {
    for(int i = 0; i < app->saved_n; i++) fs_saved_delete(app, app->saved_names[i]);
}

void fs_trace_reset(FsApp* app) {
    if(app->storage) storage_common_remove(app->storage, FS_DIR "/trace.log");
}

void fs_trace(FsApp* app, const char* msg) {
    if(!app->storage) return;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, FS_DIR "/trace.log", FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char b[72];
        int n = snprintf(b, sizeof(b), "%lu %s\n", furi_get_tick(), msg);
        storage_file_write(f, b, n);
        storage_file_close(f);
    }
    storage_file_free(f);
}