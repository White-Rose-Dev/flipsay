#include "flipsay.h"

#include <stdio.h>
#include <string.h>

/* TTX screens: file browser and the transmit setup / status screen. The transmitting itself
 * happens in the radio worker (see ttx_step in flipsay_radio.c). */

#define KHZ_MIN 281000UL
#define KHZ_MAX 962000UL

static const uint8_t pass_tab[3] = {1, 2, 3};
static const char* const mode_names[3] = {"INT", "EXT", "DUAL"};
static uint8_t g_prev_mode = 0;

static int lc_cmp_sub(const char* name) {
    size_t l = strlen(name);
    if(l < 5) return 0;
    const char* e = name + l - 4;
    return e[0] == '.' && (e[1] | 32) == 's' && (e[2] | 32) == 'u' && (e[3] | 32) == 'b';
}

static void dir_load(FsApp* a) {
    FsTtx* t = a->ttx;
    t->n = 0;
    t->dir_mask = 0;
    t->more = false;
    File* d = storage_file_alloc(a->storage);
    {
        FileInfo fi;
        char name[64];
        uint16_t idx = 0;
        for(int pass = 0; pass < 2; pass++) {
            if(!storage_dir_open(d, t->dir)) break;
            while(storage_dir_read(d, &fi, name, sizeof(name))) {
                bool isdir = file_info_is_dir(&fi);
                if(name[0] == '.') continue;
                if(pass == 0 && !isdir) continue;
                if(pass == 1 && (isdir || !lc_cmp_sub(name))) continue;
                if(strlen(name) >= FS_TTX_NAME) continue;
                if(idx++ < t->skip) continue;
                if(t->n >= FS_TTX_MAX) {
                    t->more = true;
                    break;
                }
                strncpy(t->names[t->n], name, FS_TTX_NAME - 1);
                t->names[t->n][FS_TTX_NAME - 1] = 0;
                if(isdir) t->dir_mask |= (1UL << t->n);
                t->n++;
            }
            storage_dir_close(d);
            if(t->more) break;
        }
    }
    storage_file_free(d);
    if(t->sel >= t->n) t->sel = t->n ? (uint8_t)(t->n - 1) : 0;
}

void fs_ttx_open(FsApp* a) {
    if(!a->ttx) {
        if(memmgr_get_free_heap() < 20 * 1024) {
            a->ui.mode = FsUiMenu;
            fs_ui_toast(a, "LOW MEMORY");
            return;
        }
        a->ttx = malloc(sizeof(FsTtx));
        memset(a->ttx, 0, sizeof(FsTtx));
    }
    FsTtx* t = a->ttx;
    t->quit = false;
    t->state = FsTtxIdle;
    t->cmd = 0;
    t->hold_tick = 0;
    snprintf(t->dir, sizeof(t->dir), "%s", FS_TTX_DIR_ROOT);
    t->skip = 0;
    t->sel = 0;
    dir_load(a);
    a->ui.mode = FsUiTtxBrowse;
}

static void leave_setup(FsApp* a) {
    a->worker_mode = g_prev_mode;
    a->hw_rev++;
}

void fs_ttx_close(FsApp* a) {
    FsTtx* t = a->ttx;
    if(a->ui.mode == FsUiTtxSetup) leave_setup(a);
    if(t) {
        t->hold_tick = 0;
        t->cmd = 0;
        t->quit = true;
    }
    a->ui.mode = FsUiMenu;
}

/* called from the UI tick: frees the state once the worker is done with it */
void fs_ttx_reap(FsApp* a) {
    if(!a->ttx || !a->ttx->quit) return;
    furi_mutex_acquire(a->mtx, FuriWaitForever);
    FsTtx* t = a->ttx;
    if(t && t->quit && t->state != FsTtxSending) {
        a->ttx = NULL;
        free(t);
    }
    furi_mutex_release(a->mtx);
}

