#include "flipsay_gfx.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

#define G(a, b, c, d, e) ((uint16_t)(((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e)))

static const uint16_t font3x5[64] = {
    G(0, 0, 0, 0, 0), G(2, 2, 2, 0, 2), G(5, 5, 0, 0, 0), G(5, 7, 5, 7, 5), // sp ! " #
    G(3, 6, 7, 3, 6), G(5, 1, 2, 4, 5), G(2, 5, 2, 5, 3), G(2, 2, 0, 0, 0), // $ % & '
    G(1, 2, 2, 2, 1), G(4, 2, 2, 2, 4), G(0, 5, 2, 5, 0), G(0, 2, 7, 2, 0), // ( ) * +
    G(0, 0, 0, 2, 4), G(0, 0, 7, 0, 0), G(0, 0, 0, 0, 2), G(1, 1, 2, 4, 4), // , - . /
    G(7, 5, 5, 5, 7), G(2, 6, 2, 2, 7), G(7, 1, 7, 4, 7), G(7, 1, 7, 1, 7), // 0 1 2 3
    G(5, 5, 7, 1, 1), G(7, 4, 7, 1, 7), G(7, 4, 7, 5, 7), G(7, 1, 2, 2, 2), // 4 5 6 7
    G(7, 5, 7, 5, 7), G(7, 5, 7, 1, 7), G(0, 2, 0, 2, 0), G(0, 2, 0, 2, 4), // 8 9 : ;
    G(1, 2, 4, 2, 1), G(0, 7, 0, 7, 0), G(4, 2, 1, 2, 4), G(7, 1, 3, 0, 2), // < = > ?
    G(3, 5, 7, 4, 3), G(2, 5, 7, 5, 5), G(6, 5, 6, 5, 6), G(3, 4, 4, 4, 3), // @ A B C
    G(6, 5, 5, 5, 6), G(7, 4, 6, 4, 7), G(7, 4, 6, 4, 4), G(3, 4, 5, 5, 3), // D E F G
    G(5, 5, 7, 5, 5), G(7, 2, 2, 2, 7), G(1, 1, 1, 5, 2), G(5, 5, 6, 5, 5), // H I J K
    G(4, 4, 4, 4, 7), G(5, 7, 7, 5, 5), G(6, 5, 5, 5, 5), G(2, 5, 5, 5, 2), // L M N O
    G(6, 5, 6, 4, 4), G(2, 5, 5, 7, 3), G(6, 5, 6, 5, 5), G(3, 4, 2, 1, 6), // P Q R S
    G(7, 2, 2, 2, 2), G(5, 5, 5, 5, 7), G(5, 5, 5, 5, 2), G(5, 5, 7, 7, 5), // T U V W
    G(5, 5, 2, 5, 5), G(5, 5, 2, 2, 2), G(7, 1, 2, 4, 7), G(3, 2, 2, 2, 3), // X Y Z [
    G(4, 4, 2, 1, 1), G(6, 2, 2, 2, 6), G(2, 5, 0, 0, 0), G(0, 0, 0, 0, 7), // \ ] ^ _
};

static const uint8_t bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

static const uint8_t sin_lut[91] = {
    0,   4,   9,   13,  18,  22,  27,  31,  36,  40,  44,  49,  53,  58,  62,  66,  71,  75,  79,
    83,  88,  92,  96,  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147, 150, 154,
    158, 161, 165, 168, 171, 175, 178, 181, 184, 187, 190, 193, 196, 199, 202, 204, 207, 210, 212,
    215, 217, 219, 222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247,
    248, 249, 250, 251, 252, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255};

static uint8_t* fb = NULL;

static inline int glyph_index(char c) {
    if(c >= 'a' && c <= 'z') c -= 32;
    if(c < 32 || c > 95) c = '?';
    return c - 32;
}

int gfx_text_width(const char* s) {
    int n = (int)strlen(s);
    return n > 0 ? n * 4 - 1 : 0;
}

void gfx_text(Canvas* canvas, int x, int y, const char* s) {
    for(; *s; s++, x += 4) {
        uint16_t g = font3x5[glyph_index(*s)];
        for(int r = 0; r < 5; r++) {
            int bits = (g >> (12 - 3 * r)) & 7;
            if(bits & 4) canvas_draw_dot(canvas, x, y + r);
            if(bits & 2) canvas_draw_dot(canvas, x + 1, y + r);
            if(bits & 1) canvas_draw_dot(canvas, x + 2, y + r);
        }
    }
}

void gfx_text_right(Canvas* canvas, int xr, int y, const char* s) {
    gfx_text(canvas, xr - gfx_text_width(s) + 1, y, s);
}

void gfx_text_center(Canvas* canvas, int xc, int y, const char* s) {
    gfx_text(canvas, xc - gfx_text_width(s) / 2, y, s);
}

