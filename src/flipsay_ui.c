#include "flipsay.h"

#include <stdio.h>
#include <string.h>

#define PL_Y0      9
#define PL_Y1      55
#define SPLASH_OUT 380
#define FADE_IN    380

const char* const fs_view_names[] = {
    "SPECTRUM",
    "WATERFALL",
    "COMBO",
    "PERSIST",
    "SCOPE",
    "S-METER",
    "SIGNALS",
    "CHANNELS",
};

static const char* const view_tags[] =
    {"SPEC", "WFALL", "COMBO", "PERS", "SCOPE", "METER", "SIGS", "CHAN"};

typedef struct {
    const char* name;
    uint32_t center;
    uint32_t span;
} Preset;

static const Preset presets0[] = {
    {"315.00", 315000, 2000},
    {"318.00", 318000, 2000},
    {"345.00", 345000, 2000},
    {"390.00", 390000, 2000},
    {"433.92", 433920, 2000},
    {"433 WIDE", 433920, 8000},
    {"868.35", 868350, 4000},
    {"915.00", 915000, 6000},
    {"300-348", 324000, 48000},
    {"387-464", 425500, 77000},
    {"779-928", 853500, 149000},
    {"FULL 281-962", 621500, 681000},
};

static const Preset presets1[] = {
    {"2.4G FULL", 2463500, 127000},
    {"WIFI CH1", 2412000, 24000},
    {"WIFI CH6", 2437000, 24000},
    {"WIFI CH11", 2462000, 24000},
    {"BLE ADV", 2441000, 80000},
    {"ZIGBEE", 2440000, 80000},
    {"2400-2440", 2420000, 40000},
    {"2440-2480", 2460000, 40000},
};

#define N_PRESETS0 (sizeof(presets0) / sizeof(presets0[0]))
#define N_PRESETS1 (sizeof(presets1) / sizeof(presets1[0]))

static const uint32_t step_tab[6] = {1, 5, 10, 25, 100, 1000};

typedef enum {
    M_SOURCE,
    M_PRESET,
    M_PIN_CC,
    M_PIN_NRF,
    M_PINS_VIEW,
    M_SPEED,
    M_RES,
    M_BW,
    M_RSSIOFF,
    M_OTG,
    M_GAIN_LNA,
    M_GAIN_MIX,
    M_GAIN_IF,
    M_GAIN_RESET,
    M_VIEW,
    M_WAVE,
    M_SCALE,
    M_STYLE,
    M_OVERLAY,
    M_GRID,
    M_WFPAL,
    M_WFRATE,
    M_CURSOR,
    M_AVG,
    M_HOLD,
    M_BACKLIGHT,
    M_SQUELCH,
    M_TRIGACT,
    M_ALERTS,
    M_MOD,
    M_GAP,
    M_AUTODEC,
    M_NOTIFY,
    M_DECSENS,
    M_SAVED,
    M_DECSTAT,
    M_PG_ENABLE,
    M_PG_MODE,
    M_PG_COMBINE,
    M_OFF_INT,
    M_OFF_EXT,
    M_MU_FOCUS,
    M_MU_EXT,
    M_MU_INT,
    M_MU_NRF,
    M_MU_CLEAR,
    M_MU_LAYOUT,
    M_MU_S0,
    M_MU_S1,
    M_MU_S2,
    M_MU_S3,
    M_MU_NRFBAND,
    M_WIFI_MODE,
    M_SYS_MEM,
    M_SYS_STACK,
    M_LITE,
    M_INTRO,
    M_REPLAY,
    M_FREEZE,
    M_RESET,
    M_CURPEAK,
    M_MARKB,
    M_BMADD,
    M_BMLIST,
    M_LOG,
    M_SNAP,
    M_CAPTURE,
    M_SAVE,
    M_COUNT,
} MenuId;

static const char* const menu_labels[M_COUNT] = {
    "SOURCE",      "PRESET",     "CC1101 CS",  "NRF24 CS",   "PIN LAYOUT",  "SWEEP SPEED",
    "RESOLUTION",  "RX BW",      "RSSI OFFSET", "5V OTG",    "LNA GAIN",    "MIXER GAIN",
    "IF TARGET",   "GAIN RESET", "VIEW",       "WAVE SIZE",  "SCALE",       "TRACE",
    "OVERLAYS",    "GRID",       "WF PALETTE", "WF RATE",    "CURSOR",      "AVERAGING",
    "PEAK HOLD",   "BACKLIGHT",  "SQUELCH",    "ON TRIGGER", "ALERTS",      "MODULATION",
    "SCOPE GAP",   "AUTO DECODE", "DECODE NOTIFY", "DECODE SENS", "SAVED DECODES", "DECODED",
    "PARALLEL GATHER", "PG MODE", "PG COMBINE", "INT OFFSET", "EXT OFFSET", "FOCUS WINDOW",
    "EXT BAND",    "INT BAND",   "NRF SCAN",   "CLEAR PACKETS", "LAYOUT", "WINDOW 1", "WINDOW 2", "WINDOW 3", "WINDOW 4", "NRF BAND", "WIFI MODE", "MEM FREE/MAX", "STACK FREE",
    "LITE MODE",   "INTRO",    "REPLAY INTRO", "FREEZE",  "RESET HOLD",
    "CURSOR>PEAK", "MARKER B",   "ADD BOOKMARK", "BOOKMARKS", "LOG CSV",    "SNAPSHOT",
    "CAPTURE .SUB", "SAVE SETTINGS",
};

enum {
    P_RADIO,
    P_GAIN,
    P_DISPLAY,
    P_ANALYSIS,
    P_DECODE,
    P_PG,
    P_MULTI,
    P_SYSTEM,
    P_TOOLS,
    P_COUNT,
};

#define ROOT_SDR   P_COUNT
#define ROOT_TTX   (P_COUNT + 1)
#define ROOT_HELP  (P_COUNT + 2)
#define ROOT_ABOUT (P_COUNT + 3)
#define ROOT_EXIT  (P_COUNT + 4)
#define ROOT_COUNT (P_COUNT + 5)
#define PAGE_ROOT  0xFF

static const char* const page_names[P_COUNT] = {
    "RADIO & PINS",
    "GAIN",
    "DISPLAY",
    "ANALYSIS",
    "AUTO DECODE",
    "PARALLEL GATHER",
    "MULTI USE",
    "SYSTEM",
    "TOOLS & FILES",
};

static const uint8_t page_items[P_COUNT][12] = {
    {M_SOURCE, M_PRESET, M_PIN_CC, M_PIN_NRF, M_PINS_VIEW, M_SPEED, M_RES, M_BW, M_RSSIOFF, M_OTG},
    {M_GAIN_LNA, M_GAIN_MIX, M_GAIN_IF, M_GAIN_RESET},
    {M_VIEW, M_WAVE, M_SCALE, M_STYLE, M_OVERLAY, M_GRID, M_WFPAL, M_WFRATE, M_CURSOR, M_AVG, M_HOLD, M_BACKLIGHT},
    {M_SQUELCH, M_TRIGACT, M_ALERTS, M_MOD, M_GAP},
    {M_AUTODEC, M_NOTIFY, M_DECSENS, M_SAVED, M_DECSTAT},
    {M_PG_ENABLE, M_PG_MODE, M_PG_COMBINE, M_OFF_INT, M_OFF_EXT},
    {M_MU_LAYOUT, M_MU_S0, M_MU_S1, M_MU_S2, M_MU_S3, M_MU_EXT, M_MU_INT, M_MU_NRFBAND, M_MU_NRF, M_WIFI_MODE, M_MU_CLEAR, M_MU_FOCUS},
    {M_SYS_MEM, M_SYS_STACK, M_LITE, M_INTRO, M_REPLAY},
    {M_FREEZE, M_RESET, M_CURPEAK, M_MARKB, M_BMADD, M_BMLIST, M_LOG, M_SNAP, M_CAPTURE, M_SAVE},
};

static const uint8_t page_count[P_COUNT] = {10, 4, 12, 5, 5, 5, 12, 5, 10};

static const char* const src_names[] = {"AUTO", "INT CC1101", "EXT CC1101", "NRF24"};
static const char* const speed_names[] = {"FAST", "NORMAL", "SLOW", "PRECISE"};
static const char* const bw_names[] = {"AUTO", "58K", "102K", "203K", "325K", "464K", "812K"};
static const char* const avg_names[] = {"OFF", "2", "4", "8", "16"};
static const char* const hold_names[] = {"OFF", "SLOW", "MEDIUM", "FAST", "INFINITE"};
static const char* const scale_names[] =
    {"AUTO", "-20/100", "-30/80", "-40/70", "-50/60", "-60/50", "-70/40"};
static const char* const style_names[] = {"HATCH", "SOLID", "LINE", "GRADIENT"};
static const char* const overlay_names[] = {"NONE", "NOISE", "AVG", "NOISE+AVG"};
static const char* const pal_names[] = {"DITHER", "INVERTED", "CONTOUR", "THRESHOLD"};
static const char* const wfrate_names[] = {"1X", "1/2", "1/4", "1/8"};
static const char* const trig_names[] = {"OFF", "LOG EVENT", "SNAPSHOT", "CAPTURE SUB"};
static const char* const alert_names[] = {"NONE", "LED", "VIBRO", "LED+VIBRO"};
static const char* const mod_names[] =
    {"OOK 650", "OOK 270", "2FSK 2.4K", "2FSK 12K", "2FSK 47K", "GFSK 10K", "MSK 100K"};
static const char* const gap_names[] = {"1 MS", "2 MS", "4 MS", "8 MS", "16 MS"};
static const char* const hint_names[] = {"-", "1:2 MANCH?", "1:3 PWM?", "MULTI-T", "NOISY"};
static const char* const wave_names[] = {"NORMAL", "BIG", "HUGE"};
static const char* const lna_names[] =
    {"AUTO", "MAX", "-2.6DB", "-6.1DB", "-7.4DB", "-9.2DB", "-11.5DB", "-14.6DB", "-17.1DB"};
static const char* const mix_names[] = {"AUTO", "MAX", "-1 STEP", "-2 STEP", "-3 STEP"};
static const char* const if_names[] =
    {"AUTO", "24DB", "27DB", "30DB", "33DB", "36DB", "38DB", "40DB", "42DB"};
static const char* const sens_names[] = {"HIGH", "MEDIUM", "LOW"};
static const char* const nrfscan_names[] = {"SLOW", "MEDIUM", "FAST"};
static const char* const layout_names[] = {"LEFT|RIGHT", "TOP|BOTTOM", "2 TOP+1", "1 TOP+2", "4 GRID"};
static const char* const slot_names[] = {"OFF", "EXT CC", "INT CC", "NRF SPEC", "NRF PKTS", "WIFI"};
static const char* const wifimode_names[] = {"SCAN AP", "BEACON", "DEAUTH", "PMKID"};
static const char* const slot_pos[5][4] = {
    {"LEFT", "RIGHT", "-", "-"},
    {"TOP", "BOTTOM", "-", "-"},
    {"TOP LEFT", "TOP RIGHT", "BOTTOM", "-"},
    {"TOP", "BOT LEFT", "BOT RIGHT", "-"},
    {"TOP LEFT", "TOP RIGHT", "BOT LEFT", "BOT RIGHT"},
};

int fs_family(const FsApp* app) {
    if(app->radio == FsRadioNrf) return 1;
    if(app->radio == FsRadioNone && !app->pg_on && app->cfg.source == FsSrcNrf) return 1;
    return 0;
}

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void cycle_u8(uint8_t* v, int count, int dir) {
    int n = ((int)*v + dir + count) % count;
    *v = (uint8_t)n;
}

void fs_ui_toast(FsApp* app, const char* msg) {
    snprintf(app->ui.toast, sizeof(app->ui.toast), "%s", msg);
    app->ui.toast_until = furi_get_tick() + 1800;
    app->ui.dirty = true;
}

static void notify_set(FsApp* app, uint8_t kind, const char* title, const char* text) {
    snprintf(app->nt_title, sizeof(app->nt_title), "%s", title);
    snprintf(app->nt_text, sizeof(app->nt_text), "%s", text);
    app->nt_kind = kind;
    app->nt_start = furi_get_tick();
    app->nt_event = true;
}

void fs_notify(FsApp* app, uint8_t kind, const char* title, const char* text) {
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    notify_set(app, kind, title, text);
    furi_mutex_release(app->mtx);
}

void fs_ui_init(FsApp* app) {
    memset(&app->ui, 0, sizeof(FsUi));
    app->ui.mode = FsUiSplash;
    app->ui.mode_tick = furi_get_tick();
    app->ui.cursor = 64;
    app->ui.menu_page = PAGE_ROOT;
}

static void heat_sync(FsApp* app) {
    FsChan* ch = app->ch[0];
    if(!ch) return;
    bool need = app->cfg.view == FsViewPersist && !app->cfg.lite;
    if(need && !ch->heat) {
        if(memmgr_get_free_heap() > 24 * 1024) {
            ch->heat = malloc(FS_BINS * FS_PLOT_H);
            memset(ch->heat, 0, FS_BINS * FS_PLOT_H);
        }
    } else if(!need && ch->heat) {
        free(ch->heat);
        ch->heat = NULL;
    }
}

void fs_ui_set_view(FsApp* app, FsView v) {
    FsView old = (FsView)app->cfg.view;
    int fam = fs_family(app);
    FsChan* ch = app->ch[0];
    if(ch && (v == FsViewMeter || v == FsViewScope) && old != FsViewMeter && old != FsViewScope) {
        uint32_t f = fs_col_khz(&ch->geo, app->ui.cursor);
        if(fam) {
            f = (uint32_t)clampi((int)((f + 500) / 1000 * 1000), 2400000, 2527000);
            app->cfg.lock_khz[1] = f;
        } else {
            f = (uint32_t)clampi((int)f, 281000, 962000);
            app->cfg.lock_khz[0] = f;
        }
    }
    app->cfg.view = (uint8_t)v;
    if(v == FsViewScope && !app->scope) {
        if(memmgr_get_free_heap() > 16 * 1024) {
            app->scope = malloc(FS_SCOPE_MAX * sizeof(FsPulse));
            memset(app->scope, 0, FS_SCOPE_MAX * sizeof(FsPulse));
        }
    } else if(v != FsViewScope && app->scope) {
        free(app->scope);
        app->scope = NULL;
        app->scope_n = 0;
    }
    app->worker_mode = (v == FsViewScope) ? FsModeScope :
                       (v == FsViewMeter) ? FsModeLock :
                                            FsModeSweep;
    app->ui.sig_sel = 0;
    heat_sync(app);
    app->ui.dirty = true;
}

static void apply_preset_fam(FsApp* app, int fam) {
    const Preset* p = fam ? &presets1[app->cfg.preset[1] % N_PRESETS1] :
                            &presets0[app->cfg.preset[0] % N_PRESETS0];
    app->cfg.center_khz[fam] = p->center;
    app->cfg.span_khz[fam] = p->span;
    fs_geo_normalize(&app->cfg, fam);
    app->ui.cursor = 64;
    app->ui.marker_b_khz = 0;
}

void fs_apply_preset(FsApp* app) {
    apply_preset_fam(app, fs_family(app));
}

/* ================= helpers ================= */

static int lvl_y(int lvl, int lo, int hi, int y0, int y1) {
    int rng = hi - lo;
    if(rng < 1) rng = 1;
    int v = clampi(lvl - lo, 0, rng);
    return y1 - v * (y1 - y0) / rng;
}

static inline int col_x(int col, int x0, int w) {
    return x0 + col * (w - 1) / (FS_BINS - 1);
}

static uint8_t col_max(const uint8_t* arr, int xs, int w) {
    if(w >= FS_BINS) return arr[xs];
    int per = FS_BINS / w;
    int c = xs * per;
    uint8_t m = arr[c];
    for(int k = 1; k < per; k++)
        if(arr[c + k] > m) m = arr[c + k];
    return m;
}

static void fmt_span(char* out, size_t n, uint32_t khz) {
    if(khz >= 1000) {
        if(khz % 1000 == 0)
            snprintf(out, n, "%luM", khz / 1000);
        else
            snprintf(out, n, "%lu.%luM", khz / 1000, (khz % 1000) / 100);
    } else {
        snprintf(out, n, "%luK", khz);
    }
}

static void fmt_us(char* out, size_t n, uint32_t us) {
    if(us < 1000)
        snprintf(out, n, "%luUS", us);
    else if(us < 100000)
        snprintf(out, n, "%lu.%luMS", us / 1000, (us % 1000) / 100);
    else
        snprintf(out, n, "%luMS", us / 1000);
}