static bool file_info(FsApp* a) {
    FsTtx* t = a->ttx;
    FlipperFormat* ff = flipper_format_file_alloc(a->storage);
    FuriString* s = furi_string_alloc();
    bool ok = false;
    do {
        if(!flipper_format_file_open_existing(ff, t->path)) break;
        if(!flipper_format_read_string(ff, "Filetype", s)) break;
        if(!strstr(furi_string_get_cstr(s), "SubGhz")) break;
        uint32_t hz = 0;
        if(!flipper_format_read_uint32(ff, "Frequency", &hz, 1)) break;
        if(!flipper_format_read_string(ff, "Preset", s)) break;
        snprintf(t->preset, sizeof(t->preset), "%s", furi_string_get_cstr(s));
        if(!flipper_format_read_string(ff, "Protocol", s)) break;
        snprintf(t->proto, sizeof(t->proto), "%s", furi_string_get_cstr(s));
        t->file_khz = (hz + 500) / 1000;
        t->is_raw = !strcmp(t->proto, "RAW");
        ok = true;
    } while(false);
    furi_string_free(s);
    flipper_format_free(ff);
    return ok;
}

static void enter_setup(FsApp* a) {
    FsTtx* t = a->ttx;
    uint32_t f = t->file_khz;
    if(f < KHZ_MIN) f = KHZ_MIN;
    if(f > KHZ_MAX) f = KHZ_MAX;
    t->khz[0] = f;
    t->khz[1] = f;
    t->mode = FsTtxModeInt;
    t->row = 0;
    t->passes = 0;
    t->state = FsTtxIdle;
    t->msg[0] = 0;
    t->ant[0] = FsTtxAntUnknown;
    t->ant[1] = FsTtxAntUnknown;
    g_prev_mode = a->worker_mode == FsModeTtx ? FsModeSweep : a->worker_mode;
    a->worker_mode = FsModeTtx;
    a->hw_rev++;
    a->ui.mode = FsUiTtxSetup;
}

static void browse_input(FsApp* a, const InputEvent* ev) {
    FsTtx* t = a->ttx;
    const bool act = ev->type == InputTypeShort || ev->type == InputTypeRepeat;
    switch(ev->key) {
    case InputKeyUp:
        if(!act) break;
        if(t->sel > 0) {
            t->sel--;
        } else if(t->skip > 0) {
            t->skip = t->skip > FS_TTX_MAX ? (uint16_t)(t->skip - FS_TTX_MAX) : 0;
            dir_load(a);
            t->sel = t->n ? (uint8_t)(t->n - 1) : 0;
        }
        break;
    case InputKeyDown:
        if(!act) break;
        if(t->sel + 1 < t->n) {
            t->sel++;
        } else if(t->more) {
            t->skip = (uint16_t)(t->skip + FS_TTX_MAX);
            t->sel = 0;
            dir_load(a);
        }
        break;
    case InputKeyOk:
    case InputKeyRight:
        if(ev->type != InputTypeShort || !t->n) break;
        {
            const char* nm = t->names[t->sel];
            bool isdir = (t->dir_mask >> t->sel) & 1;
            if(isdir) {
                if(strlen(t->dir) + strlen(nm) + 2 < sizeof(t->dir)) {
                    strcat(t->dir, "/");
                    strcat(t->dir, nm);
                    t->skip = 0;
                    t->sel = 0;
                    dir_load(a);
                } else {
                    fs_ui_toast(a, "PATH TOO LONG");
                }
            } else if(strlen(t->dir) + strlen(nm) + 2 < sizeof(t->path)) {
                snprintf(t->path, sizeof(t->path), "%s/%s", t->dir, nm);
                snprintf(t->name, sizeof(t->name), "%s", nm);
                if(file_info(a))
                    enter_setup(a);
                else
                    fs_ui_toast(a, "NOT A VALID .SUB FILE");
            } else {
                fs_ui_toast(a, "PATH TOO LONG");
            }
        }
        break;
    case InputKeyBack:
    case InputKeyLeft:
        if(ev->type != InputTypeShort) break;
        if(strlen(t->dir) > strlen(FS_TTX_DIR_ROOT)) {
            char* sl = strrchr(t->dir, '/');
            if(sl) *sl = 0;
            t->skip = 0;
            t->sel = 0;
            dir_load(a);
        } else if(ev->key == InputKeyBack) {
            fs_ttx_close(a);
        }
        break;
    default:
        break;
    }
}

static void adjust_khz(FsTtx* t, int idx, int dir, int step) {
    int64_t v = (int64_t)t->khz[idx] + (int64_t)dir * step;
    if(v < (int64_t)KHZ_MIN) v = KHZ_MIN;
    if(v > (int64_t)KHZ_MAX) v = KHZ_MAX;
    t->khz[idx] = (uint32_t)v;
}