void gfx_text_scaled(Canvas* canvas, int x, int y, const char* s, int scale) {
    for(; *s; s++, x += 4 * scale) {
        uint16_t g = font3x5[glyph_index(*s)];
        for(int r = 0; r < 5; r++) {
            int bits = (g >> (12 - 3 * r)) & 7;
            for(int c = 0; c < 3; c++) {
                if(bits & (4 >> c))
                    canvas_draw_box(canvas, x + c * scale, y + r * scale, scale, scale);
            }
        }
    }
}

void gfx_bind(Canvas* canvas) {
    fb = canvas_get_buffer(canvas);
}

void gfx_px(int x, int y) {
    if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
    fb[(y >> 3) * SCR_W + x] |= (uint8_t)(1 << (y & 7));
}

void gfx_px_clr(int x, int y) {
    if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
    fb[(y >> 3) * SCR_W + x] &= (uint8_t)~(1 << (y & 7));
}

void gfx_px_xor(int x, int y) {
    if((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
    fb[(y >> 3) * SCR_W + x] ^= (uint8_t)(1 << (y & 7));
}

void gfx_hline(int x0, int x1, int y) {
    if(x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for(int x = x0; x <= x1; x++) gfx_px(x, y);
}

void gfx_vline(int x, int y0, int y1) {
    if(y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    for(int y = y0; y <= y1; y++) gfx_px(x, y);
}

void gfx_hline_dot(int x0, int x1, int y, int period) {
    for(int x = x0; x <= x1; x++)
        if(x % period == 0) gfx_px(x, y);
}

void gfx_vline_dot(int x, int y0, int y1, int period) {
    for(int y = y0; y <= y1; y++)
        if(y % period == 0) gfx_px(x, y);
}

bool gfx_dither(int x, int y, int shade) {
    return shade > bayer4[y & 3][x & 3];
}

void gfx_text_px(int x, int y, const char* s, int scale, int shade, bool white) {
    for(; *s; s++, x += 4 * scale) {
        uint16_t g = font3x5[glyph_index(*s)];
        for(int r = 0; r < 5; r++) {
            int bits = (g >> (12 - 3 * r)) & 7;
            for(int c = 0; c < 3; c++) {
                if(!(bits & (4 >> c))) continue;
                for(int dy = 0; dy < scale; dy++) {
                    for(int dx = 0; dx < scale; dx++) {
                        int px = x + c * scale + dx;
                        int py = y + r * scale + dy;
                        if(shade < 16 && !gfx_dither(px, py, shade)) continue;
                        if(white)
                            gfx_px_clr(px, py);
                        else
                            gfx_px(px, py);
                    }
                }
            }
        }
    }
}

void gfx_dissolve(int shade) {
    if(shade >= 16) return;
    for(int page = 0; page < 8; page++) {
        for(int x = 0; x < SCR_W; x++) {
            uint8_t keep = 0;
            for(int r = 0; r < 8; r++) {
                if(gfx_dither(x, page * 8 + r, shade)) keep |= (uint8_t)(1 << r);
            }
            fb[page * SCR_W + x] &= keep;
        }
    }
}

void gfx_fill_black(void) {
    memset(fb, 0xFF, SCR_W * SCR_H / 8);
}

int gfx_ease_out(int t) {
    if(t < 0) t = 0;
    if(t > 256) t = 256;
    int u = 256 - t;
    return 256 - (u * u / 256) * u / 256;
}

int gfx_ease_in(int t) {
    if(t < 0) t = 0;
    if(t > 256) t = 256;
    return t * t / 256;
}

int gfx_ease_back(int t) {
    if(t < 0) t = 0;
    if(t > 256) t = 256;
    int u = t - 256;
    int u2 = u * u / 256;
    int u3 = u2 * u / 256;
    return 256 + (692 * u3 + 436 * u2) / 256;
}

int gfx_sin256(int deg) {
    deg %= 360;
    if(deg < 0) deg += 360;
    if(deg <= 90) return sin_lut[deg];
    if(deg <= 180) return sin_lut[180 - deg];
    if(deg <= 270) return -(int)sin_lut[deg - 180];
    return -(int)sin_lut[360 - deg];
}

int gfx_cos256(int deg) {
    return gfx_sin256(deg + 90);
}

void fmt_mhz(char* out, size_t n, uint32_t khz, int decimals) {
    uint32_t ip = khz / 1000;
    uint32_t fp = khz % 1000;
    if(decimals <= 0) {
        snprintf(out, n, "%lu", ip);
    } else if(decimals == 1) {
        snprintf(out, n, "%lu.%01lu", ip, fp / 100);
    } else if(decimals == 2) {
        snprintf(out, n, "%lu.%02lu", ip, fp / 10);
    } else {
        snprintf(out, n, "%lu.%03lu", ip, fp);
    }
}

int fs_itoa(char* dst, int32_t v) {
    char tmp[12];
    int n = 0, len = 0;
    uint32_t u;
    if(v < 0) {
        dst[len++] = '-';
        u = (uint32_t)(-(int64_t)v);
    } else {
        u = (uint32_t)v;
    }
    do {
        tmp[n++] = (char)('0' + (u % 10));
        u /= 10;
    } while(u);
    while(n) dst[len++] = tmp[--n];
    dst[len] = 0;
    return len;
}
