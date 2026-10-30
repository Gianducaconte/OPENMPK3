#pragma once
#include <stdbool.h>
void gfx_clear(void);
void gfx_pixel(int x, int y, bool on);
void gfx_fill(int x, int y, int w, int h, bool on);
void gfx_rect(int x, int y, int w, int h);
void gfx_hline(int x, int y, int w);
void gfx_invert(int x, int y, int w, int h);
int  gfx_char(int x, int y, char c, int scale);
int  gfx_text(int x, int y, const char *s, int scale);
int  gfx_text_width(const char *s, int scale);
void gfx_text_center(int y, const char *s, int scale);
int  gfx_text_gap(int x, int y, const char *s, int scale, int gap);
int  gfx_text_fit(int x, int y, const char *s, int w);   /* 2x value text fitted to width w; y = top */