static void setup_input(FsApp* a, const InputEvent* ev) {
    FsTtx* t = a->ttx;
    const bool sending = t->state == FsTtxSending;
    const bool act = ev->type == InputTypeShort || ev->type == InputTypeRepeat;
    const uint32_t now = furi_get_tick();

    if(ev->key == InputKeyOk) {
        if(ev->type == InputTypeLong && !sending) {
            t->state = FsTtxIdle;
            t->msg[0] = 0;
            t->hold_tick = now;
            t->cmd = 1;
        } else if(ev->type == InputTypeRepeat && sending) {
            t->hold_tick = now;
        } else if(ev->type == InputTypeShort && !sending) {
            if(t->row == 1) t->khz[0] = t->file_khz < KHZ_MIN ? KHZ_MIN : t->file_khz;
            if(t->row == 2) t->khz[1] = t->file_khz < KHZ_MIN ? KHZ_MIN : t->file_khz;
        }
        return;
    }
    if(ev->key == InputKeyBack) {
        if(ev->type != InputTypeShort) return;
        if(sending || t->cmd) {
            t->hold_tick = 0;
            t->cmd = 0;
            return;
        }
        leave_setup(a);
        a->ui.mode = FsUiTtxBrowse;
        return;
    }
    if(sending || !act) return;
    const bool rep = ev->type == InputTypeRepeat;
    switch(ev->key) {
    case InputKeyUp:
        t->row = (uint8_t)((t->row + 3) % 4);
        break;
    case InputKeyDown:
        t->row = (uint8_t)((t->row + 1) % 4);
        break;
    case InputKeyLeft:
    case InputKeyRight: {
        int dir = ev->key == InputKeyLeft ? -1 : 1;
        if(t->row == 0) {
            t->mode = (uint8_t)((t->mode + 3 + dir) % 3);
        } else if(t->row == 1) {
            adjust_khz(t, 0, dir, rep ? 100 : 10);
        } else if(t->row == 2) {
            adjust_khz(t, 1, dir, rep ? 100 : 10);
        } else {
            t->passes = (uint8_t)((t->passes + 3 + dir) % 3);
        }
        break;
    }
    default:
        break;
    }
}

void fs_ttx_input(FsApp* a, const InputEvent* ev) {
    if(!a->ttx) {
        a->ui.mode = FsUiMenu;
        return;
    }
    if(a->ui.mode == FsUiTtxBrowse)
        browse_input(a, ev);
    else
        setup_input(a, ev);
}

void fs_ttx_release(FsApp* a, InputKey key) {
    if(a->ttx && a->ui.mode == FsUiTtxSetup && (key == InputKeyOk || key == InputKeyBack)) {
        a->ttx->hold_tick = 0;
        a->ttx->cmd = 0;
    }
}

/* ---------------- drawing ---------------- */

static void bar(Canvas* canvas, const char* l, const char* r) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 8);
    canvas_set_color(canvas, ColorWhite);
    gfx_text(canvas, 2, 1, l);
    if(r) gfx_text_right(canvas, 126, 1, r);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_browse(Canvas* canvas, FsApp* a) {
    FsTtx* t = a->ttx;
    char b[48];
    const char* tail = t->dir + strlen("/ext");
    size_t tl = strlen(tail);
    if(tl > 22) tail += tl - 22;
    bar(canvas, "TTX FILES", tail);
    if(t->n == 0) {
        gfx_text_center(canvas, 64, 24, "NO .SUB FILES HERE");
        gfx_text_center(canvas, 64, 34, "RECORD OR DECODE SOME,");
        gfx_text_center(canvas, 64, 42, "OR COPY THEM TO /SUBGHZ");
    }
    int sel = t->sel;
    int top = sel > 5 ? sel - 5 : 0;
    for(int i = 0; i < 6; i++) {
        int idx = top + i;
        if(idx >= t->n) break;
        int y = 10 + i * 8;
        bool s = idx == sel;
        if(s) {
            canvas_draw_box(canvas, 0, y - 1, 128, 8);
            canvas_set_color(canvas, ColorWhite);
        }
        bool isdir = (t->dir_mask >> idx) & 1;
        snprintf(b, sizeof(b), isdir ? "> %s" : "  %s", t->names[idx]);
        gfx_text(canvas, 2, y + 1, b);
        canvas_set_color(canvas, ColorBlack);
    }
    if(t->more || t->skip) gfx_text_right(canvas, 126, 58, t->more ? "MORE v" : "^");
    gfx_text(canvas, 2, 58, "OK OPEN  BACK UP");
}

