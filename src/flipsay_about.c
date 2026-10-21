#include "flipsay.h"

#include <stdio.h>
#include <string.h>

#define ABOUT_PAGES 14

enum { KeyUp, KeyDown, KeyLeft, KeyRight, KeyOk, KeyBack };

typedef struct {
    uint8_t key;
    const char* title;
    const char* line[3];
} KeyPage;

static const KeyPage key_pages[4] = {
    {KeyOk, "OK BUTTON", {"TAP: OPTIONS", "HOLD: NEXT BAND", "MENU: SELECT"}},
    {KeyLeft, "LEFT / RIGHT", {"SCROLL THE BAND", "HOLD: FAST", "MENU: CHANGE"}},
    {KeyUp, "UP / DOWN", {"ZOOM IN / OUT", "UP HOLD: FREEZE", "MENU: MOVE"}},
    {KeyBack, "BACK BUTTON", {"TAP: NEXT VIEW", "HOLD: EXIT APP", "MULTI: NEXT WIN"}},
};

static const char* const text_pages[][6] = {
    {"8 VIEWS",
     "SPECTRUM  WATERFALL  COMBO",
     "PERSIST   SCOPE      S-METER",
     "SIGNALS   CHANNELS",
     "BACK CYCLES THROUGH VIEWS",
     "CURSOR, MARKERS AND PEAKS"},
    {"RADIOS",
     "EXT CC1101 ON GPIO HEADER",
     "INT CC1101 BUILT IN",
     "NRF24 2.4GHZ SPECTRUM",
     "SOURCE AUTO PICKS THE BEST",
     "PINS: OPTIONS > RADIO"},
    {"AUTO DECODE",
     "WATCHES COMMON FREQUENCIES",
     "AND THE STRONGEST PEAK",
     "DECODES STATIC + ROLLING",
     "SAVES .SUB FILES TO SD",
     "RX ONLY. NEVER TRANSMITS"},
    {"PARALLEL GATHER",
     "RUN INT + EXT AT ONCE",
     "MERGED = BEST OF BOTH",
     "MULTI USE = WINDOWS",
     "OPTIONS > PARALLEL GATHER",
     "ENABLE: HOLD OK TO CONFIRM"},
    {"MULTI USE",
     "",
     "",
     "1-4 WINDOWS, YOU PICK EACH",
     "EXT INT NRF24 WIFI MODULE",
     "OPTIONS > MULTI USE"},
    {"WIFI MODULE",
     "OFFICIAL WIFI DEVBOARD",
     "NEEDS MARAUDER FIRMWARE",
     "OTHER FW: UPDATE MARAUDER",
     "AP LIST CHANNELS RATE LOG",
     "PASSIVE LISTENING ONLY"},
    {"SDR MODE",
     "OPTIONS > SDR MODE",
     "USB + PC BRIDGE SCRIPT",
     "GQRX SHOWS THE SPECTRUM",
     "TUNE + GAIN FROM GQRX",
     "SWEEP DATA, NOT TRUE IQ"},
    {"TOOLS",
     "LOG CSV  SNAPSHOT",
     "CAPTURE RAW .SUB",
     "BOOKMARKS + SAVED DECODES",
     "SQUELCH + TRIGGER ALERTS",
     "SETTINGS SAVE TO SD"},
};

static bool g_from_menu = false;

void fs_about_open(FsApp* a, bool from_menu) {
    g_from_menu = from_menu;
    a->ui.about_page = 0;
    a->ui.mode = FsUiAbout;
}

static void about_close(FsApp* a) {
    if(!a->cfg.tut_done) {
        a->cfg.tut_done = 1;
        fs_cfg_save(a);
    }
    a->ui.mode = g_from_menu ? FsUiMenu : FsUiView;
    a->ui.fade_start = furi_get_tick();
}

void fs_about_input(FsApp* a, const InputEvent* ev) {
    if(ev->type == InputTypeRepeat) return;
    FsUi* ui = &a->ui;
    if(ev->key == InputKeyBack) {
        about_close(a);
    } else if(ev->key == InputKeyLeft || ev->key == InputKeyUp) {
        ui->about_page = (uint8_t)((ui->about_page + ABOUT_PAGES - 1) % ABOUT_PAGES);
    } else {
        if(ui->about_page + 1 >= ABOUT_PAGES)
            about_close(a);
        else
            ui->about_page++;
    }
}

