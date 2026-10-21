#pragma once

#include <gui/gui.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SCR_W 128
#define SCR_H 64

int gfx_text_width(const char* s);
void gfx_text(Canvas* canvas, int x, int y, const char* s);
void gfx_text_right(Canvas* canvas, int xr, int y, const char* s);
void gfx_text_center(Canvas* canvas, int xc, int y, const char* s);
void gfx_text_scaled(Canvas* canvas, int x, int y, const char* s, int scale);

void gfx_bind(Canvas* canvas);
void gfx_px(int x, int y);
void gfx_px_clr(int x, int y);
void gfx_px_xor(int x, int y);
void gfx_hline(int x0, int x1, int y);
void gfx_vline(int x, int y0, int y1);
void gfx_hline_dot(int x0, int x1, int y, int period);
void gfx_vline_dot(int x, int y0, int y1, int period);
bool gfx_dither(int x, int y, int shade);
void gfx_text_px(int x, int y, const char* s, int scale, int shade, bool white);
void gfx_dissolve(int shade);
void gfx_fill_black(void);
int gfx_ease_out(int t256);
int gfx_ease_in(int t256);
int gfx_ease_back(int t256);

int gfx_sin256(int deg);
int gfx_cos256(int deg);

void fmt_mhz(char* out, size_t n, uint32_t khz, int decimals);
int fs_itoa(char* dst, int32_t v);