static const char* ant_word(uint8_t st) {
    switch(st) {
    case FsTtxAntReady:
        return "READY";
    case FsTtxAntNoRadio:
        return "NO RADIO";
    case FsTtxAntBusBusy:
        return "BUS BUSY";
    case FsTtxAntBlockedRegion:
        return "REGION";
    case FsTtxAntBlockedBand:
        return "BAND OFF";
    case FsTtxAntUnsupported:
        return "UNSUPP";
    default:
        return "...";
    }
}

static void row_text(Canvas* canvas, int y, bool sel, bool dim, const char* l, const char* r) {
    if(sel) {
        canvas_draw_box(canvas, 0, y - 1, 128, 8);
        canvas_set_color(canvas, ColorWhite);
    }
    gfx_text(canvas, 2, y + 1, l);
    gfx_text_right(canvas, 126, y + 1, r);
    UNUSED(dim);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_setup(Canvas* canvas, FsApp* a) {
    FsTtx* t = a->ttx;
    char b[64], c[24], f[16];
    const bool sending = t->state == FsTtxSending;
    const uint32_t now = furi_get_tick();

    snprintf(b, sizeof(b), "%.18s", t->name);
    bar(canvas, "TTX", b);

    const char* pre = strstr(t->preset, "Preset");
    pre = pre ? pre + 6 : t->preset;
    snprintf(c, sizeof(c), "%.12s", pre);
    char* as = strstr(c, "Async");
    if(as) *as = 0;
    fmt_mhz(f, sizeof(f), t->file_khz, 2);
    snprintf(b, sizeof(b), "%.10s  %sM  %s", t->proto, f, c);
    gfx_text(canvas, 2, 10, b);

    const bool useA = t->mode != FsTtxModeExt;
    const bool useB = t->mode != FsTtxModeInt;

    row_text(canvas, 17, t->row == 0, false, "MODE", mode_names[t->mode % 3]);

    fmt_mhz(f, sizeof(f), t->khz[0], 3);
    snprintf(b, sizeof(b), "A INT %s", useA ? f : "-");
    snprintf(c, sizeof(c), "%s", useA ? ant_word(t->ant[0]) : "OFF");
    row_text(canvas, 25, t->row == 1, !useA, b, c);

    fmt_mhz(f, sizeof(f), t->khz[1], 3);
    snprintf(b, sizeof(b), "B EXT %s", useB ? f : "-");
    snprintf(c, sizeof(c), "%s", useB ? ant_word(t->ant[1]) : "OFF");
    row_text(canvas, 33, t->row == 2, !useB, b, c);

    snprintf(c, sizeof(c), "%d", pass_tab[t->passes % 3]);
    row_text(canvas, 41, t->row == 3, false, "PASSES", c);

    gfx_hline(0, 127, 49);
    if(sending) {
        uint32_t el = now - t->t_start;
        bool blink = ((now / 200) & 1) != 0;
        snprintf(
            b,
            sizeof(b),
            "%sSENDING %s  PASS %d/%d",
            blink ? "* " : "  ",
            mode_names[t->mode % 3],
            t->pass_now,
            t->pass_all ? t->pass_all : 1);
        gfx_text_center(canvas, 64, 51, b);
        snprintf(b, sizeof(b), "%lu.%lu S  KEEP HOLDING OK", el / 1000, (el % 1000) / 100);
        gfx_text_center(canvas, 64, 58, b);
    } else if(t->state == FsTtxError) {
        gfx_text_center(canvas, 64, 51, t->msg);
        gfx_text_center(canvas, 64, 58, "HOLD OK TO RETRY");
    } else if(t->state == FsTtxDone && t->t_end && (now - t->t_end) < 4000) {
        if(t->both_started)
            snprintf(b, sizeof(b), "%s  A/B START +%u US", t->msg, t->start_delta_us);
        else
            snprintf(b, sizeof(b), "%s", t->msg);
        gfx_text_center(canvas, 64, 51, b);
        gfx_text_center(canvas, 64, 58, "HOLD OK TO SEND AGAIN");
    } else {
        gfx_text_center(canvas, 64, 51, "HOLD OK = TRANSMIT");
        gfx_text_center(canvas, 64, 58, "RELEASE = STOP");
    }
}

void fs_ttx_draw(Canvas* canvas, FsApp* a) {
    if(!a->ttx) return;
    if(a->ui.mode == FsUiTtxBrowse)
        draw_browse(canvas, a);
    else
        draw_setup(canvas, a);
}