static void tri(Canvas* c, int x, int y, int dir, bool white) {
    canvas_set_color(c, white ? ColorWhite : ColorBlack);
    for(int i = 0; i < 3; i++) {
        switch(dir) {
        case KeyUp:
            canvas_draw_line(c, x - i, y - 1 + i, x + i, y - 1 + i);
            break;
        case KeyDown:
            canvas_draw_line(c, x - i, y + 1 - i, x + i, y + 1 - i);
            break;
        case KeyLeft:
            canvas_draw_line(c, x - 1 + i, y - i, x - 1 + i, y + i);
            break;
        default:
            canvas_draw_line(c, x + 1 - i, y - i, x + 1 - i, y + i);
            break;
        }
    }
    canvas_set_color(c, ColorBlack);
}

static void draw_dpad(Canvas* c, int cx, int cy, int key) {
    const bool blink = ((furi_get_tick() / 350) & 1) == 0;
    const bool lr = (key == KeyLeft);
    const bool ud = (key == KeyUp);
    canvas_set_color(c, ColorBlack);
    canvas_draw_circle(c, cx, cy, 20);
    canvas_draw_circle(c, cx, cy, 7);
    static const int8_t off[4][2] = {{0, -13}, {0, 13}, {-13, 0}, {13, 0}};
    for(int d = 0; d < 4; d++) {
        bool on = blink && ((d == KeyUp || d == KeyDown) ? ud : lr);
        int px = cx + off[d][0], py = cy + off[d][1];
        if(on) {
            canvas_set_color(c, ColorBlack);
            canvas_draw_disc(c, px, py, 6);
        }
        tri(c, px, py, d, on);
    }
    if(key == KeyOk && blink) {
        canvas_set_color(c, ColorBlack);
        canvas_draw_disc(c, cx, cy, 7);
        canvas_set_color(c, ColorWhite);
        gfx_text(c, cx - 3, cy - 2, "OK");
    } else {
        canvas_set_color(c, ColorBlack);
        gfx_text(c, cx - 3, cy - 2, "OK");
    }
    canvas_set_color(c, ColorBlack);
}

static void draw_back_key(Canvas* c, int cx, int cy) {
    const bool blink = ((furi_get_tick() / 350) & 1) == 0;
    canvas_set_color(c, ColorBlack);
    if(blink) {
        canvas_draw_rbox(c, cx - 20, cy - 10, 40, 20, 4);
        canvas_set_color(c, ColorWhite);
    } else {
        canvas_draw_rframe(c, cx - 20, cy - 10, 40, 20, 4);
    }
    gfx_text(c, cx - 7, cy - 6, "BACK");
    canvas_draw_line(c, cx - 8, cy + 3, cx + 8, cy + 3);
    canvas_draw_line(c, cx - 8, cy + 3, cx - 5, cy + 1);
    canvas_draw_line(c, cx - 8, cy + 3, cx - 5, cy + 5);
    canvas_set_color(c, ColorBlack);
}

static void draw_layout_icons(Canvas* c, FsApp* a, int y) {
    static const uint8_t cnt[5] = {2, 2, 3, 3, 4};
    canvas_set_color(c, ColorBlack);
    for(int i = 0; i < 5; i++) {
        int x = 5 + i * 24;
        canvas_draw_frame(c, x, y, 20, 15);
        switch(i) {
        case 0:
            canvas_draw_line(c, x + 10, y, x + 10, y + 14);
            break;
        case 1:
            canvas_draw_line(c, x, y + 7, x + 19, y + 7);
            break;
        case 2:
            canvas_draw_line(c, x, y + 7, x + 19, y + 7);
            canvas_draw_line(c, x + 10, y, x + 10, y + 7);
            break;
        case 3:
            canvas_draw_line(c, x, y + 7, x + 19, y + 7);
            canvas_draw_line(c, x + 10, y + 7, x + 10, y + 14);
            break;
        default:
            canvas_draw_line(c, x, y + 7, x + 19, y + 7);
            canvas_draw_line(c, x + 10, y, x + 10, y + 14);
            break;
        }
        char b[4];
        snprintf(b, sizeof(b), "%d", cnt[i]);
        gfx_text(c, x + 9, y + 17, b);
        if(a->cfg.mu_layout == i) gfx_hline(x + 2, x + 17, y + 24);
    }
}

static void draw_intro(Canvas* c) {
    uint32_t t = furi_get_tick();
    for(int x = 4; x < 124; x += 3) {
        int v = 3 + (int)(((uint32_t)x * 37 + (t / 90) * 11) % 6);
        v += 14 - (int)(x > 64 ? x - 64 : 64 - x) / 4;
        if(v < 2) v = 2;
        canvas_draw_box(c, x, 36 - v, 2, v);
    }
    gfx_text_scaled(c, 22, 11, "FLIPSAY", 3);
    gfx_text_center(c, 64, 41, "MINI SDR FOR FLIPPER ZERO");
    gfx_text_center(c, 64, 48, "BY ITSC0C0");
    gfx_hline(4, 123, 55);
}