static void fmt_level(char* out, size_t n, int fam, int lvl) {
    if(fam)
        snprintf(out, n, "%d%%", lvl * 100 / 255);
    else
        snprintf(out, n, "%dDBM", fs_lvl_to_dbm(lvl));
}

static void draw_header(Canvas* canvas, FsApp* a, FsChan* ch) {
    char b[32];
    int fam = ch->geo.fam;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);

    const char* src = a->pg_on ? "PG" :
                      a->radio == FsRadioInt ? "INT" :
                      a->radio == FsRadioExt ? "EXT" :
                      a->radio == FsRadioNrf ? "NRF" :
                                               "---";
    gfx_text(canvas, 2, 1, src);

    FsView v = (FsView)a->cfg.view;
    uint32_t f;
    if(v == FsViewMeter || v == FsViewScope)
        f = a->cfg.lock_khz[fam];
    else
        f = ch->geo.start_khz + ch->geo.span_khz / 2;
    fmt_mhz(b, sizeof(b), f, fam ? 1 : 3);
    gfx_text(canvas, 16, 1, b);
    gfx_text(canvas, 47, 1, view_tags[v]);

    if(v <= FsViewPersist) {
        if(ch->sweep_ms > 0) {
            uint32_t x10 = 10000 / ch->sweep_ms;
            snprintf(b, sizeof(b), "%lu.%lu/S", x10 / 10, x10 % 10);
            gfx_text(canvas, 70, 1, b);
        }
        fmt_span(b, sizeof(b), ch->geo.span_khz);
        gfx_text_right(canvas, 112, 1, b);
    }

    const char* st = "RUN";
    if(a->dec_state == 2)
        st = "DEC";
    else if(a->log_active && ((furi_get_tick() / 400) & 1))
        st = "REC";
    else if(a->ui.freeze)
        st = "HLD";
    else if(ch->trig_active)
        st = "TRG";
    gfx_text_right(canvas, 126, 1, st);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_axis_win(
    Canvas* canvas,
    FsChan* ch,
    int x0,
    int w,
    int yline,
    int ty,
    bool full) {
    char l[16], r[16];
    int fam = ch->geo.fam;
    gfx_hline(x0, x0 + w - 1, yline);
    for(int k = 0; k <= 8; k++) gfx_px(x0 + k * (w - 1) / 8, yline + 1);
    int dec;
    if(fam)
        dec = 0;
    else if(w < 128)
        dec = ch->geo.span_khz <= 20000 ? 1 : 0;
    else
        dec = ch->geo.span_khz <= 20000 ? 2 : (ch->geo.span_khz <= 200000 ? 1 : 0);
    fmt_mhz(l, sizeof(l), ch->geo.start_khz, dec);
    fmt_mhz(r, sizeof(r), ch->geo.start_khz + ch->geo.span_khz, dec);
    gfx_text(canvas, x0 + 1, ty, l);
    gfx_text_right(canvas, x0 + w - 2, ty, r);
    UNUSED(full);
}

static void draw_axis(Canvas* canvas, FsApp* a, FsChan* ch, int y1) {
    char mid[28];
    int fam = ch->geo.fam;
    int ty = y1 + 4;
    draw_axis_win(canvas, ch, 0, 128, y1 + 1, ty, true);

    FsUi* ui = &a->ui;
    int col = clampi(ui->cursor, 0, 127);
    uint32_t khz = fs_col_khz(&ch->geo, col);
    int lvl = ch->valid[col] ? ch->trace[col] : 0;
    if(ui->marker_b_khz) {
        uint32_t kb = ui->marker_b_khz;
        uint32_t d = khz > kb ? khz - kb : kb - khz;
        char ds[12];
        if(d >= 1000)
            snprintf(ds, sizeof(ds), "%lu.%02luM", d / 1000, (d % 1000) / 10);
        else
            snprintf(ds, sizeof(ds), "%luK", d);
        int cb = fs_khz_col(&ch->geo, kb);
        int lb = ch->valid[cb] ? ch->trace[cb] : 0;
        int dl = (lvl - lb) * 100 / 255;
        snprintf(
            mid,
            sizeof(mid),
            "D%s %c%d%s",
            ds,
            dl < 0 ? '-' : '+',
            dl < 0 ? -dl : dl,
            fam ? "%" : "DB");
    } else {
        char f[16];
        fmt_mhz(f, sizeof(f), khz, fam ? 1 : 3);
        if(fam)
            snprintf(mid, sizeof(mid), "%sM %d%%", f, lvl * 100 / 255);
        else
            snprintf(mid, sizeof(mid), "%sM %dDBM", f, fs_lvl_to_dbm(lvl));
    }
    gfx_text_center(canvas, 64, ty, mid);
}

static void draw_cursor_lines(FsApp* a, FsChan* ch, int y0, int y1) {
    FsUi* ui = &a->ui;
    int cx = clampi(ui->cursor, 0, 127);
    for(int y = y0; y <= y1; y += 2) gfx_px_xor(cx, y);
    gfx_px_xor(cx - 1, y0);
    gfx_px_xor(cx + 1, y0);
    if(ui->marker_b_khz) {
        uint32_t kb = ui->marker_b_khz;
        if(kb >= ch->geo.start_khz && kb <= ch->geo.start_khz + ch->geo.span_khz) {
            int bx = clampi(fs_khz_col(&ch->geo, kb), 0, 127);
            for(int y = y0; y <= y1; y += 3) gfx_px_xor(bx, y);
            gfx_px_xor(bx - 1, y0);
            gfx_px_xor(bx + 1, y0);
            gfx_px_xor(bx + 1, y0 + 1);
            gfx_px_xor(bx + 2, y0 + 1);
            gfx_px_xor(bx + 2, y0 + 2);
        }
    }
}

static void draw_status_box(Canvas* canvas, FsApp* a) {
    const char* l1;
    const char* l2;
    switch(a->status) {
    case FsStatusStarting:
        l1 = "INITIALIZING RADIO";
        l2 = "PLEASE WAIT";
        break;
    case FsStatusNoExt:
        l1 = "EXT CC1101 NOT FOUND";
        l2 = "CHECK WIRING (SEE HELP)";
        break;
    case FsStatusNoNrf:
        l1 = "NRF24 NOT FOUND";
        l2 = "CHECK WIRING (SEE HELP)";
        break;
    case FsStatusNoPg:
        l1 = a->cfg.pg_mode ? "PG NEEDS EXT OR NRF24" : "MERGED NEEDS EXT CC1101";
        l2 = "CHECK PINS / WIRING";
        break;
    default:
        l1 = "RADIO ERROR";
        l2 = "TRY ANOTHER SOURCE";
        break;
    }
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 6, 18, 116, 30, 3);
    gfx_text_center(canvas, 64, 24, l1);
    gfx_text_center(canvas, 64, 32, l2);
    gfx_text_center(canvas, 64, 40, "OK = OPTIONS");
}

/* ================= spectrum ================= */

static void plot_spectrum(
    Canvas* canvas,
    FsApp* a,
    FsChan* ch,
    int x0,
    int w,
    int y0,
    int y1,
    bool big,
    bool cursor) {
    const FsConfig* c = &a->cfg;
    const int fam = ch->geo.fam;
    int lo, hi;
    fs_scale(a, ch, fam, &lo, &hi);

    if(c->grid) {
        for(int g = 1; g < 4; g++) gfx_hline_dot(x0, x0 + w - 1, y0 + (y1 - y0) * g / 4, 4);
        for(int k = 1; k < 8; k++) gfx_vline_dot(x0 + k * (w - 1) / 8, y0, y1, 4);
    }
    if(c->overlay & 1) {
        int yn = lvl_y(ch->noise, lo, hi, y0, y1);
        for(int xs = 0; xs < w; xs++)
            if((xs % 6) < 3) gfx_px(x0 + xs, yn);
    }

    int prev = -1;
    for(int xs = 0; xs < w; xs++) {
        int x = x0 + xs;
        if(!col_max(ch->valid, xs, w)) {
            prev = -1;
            if(xs & 1) gfx_px(x, y1);
            continue;
        }
        int yt = lvl_y(col_max(ch->trace, xs, w), lo, hi, y0, y1);
        switch(c->style) {
        case 0:
            for(int y = yt + 1; y <= y1; y++)
                if(((x + y) & 1) == 0) gfx_px(x, y);
            break;
        case 1:
            gfx_vline(x, yt, y1);
            break;
        case 3: {
            int depth = y1 - y0 + 1;
            for(int y = yt; y <= y1; y++) {
                int shade = 16 - (y - yt) * 16 / depth;
                if(gfx_dither(x, y, shade)) gfx_px(x, y);
            }
            break;
        }
        default:
            break;
        }
        gfx_px(x, yt);
        if(prev >= 0) gfx_vline(x, prev, yt);
        prev = yt;
    }

    if(c->overlay & 2) {
        for(int xs = 0; xs < w; xs += 2) {
            if(!col_max(ch->valid, xs, w)) continue;
            gfx_px(x0 + xs, lvl_y(col_max(ch->slow, xs, w), lo, hi, y0, y1));
        }
    }
    if(c->hold != 0) {
        for(int xs = 0; xs < w; xs += 2) {
            if(!col_max(ch->valid, xs, w)) continue;
            gfx_px(x0 + xs, lvl_y(col_max(ch->hold, xs, w), lo, hi, y0, y1));
        }
    }

    uint8_t sq = fs_sq_level(c, fam);
    if(sq) {
        int ys = lvl_y(sq, lo, hi, y0, y1);
        for(int xs = 0; xs < w; xs++)
            if((xs % 8) < 4) gfx_px(x0 + xs, ys);
    }

    int npk = big ? 3 : 1;
    for(int k = 0; k < npk && k < ch->n_peaks; k++) {
        int x = col_x(ch->peaks[k].col, x0, w);
        int yt = lvl_y(ch->peaks[k].lvl, lo, hi, y0, y1);
        int ty = yt - 4;
        if(ty < y0) ty = y0;
        gfx_hline(x - 2, x + 2, ty);
        gfx_hline(x - 1, x + 1, ty + 1);
        gfx_px(x, ty + 2);
        if(big && k == 0 && ty - 6 >= y0) {
            char b[16];
            uint32_t f = fs_col_khz(&ch->geo, ch->peaks[k].col);
            fmt_mhz(b, sizeof(b), f, fam ? 0 : 2);
            int tw = gfx_text_width(b);
            int tx = clampi(x - tw / 2, x0, x0 + w - tw);
            gfx_text(canvas, tx, ty - 6, b);
        }
    }

    if(cursor) {
        int cx = clampi(a->ui.cursor, 0, 127);
        if(ch->valid[cx]) {
            int cy = lvl_y(ch->trace[cx], lo, hi, y0, y1);
            gfx_px_xor(cx - 2, cy);
            gfx_px_xor(cx + 2, cy);
            gfx_px_xor(cx, cy - 2);
            gfx_px_xor(cx, cy + 2);
        }
        draw_cursor_lines(a, ch, y0, y1);
    }

    if(big) {
        char b[12];
        if(fam) {
            gfx_text(canvas, x0 + 1, y0 + 1, "100%");
            gfx_text(canvas, x0 + 1, y1 - 5, "0");
        } else {
            snprintf(b, sizeof(b), "%d", fs_lvl_to_dbm(hi));
            gfx_text(canvas, x0 + 1, y0 + 1, b);
            snprintf(b, sizeof(b), "%d", fs_lvl_to_dbm(lo));
            gfx_text(canvas, x0 + 1, y1 - 5, b);
        }
        if(ch->trig_active && ((furi_get_tick() / 250) & 1)) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 106, y0, 21, 7);
            canvas_set_color(canvas, ColorWhite);
            gfx_text(canvas, 108, y0 + 1, "TRIG");
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

/* ================= waterfall / persistence ================= */

static void build_lut(uint8_t* lut, int pal) {
    for(int n = 0; n < 16; n++) {
        int s = n * 16 / 15;
        if(pal == 1) s = 16 - s;
        if(pal == 3) s = (n >= 5) ? 16 : 0;
        lut[n] = (uint8_t)s;
    }
}

static inline uint8_t wf_nib(const uint8_t* row, int col) {
    uint8_t b = row[col >> 1];
    return (col & 1) ? (b & 15) : (b >> 4);
}

static uint8_t wf_get(const uint8_t* row, int xs, int w) {
    if(w >= FS_BINS) return wf_nib(row, xs);
    int per = FS_BINS / w;
    int c = xs * per;
    uint8_t m = wf_nib(row, c);
    for(int k = 1; k < per; k++) {
        uint8_t v = wf_nib(row, c + k);
        if(v > m) m = v;
    }
    return m;
}

static void plot_waterfall(FsApp* a, FsChan* ch, int x0, int w, int y0, int y1) {
    uint8_t* lut = a->lut;
    int pal = a->cfg.wf_pal;
    build_lut(lut, pal);
    int rows = y1 - y0 + 1;
    const int R = ch->wf_rows;
    const int RB = FS_BINS / 2;
    for(int r = 0; r < rows; r++) {
        int y = y0 + r;
        if(r >= ch->wf_count) {
            for(int xs = (r & 1); xs < w; xs += 2) gfx_px(x0 + xs, y);
            break;
        }
        const uint8_t* row = ch->wf + (size_t)((ch->wf_head + R - 1 - r) % R) * RB;
        const uint8_t* below = (r + 1 < ch->wf_count) ?
                                   ch->wf + (size_t)((ch->wf_head + R - 2 - r) % R) * RB :
                                   row;
        for(int xs = 0; xs < w; xs++) {
            bool on;
            int x = x0 + xs;
            uint8_t v = wf_get(row, xs, w);
            if(pal == 2) {
                int cur = lut[v] >> 1;
                int rt = (xs < w - 1) ? (lut[wf_get(row, xs + 1, w)] >> 1) : cur;
                int dn = lut[wf_get(below, xs, w)] >> 1;
                on = (cur != rt) || (cur != dn);
            } else {
                on = gfx_dither(x, y, lut[v]);
            }
            if(on) gfx_px(x, y);
        }
        if((r & 7) == 0) gfx_px(x0 + w - 1, y);
    }
}
static void plot_persist(Canvas* canvas, FsApp* a, FsChan* ch) {
    if(a->cfg.grid) {
        for(int g = 1; g < 4; g++) gfx_hline_dot(0, 127, PL_Y0 + (PL_Y1 - PL_Y0) * g / 4, 4);
        for(int k = 1; k < 8; k++) gfx_vline_dot(k * 127 / 8, PL_Y0, PL_Y1, 4);
    }
    if(!ch->heat) {
        gfx_text_center(canvas, 64, 30, a->cfg.lite ? "PERSIST OFF (LITE)" : "LOW MEMORY");
        return;
    }
    for(int x = 0; x < 128; x++) {
        const uint8_t* col = &ch->heat[x * FS_PLOT_H];
        for(int yi = 0; yi < FS_PLOT_H; yi++) {
            int h = col[yi];
            if(!h) continue;
            int shade = clampi(h * 17 / 150, 0, 16);
            int y = PL_Y1 - yi;
            if(gfx_dither(x, y, shade)) gfx_px(x, y);
        }
    }
}

/* ================= scope ================= */

static void draw_scope(Canvas* canvas, FsApp* a, FsChan* ch) {
    char b[72], t1[16], t2[16];
    const int fam = ch->geo.fam;
    int lvl = a->meter_cur;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 9, lvl * 127 / 255 + 1, 2);
    gfx_hline_dot(0, 127, 11, 2);

    const int gy0 = 14, gy1 = 40;
    if(fam) {
        gfx_hline(0, 127, gy1 + 1);
        int sum = 0;
        for(int i = 0; i < FS_ACT_COLS; i++) {
            int idx = (a->act_head + i) % FS_ACT_COLS;
            int h = a->act[idx];
            sum += h;
            if(h) gfx_vline(i, gy1 - h * (gy1 - gy0) / 4, gy1);
        }
        gfx_text(canvas, 1, 43, "NRF24 CARRIER ACTIVITY");
        snprintf(
            b,
            sizeof(b),
            "LOCK %luM  DUTY %d%%",
            a->cfg.lock_khz[1] / 1000,
            sum * 100 / (FS_ACT_COLS * 4));
        gfx_text(canvas, 1, 50, b);
        snprintf(b, sizeof(b), "WINDOW ~80MS  LEVEL %d%%", lvl * 100 / 255);
        gfx_text(canvas, 1, 57, b);
        return;
    }

    if(!a->scope || a->scope_n < 2 || a->scope_total_us == 0) {
        gfx_text_center(canvas, 64, 24, "WAITING FOR SIGNAL");
        snprintf(
            b,
            sizeof(b),
            "MOD %s  GAP %s",
            mod_names[a->cfg.mod % 7],
            gap_names[a->cfg.scope_gap % 5]);
        gfx_text_center(canvas, 64, 32, b);
    } else {
        const int yh = gy0 + 2, yl = gy1 - 2;
        uint32_t acc = 0, total = a->scope_total_us;
        int px = 0;
        int py = a->scope[0].lvl ? yh : yl;
        for(uint16_t i = 0; i < a->scope_n; i++) {
            int y = a->scope[i].lvl ? yh : yl;
            acc += a->scope[i].dur;
            int nx = (int)(acc * 127U / total);
            if(y != py) gfx_vline(px, py, y);
            gfx_hline(px, nx, y);
            px = nx;
            py = y;
        }
    }
    uint32_t te = a->scope_min_us;
    fmt_us(t1, sizeof(t1), a->scope_total_us);
    fmt_us(t2, sizeof(t2), a->scope_max_us);
    snprintf(b, sizeof(b), "N=%u TOT=%s MAX=%s", a->scope_n, t1, t2);
    gfx_text(canvas, 1, 43, b);
    fmt_us(t1, sizeof(t1), te);
    snprintf(b, sizeof(b), "TE=%s BAUD~%lu", t1, te ? 1000000UL / te : 0UL);
    gfx_text(canvas, 1, 50, b);
    snprintf(
        b, sizeof(b), "%s B%lu %d", hint_names[a->scope_hint % 5], a->scope_bursts, fs_lvl_to_dbm(lvl));
    gfx_text(canvas, 1, 57, b);
}

