#include "ui.h"

#include <string.h>

unsigned int rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return RGBA8(r, g, b, a);
}

void ui_draw_rounded_rect(float x, float y, float w, float h, float radius, unsigned int color)
{
	float r = radius;
	if (r * 2.0f > w)
		r = w * 0.5f;
	if (r * 2.0f > h)
		r = h * 0.5f;

	/* Center + cross bars */
	vita2d_draw_rectangle(x + r, y, w - 2.0f * r, h, color);
	vita2d_draw_rectangle(x, y + r, w, h - 2.0f * r, color);

	/* Corners */
	vita2d_draw_fill_circle(x + r, y + r, r, color);
	vita2d_draw_fill_circle(x + w - r, y + r, r, color);
	vita2d_draw_fill_circle(x + r, y + h - r, r, color);
	vita2d_draw_fill_circle(x + w - r, y + h - r, r, color);
}

void ui_draw_rounded_rect_outline(float x, float y, float w, float h, float radius,
                                  float thickness, unsigned int color)
{
	ui_draw_rounded_rect(x, y, w, h, radius, color);
	ui_draw_rounded_rect(x + thickness, y + thickness,
	                     w - 2.0f * thickness, h - 2.0f * thickness,
	                     radius > thickness ? radius - thickness : 0.0f,
	                     rgba(18, 28, 48, 255));
}

void ui_draw_text_centered(vita2d_pgf *font, float cx, float cy, float scale,
                           unsigned int color, const char *text)
{
	int tw = vita2d_pgf_text_width(font, scale, text);
	int th = vita2d_pgf_text_height(font, scale, text);
	vita2d_pgf_draw_text(font, cx - tw * 0.5f, cy + th * 0.35f, color, scale, text);
}