static void draw_credits(Canvas* c) {
    gfx_text_center(c, 64, 12, "FLIPSAY V" FS_VERSION);
    gfx_text_scaled(c, 24, 20, "BY ITSC0C0", 2);
    gfx_text_center(c, 64, 35, "GITHUB.COM/ITSC0C0/FLIPSAY");
    gfx_text_center(c, 64, 43, "GPL-3.0  RX ONLY");
    gfx_text_center(c, 64, 50, "RSSI SWEEP, NOT A TRUE SDR");
}

void fs_about_draw(Canvas* canvas, FsApp* a) {
    const int pg = a->ui.about_page % ABOUT_PAGES;
    canvas_set_color(canvas, ColorBlack);

    const char* title = "ABOUT FLIPSAY";
    if(pg >= 1 && pg <= 4) title = "HOW TO USE";
    if(pg >= 5 && pg <= 12) title = text_pages[pg - 5][0];
    if(pg == 13) title = "CREDITS";
    if(pg != 0) {
        canvas_draw_box(canvas, 0, 0, 128, 8);
        canvas_set_color(canvas, ColorWhite);
        gfx_text(canvas, 2, 1, title);
        char b[12];
        snprintf(b, sizeof(b), "%d/%d", pg + 1, ABOUT_PAGES);
        gfx_text_right(canvas, 126, 1, b);
        canvas_set_color(canvas, ColorBlack);
    }

    if(pg == 0) {
        draw_intro(canvas);
    } else if(pg <= 4) {
        const KeyPage* kp = &key_pages[pg - 1];
        if(kp->key == KeyBack)
            draw_back_key(canvas, 28, 34);
        else
            draw_dpad(canvas, 28, 34, kp->key);
        gfx_text(canvas, 58, 14, kp->title);
        gfx_hline(58, 124, 21);
        for(int i = 0; i < 3; i++) gfx_text(canvas, 58, 26 + i * 8, kp->line[i]);
    } else if(pg <= 12) {
        const char* const* t = text_pages[pg - 5];
        if(pg == 9) {
            draw_layout_icons(canvas, a, 11);
            for(int i = 3; i < 6; i++) gfx_text(canvas, 2, 42 + (i - 3) * 7 - 2, t[i]);
        } else {
            for(int i = 1; i < 6; i++) gfx_text(canvas, 2, 12 + (i - 1) * 8, t[i]);
        }
    } else {
        draw_credits(canvas);
    }

    if(pg == 0 || pg == 13) {
        gfx_text_center(canvas, 64, 58, pg == 0 ? "RIGHT: HOW TO USE" : "BACK: CLOSE");
    } else {
        gfx_text(canvas, 2, 58, "< >");
        gfx_text_center(canvas, 64, 58, "BACK: CLOSE");
        for(int i = 0; i < ABOUT_PAGES; i++) {
            int x = 90 + i * 3 - 0;
            if(i == pg)
                canvas_draw_box(canvas, x, 58, 2, 3);
            else
                gfx_px(x, 60);
        }
    }
}

/* ---------- SDR screen ---------- */

static uint8_t g_prev_mode = 0;

void fs_sdr_open(FsApp* a) {
    g_prev_mode = a->worker_mode;
    a->sdr_state = 0;
    a->sdr_active = true;
    a->worker_mode = FsModeSdr;
    a->hw_rev++;
    a->ui.mode = FsUiSdr;
}

void fs_sdr_close(FsApp* a) {
    a->sdr_active = false;
    a->worker_mode = g_prev_mode;
    a->hw_rev++;
    a->ui.mode = FsUiMenu;
}

static void wpx(int x, int y) {
    gfx_px_clr(x, y);
}