/* ================= s-meter ================= */

static void gauge_pt(int r, int deg, int* x, int* y) {
    *x = 64 + r * gfx_cos256(deg) / 256;
    *y = 52 - r * gfx_sin256(deg) / 256;
}

static void draw_meter(Canvas* canvas, FsApp* a, FsChan* ch) {
    const int fam = ch->geo.fam;
    const int R = 31;
    char b[24];
    int x, y, x2, y2;

    for(int d = 20; d <= 160; d += 2) {
        gauge_pt(R, d, &x, &y);
        gfx_px(x, y);
    }
    for(int k = 0; k <= 5; k++) {
        int lvl = k * 51;
        int deg = 160 - lvl * 140 / 255;
        gauge_pt(R - 5, deg, &x, &y);
        gauge_pt(R, deg, &x2, &y2);
        canvas_draw_line(canvas, x, y, x2, y2);
        if(fam)
            snprintf(b, sizeof(b), "%d", k * 20);
        else
            snprintf(b, sizeof(b), "%d", -120 + k * 20);
        gauge_pt(R + 7, deg, &x, &y);
        gfx_text_center(canvas, x, y - 2, b);
    }
    for(int k = 0; k < 25; k++) {
        int deg = 160 - (k * 255 / 25) * 140 / 255;
        gauge_pt(R - 2, deg, &x, &y);
        gfx_px(x, y);
    }

    int lvl = a->meter_cur;
    int deg = 160 - lvl * 140 / 255;
    gauge_pt(R - 3, deg, &x, &y);
    canvas_draw_line(canvas, 64, 52, x, y);
    canvas_draw_disc(canvas, 64, 52, 2);
    int pdeg = 160 - a->meter_peak * 140 / 255;
    gauge_pt(R + 2, pdeg, &x, &y);
    canvas_draw_disc(canvas, x, y, 1);

    if(fam)
        snprintf(b, sizeof(b), "%d", lvl * 100 / 255);
    else
        snprintf(b, sizeof(b), "%d", fs_lvl_to_dbm(lvl));
    gfx_text_scaled(canvas, 2, 24, b, 2);
    gfx_text(canvas, 2, 36, fam ? "PCT" : "DBM");

    gfx_text(canvas, 100, 12, "PEAK");
    fmt_level(b, sizeof(b), fam, a->meter_peak);
    gfx_text(canvas, 100, 19, b);
    if(fam) {
        gfx_text(canvas, 100, 28, "STEP 1M");
    } else {
        snprintf(b, sizeof(b), "STEP %luK", step_tab[a->cfg.step_idx % 6]);
        gfx_text(canvas, 100, 28, b);
    }
    uint8_t sq = fs_sq_level(&a->cfg, fam);
    if(sq) {
        fmt_level(b, sizeof(b), fam, sq);
        gfx_text(canvas, 100, 36, "SQ");
        gfx_text(canvas, 100, 43, b);
    }

    gfx_hline_dot(0, 127, 55, 2);
    for(int i = 0; i < FS_HIST; i++) {
        int idx = (a->meter_head + i) % FS_HIST;
        int h = a->meter_hist[idx] * 8 / 255;
        if(h > 0) gfx_vline(i, 63 - h + 1, 63);
    }
}

/* ================= signals ================= */

static void draw_signals(Canvas* canvas, FsApp* a, FsChan* ch) {
    const int fam = ch->geo.fam;
    char b[56], lb[12];
    if(ch->n_peaks == 0) gfx_text_center(canvas, 64, 24, "NO SIGNALS ABOVE NOISE");
    int sel = ch->n_peaks ? clampi(a->ui.sig_sel, 0, ch->n_peaks - 1) : 0;
    for(int i = 0; i < ch->n_peaks && i < 6; i++) {
        int y = 10 + i * 7;
        uint32_t f = fs_col_khz(&ch->geo, ch->peaks[i].col);
        bool s = (i == sel);
        if(s) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 1, 128, 7);
            canvas_set_color(canvas, ColorWhite);
        }
        snprintf(b, sizeof(b), "%d", i + 1);
        gfx_text(canvas, 1, y, b);
        fmt_mhz(b, sizeof(b), f, fam ? 1 : 3);
        gfx_text(canvas, 8, y, b);
        fmt_level(b, sizeof(b), fam, ch->peaks[i].lvl);
        gfx_text(canvas, 44, y, b);
        int w = ch->peaks[i].lvl * 22 / 255;
        canvas_draw_box(canvas, 70, y + 1, w + 1, 3);
        fs_label(f, fam, lb, sizeof(lb));
        gfx_text(canvas, 96, y, lb);
        canvas_set_color(canvas, ColorBlack);
    }
    int nf = ch->noise;
    if(fam)
        snprintf(b, sizeof(b), "NF %d%% SNR %d", nf * 100 / 255, fs_snr(ch));
    else
        snprintf(b, sizeof(b), "NF %dDBM SNR %dDB", fs_lvl_to_dbm(nf), fs_snr(ch));
    gfx_text(canvas, 1, 53, b);
    char o[12];
    if(ch->obw_khz >= 1000)
        snprintf(o, sizeof(o), "%lu.%luM", ch->obw_khz / 1000, (ch->obw_khz % 1000) / 100);
    else
        snprintf(o, sizeof(o), "%luK", ch->obw_khz);
    snprintf(b, sizeof(b), "OBW %s HIT %lu DUTY %d%%", o, ch->hits, ch->duty / 10);
    gfx_text(canvas, 1, 59, b);
}

/* ================= channels ================= */

static int win_max(const FsChan* ch, uint32_t f_lo, uint32_t f_hi) {
    int c0 = fs_khz_col(&ch->geo, f_lo);
    int c1 = fs_khz_col(&ch->geo, f_hi);
    int m = 0;
    for(int c = c0; c <= c1; c++)
        if(ch->valid[c] && ch->trace[c] > m) m = ch->trace[c];
    return m;
}

static void draw_bars_nrf(Canvas* canvas, FsApp* a, FsChan* ch, int page) {
    int lo, hi;
    fs_scale(a, ch, 1, &lo, &hi);
    const int yt = 17, yb = 52;
    char b[8];
    if(page == 0) {
        gfx_text(canvas, 1, 10, "WI-FI 2.4G OCCUPANCY");
        for(int c = 1; c <= 13; c++) {
            uint32_t fc = 2407000UL + 5000UL * c;
            int v = win_max(ch, fc - 10000, fc + 10000);
            int h = clampi((v - lo) * (yb - yt) / (hi - lo), 0, yb - yt);
            int x = 6 + (c - 1) * 9;
            canvas_draw_frame(canvas, x, yt, 7, yb - yt + 1);
            if(h > 0) canvas_draw_box(canvas, x, yb - h, 7, h + 1);
            snprintf(b, sizeof(b), "%d", c);
            gfx_text_center(canvas, x + 3, 57, b);
        }
    } else if(page == 1) {
        gfx_text(canvas, 1, 10, "ZIGBEE 11-26");
        for(int k = 0; k < 16; k++) {
            uint32_t fc = 2405000UL + 5000UL * k;
            int v = win_max(ch, fc - 2000, fc + 2000);
            int h = clampi((v - lo) * (yb - yt) / (hi - lo), 0, yb - yt);
            int x = 8 + k * 7;
            canvas_draw_frame(canvas, x, yt, 5, yb - yt + 1);
            if(h > 0) canvas_draw_box(canvas, x, yb - h, 5, h + 1);
            if((k & 1) == 0) {
                snprintf(b, sizeof(b), "%d", 11 + k);
                gfx_text_center(canvas, x + 2, 57, b);
            }
        }
    } else {
        gfx_text(canvas, 1, 10, "BLE CH 0-39  ADV 37/38/39");
        for(int s = 0; s < 40; s++) {
            uint32_t fc = 2402000UL + 2000UL * s;
            int v = win_max(ch, fc - 1000, fc + 1000);
            int h = clampi((v - lo) * (yb - yt) / (hi - lo), 0, yb - yt);
            int x = 4 + s * 3;
            bool adv = (s == 0 || s == 12 || s == 39);
            if(adv) gfx_vline(x + 1, yt - 3, yt - 1);
            if(h > 0) canvas_draw_box(canvas, x, yb - h, 2, h + 1);
            gfx_px(x, yb + 1);
            gfx_px(x + 1, yb + 1);
            if(adv) {
                snprintf(b, sizeof(b), "%d", s == 0 ? 37 : (s == 12 ? 38 : 39));
                gfx_text_center(canvas, x + 1, 57, b);
            }
        }
    }
}

static void draw_bars_sub(Canvas* canvas, FsApp* a, FsChan* ch, int page) {
    int lo, hi;
    fs_scale(a, ch, 0, &lo, &hi);
    static const int segs[3] = {16, 32, 64};
    int n = segs[page % 3];
    int per = 128 / n;
    const int yt = 17, yb = 52;
    char b[24];
    snprintf(b, sizeof(b), "OCCUPANCY %d CH", n);
    gfx_text(canvas, 1, 10, b);
    int pitch = 128 / n;
    for(int s = 0; s < n; s++) {
        int m = 0;
        for(int c = s * per; c < (s + 1) * per; c++)
            if(ch->valid[c] && ch->trace[c] > m) m = ch->trace[c];
        int h = clampi((m - lo) * (yb - yt) / (hi - lo), 0, yb - yt);
        int x = s * pitch;
        int w = pitch > 2 ? pitch - 1 : pitch;
        if(h > 0) canvas_draw_box(canvas, x, yb - h, w, h + 1);
    }
    gfx_hline(0, 127, yb + 1);
    char l[16], r[16];
    fmt_mhz(l, sizeof(l), ch->geo.start_khz, 2);
    fmt_mhz(r, sizeof(r), ch->geo.start_khz + ch->geo.span_khz, 2);
    gfx_text(canvas, 1, 57, l);
    gfx_text_right(canvas, 126, 57, r);
    gfx_text_center(canvas, 64, 57, "MHZ");
}

/* ================= multi use ================= */

static void win_title(Canvas* canvas, FsApp* a, FsChan* ch, int x0, int w, int y, const char* tag, bool focus) {
    char b[24], f[12];
    canvas_set_color(canvas, ColorBlack);
    if(focus) canvas_draw_box(canvas, x0, y, w, 7);
    canvas_set_color(canvas, focus ? ColorWhite : ColorBlack);
    uint32_t cf = ch->geo.start_khz + ch->geo.span_khz / 2;
    fmt_mhz(f, sizeof(f), cf, 2);
    snprintf(b, sizeof(b), "%s %s", tag, f);
    gfx_text(canvas, x0 + 2, y + 1, b);
    if(ch->n_peaks) {
        snprintf(b, sizeof(b), "%d", fs_lvl_to_dbm(ch->peaks[0].lvl));
        gfx_text_right(canvas, x0 + w - 2, y + 1, b);
    }
    if(!focus) gfx_hline_dot(x0, x0 + w - 1, y + 7, 2);
    canvas_set_color(canvas, ColorBlack);
    UNUSED(a);
}

typedef struct {
    int16_t x, y, w, h;
} Rect;

static const char* const slot_tags[FsSlotCount] = {"OFF", "EXT", "INT", "NRF", "PKT", "WIFI"};
static const char* const wifi_view_names[4] = {"APS", "CHAN", "RATE", "LOG"};
static const char* const wifi_mode_tags[4] = {"SCAN", "BCN", "DEAUTH", "PMKID"};

