#ifndef METEO_UI_H
#define METEO_UI_H

#include <vita2d.h>

/* PS Vita screen */
#define SCREEN_W 960
#define SCREEN_H 544

unsigned int rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

void ui_draw_rounded_rect(float x, float y, float w, float h, float radius, unsigned int color);
void ui_draw_rounded_rect_outline(float x, float y, float w, float h, float radius, float thickness, unsigned int color);

void ui_draw_text_centered(vita2d_pgf *font, float cx, float cy, float scale,
                           unsigned int color, const char *text);

#endif