static void draw_bus(int x0, int y0, bool left, int wheel) {
#define BX(lx) (left ? (x0 + 29 - (lx)) : (x0 + (lx)))
    for(int ly = 0; ly <= 8; ly++)
        for(int lx = 0; lx <= 23; lx++) wpx(BX(lx), y0 + ly);
    for(int ly = 4; ly <= 8; ly++)
        for(int lx = 24; lx <= 29; lx++) wpx(BX(lx), y0 + ly);
    for(int lx = 0; lx <= 29; lx++) wpx(BX(lx), y0 + 9);
    for(int w = 0; w < 5; w++) {
        int lx = 2 + w * 4;
        for(int ly = 2; ly <= 4; ly++)
            for(int d = 0; d < 3; d++) gfx_px(BX(lx + d), y0 + ly);
    }
    for(int ly = 2; ly <= 4; ly++)
        for(int lx = 22; lx <= 23; lx++) gfx_px(BX(lx), y0 + ly);
    for(int lx = 0; lx <= 29; lx++) gfx_px(BX(lx), y0 + 6);
    gfx_px(BX(28), y0 + 8);
    static const int8_t spoke[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    static const int8_t wx[2] = {6, 22};
    for(int w = 0; w < 2; w++) {
        int cx = BX(wx[w]), cy = y0 + 11;
        for(int dy = -2; dy <= 2; dy++) {
            for(int dx = -2; dx <= 2; dx++) {
                if(dx * dx + dy * dy <= 5) wpx(cx + dx, cy + dy);
            }
        }
        int s = wheel & 3;
        gfx_px(cx + spoke[s][0], cy + spoke[s][1]);
        gfx_px(cx - spoke[s][0], cy - spoke[s][1]);
        gfx_px(cx, cy);
    }
    if(((furi_get_tick() / 250) & 1) == 0) {
        wpx(BX(3), y0 - 1);
        wpx(BX(20), y0 - 1);
    }
#undef BX
}

static void fmt_span_khz(char* out, size_t n, uint32_t khz) {
    if(khz >= 1000)
        snprintf(out, n, "%lu.%02lu MHZ", khz / 1000, (khz % 1000) / 10);
    else
        snprintf(out, n, "%lu KHZ", khz);
}

void fs_sdr_draw(Canvas* canvas, FsApp* a) {
    UNUSED(canvas);
    const uint32_t now = furi_get_tick();
    gfx_fill_black();

    static const char* const dots[4] = {"", ".", "..", "..."};
    const char* d = dots[(now / 380) & 3];
    char b[60], f[16], s[20];
    const int st = a->sdr_state;

    gfx_text_px(2, 1, "SDR MODE", 1, 16, true);
    gfx_text_px(126 - gfx_text_width("BACK: EXIT") + 1, 1, "BACK: EXIT", 1, 16, true);
    for(int x = 0; x < 128; x += 2) wpx(x, 8);

    if(st == 0) {
        gfx_text_px(4, 13, "WAITING", 2, 16, true);
        snprintf(b, sizeof(b), "FOR USB TO CONNECT%s", d);
        gfx_text_px(4, 25, b, 1, 16, true);
        gfx_text_px(4, 33, "PLUG THE FLIPPER INTO THE PC", 1, 12, true);
    } else if(st == 1) {
        snprintf(b, sizeof(b), "CONNECTING%s", d);
        gfx_text_px(4, 13, b, 2, 16, true);
        gfx_text_px(4, 25, "USB IS UP. WAITING FOR BRIDGE", 1, 16, true);
        gfx_text_px(4, 33, "RUN TOOLS/FLIPSAY_SDR_BRIDGE.PY", 1, 12, true);
    } else if(st == 2) {
        gfx_text_px(4, 13, "CONNECTED", 2, 16, true);
        fmt_mhz(f, sizeof(f), a->sdr_center_khz, 3);
        fmt_span_khz(s, sizeof(s), a->sdr_span_khz);
        snprintf(b, sizeof(b), "%s MHZ  SPAN %s", f, s);
        gfx_text_px(4, 25, b, 1, 16, true);
        if(a->sdr_gain > 100)
            snprintf(s, sizeof(s), "AUTO");
        else
            snprintf(s, sizeof(s), "%u%%", (unsigned)a->sdr_gain);
        snprintf(
            b,
            sizeof(b),
            "GAIN %s  %u.%u FPS  %lu",
            s,
            a->sdr_fps10 / 10u,
            a->sdr_fps10 % 10u,
            a->sdr_frames);
        gfx_text_px(4, 33, b, 1, 16, true);
        gfx_text_px(4, 41, "TUNE FROM GQRX", 1, 10, true);
    } else {
        gfx_text_px(4, 13, "NO CC1101", 2, 16, true);
        gfx_text_px(4, 25, "SDR MODE NEEDS THE INT OR", 1, 16, true);
        gfx_text_px(4, 33, "EXT CC1101 (NOT NRF24)", 1, 16, true);
    }

    const int D = 160;
    const int speed = 44;
    uint32_t period = (uint32_t)(2 * D) * 1000UL / speed;
    int pos = (int)(((now % period) * speed) / 1000UL);
    bool left = pos >= D;
    int x = left ? 128 - (pos - D) : -32 + pos;
    int dist = left ? pos - D : pos;
    int bob = ((dist / 5) & 1);
    draw_bus(x, 46 - bob, left, dist / 3);

    for(int xx = 0; xx < 128; xx++) wpx(xx, 60);
    int dash_off = left ? (dist / 2) : -(dist / 2);
    for(int xx = 0; xx < 128; xx++) {
        int v = ((xx + dash_off) % 12 + 12) % 12;
        if(v < 6) wpx(xx, 62);
    }
}