static void layout_rects(int layout, Rect* r) {
    static const Rect tab[5][4] = {
        {{0, 9, 63, 55}, {65, 9, 63, 55}, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {{0, 9, 128, 27}, {0, 37, 128, 27}, {0, 0, 0, 0}, {0, 0, 0, 0}},
        {{0, 9, 63, 27}, {65, 9, 63, 27}, {0, 37, 128, 27}, {0, 0, 0, 0}},
        {{0, 9, 128, 27}, {0, 37, 63, 27}, {65, 37, 63, 27}, {0, 0, 0, 0}},
        {{0, 9, 63, 27}, {65, 9, 63, 27}, {0, 37, 63, 27}, {65, 37, 63, 27}},
    };
    memcpy(r, tab[layout > 4 ? 2 : layout], sizeof(Rect) * 4);
}

static void title_bar(Canvas* canvas, const Rect* r, const char* l, const char* rt, bool focus) {
    canvas_set_color(canvas, ColorBlack);
    if(focus) canvas_draw_box(canvas, r->x, r->y, r->w, 7);
    canvas_set_color(canvas, focus ? ColorWhite : ColorBlack);
    gfx_text(canvas, r->x + 2, r->y + 1, l);
    if(rt) gfx_text_right(canvas, r->x + r->w - 2, r->y + 1, rt);
    if(!focus) gfx_hline_dot(r->x, r->x + r->w - 1, r->y + 7, 2);
    canvas_set_color(canvas, ColorBlack);
}

static void win_msg(Canvas* canvas, const Rect* r, const char* l1, const char* l2) {
    int cy = r->y + 8 + (r->h - 8) / 2;
    gfx_text_center(canvas, r->x + r->w / 2, cy - (l2 ? 4 : 2), l1);
    if(l2) gfx_text_center(canvas, r->x + r->w / 2, cy + 3, l2);
}

static void draw_spec_win(
    Canvas* canvas,
    FsApp* a,
    const Rect* r,
    FsChan* ch,
    const char* tag,
    bool focus) {
    if(!ch || ch->sweeps == 0) {
        title_bar(canvas, r, tag, NULL, focus);
        win_msg(canvas, r, ch ? "SWEEPING" : "WAITING", (!ch && memmgr_get_free_heap() < 12 * 1024) ? "LOW MEMORY" : NULL);
        return;
    }
    win_title(canvas, a, ch, r->x, r->w, r->y, tag, focus);
    int by0 = r->y + 8, by1 = r->y + r->h - 1;
    int yl = by1 - 7;
    int py1 = yl - 1;
    int ph = py1 - by0 + 1;
    if(ph >= 30 && ch->wf) {
        int ys = by0 + ph * 5 / 9;
        plot_spectrum(canvas, a, ch, r->x, r->w, by0, ys, false, false);
        gfx_hline_dot(r->x, r->x + r->w - 1, ys + 1, 2);
        plot_waterfall(a, ch, r->x, r->w, ys + 3, py1);
    } else {
        plot_spectrum(canvas, a, ch, r->x, r->w, by0, py1, false, false);
    }
    draw_axis_win(canvas, ch, r->x, r->w, yl, by1 - 5, false);
}

static void draw_pkt_win(Canvas* canvas, FsApp* a, const Rect* r, bool focus) {
    char l[24], rt[24], b[48];
    const bool wide = r->w >= 100;
    snprintf(l, sizeof(l), "NRF PKT %lu", (unsigned long)a->pkt_total);
    if(wide)
        snprintf(rt, sizeof(rt), "%u.%u/S CH%d", a->pkt_rate / 10u, a->pkt_rate % 10u, a->pkt_ch);
    else
        snprintf(rt, sizeof(rt), "%u.%u/S", a->pkt_rate / 10u, a->pkt_rate % 10u);
    title_bar(canvas, r, wide ? l : "NRF PKT", rt, focus);

    int order[FS_MAX_PKTS];
    int n = a->n_pkts;
    for(int i = 0; i < n; i++) order[i] = i;
    for(int i = 0; i < n; i++)
        for(int j = i + 1; j < n; j++)
            if(a->pkts[order[j]].last > a->pkts[order[i]].last) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
    if(n == 0) {
        static const char* const dots[4] = {"", ".", "..", "..."};
        snprintf(b, sizeof(b), "SCAN CH %d%s", a->pkt_ch, dots[(furi_get_tick() / 300) & 3]);
        win_msg(canvas, r, b, NULL);
        return;
    }
    int rows = (r->h - 9) / 6;
    for(int i = 0; i < n && i < rows; i++) {
        const FsPkt* p = &a->pkts[order[i]];
        if(wide)
            snprintf(
                b,
                sizeof(b),
                "%02X%02X%02X%02X%02X %d X%u %02X%02X%02X",
                p->addr[0],
                p->addr[1],
                p->addr[2],
                p->addr[3],
                p->addr[4],
                p->ch,
                p->count,
                p->pay[0],
                p->pay[1],
                p->pay[2]);
        else
            snprintf(
                b,
                sizeof(b),
                "%02X%02X%02X%02X%02X X%u",
                p->addr[0],
                p->addr[1],
                p->addr[2],
                p->addr[3],
                p->addr[4],
                p->count);
        gfx_text(canvas, r->x + 1, r->y + 9 + i * 6, b);
    }
}

static void draw_wifi_win(Canvas* canvas, FsApp* a, const Rect* r, bool focus) {
    const FsWifi* s = a->wifi;
    const bool wide = r->w >= 100;
    const int view = a->cfg.wifi_view & 3;
    char l[24], rt[24], b[48];
    snprintf(l, sizeof(l), "WIFI %s", wifi_view_names[view]);
    const char* mt = wifi_mode_tags[a->cfg.wifi_mode & 3];
    if(s && s->state == FsWifiReady && wide)
        snprintf(rt, sizeof(rt), "%s %lu", mt, (unsigned long)s->total);
    else
        snprintf(rt, sizeof(rt), "%s", mt);
    title_bar(canvas, r, l, rt, focus);

    if(!s) {
        win_msg(canvas, r, "WIFI MODULE", "NO MEMORY");
        return;
    }
    switch(s->state) {
    case FsWifiNoResponse:
        win_msg(canvas, r, "NO RESPONSE", wide ? "CHECK WIRING / MARAUDER" : "CHECK WIRING");
        return;
    case FsWifiNotMarauder:
        win_msg(canvas, r, "NOT MARAUDER FW", wide ? "UPDATE TO MARAUDER FIRMWARE" : "UPDATE FIRMWARE");
        return;
    case FsWifiBusy:
        win_msg(canvas, r, "UART IN USE", wide ? "CLOSE OTHER UART APPS" : "CLOSE UART APPS");
        return;
    case FsWifiReady:
        break;
    default: {
        static const char* const dots[4] = {"", ".", "..", "..."};
        snprintf(b, sizeof(b), "STARTING%s", dots[(furi_get_tick() / 300) & 3]);
        win_msg(canvas, r, b, wide ? "LOOKING FOR MARAUDER" : "WIFI MODULE");
        return;
    }
    }

    const int by0 = r->y + 9, by1 = r->y + r->h - 1;
    const int bh = by1 - by0 + 1;
    if(view == 0) {
        int order[FS_MAX_APS];
        int n = s->n_aps;
        for(int i = 0; i < n; i++) order[i] = i;
        for(int i = 0; i < n; i++)
            for(int j = i + 1; j < n; j++)
                if(s->aps[order[j]].rssi > s->aps[order[i]].rssi) {
                    int t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                }
        if(n == 0) {
            win_msg(canvas, r, "LISTENING...", NULL);
            return;
        }
        int rows = bh / 6;
        for(int i = 0; i < n && i < rows; i++) {
            const FsAp* ap = &s->aps[order[i]];
            const char* nm = ap->ssid[0] ? ap->ssid : "(HIDDEN)";
            int y = by0 + i * 6;
            if(wide) {
                snprintf(b, sizeof(b), "%-10.10s CH%-2u %d", nm, ap->ch, ap->rssi);
                gfx_text(canvas, r->x + 1, y, b);
                int len = ((int)ap->rssi + 100) * 14 / 60;
                if(len < 1) len = 1;
                if(len > 14) len = 14;
                canvas_draw_box(canvas, r->x + r->w - 17, y + 1, len, 3);
            } else {
                snprintf(b, sizeof(b), "%-6.6s %u %d", nm, ap->ch, ap->rssi);
                gfx_text(canvas, r->x + 1, y, b);
            }
        }
    } else if(view == 1) {
        uint8_t cnt[14], best[14];
        memset(cnt, 0, sizeof(cnt));
        memset(best, 0, sizeof(best));
        for(int i = 0; i < s->n_aps; i++) {
            int c = s->aps[i].ch;
            if(c < 1 || c > 13) continue;
            int lv = (int)s->aps[i].rssi + 100;
            lv = lv < 1 ? 1 : (lv > 70 ? 70 : lv);
            cnt[c]++;
            if(lv > best[c]) best[c] = (uint8_t)lv;
        }
        const int bw = (r->w - 2) / 13;
        const int base = by1 - 6;
        const int top = by0 + 1;
        gfx_hline_dot(r->x + 1, r->x + r->w - 2, base + 1, 2);
        for(int c = 1; c <= 13; c++) {
            int x = r->x + 1 + (c - 1) * bw;
            if(cnt[c]) {
                int h = best[c] * (base - top) / 70;
                if(h < 2) h = 2;
                canvas_draw_box(canvas, x, base - h, bw > 1 ? bw - 1 : 1, h);
            }
            if(c == 1 || c == 6 || c == 11 || (wide && c == 13)) {
                snprintf(b, sizeof(b), "%d", c);
                gfx_text(canvas, x, by1 - 4, b);
            }
        }
    } else if(view == 2) {
        uint8_t mx = 4;
        for(int i = 0; i < 32; i++)
            if(s->hist[i] > mx) mx = s->hist[i];
        const int bw = wide ? 3 : 1;
        const int step = wide ? 4 : 2;
        const int ph = bh - 7;
        int x = r->x + 1;
        int cnt = (r->w - 2) / step;
        if(cnt > 32) cnt = 32;
        for(int i = 0; i < cnt; i++) {
            int v = s->hist[(s->hist_head + 32 - cnt + i) % 32];
            int h = v * ph / mx;
            if(v && h < 1) h = 1;
            if(h) canvas_draw_box(canvas, x + i * step, by1 - h, bw, h);
        }
        if(wide)
            snprintf(b, sizeof(b), "%u/S  MAX %u", s->rate, mx);
        else
            snprintf(b, sizeof(b), "%u/S", s->rate);
        gfx_text(canvas, r->x + 2, by0, b);
    } else {
        int rows = bh / 6;
        if(rows > 4) rows = 4;
        int maxc = (r->w - 2) / 4;
        if(maxc > 30) maxc = 30;
        for(int i = 0; i < rows; i++) {
            int idx = (s->log_head + 4 - rows + i) % 4;
            const char* ln = s->log[idx];
            size_t k = strlen(ln);
            if((int)k > maxc) k = (size_t)maxc;
            memcpy(b, ln, k);
            b[k] = 0;
            gfx_text(canvas, r->x + 1, by0 + i * 6, b);
        }
    }
}

static void draw_slot(Canvas* canvas, FsApp* a, int slot, const Rect* r, bool focus) {
    switch(a->cfg.mu_slot[slot]) {
    case FsSlotExt:
        if(!a->have_ext) {
            title_bar(canvas, r, "EXT", NULL, focus);
            win_msg(canvas, r, "EXT CC1101", "NOT FOUND");
        } else {
            draw_spec_win(canvas, a, r, a->ch[0], "EXT", focus);
        }
        break;
    case FsSlotInt:
        draw_spec_win(canvas, a, r, a->ch[1], "INT", focus);
        break;
    case FsSlotNrfSpec:
        if(!a->have_nrf) {
            title_bar(canvas, r, "NRF24", NULL, focus);
            win_msg(canvas, r, "NRF24", "NOT FOUND");
        } else {
            draw_spec_win(canvas, a, r, a->ch[2], "NRF", focus);
        }
        break;
    case FsSlotNrfPkt:
        if(!a->have_nrf) {
            title_bar(canvas, r, "NRF PKT", NULL, focus);
            win_msg(canvas, r, "NRF24", "NOT FOUND");
        } else {
            draw_pkt_win(canvas, a, r, focus);
        }
        break;
    case FsSlotWifi:
        draw_wifi_win(canvas, a, r, focus);
        break;
    default:
        title_bar(canvas, r, "OFF", NULL, focus);
        win_msg(canvas, r, "WINDOW OFF", "OPTIONS>MULTI USE");
        break;
    }
}

static void draw_multi(Canvas* canvas, FsApp* a) {
    FsUi* ui = &a->ui;
    const FsConfig* c = &a->cfg;
    const int n = fs_slot_count(c->mu_layout);
    const int foc = ui->focus < n ? ui->focus : 0;
    Rect r[4];
    layout_rects(c->mu_layout, r);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    gfx_text(canvas, 2, 1, "MULTI");
    char b[40];
    int len = 0;
    b[0] = 0;
    for(int i = 0; i < n; i++) {
        int s = c->mu_slot[i] < FsSlotCount ? c->mu_slot[i] : 0;
        len += snprintf(b + len, sizeof(b) - (size_t)len, "%s%s", i ? " " : "", slot_tags[s]);
    }
    gfx_text(canvas, 26, 1, b);
    gfx_text_right(canvas, 126, 1, ui->freeze ? "HLD" : (a->dec_state == 2 ? "DEC" : "PG"));
    canvas_set_color(canvas, ColorBlack);

    for(int i = 0; i < n; i++) draw_slot(canvas, a, i, &r[i], i == foc);

    switch(c->mu_layout) {
    case 0:
        gfx_vline_dot(64, 9, 63, 2);
        break;
    case 1:
        gfx_hline_dot(0, 127, 36, 2);
        break;
    case 2:
        gfx_vline_dot(64, 9, 35, 2);
        gfx_hline_dot(0, 127, 36, 2);
        break;
    case 3:
        gfx_hline_dot(0, 127, 36, 2);
        gfx_vline_dot(64, 37, 63, 2);
        break;
    default:
        gfx_vline_dot(64, 9, 63, 2);
        gfx_hline_dot(0, 127, 36, 2);
        break;
    }
}

/* ================= splash / help / pins / warn ================= */

static int bump(int x, int xc, int w, int h) {
    int d = x > xc ? x - xc : xc - x;
    int v = h - d * h / w;
    return v > 0 ? v : 0;
}

static int splash_val(int x, uint32_t t) {
    int v = 2 + (int)(((uint32_t)x * 53 + (t / 40) * 31) % 5);
    v += bump(x, 22 + (int)((t / 16) % 84), 10, 22);
    v += bump(x, 82, 12, (int)(t / 45 > 26 ? 26 : t / 45));
    v += bump(x, 108 - (int)((t / 70) % 20), 6, 14);
    return v;
}

static void draw_splash(Canvas* canvas, FsApp* a) {
    UNUSED(canvas);
    FsUi* ui = &a->ui;
    uint32_t now = furi_get_tick();
    uint32_t t = now - ui->mode_tick;
    char b[32];
    gfx_fill_black();

    const char* title = "FLIPSAY";
    for(int i = 0; i < 7; i++) {
        int start = 150 + i * 80;
        if((int)t < start) continue;
        int p = clampi(((int)t - start) * 256 / 360, 0, 256);
        int e = gfx_ease_back(p);
        int y = -18 + (6 + 18) * e / 256;
        char one[2] = {title[i], 0};
        gfx_text_px(22 + i * 12, y, one, 3, 16, true);
    }
    if(t > 1000 && t < 1750) {
        int sx = -12 + (int)(t - 1000) * 160 / 750;
        for(int y = 6; y < 21; y++) {
            int x = sx - (y - 6) / 2;
            for(int d = 0; d < 3; d++) gfx_px_xor(x + d, y);
        }
    }

    int s1 = clampi(((int)t - 700) * 16 / 400, 0, 16);
    int s2 = clampi(((int)t - 900) * 16 / 400, 0, 16);
    int s4 = clampi(((int)t - 1500) * 16 / 300, 0, 16);
    if(s1 > 0) gfx_text_px(14, 25, "MINI SDR FOR FLIPPER ZERO", 1, s1, true);
    if(s2 > 0) gfx_text_px(9, 31, "CC1101 + NRF24 + AUTO DECODE", 1, s2, true);
    if(s4 > 0) gfx_text_px(104, 0, "V" FS_VERSION, 1, s4, true);
    if(s4 > 0) gfx_text_px(2, 0, "BY ITSC0C0", 1, s4, true);

    int target = a->boot_pct;
    int disp = ui->boot_disp;
    if(disp < target) disp += (target - disp + 3) / 4;
    if(disp > target) disp = target;
    ui->boot_disp = (uint8_t)disp;

    const char* msg = a->boot_msg ? a->boot_msg : "STARTING";
    gfx_text_px(14, 37, msg, 1, 16, true);
    snprintf(b, sizeof(b), "%d%%", disp);
    gfx_text_px(113 - gfx_text_width(b) + 1, 37, b, 1, 16, true);

    for(int x = 14; x < 114; x++) {
        gfx_px_clr(x, 43);
        gfx_px_clr(x, 47);
    }
    for(int y = 44; y < 47; y++) {
        gfx_px_clr(14, y);
        gfx_px_clr(113, y);
    }
    int bar_w = disp * 97 / 100;
    for(int x = 0; x < bar_w; x++) {
        for(int y = 44; y < 47; y++) {
            if(y == 45 || ((x + (int)(t / 60)) & 3) != 0) gfx_px_clr(15 + x, y);
        }
    }

    snprintf(b, sizeof(b), "MEM %uK FREE", (unsigned)(memmgr_get_free_heap() / 1024));
    gfx_text_px(64 - gfx_text_width(b) / 2, 50, b, 1, 16, true);

    for(int r = 0; r < 7; r++) {
        int y = 57 + r;
        uint32_t tr = t > (uint32_t)(r * 70) ? t - (uint32_t)(r * 70) : 0;
        for(int x = 0; x < 128; x++) {
            int v = splash_val(x, tr);
            int shade = clampi(v * 17 / 32, 0, 16);
            if(gfx_dither(x, y, shade)) gfx_px_clr(x, y);
        }
    }

    if(ui->splash_out) {
        int sh = 16 - clampi((int)(now - ui->splash_out) * 16 / SPLASH_OUT, 0, 16);
        gfx_dissolve(sh);
    }
}
static const char* const help_pages[3][8] = {
    {"KEYS: SPECTRUM VIEWS",
     "OK        OPTIONS MENU",
     "OK HOLD   NEXT BAND",
     "BACK      NEXT VIEW",
     "LEFT/RT   SCROLL BAND",
     "UP/DN     ZOOM IN/OUT",
     "UP HOLD   FREEZE",
     "BACK HOLD EXIT APP"},
    {"KEYS: OTHER VIEWS",
     "METER/SCOPE LEFT/RT TUNE",
     "METER/SCOPE UP/DN STEP",
     "SIGNALS UP/DN SELECT",
     "SIGNALS RT  CENTER",
     "SIGNALS LT  CAPTURE SUB",
     "CHANNELS LT/RT PAGE",
     "MULTI: BACK = NEXT WINDOW"},
    {"WIRING (EXT CC1101/NRF)",
     "PIN 2  MOSI     PIN 3 MISO",
     "PIN 5  SCK      PIN 4 CS",
     "PIN 6  GDO0/CE  PIN 7 CS2",
     "PIN 8  GND   PIN 9  3V3",
     "CC1101 + NRF24 TOGETHER:",
     "USE DIFFERENT CS PINS",
     "SEE OPTIONS > PIN LAYOUT"},
};

static void draw_help(Canvas* canvas, FsApp* a) {
    int pg = a->ui.help_page % 3;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    char b[24];
    snprintf(b, sizeof(b), "HELP %d/3", pg + 1);
    gfx_text(canvas, 2, 1, b);
    gfx_text_right(canvas, 126, 1, "<> PAGE  BACK");
    canvas_set_color(canvas, ColorBlack);
    for(int i = 0; i < 8; i++) gfx_text(canvas, 2, 11 + i * 6 + (i > 0 ? 2 : 0), help_pages[pg][i]);
}

static void draw_pins(Canvas* canvas, FsApp* a) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    gfx_text(canvas, 2, 1, "PIN LAYOUT");
    gfx_text_right(canvas, 126, 1, "BACK = EXIT");
    canvas_set_color(canvas, ColorBlack);
    gfx_text(canvas, 2, 10, "PIN SIGNAL  CC1101  NRF24");
    gfx_hline(0, 127, 16);
    static const char* const rows[8][4] = {
        {"2", "MOSI", "SI", "MOSI"},
        {"3", "MISO", "SO", "MISO"},
        {"4", "CS-A", "CSN", "CSN"},
        {"5", "SCK", "SCK", "SCK"},
        {"6", "PB2", "GDO0", "CE"},
        {"7", "CS-B", "CSN", "CSN"},
        {"8", "GND", "GND", "GND"},
        {"9", "3V3", "VCC", "VCC"},
    };
    for(int i = 0; i < 8; i++) {
        int y = 17 + i * 6;
        gfx_text(canvas, 2, y, rows[i][0]);
        gfx_text(canvas, 14, y, rows[i][1]);
        gfx_text(canvas, 50, y, rows[i][2]);
        gfx_text(canvas, 82, y, rows[i][3]);
    }
    int ycc = 17 + (a->cfg.pin_cc ? 5 : 2) * 6;
    int ynr = 17 + (a->cfg.pin_nrf ? 5 : 2) * 6;
    if(ycc == ynr) {
        gfx_text(canvas, 104, ycc, "<CC+NR");
    } else {
        gfx_text(canvas, 112, ycc, "<CC");
        gfx_text(canvas, 112, ynr, "<NR");
    }
}

static const char* const warn_text[2][5] = {
    {"PARALLEL GATHER", "RUNS BOTH RADIOS", "MORE HEAT + BATTERY", "BAD WIRING CAN", "DAMAGE THE FLIPPER"},
    {"TTX = TRANSMIT", "SEND ONLY SIGNALS", "YOU OWN OR MAY USE", "NO JAMMING. LOCAL", "LAW APPLIES."},
};

static void draw_warn_sign(Canvas* canvas, int cx, int cy, bool inv) {
    canvas_set_color(canvas, ColorBlack);
    for(int k = 0; k < 2; k++) {
        canvas_draw_line(canvas, cx - k, cy - 13, cx - 15 + k, cy + 12);
        canvas_draw_line(canvas, cx + k, cy - 13, cx + 15 - k, cy + 12);
        canvas_draw_line(canvas, cx - 15, cy + 12 - k, cx + 15, cy + 12 - k);
    }
    canvas_draw_box(canvas, cx - 1, cy - 4, 3, 9);
    canvas_draw_box(canvas, cx - 1, cy + 7, 3, 3);
    if(inv) {
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_box(canvas, cx - 17, cy - 15, 35, 30);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_warn(Canvas* canvas, FsApp* a) {
    FsUi* ui = &a->ui;
    int st = ui->warn_kind % 2;
    uint32_t el = furi_get_tick() - ui->warn_tick;
    bool blink = ((furi_get_tick() / 280) & 1) != 0;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    char b[28];
    gfx_text_center(canvas, 64, 1, st ? "! TTX WARNING !" : "! WARNING !");
    canvas_set_color(canvas, ColorBlack);

    draw_warn_sign(canvas, 22, 28, blink);
    canvas_draw_rframe(canvas, 1, 10, 42, 36, 3);
    for(int i = 0; i < 5; i++) {
        if(!warn_text[st][i][0]) continue;
        gfx_text(canvas, 45, 12 + i * 7, warn_text[st][i]);
    }
    gfx_hline(0, 127, 49);
    if(el < 1600) {
        snprintf(b, sizeof(b), "READ... %d", (int)((1600 - el) / 400) + 1);
        gfx_text_center(canvas, 64, 52, b);
    } else {
        gfx_text_center(canvas, 64, 52, st ? "HOLD OK: OPEN TTX" : "HOLD OK: ENABLE PG");
    }
    gfx_text_center(canvas, 64, 58, "BACK = CANCEL");
}

/* ================= saved decodes ================= */

static void draw_saved(Canvas* canvas, FsApp* a) {
    FsUi* ui = &a->ui;
    char b[40];
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    snprintf(b, sizeof(b), "SAVED DECODES %d", a->saved_n);
    gfx_text(canvas, 2, 1, b);
    gfx_text_right(canvas, 126, 1, "OK INFO  HOLD DEL");
    canvas_set_color(canvas, ColorBlack);
    int total = a->saved_n + (a->saved_n ? 1 : 0);
    if(a->saved_n == 0) {
        gfx_text_center(canvas, 64, 26, "NO SAVED DECODES");
        gfx_text_center(canvas, 64, 34, "ENABLE AUTO DECODE");
        gfx_text_center(canvas, 64, 42, "FILES: /SUBGHZ/FLIPSAY");
        return;
    }
    int sel = ui->saved_sel;
    int top = sel > 5 ? sel - 5 : 0;
    for(int i = 0; i < 6; i++) {
        int idx = top + i;
        if(idx >= total) break;
        int y = 10 + i * 9;
        bool s = idx == sel;
        if(s) {
            canvas_draw_box(canvas, 0, y - 1, 128, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        if(idx < a->saved_n) {
            snprintf(b, sizeof(b), "%d %s", idx + 1, a->saved_names[idx]);
            gfx_text(canvas, 3, y + 1, b);
        } else {
            gfx_text(canvas, 3, y + 1, "[ DELETE ALL ]");
        }
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_saved_info(Canvas* canvas, FsApp* a) {
    FsUi* ui = &a->ui;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    gfx_text(canvas, 2, 1, "DECODE INFO");
    gfx_text_right(canvas, 126, 1, "HOLD OK DELETE");
    canvas_set_color(canvas, ColorBlack);
    if(ui->saved_sel < a->saved_n) gfx_text(canvas, 2, 11, a->saved_names[ui->saved_sel]);
    gfx_hline(0, 127, 18);
    for(int i = 0; i < 4; i++) {
        if(a->saved_info[i][0]) gfx_text(canvas, 2, 22 + i * 8, a->saved_info[i]);
    }
    gfx_text(canvas, 2, 56, "OPEN IN SUB-GHZ APP TO USE");
}

/* ================= menu ================= */

static void menu_label(const FsApp* a, int id, char* out, size_t n) {
    int fam = fs_family(a);
    const char* s = menu_labels[id];
    if(id == M_BW && fam) s = "NRF RATE";
    if(id == M_MOD && fam) s = "MODULATION";
    if(id == M_FREEZE && a->ui.freeze) s = "RESUME";
    if(id == M_LOG && a->log_active) s = "STOP LOG";
    if(id == M_PG_ENABLE) s = a->pg_on ? "PG: DISABLE" : "PG: ENABLE...";
    if(id >= M_MU_S0 && id <= M_MU_S3) {
        int i = id - M_MU_S0;
        s = i < fs_slot_count(a->cfg.mu_layout) ? slot_pos[a->cfg.mu_layout % 5][i] : "UNUSED";
    }
    snprintf(out, n, "%s", s);
}

static bool menu_is_adjust(int id) {
    switch(id) {
    case M_SOURCE:
    case M_PRESET:
    case M_PIN_CC:
    case M_PIN_NRF:
    case M_SPEED:
    case M_RES:
    case M_BW:
    case M_RSSIOFF:
    case M_OTG:
    case M_GAIN_LNA:
    case M_GAIN_MIX:
    case M_GAIN_IF:
    case M_VIEW:
    case M_WAVE:
    case M_SCALE:
    case M_STYLE:
    case M_OVERLAY:
    case M_GRID:
    case M_WFPAL:
    case M_WFRATE:
    case M_CURSOR:
    case M_AVG:
    case M_HOLD:
    case M_BACKLIGHT:
    case M_SQUELCH:
    case M_TRIGACT:
    case M_ALERTS:
    case M_MOD:
    case M_GAP:
    case M_AUTODEC:
    case M_NOTIFY:
    case M_DECSENS:
    case M_PG_MODE:
    case M_PG_COMBINE:
    case M_OFF_INT:
    case M_OFF_EXT:
    case M_MU_FOCUS:
    case M_MU_EXT:
    case M_MU_INT:
    case M_MU_NRF:
    case M_MU_LAYOUT:
    case M_MU_S0:
    case M_MU_S1:
    case M_MU_S2:
    case M_MU_S3:
    case M_MU_NRFBAND:
    case M_WIFI_MODE:
    case M_LITE:
    case M_INTRO:
        return true;
    default:
        return false;
    }
}

static void fmt_off(char* out, size_t n, int v) {
    snprintf(out, n, "%c%dDB", v < 0 ? '-' : '+', v < 0 ? -v : v);
}

static void menu_value(const FsApp* a, int id, char* out, size_t n) {
    const FsConfig* c = &a->cfg;
    int fam = fs_family(a);
    out[0] = 0;
    switch(id) {
    case M_SOURCE:
        snprintf(out, n, "%s", a->pg_on ? "PG ACTIVE" : src_names[c->source % FsSrcCount]);
        break;
    case M_PRESET:
        snprintf(
            out,
            n,
            "%s",
            fam ? presets1[c->preset[1] % N_PRESETS1].name :
                  presets0[c->preset[0] % N_PRESETS0].name);
        break;
    case M_PIN_CC:
        snprintf(out, n, "%s", !FS_HAS_PIN_SELECT ? "PIN 4 FIXED" : (c->pin_cc ? "PIN 7" : "PIN 4"));
        break;
    case M_PIN_NRF:
        snprintf(out, n, "%s", !FS_HAS_PIN_SELECT ? "PIN 4 FIXED" : (c->pin_nrf ? "PIN 7" : "PIN 4"));
        break;
    case M_RES:
        snprintf(out, n, "%d BINS", c->res ? 64 : 128);
        break;
    case M_SPEED:
        snprintf(out, n, "%s", speed_names[c->speed & 3]);
        break;
    case M_BW:
        if(fam)
            snprintf(out, n, "%s", c->nrf_rate ? "2 MBPS" : "1 MBPS");
        else
            snprintf(out, n, "%s", bw_names[c->bw % 7]);
        break;
    case M_RSSIOFF:
        fmt_off(out, n, c->rssi_off);
        break;
    case M_OTG:
        snprintf(out, n, "%s", c->otg ? "ON" : "OFF");
        break;
    case M_GAIN_LNA:
        snprintf(out, n, "%s", lna_names[c->gain_lna % 9]);
        break;
    case M_GAIN_MIX:
        snprintf(out, n, "%s", mix_names[c->gain_dvga % 5]);
        break;
    case M_GAIN_IF:
        snprintf(out, n, "%s", if_names[c->gain_tgt % 9]);
        break;
    case M_VIEW:
        snprintf(out, n, "%s", fs_view_names[c->view % FsViewCount]);
        break;
    case M_WAVE:
        snprintf(out, n, "%s", wave_names[c->wave % 3]);
        break;
    case M_SCALE:
        snprintf(out, n, "%s", fam ? "FIXED" : scale_names[c->scale % 7]);
        break;
    case M_STYLE:
        snprintf(out, n, "%s", style_names[c->style & 3]);
        break;
    case M_OVERLAY:
        snprintf(out, n, "%s", overlay_names[c->overlay & 3]);
        break;
    case M_GRID:
        snprintf(out, n, "%s", c->grid ? "ON" : "OFF");
        break;
    case M_WFPAL:
        snprintf(out, n, "%s", pal_names[c->wf_pal & 3]);
        break;
    case M_WFRATE:
        snprintf(out, n, "%s", wfrate_names[c->wf_rate & 3]);
        break;
    case M_CURSOR:
        snprintf(out, n, "%s", c->track ? "FOLLOW PEAK" : "CENTER");
        break;
    case M_AVG:
        snprintf(out, n, "%s", avg_names[c->avg % 5]);
        break;
    case M_HOLD:
        snprintf(out, n, "%s", hold_names[c->hold % 5]);
        break;
    case M_BACKLIGHT:
        snprintf(out, n, "%s", c->backlight ? "ALWAYS ON" : "AUTO");
        break;
    case M_SQUELCH:
        if(c->squelch == 0)
            snprintf(out, n, "OFF");
        else if(fam)
            snprintf(out, n, "%d%%", c->squelch * 6);
        else
            snprintf(out, n, "%dDBM", -105 + 5 * (c->squelch - 1));
        break;
    case M_TRIGACT:
        snprintf(out, n, "%s", trig_names[c->trig_act & 3]);
        break;
    case M_ALERTS:
        snprintf(out, n, "%s", alert_names[c->alerts & 3]);
        break;
    case M_MOD:
        snprintf(out, n, "%s", fam ? "N/A" : mod_names[c->mod % 7]);
        break;
    case M_GAP:
        snprintf(out, n, "%s", gap_names[c->scope_gap % 5]);
        break;
    case M_AUTODEC:
        snprintf(out, n, "%s", c->auto_decode ? "ON" : "OFF");
        break;
    case M_NOTIFY:
        snprintf(out, n, "%s", c->notify ? "ON" : "OFF");
        break;
    case M_DECSENS:
        snprintf(out, n, "%s", sens_names[c->dec_sens % 3]);
        break;
    case M_SAVED:
        snprintf(out, n, "OPEN");
        break;
    case M_DECSTAT:
        snprintf(out, n, "%lu OK / %lu HITS", a->dec_saved, a->dec_seen);
        break;
    case M_PG_ENABLE:
        snprintf(out, n, "%s", a->pg_on ? "ON" : "OFF");
        break;
    case M_PG_MODE:
        snprintf(out, n, "%s", c->pg_mode ? "MULTI USE" : "MERGED");
        break;
    case M_PG_COMBINE:
        snprintf(out, n, "%s", c->pg_combine ? "AVERAGE" : "BEST OF 2");
        break;
    case M_OFF_INT:
        fmt_off(out, n, c->off_int);
        break;
    case M_OFF_EXT:
        fmt_off(out, n, c->off_ext);
        break;
    case M_MU_FOCUS:
        snprintf(out, n, "WINDOW %d", (a->ui.focus % 4) + 1);
        break;
    case M_MU_EXT:
        snprintf(out, n, "%s", presets0[c->preset[0] % N_PRESETS0].name);
        break;
    case M_MU_INT:
        snprintf(out, n, "%s", presets0[c->pad[0] % N_PRESETS0].name);
        break;
    case M_MU_NRF:
        snprintf(out, n, "%s", nrfscan_names[c->mu_nrf_speed % 3]);
        break;
    case M_MU_LAYOUT:
        snprintf(out, n, "%s", layout_names[c->mu_layout % 5]);
        break;
    case M_MU_S0:
    case M_MU_S1:
    case M_MU_S2:
    case M_MU_S3: {
        int i = id - M_MU_S0;
        snprintf(
            out,
            n,
            "%s",
            i < fs_slot_count(c->mu_layout) ? slot_names[c->mu_slot[i] % FsSlotCount] : "-");
        break;
    }
    case M_MU_NRFBAND:
        snprintf(out, n, "%s", presets1[c->preset[1] % N_PRESETS1].name);
        break;
    case M_WIFI_MODE:
        snprintf(out, n, "%s", wifimode_names[c->wifi_mode % 4]);
        break;
    case M_SYS_MEM:
        snprintf(
            out,
            n,
            "%uK / %uK",
            (unsigned)(memmgr_get_free_heap() / 1024),
            (unsigned)(memmgr_heap_get_max_free_block() / 1024));
        break;
    case M_SYS_STACK: {
        unsigned m = (unsigned)furi_thread_get_stack_space(a->main_id);
        unsigned w = a->worker ? (unsigned)furi_thread_get_stack_space(furi_thread_get_id(a->worker)) : 0;
        snprintf(out, n, "M %u  W %u", m, w);
        break;
    }
    case M_LITE:
        snprintf(out, n, "%s", c->lite ? "ON" : "OFF");
        break;
    case M_INTRO:
        snprintf(out, n, "%s", c->intro ? "FAST" : "FULL");
        break;
    default:
        break;
    }
}

static void do_cap_request(FsApp* a, uint32_t khz) {
    if(a->cap_req) return;
    if(fs_family(a)) {
        fs_ui_toast(a, "CAPTURE: CC1101 ONLY");
        return;
    }
    a->cap_khz = khz;
    a->cap_req = true;
    fs_ui_toast(a, "CAPTURE ARMED...");
}

static void log_event(FsApp* a) {
    File* f = storage_file_alloc(a->storage);
    if(storage_file_open(f, FS_DIR "/events.csv", FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char line[64];
        FsChan* ch = a->ch[0];
        int fam = ch ? ch->geo.fam : 0;
        int n = snprintf(
            line,
            sizeof(line),
            "%lu,%lu,%d\n",
            furi_get_tick(),
            a->trig_khz,
            fam ? a->trig_lvl * 100 / 255 : fs_lvl_to_dbm(a->trig_lvl));
        storage_file_write(f, line, n);
        storage_file_close(f);
    }
    storage_file_free(f);
}

static void menu_adjust(FsApp* a, int id, int dir) {
    FsConfig* c = &a->cfg;
    int fam = fs_family(a);
    switch(id) {
    case M_SOURCE:
        if(a->pg_on) {
            fs_ui_toast(a, "DISABLE PG FIRST");
            break;
        }
        cycle_u8(&c->source, FsSrcCount, dir);
        a->restart_req = true;
        fs_ui_toast(a, "SWITCHING RADIO...");
        break;
    case M_PRESET:
        if(fam)
            c->preset[1] = (uint8_t)((c->preset[1] + dir + N_PRESETS1) % N_PRESETS1);
        else
            c->preset[0] = (uint8_t)((c->preset[0] + dir + N_PRESETS0) % N_PRESETS0);
        apply_preset_fam(a, fam);
        break;
    case M_PIN_CC:
        if(!FS_HAS_PIN_SELECT) {
            fs_ui_toast(a, "CS PIN FIXED ON THIS FW");
            break;
        }
        c->pin_cc ^= 1;
        a->restart_req = true;
        fs_ui_toast(a, c->pin_cc ? "CC1101 CS = PIN 7" : "CC1101 CS = PIN 4");
        break;
    case M_PIN_NRF:
        if(!FS_HAS_PIN_SELECT) {
            fs_ui_toast(a, "CS PIN FIXED ON THIS FW");
            break;
        }
        c->pin_nrf ^= 1;
        a->restart_req = true;
        fs_ui_toast(a, c->pin_nrf ? "NRF24 CS = PIN 7" : "NRF24 CS = PIN 4");
        break;
    case M_RES:
        c->res ^= 1;
        break;
    case M_SPEED:
        cycle_u8(&c->speed, 4, dir);
        break;
    case M_BW:
        if(fam)
            c->nrf_rate ^= 1;
        else
            cycle_u8(&c->bw, 7, dir);
        a->hw_rev++;
        break;
    case M_RSSIOFF:
        c->rssi_off = (int8_t)clampi(c->rssi_off + dir, -20, 20);
        break;
    case M_OTG:
        c->otg ^= 1;
        a->restart_req = true;
        fs_ui_toast(a, "RESTARTING RADIO...");
        break;
    case M_GAIN_LNA:
        cycle_u8(&c->gain_lna, 9, dir);
        a->hw_rev++;
        break;
    case M_GAIN_MIX:
        cycle_u8(&c->gain_dvga, 5, dir);
        a->hw_rev++;
        break;
    case M_GAIN_IF:
        cycle_u8(&c->gain_tgt, 9, dir);
        a->hw_rev++;
        break;
    case M_VIEW:
        fs_ui_set_view(a, (FsView)((c->view + FsViewCount + dir) % FsViewCount));
        break;
    case M_WAVE:
        cycle_u8(&c->wave, 3, dir);
        break;
    case M_SCALE:
        cycle_u8(&c->scale, 7, dir);
        break;
    case M_STYLE:
        cycle_u8(&c->style, 4, dir);
        break;
    case M_OVERLAY:
        cycle_u8(&c->overlay, 4, dir);
        break;
    case M_GRID:
        c->grid ^= 1;
        break;
    case M_WFPAL:
        cycle_u8(&c->wf_pal, 4, dir);
        break;
    case M_WFRATE:
        cycle_u8(&c->wf_rate, 4, dir);
        break;
    case M_CURSOR:
        c->track ^= 1;
        break;
    case M_AVG:
        cycle_u8(&c->avg, 5, dir);
        break;
    case M_HOLD:
        cycle_u8(&c->hold, 5, dir);
        break;
    case M_BACKLIGHT:
        c->backlight ^= 1;
        notification_message(
            a->notif,
            c->backlight ? &sequence_display_backlight_enforce_on :
                           &sequence_display_backlight_enforce_auto);
        break;
    case M_SQUELCH:
        cycle_u8(&c->squelch, 16, dir);
        break;
    case M_TRIGACT:
        cycle_u8(&c->trig_act, 4, dir);
        break;
    case M_ALERTS:
        cycle_u8(&c->alerts, 4, dir);
        break;
    case M_MOD:
        if(!fam) {
            cycle_u8(&c->mod, 7, dir);
            a->hw_rev++;
        }
        break;
    case M_GAP:
        cycle_u8(&c->scope_gap, 5, dir);
        a->hw_rev++;
        break;
    case M_AUTODEC:
        c->auto_decode ^= 1;
        if(c->auto_decode) {
            if(fam)
                fs_ui_toast(a, "AUTO DECODE: CC1101 ONLY");
            else
                fs_ui_toast(a, "AUTO DECODE ON");
        }
        a->hw_rev++;
        break;
    case M_NOTIFY:
        c->notify ^= 1;
        if(c->notify) notify_set(a, 0, "Princeton 24bit", "Key:0x00AB12CD  433.92");
        break;
    case M_DECSENS:
        cycle_u8(&c->dec_sens, 3, dir);
        break;
    case M_PG_MODE:
        cycle_u8(&c->pg_mode, 2, dir);
        if(a->pg_on) {
            a->restart_req = true;
            fs_ui_toast(a, "RESTARTING PG...");
        }
        break;
    case M_PG_COMBINE:
        cycle_u8(&c->pg_combine, 2, dir);
        break;
    case M_OFF_INT:
        c->off_int = (int8_t)clampi(c->off_int + dir, -20, 20);
        break;
    case M_OFF_EXT:
        c->off_ext = (int8_t)clampi(c->off_ext + dir, -20, 20);
        break;
    case M_MU_FOCUS: {
        int cnt = fs_slot_count(c->mu_layout);
        a->ui.focus = (uint8_t)((a->ui.focus % cnt + cnt + dir) % cnt);
        break;
    }
    case M_MU_LAYOUT:
        cycle_u8(&c->mu_layout, 5, dir);
        a->ui.focus = 0;
        break;
    case M_MU_S0:
    case M_MU_S1:
    case M_MU_S2:
    case M_MU_S3: {
        int i = id - M_MU_S0;
        if(i < fs_slot_count(c->mu_layout)) cycle_u8(&c->mu_slot[i], FsSlotCount, dir);
        break;
    }
    case M_MU_NRFBAND:
        c->preset[1] = (uint8_t)((c->preset[1] + dir + N_PRESETS1) % N_PRESETS1);
        apply_preset_fam(a, 1);
        break;
    case M_WIFI_MODE:
        cycle_u8(&c->wifi_mode, 4, dir);
        break;
    case M_MU_EXT:
        c->preset[0] = (uint8_t)((c->preset[0] + dir + N_PRESETS0) % N_PRESETS0);
        apply_preset_fam(a, 0);
        break;
    case M_MU_INT:
        c->pad[0] = (uint8_t)((c->pad[0] + dir + N_PRESETS0) % N_PRESETS0);
        c->int_center_khz = presets0[c->pad[0] % N_PRESETS0].center;
        c->int_span_khz = presets0[c->pad[0] % N_PRESETS0].span;
        fs_geo_normalize_int(c);
        break;
    case M_MU_NRF:
        cycle_u8(&c->mu_nrf_speed, 3, dir);
        break;
    case M_LITE:
        c->lite ^= 1;
        a->restart_req = true;
        fs_ui_toast(a, c->lite ? "LITE MODE ON" : "LITE MODE OFF");
        break;
    case M_INTRO:
        c->intro ^= 1;
        break;
    default:
        break;
    }
}

static void reset_channels(FsApp* a) {
    for(int i = 0; i < 3; i++)
        if(a->ch[i]) fs_chan_reset(a->ch[i]);
}

static bool menu_activate(FsApp* a, int id) {
    FsUi* ui = &a->ui;
    FsChan* ch = a->ch[0];
    switch(id) {
    case M_PINS_VIEW:
        ui->mode = FsUiPins;
        break;
    case M_GAIN_RESET:
        a->cfg.gain_lna = 0;
        a->cfg.gain_dvga = 0;
        a->cfg.gain_tgt = 0;
        a->hw_rev++;
        fs_ui_toast(a, "GAIN: AUTO");
        break;
    case M_SAVED:
        fs_saved_load(a);
        ui->saved_sel = 0;
        ui->saved_armed = 0;
        ui->mode = FsUiSaved;
        break;
    case M_DECSTAT:
        break;
    case M_PG_ENABLE:
        if(a->pg_on) {
            a->pg_on = false;
            a->restart_req = true;
            fs_ui_toast(a, "PARALLEL GATHER OFF");
        } else {
            ui->warn_kind = 0;
            ui->warn_tick = furi_get_tick();
            ui->mode = FsUiWarn;
        }
        break;
    case M_REPLAY:
        ui->mode = FsUiSplash;
        ui->mode_tick = furi_get_tick();
        ui->splash_out = 0;
        ui->splash_skip = false;
        break;
    case M_MU_CLEAR:
        a->n_pkts = 0;
        a->pkt_total = 0;
        if(a->wifi) {
            a->wifi->n_aps = 0;
            a->wifi->total = 0;
            memset(a->wifi->hist, 0, sizeof(a->wifi->hist));
            memset(a->wifi->log, 0, sizeof(a->wifi->log));
        }
        fs_ui_toast(a, "PACKETS CLEARED");
        break;
    case M_FREEZE:
        ui->freeze = !ui->freeze;
        ui->mode = FsUiView;
        fs_ui_toast(a, ui->freeze ? "HOLD" : "RUNNING");
        break;
    case M_RESET:
        reset_channels(a);
        ui->mode = FsUiView;
        fs_ui_toast(a, "RESET");
        break;
    case M_CURPEAK:
        if(ch && ch->n_peaks) ui->cursor = ch->peaks[0].col;
        ui->mode = FsUiView;
        break;
    case M_MARKB:
        if(ui->marker_b_khz) {
            ui->marker_b_khz = 0;
        } else if(ch) {
            ui->marker_b_khz = fs_col_khz(&ch->geo, ui->cursor);
        }
        ui->mode = FsUiView;
        fs_ui_toast(a, ui->marker_b_khz ? "MARKER B SET" : "MARKER B CLEARED");
        break;
    case M_BMADD: {
        int fam = fs_family(a);
        if(a->bm.count >= FS_MAX_BM) {
            fs_ui_toast(a, "BOOKMARKS FULL");
            break;
        }
        FsBookmark* b = &a->bm.items[a->bm.count++];
        b->center_khz = a->cfg.center_khz[fam];
        b->span_khz = a->cfg.span_khz[fam];
        b->fam = (uint8_t)fam;
        b->src = a->cfg.source;
        char tmp[16];
        fmt_mhz(tmp, sizeof(tmp), b->center_khz, fam ? 1 : 3);
        memset(b->name, 0, sizeof(b->name));
        strncpy(b->name, tmp, sizeof(b->name) - 1);
        fs_bm_save(a);
        ui->mode = FsUiView;
        fs_ui_toast(a, "BOOKMARK SAVED");
        break;
    }
    case M_BMLIST:
        ui->mode = FsUiBookmarks;
        ui->bm_sel = 0;
        break;
    case M_LOG:
        a->log_req = a->log_active ? 2 : 1;
        ui->mode = FsUiView;
        break;
    case M_SNAP:
        a->snap_req = true;
        ui->mode = FsUiView;
        break;
    case M_CAPTURE: {
        FsView v = (FsView)a->cfg.view;
        uint32_t f = (v == FsViewScope || v == FsViewMeter) ?
                         a->cfg.lock_khz[0] :
                         (ch ? fs_col_khz(&ch->geo, ui->cursor) : 433920);
        do_cap_request(a, f);
        ui->mode = FsUiView;
        break;
    }
    case M_SAVE:
        fs_cfg_save(a);
        fs_ui_toast(a, "SETTINGS SAVED");
        ui->mode = FsUiView;
        break;
    default:
        menu_adjust(a, id, 1);
        break;
    }
    return true;
}

static int menu_count(const FsUi* ui) {
    return ui->menu_page == PAGE_ROOT ? ROOT_COUNT : page_count[ui->menu_page % P_COUNT];
}

static bool menu_show_value(int id) {
    return menu_is_adjust(id) || id == M_SAVED || id == M_DECSTAT || id == M_PG_ENABLE ||
           id == M_SYS_MEM || id == M_SYS_STACK;
}

static void draw_menu(Canvas* canvas, FsApp* a) {
    FsUi* ui = &a->ui;
    char b[32];
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    if(ui->menu_page == PAGE_ROOT) {
        gfx_text(canvas, 2, 1, "OPTIONS");
        gfx_text_right(canvas, 126, 1, "HOLD BACK = EXIT");
    } else {
        snprintf(
            b,
            sizeof(b),
            "%s %02d/%02d",
            page_names[ui->menu_page % P_COUNT],
            ui->menu_sel + 1,
            menu_count(ui));
        gfx_text(canvas, 2, 1, b);
    }
    canvas_set_color(canvas, ColorBlack);

    int cnt = menu_count(ui);
    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < 5; i++) {
        int idx = ui->menu_top + i;
        if(idx >= cnt) break;
        int y = 10 + i * 10;
        bool sel = idx == ui->menu_sel;
        if(sel) {
            canvas_draw_box(canvas, 0, y, 125, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        if(ui->menu_page == PAGE_ROOT) {
            const char* s = idx < P_COUNT ? page_names[idx] :
                            idx == ROOT_SDR   ? "SDR MODE (GQRX)" :
                            idx == ROOT_TTX   ? "TTX (TRANSMIT)" :
                            idx == ROOT_ABOUT ? "ABOUT FLIPSAY" :
                                                "EXIT APP";
            canvas_draw_str(canvas, 3, y + 8, s);
            if(idx < P_COUNT) gfx_text(canvas, 119, y + 3, ">");
        } else {
            int id = page_items[ui->menu_page % P_COUNT][idx];
            menu_label(a, id, b, sizeof(b));
            canvas_draw_str(canvas, 3, y + 8, b);
            if(menu_show_value(id)) {
                menu_value(a, id, b, sizeof(b));
                uint16_t w = canvas_string_width(canvas, b);
                canvas_draw_str(canvas, 119 - w, y + 8, b);
                if(sel && menu_is_adjust(id)) {
                    gfx_text(canvas, 119 - w - 5, y + 3, "<");
                    gfx_text(canvas, 121, y + 3, ">");
                }
            }
        }
        canvas_set_color(canvas, ColorBlack);
    }
    canvas_draw_line(canvas, 127, 10, 127, 62);
    int th = 52 * 5 / (cnt > 5 ? cnt : 5);
    if(th < 3) th = 3;
    int ty = cnt > 5 ? 10 + (52 - th) * ui->menu_top / (cnt - 5) : 10;
    canvas_draw_box(canvas, 126, ty, 2, th);
}

static void draw_bookmarks(Canvas* canvas, FsApp* a) {
    FsUi* ui = &a->ui;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    gfx_text(canvas, 2, 1, "BOOKMARKS");
    gfx_text_right(canvas, 126, 1, "OK LOAD  HOLD DEL");
    canvas_set_color(canvas, ColorBlack);
    if(a->bm.count == 0) {
        gfx_text_center(canvas, 64, 28, "NO BOOKMARKS");
        gfx_text_center(canvas, 64, 36, "TOOLS > ADD BOOKMARK");
        return;
    }
    int top = ui->bm_sel > 4 ? ui->bm_sel - 4 : 0;
    for(int i = 0; i < 6; i++) {
        int idx = top + i;
        if(idx >= (int)a->bm.count) break;
        int y = 11 + i * 9;
        FsBookmark* b = &a->bm.items[idx];
        bool sel = idx == ui->bm_sel;
        if(sel) {
            canvas_draw_box(canvas, 0, y - 1, 128, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        char t[40], sp[12];
        fmt_span(sp, sizeof(sp), b->span_khz);
        snprintf(t, sizeof(t), "%d %sM  S%s  %s", idx + 1, b->name, sp, b->fam ? "NRF24" : "SUB-GHZ");
        gfx_text(canvas, 3, y + 1, t);
        canvas_set_color(canvas, ColorBlack);
    }
}

/* ================= overlays ================= */

static void fit_text(Canvas* canvas, const char* src, char* dst, size_t n, int maxw) {
    size_t o = 0;
    dst[0] = 0;
    while(src[o] && o < n - 1) {
        dst[o] = src[o];
        dst[o + 1] = 0;
        if(canvas_string_width(canvas, dst) > maxw) {
            dst[o] = 0;
            return;
        }
        o++;
    }
}

static void draw_notify(Canvas* canvas, FsApp* a) {
    if(!a->nt_start) return;
    uint32_t t = furi_get_tick() - a->nt_start;
    if(t >= 5000) return;
    int slide;
    if(t < 240)
        slide = gfx_ease_out((int)t * 256 / 240);
    else if(t > 4760)
        slide = 256 - gfx_ease_in((int)(t - 4760) * 256 / 240);
    else
        slide = 256;
    const int h = 28, x = 4, w = 120;
    int y = -h - 6 + (h + 6 + 2) * slide / 256;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_rbox(canvas, x - 1, y - 1, w + 2, h + 2, 7);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, x, y, w, h, 6);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_disc(canvas, x + 14, y + 14, 9);
    canvas_set_color(canvas, ColorBlack);
    int cx = x + 14, cy = y + 14;
    if(a->nt_kind == 0) {
        canvas_draw_circle(canvas, cx - 4, cy, 2);
        canvas_draw_line(canvas, cx - 2, cy, cx + 5, cy);
        canvas_draw_line(canvas, cx + 3, cy, cx + 3, cy + 3);
        canvas_draw_line(canvas, cx + 5, cy, cx + 5, cy + 2);
    } else {
        for(int i = -4; i <= 4; i += 2) {
            int hh = 2 + (i < 0 ? -i : i) / 2 * 2;
            hh = 8 - hh;
            canvas_draw_line(canvas, cx + i, cy - hh / 2, cx + i, cy + hh / 2);
        }
    }

    canvas_set_color(canvas, ColorWhite);
    gfx_text(canvas, x + 27, y + 3, "FLIPSAY");
    gfx_text_right(canvas, x + w - 5, y + 3, "NOW");
    char fit[28];
    canvas_set_font(canvas, FontPrimary);
    fit_text(canvas, a->nt_title, fit, sizeof(fit), w - 32);
    canvas_draw_str(canvas, x + 27, y + 16, fit);
    canvas_set_font(canvas, FontSecondary);
    fit_text(canvas, a->nt_text, fit, sizeof(fit), w - 32);
    canvas_draw_str(canvas, x + 27, y + 25, fit);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_bottom_bar(Canvas* canvas, const char* text) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 56, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    gfx_text_center(canvas, 64, 58, text);
    canvas_set_color(canvas, ColorBlack);
}

/* ================= main draw ================= */

void fs_ui_draw(Canvas* canvas, void* ctx) {
    FsApp* a = ctx;
    furi_mutex_acquire(a->mtx, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    gfx_bind(canvas);

    FsUi* ui = &a->ui;
    FsChan* ch = a->ch[0];
    uint32_t now = furi_get_tick();
    bool overlays = true;

    switch(ui->mode) {
    case FsUiSplash:
        draw_splash(canvas, a);
        overlays = false;
        break;
    case FsUiExiting: {
        gfx_fill_black();
        uint32_t t = now - ui->exit_tick;
        gfx_text_px(37, 26, "EXITING", 2, 16, true);
        gfx_text_px(30, 42, "SEE YOU, OPERATOR", 1, 16, true);
        gfx_dissolve(16 - clampi((int)t * 16 / 300, 0, 16));
        overlays = false;
        break;
    }
    case FsUiMenu:
        draw_menu(canvas, a);
        break;
    case FsUiBookmarks:
        draw_bookmarks(canvas, a);
        break;
    case FsUiHelp:
        draw_help(canvas, a);
        break;
    case FsUiPins:
        draw_pins(canvas, a);
        break;
    case FsUiWarn:
        draw_warn(canvas, a);
        overlays = false;
        break;
    case FsUiSaved:
        draw_saved(canvas, a);
        break;
    case FsUiSavedInfo:
        draw_saved_info(canvas, a);
        break;
    case FsUiAbout:
        fs_about_draw(canvas, a);
        break;
    case FsUiSdr:
        fs_sdr_draw(canvas, a);
        overlays = false;
        break;
    case FsUiTtxBrowse:
    case FsUiTtxSetup:
        fs_ttx_draw(canvas, a);
        break;
    default: {
        if(!ch) break;
        if(a->status == FsStatusOk && a->multi_active) {
            draw_multi(canvas, a);
            break;
        }
        FsView v = (FsView)a->cfg.view;
        draw_header(canvas, a, ch);
        if(a->status != FsStatusOk) {
            draw_status_box(canvas, a);
            break;
        }
        switch(v) {
        case FsViewSpectrum:
            plot_spectrum(canvas, a, ch, 0, 128, PL_Y0, PL_Y1, true, true);
            draw_axis(canvas, a, ch, PL_Y1);
            break;
        case FsViewWaterfall:
            plot_waterfall(a, ch, 0, 128, PL_Y0, PL_Y1);
            draw_cursor_lines(a, ch, PL_Y0, PL_Y1);
            draw_axis(canvas, a, ch, PL_Y1);
            break;
        case FsViewCombo:
            plot_spectrum(canvas, a, ch, 0, 128, PL_Y0, 30, false, true);
            gfx_hline_dot(0, 127, 31, 2);
            plot_waterfall(a, ch, 0, 128, 33, PL_Y1);
            draw_cursor_lines(a, ch, 33, PL_Y1);
            draw_axis(canvas, a, ch, PL_Y1);
            break;
        case FsViewPersist:
            plot_persist(canvas, a, ch);
            draw_cursor_lines(a, ch, PL_Y0, PL_Y1);
            draw_axis(canvas, a, ch, PL_Y1);
            break;
        case FsViewScope:
            draw_scope(canvas, a, ch);
            break;
        case FsViewMeter:
            draw_meter(canvas, a, ch);
            break;
        case FsViewSignals:
            draw_signals(canvas, a, ch);
            break;
        case FsViewChannels:
            if(ch->geo.fam)
                draw_bars_nrf(canvas, a, ch, a->cfg.ch_page % 3);
            else
                draw_bars_sub(canvas, a, ch, a->cfg.ch_page % 3);
            break;
        default:
            break;
        }
        break;
    }
    }

    if(a->snap_req && ui->mode == FsUiView) {
        if(!a->snap_buf) a->snap_buf = malloc(1024);
        memcpy(a->snap_buf, canvas_get_buffer(canvas), 1024);
        a->snap_req = false;
        a->snap_ready = true;
    }

    if(overlays) {
        draw_notify(canvas, a);
        if(a->dec_state == 2 && ui->mode == FsUiView) {
            char b[32], f[16];
            fmt_mhz(f, sizeof(f), a->dec_khz, 3);
            snprintf(b, sizeof(b), "DECODING %sM%s", f, ((now / 250) & 3) == 0 ? "" : ((now / 250) & 3) == 1 ? "." : ((now / 250) & 3) == 2 ? ".." : "...");
            draw_bottom_bar(canvas, b);
        } else if(ui->toast_until && now < ui->toast_until) {
            draw_bottom_bar(canvas, ui->toast);
        }
    }

    if(ui->fade_start) {
        uint32_t el = now - ui->fade_start;
        if(el < FADE_IN)
            gfx_dissolve((int)el * 16 / FADE_IN);
        else
            ui->fade_start = 0;
    }
    furi_mutex_release(a->mtx);
}

/* ================= input ================= */

static int win_fam(FsApp* a, int win) {
    return win == 1 ? 0 : (win == 2 ? 1 : fs_family(a));
}

static uint32_t* win_center(FsApp* a, int win, int fam) {
    return win == 1 ? &a->cfg.int_center_khz : &a->cfg.center_khz[fam];
}

static uint32_t* win_span(FsApp* a, int win, int fam) {
    return win == 1 ? &a->cfg.int_span_khz : &a->cfg.span_khz[fam];
}

static void win_norm(FsApp* a, int win, int fam) {
    if(win == 1)
        fs_geo_normalize_int(&a->cfg);
    else
        fs_geo_normalize(&a->cfg, fam);
}

static void pan_win(FsApp* a, int win, int dir, int mult) {
    int fam = win_fam(a, win);
    uint32_t step = *win_span(a, win, fam) / 4;
    if(step < 1) step = 1;
    step *= (uint32_t)mult;
    uint32_t cen = *win_center(a, win, fam);
    if(dir < 0)
        cen = cen > step ? cen - step : 0;
    else
        cen += step;
    *win_center(a, win, fam) = cen;
    win_norm(a, win, fam);
}

static void zoom_win(FsApp* a, int win, int dir, uint32_t anchor) {
    int fam = win_fam(a, win);
    uint32_t span = *win_span(a, win, fam);
    span = dir > 0 ? span / 2 : span * 2;
    *win_span(a, win, fam) = span;
    if(anchor) *win_center(a, win, fam) = anchor;
    win_norm(a, win, fam);
    if(win == 0) {
        uint32_t sp = *win_span(a, 0, fam);
        uint32_t cen = *win_center(a, 0, fam);
        uint32_t start = cen - sp / 2;
        if(anchor) {
            int col = anchor <= start ? 0 : (int)(((anchor - start) * 127 + sp / 2) / sp);
            a->ui.cursor = (uint8_t)clampi(col, 0, 127);
        } else {
            a->ui.cursor = 64;
        }
    }
}

static void tune_lock(FsApp* a, int dir, int n) {
    int fam = fs_family(a);
    if(fam) {
        int v = (int)a->cfg.lock_khz[1] + dir * 1000 * n;
        a->cfg.lock_khz[1] = (uint32_t)clampi(v, 2400000, 2527000);
    } else {
        int v = (int)a->cfg.lock_khz[0] + dir * (int)step_tab[a->cfg.step_idx % 6] * n;
        a->cfg.lock_khz[0] = (uint32_t)clampi(v, 281000, 962000);
    }
}

static void open_menu(FsApp* a) {
    a->ui.mode = FsUiMenu;
    a->ui.menu_page = PAGE_ROOT;
    a->ui.menu_sel = 0;
    a->ui.menu_top = 0;
}

static void hop_preset(FsApp* a, int win) {
    FsConfig* c = &a->cfg;
    int fam = win_fam(a, win);
    const char* name;
    if(win == 1) {
        c->pad[0] = (uint8_t)((c->pad[0] + 1) % N_PRESETS0);
        c->int_center_khz = presets0[c->pad[0]].center;
        c->int_span_khz = presets0[c->pad[0]].span;
        fs_geo_normalize_int(c);
        name = presets0[c->pad[0]].name;
    } else if(fam) {
        c->preset[1] = (uint8_t)((c->preset[1] + 1) % N_PRESETS1);
        apply_preset_fam(a, 1);
        name = presets1[c->preset[1]].name;
    } else {
        c->preset[0] = (uint8_t)((c->preset[0] + 1) % N_PRESETS0);
        apply_preset_fam(a, 0);
        name = presets0[c->preset[0]].name;
    }
    char b[28];
    snprintf(b, sizeof(b), "BAND %s", name);
    fs_ui_toast(a, b);
}

static int scroll_mult(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    if(ev->type == InputTypeShort) {
        ui->scroll_rep = 0;
        return 1;
    }
    if(ui->scroll_rep < 40) ui->scroll_rep++;
    int sh = ui->scroll_rep / 2;
    if(sh > 6) sh = 6;
    return 1 << sh;
}

static bool view_input(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    FsChan* ch = a->ch[0];
    FsView v = (FsView)a->cfg.view;
    bool sh = ev->type == InputTypeShort;
    bool lg = ev->type == InputTypeLong;
    bool rp = ev->type == InputTypeRepeat;
    bool spec = (v <= FsViewPersist);
    int fam = fs_family(a);

    switch(ev->key) {
    case InputKeyBack:
        if(sh) fs_ui_set_view(a, (FsView)((v + 1) % FsViewCount));
        break;
    case InputKeyOk:
        if(sh)
            open_menu(a);
        else if(lg && spec)
            hop_preset(a, 0);
        else if(lg)
            open_menu(a);
        break;
    case InputKeyLeft:
    case InputKeyRight: {
        int dir = ev->key == InputKeyLeft ? -1 : 1;
        if(spec) {
            pan_win(a, 0, dir, scroll_mult(a, ev));
        } else if(v == FsViewMeter || v == FsViewScope) {
            if(sh)
                tune_lock(a, dir, 1);
            else if(lg || rp)
                tune_lock(a, dir, 5);
        } else if(v == FsViewSignals) {
            if(sh && ch && ch->n_peaks) {
                int sel = clampi(ui->sig_sel, 0, ch->n_peaks - 1);
                uint32_t f = fs_col_khz(&ch->geo, ch->peaks[sel].col);
                if(dir > 0) {
                    a->cfg.center_khz[fam] = f;
                    a->cfg.span_khz[fam] = a->cfg.span_khz[fam] / 4;
                    fs_geo_normalize(&a->cfg, fam);
                    ui->cursor = 64;
                    fs_ui_set_view(a, FsViewSpectrum);
                } else {
                    do_cap_request(a, f);
                }
            }
        } else if(v == FsViewChannels) {
            if(sh) cycle_u8(&a->cfg.ch_page, 3, dir);
        }
        break;
    }
    case InputKeyUp:
    case InputKeyDown: {
        int dir = ev->key == InputKeyUp ? 1 : -1;
        if(spec) {
            if(sh) {
                uint32_t anchor = ch ? fs_col_khz(&ch->geo, ui->cursor) : 0;
                zoom_win(a, 0, dir, anchor);
            } else if(lg && dir > 0) {
                ui->freeze = !ui->freeze;
                fs_ui_toast(a, ui->freeze ? "HOLD" : "RUNNING");
            } else if(lg) {
                reset_channels(a);
                fs_ui_toast(a, "RESET");
            }
        } else if(v == FsViewMeter || v == FsViewScope) {
            if(sh && !fam) {
                int s = (int)a->cfg.step_idx + dir;
                a->cfg.step_idx = (uint8_t)clampi(s, 0, 5);
            }
            if(lg && dir > 0) {
                ui->freeze = !ui->freeze;
                fs_ui_toast(a, ui->freeze ? "HOLD" : "RUNNING");
            }
        } else if(v == FsViewSignals) {
            if(sh && ch && ch->n_peaks) {
                int s = (int)ui->sig_sel - dir;
                ui->sig_sel = (uint8_t)clampi(s, 0, clampi(ch->n_peaks, 1, 6) - 1);
            }
        } else if(v == FsViewChannels) {
            if(sh) cycle_u8(&a->cfg.ch_page, 3, -dir);
        }
        break;
    }
    default:
        break;
    }
    return true;
}

static int slot_win(const FsApp* a, int slot) {
    switch(a->cfg.mu_slot[slot]) {
    case FsSlotExt:
        return 0;
    case FsSlotInt:
        return 1;
    case FsSlotNrfSpec:
        return 2;
    default:
        return -1;
    }
}

static bool multi_input(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    FsConfig* c = &a->cfg;
    bool sh = ev->type == InputTypeShort;
    bool lg = ev->type == InputTypeLong;
    bool rp = ev->type == InputTypeRepeat;
    const int n = fs_slot_count(c->mu_layout);
    const int slot = ui->focus < n ? ui->focus : 0;
    const int win = slot_win(a, slot);
    const bool is_wifi = c->mu_slot[slot] == FsSlotWifi;

    switch(ev->key) {
    case InputKeyBack:
        if(sh) {
            for(int i = 1; i <= n; i++) {
                int f = (slot + i) % n;
                if(c->mu_slot[f] != FsSlotOff) {
                    ui->focus = (uint8_t)f;
                    break;
                }
            }
        }
        break;
    case InputKeyOk:
        if(sh)
            open_menu(a);
        else if(lg && win >= 0)
            hop_preset(a, win);
        else if(lg)
            open_menu(a);
        break;
    case InputKeyLeft:
    case InputKeyRight: {
        int dir = ev->key == InputKeyLeft ? -1 : 1;
        if(win >= 0 && (sh || lg || rp)) {
            pan_win(a, win, dir, scroll_mult(a, ev));
        } else if(is_wifi && sh) {
            cycle_u8(&c->wifi_view, 4, dir);
        }
        break;
    }
    case InputKeyUp:
    case InputKeyDown: {
        int dir = ev->key == InputKeyUp ? 1 : -1;
        if(sh && win >= 0) {
            FsChan* ch = a->ch[win];
            uint32_t anchor = 0;
            if(ch && ch->n_peaks) anchor = fs_col_khz(&ch->geo, ch->peaks[0].col);
            zoom_win(a, win, dir, anchor);
        } else if(sh && is_wifi) {
            cycle_u8(&c->wifi_mode, 4, dir);
            fs_ui_toast(a, wifimode_names[c->wifi_mode % 4]);
        } else if(lg && dir > 0) {
            ui->freeze = !ui->freeze;
            fs_ui_toast(a, ui->freeze ? "HOLD" : "RUNNING");
        } else if(lg) {
            reset_channels(a);
            a->n_pkts = 0;
            fs_ui_toast(a, "RESET");
        }
        break;
    }
    default:
        break;
    }
    return true;
}

static bool menu_input(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    bool sh = ev->type == InputTypeShort;
    bool lg = ev->type == InputTypeLong;
    bool rp = ev->type == InputTypeRepeat;
    int cnt = menu_count(ui);
    bool root = ui->menu_page == PAGE_ROOT;
    int id = root ? -1 : page_items[ui->menu_page % P_COUNT][ui->menu_sel % page_count[ui->menu_page % P_COUNT]];

    switch(ev->key) {
    case InputKeyBack:
        if(sh) {
            if(root) {
                ui->mode = FsUiView;
            } else {
                int pg = ui->menu_page;
                ui->menu_page = PAGE_ROOT;
                ui->menu_sel = (uint8_t)pg;
                ui->menu_top = (uint8_t)(pg > 4 ? pg - 4 : 0);
            }
        }
        break;
    case InputKeyUp:
        if(sh || rp || lg) ui->menu_sel = (uint8_t)((ui->menu_sel + cnt - 1) % cnt);
        break;
    case InputKeyDown:
        if(sh || rp || lg) ui->menu_sel = (uint8_t)((ui->menu_sel + 1) % cnt);
        break;
    case InputKeyLeft:
        if((sh || rp) && !root && menu_is_adjust(id)) menu_adjust(a, id, -1);
        break;
    case InputKeyRight:
    case InputKeyOk:
        if(root) {
            if(sh) {
                int s = ui->menu_sel;
                if(s < P_COUNT) {
                    ui->menu_page = (uint8_t)s;
                    ui->menu_sel = 0;
                    ui->menu_top = 0;
                } else if(s == ROOT_SDR) {
                    if(a->status == FsStatusOk)
                        fs_sdr_open(a);
                    else
                        fs_ui_toast(a, "NO RADIO READY");
                } else if(s == ROOT_TTX) {
                    if(a->status == FsStatusOk && fs_family(a) == 0) {
                        ui->warn_kind = 1;
                        ui->warn_tick = furi_get_tick();
                        ui->mode = FsUiWarn;
                    } else {
                        fs_ui_toast(a, "NEEDS A CC1101 SOURCE");
                    }
                } else if(s == ROOT_HELP) {
                    ui->mode = FsUiHelp;
                    ui->help_page = 0;
                } else if(s == ROOT_ABOUT) {
                    fs_about_open(a, true);
                } else {
                    return false;
                }
            }
        } else if(ev->key == InputKeyRight) {
            if((sh || rp) && menu_is_adjust(id)) menu_adjust(a, id, 1);
        } else if(sh) {
            return menu_activate(a, id);
        }
        break;
    default:
        break;
    }
    if(ui->menu_sel < ui->menu_top) ui->menu_top = ui->menu_sel;
    if(ui->menu_sel >= ui->menu_top + 5) ui->menu_top = (uint8_t)(ui->menu_sel - 4);
    return true;
}

static void bm_load(FsApp* a, int idx) {
    FsBookmark* b = &a->bm.items[idx];
    int fam = b->fam ? 1 : 0;
    if(fam != fs_family(a)) {
        a->cfg.source = fam ? FsSrcNrf : FsSrcAuto;
        a->restart_req = true;
    }
    a->cfg.center_khz[fam] = b->center_khz;
    a->cfg.span_khz[fam] = b->span_khz;
    fs_geo_normalize(&a->cfg, fam);
    a->ui.cursor = 64;
    a->ui.marker_b_khz = 0;
    a->ui.mode = FsUiView;
}

static bool saved_arm(FsApp* a, const char* msg) {
    FsUi* ui = &a->ui;
    uint32_t now = furi_get_tick();
    if(ui->saved_armed && (now - ui->saved_arm_tick) < 3000) {
        ui->saved_armed = 0;
        return true;
    }
    ui->saved_armed = 1;
    ui->saved_arm_tick = now;
    fs_ui_toast(a, msg);
    return false;
}

static void saved_after_delete(FsApp* a) {
    fs_saved_load(a);
    int total = a->saved_n + (a->saved_n ? 1 : 0);
    if(a->ui.saved_sel >= total) a->ui.saved_sel = total ? (uint8_t)(total - 1) : 0;
    fs_ui_toast(a, "DELETED");
}

static void saved_input(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    bool sh = ev->type == InputTypeShort;
    bool lg = ev->type == InputTypeLong;
    bool rp = ev->type == InputTypeRepeat;
    int total = a->saved_n + (a->saved_n ? 1 : 0);
    if(ev->key == InputKeyBack) {
        if(sh) {
            ui->mode = FsUiMenu;
            fs_saved_free(a);
        }
    } else if(ev->key == InputKeyUp && total && (sh || rp)) {
        ui->saved_sel = (uint8_t)((ui->saved_sel + total - 1) % total);
    } else if(ev->key == InputKeyDown && total && (sh || rp)) {
        ui->saved_sel = (uint8_t)((ui->saved_sel + 1) % total);
    } else if(ev->key == InputKeyOk && total) {
        if(ui->saved_sel < a->saved_n) {
            if(sh) {
                if(fs_saved_info(a, a->saved_names[ui->saved_sel])) ui->mode = FsUiSavedInfo;
            } else if(lg && saved_arm(a, "HOLD OK AGAIN TO DELETE")) {
                fs_saved_delete(a, a->saved_names[ui->saved_sel]);
                saved_after_delete(a);
            }
        } else {
            if(sh)
                fs_ui_toast(a, "HOLD OK TO DELETE ALL");
            else if(lg && saved_arm(a, "HOLD OK AGAIN: DELETE ALL")) {
                fs_saved_delete_all(a);
                saved_after_delete(a);
            }
        }
    }
}

static void saved_info_input(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    if(ev->key == InputKeyBack && ev->type == InputTypeShort) {
        ui->mode = FsUiSaved;
    } else if(ev->key == InputKeyOk && ev->type == InputTypeLong) {
        if(saved_arm(a, "HOLD OK AGAIN TO DELETE") && ui->saved_sel < a->saved_n) {
            fs_saved_delete(a, a->saved_names[ui->saved_sel]);
            saved_after_delete(a);
            ui->mode = FsUiSaved;
        }
    }
}

bool fs_ui_input(FsApp* a, const InputEvent* ev) {
    FsUi* ui = &a->ui;
    if(ev->type == InputTypeRelease) {
        ui->scroll_rep = 0;
        if(ui->mode == FsUiTtxSetup) {
            furi_mutex_acquire(a->mtx, FuriWaitForever);
            fs_ttx_release(a, ev->key);
            furi_mutex_release(a->mtx);
        }
        return true;
    }
    if(ev->type != InputTypeShort && ev->type != InputTypeLong && ev->type != InputTypeRepeat)
        return true;
    bool keep = true;
    furi_mutex_acquire(a->mtx, FuriWaitForever);
    ui->dirty = true;
    uint32_t now = furi_get_tick();

    if(ev->key == InputKeyBack && ev->type == InputTypeLong && ui->mode != FsUiWarn &&
       ui->mode != FsUiSplash) {
        keep = false;
    } else {
        switch(ui->mode) {
        case FsUiSplash:
            ui->splash_skip = true;
            break;
        case FsUiView:
            keep = (a->status == FsStatusOk && a->multi_active) ? multi_input(a, ev) :
                                                                   view_input(a, ev);
            break;
        case FsUiMenu:
            keep = menu_input(a, ev);
            break;
        case FsUiHelp:
            if(ev->type == InputTypeRepeat) break;
            if(ev->key == InputKeyBack)
                ui->mode = FsUiMenu;
            else if(ev->key == InputKeyLeft || ev->key == InputKeyUp)
                ui->help_page = (uint8_t)((ui->help_page + 2) % 3);
            else if(ev->key == InputKeyRight || ev->key == InputKeyDown || ev->key == InputKeyOk)
                ui->help_page = (uint8_t)((ui->help_page + 1) % 3);
            break;
        case FsUiPins:
            if(ev->key == InputKeyBack && ev->type == InputTypeShort) ui->mode = FsUiMenu;
            break;
        case FsUiWarn:
            if(ev->key == InputKeyBack && ev->type != InputTypeRepeat) {
                ui->mode = FsUiMenu;
            } else if(
                ev->key == InputKeyOk && ev->type == InputTypeLong &&
                (now - ui->warn_tick) >= 1600) {
                ui->warn_tick = now;
                if(ui->warn_kind == 1) {
                    fs_ttx_open(a);
                } else {
                    a->pg_on = true;
                    a->restart_req = true;
                    ui->mode = FsUiView;
                    fs_ui_toast(a, "PARALLEL GATHER ON");
                }
            }
            break;
        case FsUiSaved:
            saved_input(a, ev);
            break;
        case FsUiSavedInfo:
            saved_info_input(a, ev);
            break;
        case FsUiBookmarks:
            if(ev->type == InputTypeRepeat) break;
            if(ev->key == InputKeyBack) {
                ui->mode = FsUiMenu;
            } else if(ev->key == InputKeyUp && a->bm.count) {
                ui->bm_sel = (uint8_t)((ui->bm_sel + a->bm.count - 1) % a->bm.count);
            } else if(ev->key == InputKeyDown && a->bm.count) {
                ui->bm_sel = (uint8_t)((ui->bm_sel + 1) % a->bm.count);
            } else if(ev->key == InputKeyOk && a->bm.count) {
                if(ev->type == InputTypeShort) {
                    bm_load(a, ui->bm_sel);
                } else if(ev->type == InputTypeLong) {
                    for(uint32_t i = ui->bm_sel; i + 1 < a->bm.count; i++)
                        a->bm.items[i] = a->bm.items[i + 1];
                    a->bm.count--;
                    if(ui->bm_sel >= a->bm.count && a->bm.count)
                        ui->bm_sel = (uint8_t)(a->bm.count - 1);
                    fs_bm_save(a);
                    fs_ui_toast(a, "BOOKMARK DELETED");
                }
            }
            break;
        case FsUiAbout:
            fs_about_input(a, ev);
            break;
        case FsUiSdr:
            if(ev->key == InputKeyBack && ev->type == InputTypeShort) fs_sdr_close(a);
            break;
        case FsUiTtxBrowse:
        case FsUiTtxSetup:
            fs_ttx_input(a, ev);
            break;
        case FsUiExiting:
            break;
        }
    }
    furi_mutex_release(a->mtx);
    return keep;
}

bool fs_ui_tick(FsApp* a) {
    FsUi* ui = &a->ui;
    uint32_t now = furi_get_tick();

    if(ui->mode == FsUiSplash) {
        uint32_t t = now - ui->mode_tick;
        uint32_t min_ms = a->cfg.intro ? 600 : (ui->splash_skip ? 0 : 2700);
        if(!ui->splash_out && ((a->boot_done && t >= min_ms) || t > 10000)) ui->splash_out = now;
        if(ui->splash_out && now - ui->splash_out >= SPLASH_OUT) {
            ui->mode = FsUiView;
            ui->splash_out = 0;
            ui->fade_start = now;
            if(!a->cfg.tut_done) fs_about_open(a, false);
            fs_trace(a, "ui: splash done, view");
        }
    }

    if(a->restart_req) {
        a->restart_req = false;
        fs_worker_stop(a);
        fs_apply_pins(a);
        furi_mutex_acquire(a->mtx, FuriWaitForever);
        {
            uint8_t rows = a->cfg.lite ? 24 : FS_WF_ROWS;
            if(a->ch[0] && a->ch[0]->wf_rows != rows) {
                fs_chan_free(a->ch[0]);
                a->ch[0] = fs_chan_alloc(rows);
            }
            if(a->ch[0]) {
                a->ch[0]->geo.fam = (!a->pg_on && a->cfg.source == FsSrcNrf) ? 1 : 0;
                fs_chan_reset(a->ch[0]);
            }
            heat_sync(a);
        }
        a->have_ext = false;
        a->have_nrf = false;
        a->multi_active = false;
        a->n_pkts = 0;
        a->dec_state = 0;
        furi_mutex_release(a->mtx);
        fs_worker_start(a);
        ui->dirty = true;
    }

    fs_ttx_reap(a);

    if(a->worker_msg_flag) {
        a->worker_msg_flag = false;
        fs_ui_toast(a, a->worker_msg);
    }

    FsChan* ch = a->ch[0];
    if(ch && !ui->freeze && ui->mode == FsUiView) {
        if(a->cfg.track && ch->n_peaks)
            ui->cursor = ch->peaks[0].col;
        else if(!a->cfg.track)
            ui->cursor = 64;
    }

    if(a->nt_event) {
        a->nt_event = false;
        if(a->cfg.alerts & 1) notification_message(a->notif, &sequence_blink_green_100);
        if(a->cfg.alerts & 2) notification_message(a->notif, &sequence_single_vibro);
    }

    if(a->trig_event) {
        a->trig_event = false;
        if(a->cfg.alerts & 1) notification_message(a->notif, &sequence_blink_red_100);
        if(a->cfg.alerts & 2) notification_message(a->notif, &sequence_single_vibro);
        char f[16], b[28];
        fmt_mhz(f, sizeof(f), a->trig_khz, (ch && ch->geo.fam) ? 1 : 3);
        snprintf(b, sizeof(b), "TRIG %sM", f);
        fs_ui_toast(a, b);
        switch(a->cfg.trig_act) {
        case 1:
            log_event(a);
            break;
        case 2:
            a->snap_req = true;
            break;
        case 3:
            do_cap_request(a, a->trig_khz);
            break;
        default:
            break;
        }
    }

    if(a->snap_ready) {
        a->snap_ready = false;
        fs_ui_toast(a, (a->snap_buf && fs_snapshot_write(a)) ? "SNAPSHOT SAVED" : "SNAPSHOT FAILED");
        if(a->snap_buf) {
            free(a->snap_buf);
            a->snap_buf = NULL;
        }
    }

    uint32_t sig = 0;
    if(a->ch[0]) sig += a->ch[0]->sweeps * 3;
    if(a->ch[1]) sig += a->ch[1]->sweeps * 7;
    if(a->ch[2]) sig += a->ch[2]->sweeps * 11;
    if(a->wifi) sig += a->wifi->total * 5 + a->wifi->state;
    sig += (uint32_t)a->meter_head * 13 + (uint32_t)a->act_head * 17 + a->scope_bursts * 19 +
           a->pkt_total * 23 + (uint32_t)a->dec_state * 29 + a->status;
    bool anim = ui->mode == FsUiSplash || ui->mode == FsUiExiting || ui->mode == FsUiWarn ||
                ui->mode == FsUiAbout || ui->mode == FsUiSdr || ui->mode == FsUiTtxSetup ||
                ui->fade_start != 0 || (a->nt_start && now - a->nt_start < 5100) ||
                (ui->toast_until && now < ui->toast_until) || a->log_active ||
                (ch && ch->trig_active) || a->dec_state == 2;
    bool need = ui->dirty || anim || sig != a->last_sig || (now - a->last_draw) > 300;
    if(need) {
        ui->dirty = false;
        a->last_sig = sig;
        a->last_draw = now;
    }
    return need;
}