/*
 * Météo Vita — UI + Open-Meteo + recherche ville (IME système)
 */

#include <math.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/libime.h>
#include <psp2/rtc.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "config.h"
#include "geo.h"
#include "http.h"
#include "i18n.h"
#include "sfx.h"
#include "ui.h"
#include "weather.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
	UI_DASH = 0,
	UI_IME,
	UI_PICK,
	UI_SEARCH /* saisie ville sans clavier (ferme OSK ≠ abandon) */
};

static unsigned int COL_BG, COL_PANEL, COL_ACCENT, COL_TEXT, COL_MUTED;

static const float HOURLY_X = 668.0f, HOURLY_Y = 10.0f, HOURLY_W = 272.0f, HOURLY_H = 198.0f;
static const int HOURLY_VISIBLE = 4;
static const float HOURLY_ROW = 34.0f;

/* Hauteur = bulle humidité/vent ; largeur plus généreuse pour les noms */
static const float STAT_BUBBLE_W = 272.0f;
static const float STAT_BUBBLE_H = 78.0f;
static const float PICK_BUBBLE_W = 560.0f;

static const float CITY_X = 14.0f, CITY_Y = 8.0f, CITY_W = 170.0f, CITY_H = 48.0f;
static const float REFRESH_X = 196.0f, REFRESH_Y = 8.0f, REFRESH_W = 48.0f, REFRESH_H = 48.0f;
static const float PICK_W = PICK_BUBBLE_W;
static const float PICK_ROW = STAT_BUBBLE_H;
static const float PICK_GAP = 20.0f;
static const float PICK_X = (960.0f - PICK_BUBBLE_W) * 0.5f;
static const float PICK_Y = 28.0f;
static const float PICK_VIEW_H = 430.0f;
static const float BACK_X = 30.0f, BACK_Y = 480.0f, BACK_W = 160.0f, BACK_H = 48.0f;

static WeatherData g_weather;
static AppConfig g_cfg;
static float g_scroll_y, g_scroll_target;
static int g_scroll_dragging;
static float g_anim;
static int g_refresh_h = -1, g_refresh_m = -1, g_refreshing;
static int g_ui = UI_DASH;

static GeoResult g_geo[GEO_MAX_RESULTS];
static int g_geo_count;
static float g_pick_scroll = 0.0f;
static int g_pick_dragging = 0;
static char g_search_query[96];

/* IME */
static uint16_t g_ime_title[32];
static uint16_t g_ime_initial[64];
static uint16_t g_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static int g_ime_active;

static void apply_dev_scene(int idx);
static void exit_dev_mode(vita2d_pgf *font);

/* Mode dev (SELECT) : scènes météo fictives */
static int g_dev_mode;
static int g_dev_scene;
static int g_dev_force_day = -1; /* -1 auto, 0 nuit, 1 jour */
static unsigned int g_ctrl_prev;

typedef struct {
	const char *city;
	int wmo;
	int force_day; /* 1 jour, 0 nuit */
	float temp;
	int humidity;
	float wind;
} DevScene;

static const DevScene DEV_SCENES[] = {
	{ "Soleilville",     0,  1, 28.0f, 40, 12.0f },
	{ "Lunaville",       0,  0, 18.0f, 55,  8.0f },
	{ "Nuageville",      1,  1, 22.0f, 50, 14.0f },
	{ "Mi-Nuages",       2,  1, 20.0f, 60, 16.0f },
	{ "Couvertbourg",    3,  1, 16.0f, 70, 20.0f },
	{ "Brouillard-sur-Mer", 45, 1, 12.0f, 95,  5.0f },
	{ "Bruinebasse",    51,  1, 14.0f, 88, 10.0f },
	{ "Bruinette",      53,  1, 13.0f, 90, 12.0f },
	{ "Bruineforte",    55,  0, 11.0f, 92, 15.0f },
	{ "Pluville",       61,  1, 15.0f, 85, 18.0f },
	{ "Pluieville",     63,  1, 14.0f, 88, 22.0f },
	{ "Deluge-les-Bains", 65, 0, 12.0f, 95, 28.0f },
	{ "Floconville",    71,  1, -2.0f, 80, 12.0f },
	{ "Neigebourg",     73,  0, -5.0f, 85, 16.0f },
	{ "Blizzardheim",   75,  0, -8.0f, 90, 30.0f },
	{ "Averseville",    81,  1, 17.0f, 82, 20.0f },
	{ "Orageville",     95,  1, 24.0f, 75, 25.0f },
	{ "Grelebourg",     96,  0, 19.0f, 78, 32.0f },
	{ "Tempete Max",    99,  1, 21.0f, 80, 40.0f },
};
static const int DEV_SCENE_COUNT = (int)(sizeof(DEV_SCENES) / sizeof(DEV_SCENES[0]));

static void init_colors(void)
{
	COL_BG = rgba(10, 38, 72, 255);
	COL_PANEL = rgba(242, 248, 255, 235);
	COL_ACCENT = rgba(255, 168, 56, 255);
	COL_TEXT = rgba(18, 32, 54, 255);
	COL_MUTED = rgba(70, 90, 120, 255);
}

static float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void utf8_to_utf16(const char *src, uint16_t *dst, size_t max_chars)
{
	size_t o = 0;
	while (*src && o + 1 < max_chars) {
		unsigned char c = (unsigned char)*src;
		if (c < 0x80) {
			dst[o++] = c;
			src++;
		} else if ((c & 0xE0) == 0xC0 && (src[1] & 0xC0) == 0x80) {
			dst[o++] = (uint16_t)(((c & 0x1F) << 6) | (src[1] & 0x3F));
			src += 2;
		} else if ((c & 0xF0) == 0xE0 && (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80) {
			dst[o++] = (uint16_t)(((c & 0x0F) << 12) | ((src[1] & 0x3F) << 6) | (src[2] & 0x3F));
			src += 3;
		} else {
			src++;
		}
	}
	dst[o] = 0;
}

static void utf16_to_utf8(const uint16_t *src, char *dst, size_t max_bytes)
{
	size_t o = 0;
	while (*src && o + 1 < max_bytes) {
		uint16_t c = *src++;
		if (c < 0x80) {
			dst[o++] = (char)c;
		} else if (c < 0x800) {
			if (o + 2 >= max_bytes) break;
			dst[o++] = (char)(0xC0 | (c >> 6));
			dst[o++] = (char)(0x80 | (c & 0x3F));
		} else {
			if (o + 3 >= max_bytes) break;
			dst[o++] = (char)(0xE0 | (c >> 12));
			dst[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
			dst[o++] = (char)(0x80 | (c & 0x3F));
		}
	}
	dst[o] = 0;
}

static float hourly_page_h(void) { return (float)HOURLY_VISIBLE * HOURLY_ROW; }

static int hourly_count(void) { return g_weather.ok ? g_weather.hourly_count : 0; }

static float hourly_max_scroll(void)
{
	int max_off = hourly_count() - HOURLY_VISIBLE;
	if (max_off < 0) max_off = 0;
	max_off = (max_off / HOURLY_VISIBLE) * HOURLY_VISIBLE;
	return (float)max_off * HOURLY_ROW;
}

static float rubber(float over) { return over * 0.34f; }

static float rubber_clamp(float scroll, float max_s)
{
	const float lim = 72.0f; /* dépassement max (avant résistance visuelle) */
	if (scroll < -lim) return -lim;
	if (scroll > max_s + lim) return max_s + lim;
	return scroll;
}

/* Rebond liste villes — plus ample que les prévisions horaires */
static float pick_rubber(float over) { return over * 0.46f; }

static float pick_rubber_clamp(float scroll, float max_s)
{
	const float lim = 96.0f;
	if (scroll < -lim) return -lim;
	if (scroll > max_s + lim) return max_s + lim;
	return scroll;
}

static float hourly_visual_scroll(void)
{
	float max_s = hourly_max_scroll();
	if (g_scroll_y < 0.0f) return rubber(g_scroll_y);
	if (g_scroll_y > max_s) return max_s + rubber(g_scroll_y - max_s);
	return g_scroll_y;
}

static float snap_page(float y)
{
	float ph = hourly_page_h();
	float max_s = hourly_max_scroll();
	int page;
	if (ph <= 0.0f) return 0.0f;
	page = (int)floorf((y + ph * 0.5f) / ph);
	if (page < 0) page = 0;
	return clampf((float)page * ph, 0.0f, max_s);
}

static void hourly_settle_update(void)
{
	float max_s = hourly_max_scroll();
	if (g_scroll_dragging) return;
	if (g_scroll_y < 0.0f || g_scroll_y > max_s) {
		float edge = (g_scroll_y < 0.0f) ? 0.0f : max_s;
		g_scroll_y += (edge - g_scroll_y) * 0.28f;
		g_scroll_target = edge;
		if (fabsf(g_scroll_y - edge) < 0.4f) g_scroll_y = edge;
		return;
	}
	g_scroll_y += (g_scroll_target - g_scroll_y) * 0.28f;
	if (fabsf(g_scroll_target - g_scroll_y) < 0.4f) g_scroll_y = g_scroll_target;
}

static void draw_message_screen(vita2d_pgf *font, const char *title, const char *sub);

static int touch_in(float tx, float ty, float x, float y, float w, float h)
{
	return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

/* Clic valide uniquement si le doigt est relâché dans la zone */
static int tap_release_in(float lx, float ly, float x, float y, float w, float h)
{
	return touch_in(lx, ly, x, y, w, h);
}

static void touch_drain(void)
{
	SceTouchData t;
	int i;
	for (i = 0; i < 45; i++) {
		memset(&t, 0, sizeof(t));
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
		if (t.reportNum == 0)
			return;
		sceKernelDelayThread(8 * 1000);
	}
}

static void reset_pointer_state(void)
{
	g_scroll_dragging = 0;
	g_scroll_y = 0.0f;
	g_scroll_target = 0.0f;
}

static void stamp_refresh_time(void)
{
	SceDateTime now;
	memset(&now, 0, sizeof(now));
	sceRtcGetCurrentClockLocalTime(&now);
	g_refresh_h = (int)now.hour;
	g_refresh_m = (int)now.minute;
}

static void do_weather_refresh(vita2d_pgf *font)
{
	if (g_dev_mode) {
		apply_dev_scene(g_dev_scene);
		return;
	}
	g_refreshing = 1;
	vita2d_start_drawing();
	draw_message_screen(font, "Mise a jour...", g_weather.city);
	vita2d_end_drawing();
	vita2d_swap_buffers();
	if (weather_fetch(&g_weather) == 0) {
		stamp_refresh_time();
		g_scroll_y = g_scroll_target = 0.0f;
	}
	g_refreshing = 0;
}

static int ime_open(const char *initial_utf8)
{
	SceImeDialogParam param;

	vita2d_disable_clipping();
	memset(g_ime_input, 0, sizeof(g_ime_input));
	utf8_to_utf16("Ville", g_ime_title, sizeof(g_ime_title) / sizeof(g_ime_title[0]));
	utf8_to_utf16(initial_utf8 ? initial_utf8 : "", g_ime_initial,
	              sizeof(g_ime_initial) / sizeof(g_ime_initial[0]));

	sceImeDialogParamInit(&param);
	/* FR forcé → disposition AZERTY du clavier système */
	param.supportedLanguages = SCE_IME_LANGUAGE_FRENCH;
	param.languagesForced = SCE_TRUE;
	param.type = SCE_IME_TYPE_DEFAULT;
	param.option = 0;
	param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
	param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
	param.title = g_ime_title;
	param.maxTextLength = 48;
	param.initialText = g_ime_initial;
	param.inputTextBuffer = g_ime_input;

	if (sceImeDialogInit(&param) < 0)
		return -1;
	g_ime_active = 1;
	g_ui = UI_IME;
	return 0;
}

static void ime_close(void)
{
	if (g_ime_active) {
		sceImeDialogTerm();
		g_ime_active = 0;
	}
}

static void persist_place(const char *city, float lat, float lon)
{
	snprintf(g_cfg.city, sizeof(g_cfg.city), "%s", city);
	g_cfg.lat = lat;
	g_cfg.lon = lon;
	g_cfg.valid = 1;
	config_save(&g_cfg);
}

static void apply_place(vita2d_pgf *font, const GeoResult *r)
{
	WeatherData next;

	if (!r)
		return;

	g_refreshing = 1;
	vita2d_disable_clipping();
	vita2d_start_drawing();
	draw_message_screen(font, "Chargement...", r->name);
	vita2d_end_drawing();
	vita2d_swap_buffers();
	sceDisplayWaitVblankStart();

	weather_set_place(&next, r->name, r->lat, r->lon);
	if (weather_fetch(&next) == 0) {
		g_dev_mode = 0;
		g_dev_force_day = -1;
		g_weather = next;
		persist_place(r->name, r->lat, r->lon);
		stamp_refresh_time();
		reset_pointer_state();
	} else {
		snprintf(g_weather.error, sizeof(g_weather.error), "%s",
		         next.error[0] ? next.error : "Chargement echoue");
	}
	g_refreshing = 0;
	g_ui = UI_DASH;
	touch_drain();
}

static void start_city_search(vita2d_pgf *font, const char *query)
{
	int n = 0;
	g_refreshing = 1;
	vita2d_start_drawing();
	draw_message_screen(font, "Recherche...", query);
	vita2d_end_drawing();
	vita2d_swap_buffers();

	if (geo_search(query, g_geo, GEO_MAX_RESULTS, &n) != 0 || n <= 0) {
		vita2d_start_drawing();
		draw_message_screen(font, "Aucune ville", query);
		vita2d_end_drawing();
		vita2d_swap_buffers();
		sceKernelDelayThread(1500 * 1000);
		g_refreshing = 0;
		snprintf(g_search_query, sizeof(g_search_query), "%s", query);
		g_ui = UI_SEARCH; /* garde l'écran recherche */
		return;
	}

	g_geo_count = n;
	g_pick_scroll = 0.0f;
	g_pick_dragging = 0;
	g_refreshing = 0;
	if (n == 1) {
		apply_place(font, &g_geo[0]);
	} else {
		g_ui = UI_PICK;
		touch_drain();
	}
}

static void draw_sun_sense(float cx, float cy, float anim, float scale)
{
	int i;
	float pulse = 1.0f + 0.04f * sinf(anim * 1.6f);
	float s = scale * pulse;
	vita2d_draw_fill_circle(cx, cy, 78.0f * s, rgba(255, 190, 60, 28));
	vita2d_draw_fill_circle(cx, cy, 58.0f * s, rgba(255, 180, 40, 55));
	vita2d_draw_fill_circle(cx, cy, 42.0f * s, rgba(255, 200, 70, 90));
	for (i = 0; i < 12; i++) {
		float a = (float)i * (float)(M_PI * 2.0 / 12.0) + anim * 0.15f;
		float c = cosf(a), sn = sinf(a);
		vita2d_draw_line(cx + c * 38 * s, cy + sn * 38 * s,
		                 cx + c * 92 * s, cy + sn * 92 * s, rgba(255, 210, 80, 160));
	}
	vita2d_draw_fill_circle(cx, cy, 34.0f * s, rgba(255, 170, 35, 255));
	vita2d_draw_fill_circle(cx, cy, 24.0f * s, rgba(255, 220, 110, 255));
}

/* Phase 0 = nouvelle, 0.5 = pleine, 1 = nouvelle (temps universel). */
static float moon_phase01_utc(const SceDateTime *utc)
{
	int y = (int)utc->year;
	int m = (int)utc->month;
	int d = (int)utc->day;
	double hour = (double)utc->hour + (double)utc->minute / 60.0 + (double)utc->second / 3600.0;
	int A, B;
	double jd, age;

	if (m <= 2) {
		y -= 1;
		m += 12;
	}
	A = y / 100;
	B = 2 - A + A / 4;
	jd = floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5 + hour / 24.0;
	age = fmod(jd - 2451550.1, 29.530588853);
	if (age < 0.0)
		age += 29.530588853;
	return (float)(age / 29.530588853);
}

static float sun_altitude_deg(float lat, float lon, const SceDateTime *utc)
{
	int y = (int)utc->year;
	int n;
	float decl, ha, lat_r, decl_r, ha_r;
	float utc_h, lst;

	{
		static const int mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
		int mo = (int)utc->month;
		if (mo < 1) mo = 1;
		if (mo > 12) mo = 12;
		n = mdays[mo - 1] + (int)utc->day;
		if ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) && mo > 2)
			n += 1;
	}

	decl = 23.44f * sinf((float)(2.0 * M_PI) * ((float)n - 81.0f) / 365.0f);
	utc_h = (float)utc->hour + (float)utc->minute / 60.0f + (float)utc->second / 3600.0f;
	lst = utc_h + lon / 15.0f;
	ha = (lst - 12.0f) * 15.0f;

	lat_r = lat * (float)(M_PI / 180.0);
	decl_r = decl * (float)(M_PI / 180.0);
	ha_r = ha * (float)(M_PI / 180.0);
	return asinf(sinf(lat_r) * sinf(decl_r) + cosf(lat_r) * cosf(decl_r) * cosf(ha_r))
	       * (float)(180.0 / M_PI);
}

static int is_day_at(float lat, float lon)
{
	SceDateTime utc;

	if (g_dev_mode && g_dev_force_day >= 0)
		return g_dev_force_day;

	memset(&utc, 0, sizeof(utc));
	if (sceRtcGetCurrentClock(&utc, 0) < 0)
		sceRtcGetCurrentClockLocalTime(&utc);
	return sun_altitude_deg(lat, lon, &utc) > -0.5f;
}

static void apply_dev_scene(int idx)
{
	const DevScene *s;
	int i;
	SceDateTime now;

	if (idx < 0 || idx >= DEV_SCENE_COUNT)
		return;
	s = &DEV_SCENES[idx];
	g_dev_mode = 1;
	g_dev_scene = idx;
	g_dev_force_day = s->force_day;

	memset(&g_weather, 0, sizeof(g_weather));
	snprintf(g_weather.city, sizeof(g_weather.city), "%s", s->city);
	g_weather.lat = s->force_day ? 45.0f : 45.0f;
	g_weather.lon = s->force_day ? 0.0f : 0.0f;
	g_weather.temp_now = s->temp;
	g_weather.temp_max = s->temp + 4.0f;
	g_weather.temp_min = s->temp - 6.0f;
	g_weather.humidity = s->humidity;
	g_weather.wind_kmh = s->wind;
	g_weather.weather_code = s->wmo;
	snprintf(g_weather.condition, sizeof(g_weather.condition), "%s",
	         weather_condition_fr(s->wmo));
	snprintf(g_weather.station, sizeof(g_weather.station), "DEV %d/%d",
	         idx + 1, DEV_SCENE_COUNT);

	memset(&now, 0, sizeof(now));
	sceRtcGetCurrentClockLocalTime(&now);
	g_weather.hourly_count = WEATHER_HOURLY_MAX;
	for (i = 0; i < WEATHER_HOURLY_MAX; i++) {
		int h = ((int)now.hour + 1 + i) % 24;
		g_weather.hourly_hour[i] = h;
		g_weather.hourly_temp[i] = s->temp - 2.0f + (float)(i % 5) * 0.8f;
	}
	g_weather.ok = 1;
	g_weather.error[0] = '\0';
	g_ui = UI_DASH;
	g_scroll_y = g_scroll_target = 0.0f;
}

static void exit_dev_mode(vita2d_pgf *font)
{
	g_dev_mode = 0;
	g_dev_force_day = -1;
	weather_set_place(&g_weather, g_cfg.city, g_cfg.lat, g_cfg.lon);
	weather_set_mf_apikey(g_cfg.mf_apikey);
	do_weather_refresh(font);
}

static void draw_moon_sense(float cx, float cy, float anim, float phase01, int southern, float scale)
{
	int i;
	float pulse = 1.0f + 0.025f * sinf(anim * 1.1f);
	float R = 40.0f * scale * pulse;
	/* Face non éclairée : gris bleuté visible (pas la couleur du fond) */
	unsigned unlit = rgba(48, 62, 88, 255);
	unsigned lit = rgba(245, 247, 252, 255);
	unsigned crater = rgba(175, 185, 205, 255);
	unsigned rim = rgba(255, 255, 255, 90);
	float ox;

	/* Halo */
	vita2d_draw_fill_circle(cx, cy, R * 1.55f, rgba(170, 190, 240, 28));
	vita2d_draw_fill_circle(cx, cy, R * 1.25f, rgba(200, 210, 245, 40));

	/* Étoiles fixes autour (petites) */
	for (i = 0; i < 6; i++) {
		float a = (float)i * (float)(M_PI * 2.0 / 6.0) + 0.4f;
		float d = R * 1.85f + ((i % 2) ? 12.0f : 0.0f) * scale;
		float tw = 0.6f + 0.4f * sinf(anim * 2.4f + (float)i);
		vita2d_draw_fill_circle(cx + cosf(a) * d, cy + sinf(a) * d * 0.85f,
		                       0.7f + 0.35f * tw, rgba(230, 235, 255, (unsigned)(140 + 90 * tw)));
	}

	/* Disque complet toujours lisible */
	vita2d_draw_fill_circle(cx, cy, R, unlit);
	vita2d_draw_fill_circle(cx, cy, R, lit);

	/* Cratères (face claire) */
	vita2d_draw_fill_circle(cx - R * 0.28f, cy - R * 0.18f, R * 0.14f, crater);
	vita2d_draw_fill_circle(cx + R * 0.18f, cy + R * 0.22f, R * 0.10f, crater);
	vita2d_draw_fill_circle(cx - R * 0.05f, cy + R * 0.38f, R * 0.07f, crater);
	vita2d_draw_fill_circle(cx + R * 0.32f, cy - R * 0.30f, R * 0.06f, crater);
	vita2d_draw_fill_circle(cx - R * 0.35f, cy + R * 0.10f, R * 0.05f, crater);

	/* Reflet */
	vita2d_draw_fill_circle(cx - R * 0.30f, cy - R * 0.35f, R * 0.22f, rim);

	/* Phase : ombre = face non éclairée (silhouette de lune claire) */
	if (phase01 < 0.03f || phase01 > 0.97f) {
		vita2d_draw_fill_circle(cx, cy, R, unlit);
	} else if (phase01 > 0.47f && phase01 < 0.53f) {
		/* pleine */
	} else if (phase01 <= 0.5f) {
		float t = phase01 * 2.0f;
		ox = -2.0f * t * R;
		if (southern)
			ox = -ox;
		vita2d_draw_fill_circle(cx + ox, cy, R, unlit);
	} else {
		float u = (phase01 - 0.5f) * 2.0f;
		ox = 2.0f * (1.0f - u) * R;
		if (southern)
			ox = -ox;
		vita2d_draw_fill_circle(cx + ox, cy, R, unlit);
	}
}

static void draw_cloud_blob(float x, float y, float s, unsigned col)
{
	vita2d_draw_fill_circle(x, y, s, col);
	vita2d_draw_fill_circle(x - s * 0.75f, y + s * 0.12f, s * 0.72f, col);
	vita2d_draw_fill_circle(x + s * 0.70f, y + s * 0.18f, s * 0.68f, col);
	vita2d_draw_fill_circle(x - s * 0.20f, y - s * 0.35f, s * 0.55f, col);
	vita2d_draw_fill_circle(x + s * 0.35f, y - s * 0.28f, s * 0.50f, col);
}

static int wmo_cloud_level(int code)
{
	if (code == 1) return 1;
	if (code == 2) return 2;
	if (code == 3) return 3;
	if (code == 45 || code == 48) return 2;
	/* Bruine / pluie : nuages moyens pour laisser voir soleil/lune */
	if (code >= 51 && code <= 67) return 2;
	if (code >= 80) return 3;
	if (code >= 71) return 2;
	return 0;
}

static int wmo_rain_level(int code)
{
	if (code == 51 || code == 56 || code == 61 || code == 80) return 1;
	if (code == 53 || code == 57 || code == 63 || code == 81) return 2;
	if (code == 55 || code == 65 || code == 67 || code == 82) return 3;
	return 0;
}

static int wmo_snow_level(int code)
{
	if (code == 71 || code == 77 || code == 85) return 1;
	if (code == 73 || code == 86) return 2;
	if (code == 75) return 3;
	return 0;
}

static int wmo_is_fog(int code)
{
	return code == 45 || code == 48;
}

static int wmo_is_thunder(int code)
{
	return code == 95 || code == 96 || code == 99;
}

static int wmo_thunder_level(int code)
{
	if (code == 95) return 1;
	if (code == 96) return 2;
	if (code == 99) return 3;
	return 0;
}

static void draw_clouds(float cx, float cy, float anim, int level, float scale)
{
	unsigned soft = rgba(245, 248, 255, 200);
	unsigned mid = rgba(210, 220, 235, 220);
	unsigned heavy = rgba(160, 175, 200, 235);
	float drift = sinf(anim * 0.35f) * 8.0f * scale;
	/* Nuages plus bas : soleil / lune restent visibles au-dessus */
	float y = cy + 48.0f * scale;

	if (level >= 1)
		draw_cloud_blob(cx - 60.0f * scale + drift, y + 10.0f * scale, 26.0f * scale, soft);
	if (level >= 2) {
		draw_cloud_blob(cx + 45.0f * scale - drift * 0.6f, y - 5.0f * scale, 32.0f * scale, mid);
		draw_cloud_blob(cx - 5.0f * scale + drift * 0.4f, y + 28.0f * scale, 28.0f * scale, mid);
	}
	if (level >= 3) {
		draw_cloud_blob(cx + 5.0f * scale - drift * 0.3f, y - 15.0f * scale, 38.0f * scale, heavy);
		draw_cloud_blob(cx - 75.0f * scale + drift, y + 22.0f * scale, 24.0f * scale, heavy);
		draw_cloud_blob(cx + 78.0f * scale, y + 20.0f * scale, 22.0f * scale, mid);
	}
}

static void draw_rain(float cx, float cy, float anim, int level, float scale)
{
	int n = 6 + level * 7;
	int i;
	unsigned col = rgba(180, 210, 255, 210);
	/* Sous les nuages (nuages ~ cy+48) — jamais au-dessus */
	float base_y = cy + (level <= 1 ? 58.0f : 62.0f) * scale;
	float fall_h = (level <= 1 ? 55.0f : 95.0f) * scale;
	float speed = 2.0f + 0.35f * (float)level;

	for (i = 0; i < n; i++) {
		float col_x = cx - 70.0f * scale + (float)(i % 8) * 20.0f * scale;
		float phase = anim * speed + (float)i * 0.73f;
		float fall = fmodf(phase * 42.0f * scale, fall_h);
		float x = col_x + 6.0f * scale * sinf((float)i);
		float y0 = base_y + fall;
		float len = ((level <= 1) ? 6.0f : (8.0f + 4.0f * (float)level)) * scale;
		vita2d_draw_line(x, y0, x - 2.5f * scale, y0 + len, col);
		if (level >= 2)
			vita2d_draw_line(x + 1.0f, y0, x - 1.5f * scale, y0 + len, col);
	}
}

static void draw_snow(float cx, float cy, float anim, int level, float scale)
{
	int n = 8 + level * 6;
	int i;
	/* Sous les nuages, comme la pluie */
	float base_y = cy + 60.0f * scale;
	float fall_h = 90.0f * scale;

	for (i = 0; i < n; i++) {
		float phase = anim * (0.9f + 0.2f * (float)level) + (float)i * 0.9f;
		float fall = fmodf(phase * 28.0f * scale, fall_h);
		float sway = sinf(phase * 1.4f + (float)i) * 12.0f * scale;
		float x = cx - 60.0f * scale + (float)(i % 7) * 20.0f * scale + sway;
		float y = base_y + fall;
		float r = (2.0f + 0.6f * (float)level) * scale;
		vita2d_draw_fill_circle(x, y, r, rgba(245, 250, 255, 230));
	}
}

static void draw_fog(float cx, float cy, float anim, float scale)
{
	int i;
	for (i = 0; i < 5; i++) {
		float y = cy + 20.0f * scale + (float)i * 18.0f * scale;
		float w = (140.0f + (float)(i % 2) * 30.0f) * scale;
		float ox = sinf(anim * 0.4f + (float)i * 0.7f) * 16.0f * scale;
		unsigned a = (unsigned)(50 + i * 18);
		ui_draw_rounded_rect(cx - w * 0.5f + ox, y, w, 10.0f * scale, 5.0f * scale,
		                     rgba(210, 220, 235, a));
	}
}

static void draw_thunder(float cx, float cy, float anim, int level, float scale)
{
	float flash = fmaxf(0.0f, sinf(anim * 5.5f));
	float flash2 = fmaxf(0.0f, sinf(anim * 4.2f + 1.7f));
	float flash3 = fmaxf(0.0f, sinf(anim * 3.4f + 3.1f));
	unsigned bolt = rgba(255, 245, 170, (unsigned)(200 + 55 * flash));
	float x = cx + 15.0f * scale;
	float y = cy + 38.0f * scale;
	float s = scale;

	/* Éclair principal (sous les nuages) */
	vita2d_draw_line(x, y, x + 14 * s, y + 32 * s, bolt);
	vita2d_draw_line(x + 1, y, x + 15 * s, y + 32 * s, bolt);
	vita2d_draw_line(x + 14 * s, y + 32 * s, x - 4 * s, y + 32 * s, bolt);
	vita2d_draw_line(x - 4 * s, y + 32 * s, x + 16 * s, y + 72 * s, bolt);
	vita2d_draw_line(x - 3 * s, y + 32 * s, x + 17 * s, y + 72 * s, bolt);

	/* 2e éclair animé (déphasé) */
	{
		float x2 = cx - 42.0f * scale;
		float y2 = cy + 42.0f * scale;
		unsigned b2 = rgba(255, 245, 170, (unsigned)(120 + 120 * flash2));
		vita2d_draw_line(x2, y2, x2 + 10 * s, y2 + 30 * s, b2);
		vita2d_draw_line(x2 + 1, y2, x2 + 11 * s, y2 + 30 * s, b2);
		vita2d_draw_line(x2 + 10 * s, y2 + 30 * s, x2 - 6 * s, y2 + 30 * s, b2);
		vita2d_draw_line(x2 - 6 * s, y2 + 30 * s, x2 + 12 * s, y2 + 64 * s, b2);
		vita2d_draw_line(x2 - 5 * s, y2 + 30 * s, x2 + 13 * s, y2 + 64 * s, b2);
	}

	/* 3e éclair à droite */
	if (flash3 > 0.4f || level >= 2) {
		float x3 = cx + 48.0f * scale;
		float y3 = cy + 45.0f * scale;
		unsigned b3 = rgba(255, 255, 220, (unsigned)(100 + 140 * flash3));
		vita2d_draw_line(x3, y3, x3 - 8 * s, y3 + 22 * s, b3);
		vita2d_draw_line(x3 - 8 * s, y3 + 22 * s, x3 + 2 * s, y3 + 22 * s, b3);
		vita2d_draw_line(x3 + 2 * s, y3 + 22 * s, x3 - 10 * s, y3 + 52 * s, b3);
	}

	if (level >= 3) {
		int i;
		for (i = 0; i < 12; i++) {
			float px = cx - 55.0f * s + (float)i * 11.0f * s;
			float py = cy + 50.0f * s + fmodf(anim * 55.0f + (float)i * 9.0f, 45.0f * s);
			vita2d_draw_fill_circle(px, py, 2.4f * s, rgba(240, 245, 255, 230));
		}
	}
}

static void draw_sky_body(float cx, float cy, float anim)
{
	SceDateTime utc;
	float phase;
	int day;
	int code = g_weather.weather_code;
	int clouds = wmo_cloud_level(code);
	int rain = wmo_rain_level(code);
	int snow = wmo_snow_level(code);
	int thunder = wmo_thunder_level(code);
	float scale = 2.0f;

	memset(&utc, 0, sizeof(utc));
	if (sceRtcGetCurrentClock(&utc, 0) < 0)
		sceRtcGetCurrentClockLocalTime(&utc);

	day = is_day_at(g_weather.lat, g_weather.lon);

	/* Soleil / lune : taille fixe, toutes conditions */
	if (day)
		draw_sun_sense(cx, cy - 18.0f * scale, anim, scale);
	else {
		phase = moon_phase01_utc(&utc);
		draw_moon_sense(cx, cy - 18.0f * scale, anim, phase, g_weather.lat < 0.0f, scale);
	}

	if (clouds > 0)
		draw_clouds(cx, cy, anim, clouds, scale);
	if (wmo_is_fog(code))
		draw_fog(cx, cy, anim, scale);
	if (rain > 0)
		draw_rain(cx, cy, anim, rain, scale);
	if (snow > 0)
		draw_snow(cx, cy, anim, snow, scale);
	if (wmo_is_thunder(code))
		draw_thunder(cx, cy, anim, thunder, scale);
}

/* Icône refresh : traits épais (cercles) — lisible en 48px sur Vita 960x544 */
static void draw_refresh_arrow_head(float cx, float cy, float R, float ang, unsigned col)
{
	float tip_x = cx + cosf(ang) * R;
	float tip_y = cy + sinf(ang) * R;
	float tx = -sinf(ang);
	float ty = cosf(ang);
	float nx = cosf(ang);
	float ny = sinf(ang);
	int i, j;

	tip_x += tx * 1.5f;
	tip_y += ty * 1.5f;
	for (i = 0; i <= 5; i++) {
		float u = (float)i / 5.0f;
		float half = (1.0f - u) * 5.5f;
		float bx = tip_x - tx * (u * 9.0f);
		float by = tip_y - ty * (u * 9.0f);
		for (j = -2; j <= 2; j++) {
			float v = (float)j / 2.0f;
			vita2d_draw_fill_circle(bx + nx * half * v, by + ny * half * v, 2.2f, col);
		}
	}
}

static void draw_refresh_icon(float cx, float cy, unsigned col)
{
	const float R = 13.0f;
	const float thick = 3.5f;
	int i;

	for (i = 0; i <= 20; i++) {
		float a = (float)(-0.18 * M_PI) + (float)(0.88 * M_PI) * ((float)i / 20.0f);
		vita2d_draw_fill_circle(cx + cosf(a) * R, cy + sinf(a) * R, thick, col);
	}
	draw_refresh_arrow_head(cx, cy, R, (float)(0.70 * M_PI), col);

	for (i = 0; i <= 20; i++) {
		float a = (float)(0.82 * M_PI) + (float)(0.88 * M_PI) * ((float)i / 20.0f);
		vita2d_draw_fill_circle(cx + cosf(a) * R, cy + sinf(a) * R, thick, col);
	}
	draw_refresh_arrow_head(cx, cy, R, (float)(1.70 * M_PI), col);
}

/* Bulle HTC Sense 2 : fond noir, texte blanc, serrée autour du texte */
static void draw_sense_pill(vita2d_pgf *font, float cx, float cy, float scale,
                            const char *text)
{
	int tw, th;
	float pad_x, pad_y, w, h, r;

	if (!text || !text[0])
		return;
	tw = vita2d_pgf_text_width(font, scale, text);
	th = vita2d_pgf_text_height(font, scale, text);
	pad_x = 8.0f + scale * 2.0f;
	pad_y = 3.0f + scale * 1.5f;
	w = (float)tw + pad_x * 2.0f;
	h = (float)th + pad_y * 2.0f;
	r = h * 0.5f;
	ui_draw_rounded_rect(cx - w * 0.5f, cy - h * 0.5f, w, h, r, rgba(0, 0, 0, 235));
	ui_draw_text_centered(font, cx, cy, scale, rgba(255, 255, 255, 255), text);
}

static void draw_message_screen(vita2d_pgf *font, const char *title, const char *sub)
{
	vita2d_set_clear_color(COL_BG);
	vita2d_clear_screen();
	ui_draw_text_centered(font, SCREEN_W * 0.5f, SCREEN_H * 0.45f, 1.2f,
	                      rgba(240, 248, 255, 255), title);
	if (sub && sub[0])
		ui_draw_text_centered(font, SCREEN_W * 0.5f, SCREEN_H * 0.55f, 0.85f,
		                      rgba(180, 200, 220, 255), sub);
}

static float pick_stride(void)
{
	return PICK_ROW + PICK_GAP;
}

static float pick_max_scroll(void)
{
	float content = (float)g_geo_count * pick_stride();
	float max_s = content - PICK_VIEW_H;
	if (max_s < 0.0f)
		max_s = 0.0f;
	return max_s;
}

static float pick_visual_scroll(void)
{
	float max_s = pick_max_scroll();
	if (g_pick_scroll < 0.0f)
		return pick_rubber(g_pick_scroll);
	if (g_pick_scroll > max_s)
		return max_s + pick_rubber(g_pick_scroll - max_s);
	return g_pick_scroll;
}

static void pick_settle_update(void)
{
	float max_s = pick_max_scroll();
	float edge;

	if (g_pick_dragging)
		return;
	if (g_pick_scroll >= 0.0f && g_pick_scroll <= max_s)
		return;

	edge = (g_pick_scroll < 0.0f) ? 0.0f : max_s;
	g_pick_scroll += (edge - g_pick_scroll) * 0.38f;
	if (fabsf(g_pick_scroll - edge) < 0.4f)
		g_pick_scroll = edge;
}

static void draw_pick_list(vita2d_pgf *font)
{
	int i;
	float max_s = pick_max_scroll();
	float vis = pick_visual_scroll();

	vita2d_set_clear_color(COL_BG);
	vita2d_clear_screen();

	vita2d_enable_clipping();
	vita2d_set_clip_rectangle((int)PICK_X - 4, (int)PICK_Y,
	                          (int)(PICK_X + PICK_W + 4), (int)(PICK_Y + PICK_VIEW_H));

	for (i = 0; i < g_geo_count; i++) {
		float y = PICK_Y + (float)i * pick_stride() - vis;

		if (y + PICK_ROW < PICK_Y || y > PICK_Y + PICK_VIEW_H)
			continue;
		ui_draw_rounded_rect(PICK_X, y, PICK_W, PICK_ROW, 18.0f, COL_PANEL);
		ui_draw_text_centered(font, PICK_X + PICK_W * 0.5f, y + 30.0f, 1.35f,
		                      COL_TEXT, g_geo[i].name);
		ui_draw_text_centered(font, PICK_X + PICK_W * 0.5f, y + 58.0f, 0.8f,
		                      COL_MUTED, g_geo[i].label);
	}
	vita2d_disable_clipping();

	if (max_s > 0.0f) {
		float area_top = PICK_Y;
		float area_h = PICK_VIEW_H;
		float track_h = area_h * 0.8f;
		float track_top = area_top + (area_h - track_h) * 0.5f;
		float clamped = clampf(vis, 0.0f, max_s);
		float thumb_h, thumb_y, over;
		float sb_x = PICK_X + PICK_W + 14.0f;

		thumb_h = area_h * (area_h / ((float)g_geo_count * pick_stride()));
		if (thumb_h < 28.0f)
			thumb_h = 28.0f;
		if (thumb_h > track_h)
			thumb_h = track_h;
		thumb_y = track_top + (track_h - thumb_h) * (clamped / max_s);

		if (vis < 0.0f) {
			over = -vis;
			thumb_h = fmaxf(10.0f, thumb_h - over * 0.85f);
			thumb_y = track_top;
		} else if (vis > max_s) {
			over = vis - max_s;
			thumb_h = fmaxf(10.0f, thumb_h - over * 0.85f);
			thumb_y = track_top + track_h - thumb_h;
		}

		vita2d_draw_rectangle(sb_x, track_top, 5.0f, track_h, rgba(180, 200, 220, 255));
		vita2d_draw_rectangle(sb_x, thumb_y, 5.0f, thumb_h, COL_ACCENT);
	}

	ui_draw_rounded_rect(BACK_X, BACK_Y, BACK_W, BACK_H, 18.0f, COL_PANEL);
	ui_draw_text_centered(font, BACK_X + BACK_W * 0.5f, BACK_Y + 26.0f, 1.0f, COL_TEXT, "Retour");
}

/* Écran recherche : clavier fermé mais fenêtre toujours là */
static void draw_search_screen(vita2d_pgf *font)
{
	const float qx = (SCREEN_W - 520.0f) * 0.5f, qy = 90.0f, qw = 520.0f, qh = 64.0f;
	const float gx = (SCREEN_W - 240.0f) * 0.5f, gy = 190.0f, gw = 240.0f, gh = 56.0f;

	vita2d_set_clear_color(COL_BG);
	vita2d_clear_screen();

	ui_draw_rounded_rect(qx, qy, qw, qh, 18.0f, COL_PANEL);
	ui_draw_text_centered(font, SCREEN_W * 0.5f, qy + 36.0f, 1.15f, COL_TEXT,
	                      g_search_query[0] ? g_search_query : "Recherche ville");

	if (g_search_query[0]) {
		ui_draw_rounded_rect(gx, gy, gw, gh, 18.0f, COL_ACCENT);
		ui_draw_text_centered(font, SCREEN_W * 0.5f, gy + 32.0f, 1.05f, COL_TEXT, "Rechercher");
	}

	ui_draw_rounded_rect(BACK_X, BACK_Y, BACK_W, BACK_H, 18.0f, COL_PANEL);
	ui_draw_text_centered(font, BACK_X + BACK_W * 0.5f, BACK_Y + 26.0f, 1.0f, COL_TEXT, "Retour");
}

static void draw_dashboard(vita2d_pgf *font)
{
	char line[64];
	int i, n = hourly_count();
	float list_top, list_bottom, vis, max_s, sb_thumb_h, sb_y, page_h;
	float cx = SCREEN_W * 0.5f;

	vita2d_set_clear_color(COL_BG);
	vita2d_clear_screen();

	ui_draw_rounded_rect(CITY_X, CITY_Y, CITY_W, CITY_H, 16.0f, COL_PANEL);
	ui_draw_text_centered(font, CITY_X + CITY_W * 0.5f, CITY_Y + 24.0f, 1.15f, COL_TEXT,
	                      g_weather.city[0] ? g_weather.city : "—");

	/* Bouton refresh : même hauteur / coins que la ville, orange + flèches blanches */
	ui_draw_rounded_rect(REFRESH_X, REFRESH_Y, REFRESH_W, REFRESH_H, 16.0f, COL_ACCENT);
	draw_refresh_icon(REFRESH_X + REFRESH_W * 0.5f, REFRESH_Y + REFRESH_H * 0.5f,
	                  rgba(255, 255, 255, 255));

	if (g_dev_mode) {
		char dev[32];
		snprintf(dev, sizeof(dev), "DEV %d/%d", g_dev_scene + 1, DEV_SCENE_COUNT);
		draw_sense_pill(font, REFRESH_X + REFRESH_W + 70.0f, REFRESH_Y + 24.0f, 0.75f, dev);
	}

	/* Grand climat (peut déborder sur la temp) puis bulle Sense par-dessus */
	draw_sky_body(cx, 250.0f, g_anim);
	snprintf(line, sizeof(line), "%.0f°", (double)g_weather.temp_now);
	draw_sense_pill(font, cx, 355.0f, 1.65f, line);
	if (g_weather.condition[0])
		draw_sense_pill(font, cx, 400.0f, 0.85f, g_weather.condition);

	ui_draw_rounded_rect(HOURLY_X, HOURLY_Y, HOURLY_W, HOURLY_H, 18.0f, COL_PANEL);
	vita2d_pgf_draw_text(font, HOURLY_X + 14.0f, HOURLY_Y + 26.0f, COL_MUTED, 0.7f, _(STR_HOURLY));
	list_top = HOURLY_Y + 44.0f;
	list_bottom = list_top + (float)HOURLY_VISIBLE * HOURLY_ROW;
	vis = hourly_visual_scroll();
	vita2d_enable_clipping();
	vita2d_set_clip_rectangle((int)HOURLY_X + 8, (int)list_top,
	                          (int)(HOURLY_X + HOURLY_W - 16.0f), (int)list_bottom);
	for (i = 0; i < n; i++) {
		float y = list_top + (float)i * HOURLY_ROW - vis;
		if (y + HOURLY_ROW < list_top || y > list_bottom) continue;
		snprintf(line, sizeof(line), "%02dh = %.0f°",
		         g_weather.hourly_hour[i], (double)g_weather.hourly_temp[i]);
		vita2d_pgf_draw_text(font, HOURLY_X + 14.0f, y + 22.0f, COL_TEXT, 1.0f, line);
	}
	vita2d_disable_clipping();

	max_s = hourly_max_scroll();
	page_h = hourly_page_h();
	{
		float area_h = (float)HOURLY_VISIBLE * HOURLY_ROW;
		float area_top = list_top;
		float track_h = area_h * 0.8f; /* 10 → 8 : rail gris raccourci / centré */
		float track_top = area_top + (area_h - track_h) * 0.5f;
		float over;

		/* Orange : même taille qu’avant (sur area_h), seul le gris est coupé */
		sb_thumb_h = (max_s <= 0.0f) ? area_h : area_h * (page_h / (max_s + page_h));
		if (sb_thumb_h < 28.0f)
			sb_thumb_h = 28.0f;
		if (sb_thumb_h > track_h)
			sb_thumb_h = track_h;

		sb_y = track_top;
		if (max_s > 0.0f)
			sb_y += (track_h - sb_thumb_h) * (clampf(vis, 0.0f, max_s) / max_s);

		if (vis < 0.0f) {
			over = -vis;
			sb_thumb_h = fmaxf(10.0f, sb_thumb_h - over * 0.85f);
			sb_y = track_top;
		} else if (max_s > 0.0f && vis > max_s) {
			over = vis - max_s;
			sb_thumb_h = fmaxf(10.0f, sb_thumb_h - over * 0.85f);
			sb_y = track_top + track_h - sb_thumb_h;
		}

		vita2d_draw_rectangle(HOURLY_X + HOURLY_W - 10.0f, track_top, 5.0f, track_h,
		                      rgba(180, 200, 220, 255));
		vita2d_draw_rectangle(HOURLY_X + HOURLY_W - 10.0f, sb_y, 5.0f, sb_thumb_h, COL_ACCENT);
	}

	ui_draw_rounded_rect(14, 448, 300, STAT_BUBBLE_H, 18.0f, COL_PANEL);
	snprintf(line, sizeof(line), "%s = %.0f°", _(STR_MAX), (double)g_weather.temp_max);
	vita2d_pgf_draw_text(font, 38, 480, COL_TEXT, 1.05f, line);
	snprintf(line, sizeof(line), "%s = %.0f°", _(STR_MIN), (double)g_weather.temp_min);
	vita2d_pgf_draw_text(font, 38, 510, COL_TEXT, 1.05f, line);

	ui_draw_rounded_rect(HOURLY_X, 448, STAT_BUBBLE_W, STAT_BUBBLE_H, 18.0f, COL_PANEL);
	snprintf(line, sizeof(line), "%s = %d%%", _(STR_HUMIDITY), g_weather.humidity);
	vita2d_pgf_draw_text(font, HOURLY_X + 24.0f, 480, COL_TEXT, 1.05f, line);
	snprintf(line, sizeof(line), "%s = %.0f km/h", _(STR_WIND), (double)g_weather.wind_kmh);
	vita2d_pgf_draw_text(font, HOURLY_X + 24.0f, 510, COL_TEXT, 1.05f, line);
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	SceTouchData touch;
	vita2d_pgf *font;
	SceAppUtilInitParam app_init;
	SceAppUtilBootParam app_boot;
	SceCommonDialogConfigParam cmn;
	float drag_start_y = 0, drag_start_scroll = 0, drag_last_y = 0;
	int had_touch = 0;
	int refresh_down = 0, city_down = 0;
	float refresh_lx = 0, refresh_ly = 0;
	float city_lx = 0, city_ly = 0;
	int pick_down = -1;
	float pick_sx = 0.0f, pick_sy = 0.0f;
	float pick_lx = 0.0f, pick_ly = 0.0f;
	float pick_scroll_start = 0.0f;
	int pick_moved = 0;
	int back_down = 0;
	float back_lx = 0.0f, back_ly = 0.0f;
	SceCtrlData pad;
	/* UI_SEARCH */
	int search_q_down = 0, search_go_down = 0, search_back_down = 0;
	float sq_lx = 0, sq_ly = 0;
	float sg_lx = 0, sg_ly = 0;
	float sb_lx = 0, sb_ly = 0;
	const float search_qx = (960.0f - 520.0f) * 0.5f, search_qy = 90.0f;
	const float search_qw = 520.0f, search_qh = 64.0f;
	const float search_gx = (960.0f - 240.0f) * 0.5f, search_gy = 190.0f;
	const float search_gw = 240.0f, search_gh = 56.0f;

	i18n_set_lang(LANG_FR);
	init_colors();

	memset(&app_init, 0, sizeof(app_init));
	memset(&app_boot, 0, sizeof(app_boot));
	sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
	sceAppUtilInit(&app_init, &app_boot);
	sceCommonDialogConfigParamInit(&cmn);
	sceCommonDialogSetConfigParam(&cmn);

	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP);
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	g_ctrl_prev = 0;

	vita2d_init();
	font = vita2d_load_default_pgf();
	sfx_init();

	vita2d_start_drawing();
	draw_message_screen(font, "Chargement meteo...", "Meteo-France");
	vita2d_end_drawing();
	vita2d_swap_buffers();

	if (config_load(&g_cfg) != 0)
		config_set_defaults(&g_cfg);
	weather_set_mf_apikey(g_cfg.mf_apikey);

	weather_set_place(&g_weather, g_cfg.city, g_cfg.lat, g_cfg.lon);
	if (http_init() != 0) {
		vita2d_start_drawing();
		draw_message_screen(font, "Erreur reseau", "Init Wi-Fi / HTTP");
		vita2d_end_drawing();
		vita2d_swap_buffers();
		sceKernelDelayThread(3 * 1000 * 1000);
	} else if (weather_fetch(&g_weather) != 0) {
		vita2d_start_drawing();
		draw_message_screen(font, "Meteo indisponible",
		                    g_weather.error[0] ? g_weather.error : "Reessaie");
		vita2d_end_drawing();
		vita2d_swap_buffers();
		sceKernelDelayThread(3 * 1000 * 1000);
	} else {
		/* resauve au cas où defaults */
		persist_place(g_weather.city, g_weather.lat, g_weather.lon);
		stamp_refresh_time();
	}

	while (1) {
		int touching_hourly = 0;
		unsigned int pressed;

		memset(&pad, 0, sizeof(pad));
		sceCtrlPeekBufferPositive(0, &pad, 1);
		pressed = pad.buttons & ~g_ctrl_prev;
		g_ctrl_prev = pad.buttons;

		/* SELECT : enter/exit mode dev — L/R : scènes */
		if (g_ui == UI_DASH && !g_ime_active && !g_refreshing) {
			if (pressed & SCE_CTRL_SELECT) {
				sfx_click();
				if (g_dev_mode)
					exit_dev_mode(font);
				else
					apply_dev_scene(g_dev_scene);
			} else if (g_dev_mode && (pressed & SCE_CTRL_LTRIGGER)) {
				sfx_click();
				apply_dev_scene((g_dev_scene + DEV_SCENE_COUNT - 1) % DEV_SCENE_COUNT);
			} else if (g_dev_mode && (pressed & SCE_CTRL_RTRIGGER)) {
				sfx_click();
				apply_dev_scene((g_dev_scene + 1) % DEV_SCENE_COUNT);
			}
		}

		/* --- IME système (boucle dédiée anti GPU crash) --- */
		if (g_ui == UI_IME && g_ime_active) {
			SceCommonDialogStatus st = sceImeDialogGetStatus();
			if (st == SCE_COMMON_DIALOG_STATUS_FINISHED) {
				SceImeDialogResult res;
				char query[96];
				memset(&res, 0, sizeof(res));
				sceImeDialogGetResult(&res);
				ime_close();
				vita2d_disable_clipping();
				touch_drain();
				had_touch = 0;
				refresh_down = 0;
				city_down = 0;
				g_scroll_dragging = 0;
				utf16_to_utf8(g_ime_input, query, sizeof(query));
				snprintf(g_search_query, sizeof(g_search_query), "%s", query);
				if (res.button == SCE_IME_DIALOG_BUTTON_ENTER) {
					if (query[0])
						start_city_search(font, query);
					else
						g_ui = UI_SEARCH; /* Enter vide → écran recherche */
				} else {
					/* CLOSE / cacher clavier : garde la fenêtre recherche */
					g_ui = UI_SEARCH;
				}
				continue;
			}

			/* Fond simple + update dialog + VBlank (ordre critique GPU) */
			vita2d_disable_clipping();
			vita2d_start_drawing();
			vita2d_set_clear_color(COL_BG);
			vita2d_clear_screen();
			vita2d_end_drawing();
			vita2d_common_dialog_update();
			vita2d_swap_buffers();
			sceDisplayWaitVblankStart();
			continue;
		}

		memset(&touch, 0, sizeof(touch));
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

		/* --- Écran recherche (clavier fermé, fenêtre ouverte) --- */
		if (g_ui == UI_SEARCH) {
			if (touch.reportNum > 0) {
				float tx = touch.report[0].x / 2.0f;
				float ty = touch.report[0].y / 2.0f;

				if (search_q_down) {
					sq_lx = tx;
					sq_ly = ty;
				} else if (search_go_down) {
					sg_lx = tx;
					sg_ly = ty;
				} else if (search_back_down) {
					sb_lx = tx;
					sb_ly = ty;
				} else if (touch_in(tx, ty, search_qx, search_qy, search_qw, search_qh)) {
					search_q_down = 1;
					sq_lx = tx;
					sq_ly = ty;
				} else if (g_search_query[0] &&
				           touch_in(tx, ty, search_gx, search_gy, search_gw, search_gh)) {
					search_go_down = 1;
					sg_lx = tx;
					sg_ly = ty;
				} else if (touch_in(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
					search_back_down = 1;
					sb_lx = tx;
					sb_ly = ty;
				}
			} else {
				if (search_q_down) {
					search_q_down = 0;
					if (tap_release_in(sq_lx, sq_ly, search_qx, search_qy, search_qw, search_qh)) {
						sfx_click();
						touch_drain();
						ime_open(g_search_query);
						continue;
					}
				}
				if (search_go_down) {
					search_go_down = 0;
					if (g_search_query[0] &&
					    tap_release_in(sg_lx, sg_ly, search_gx, search_gy, search_gw, search_gh)) {
						sfx_click();
						touch_drain();
						start_city_search(font, g_search_query);
						continue;
					}
				}
				if (search_back_down) {
					search_back_down = 0;
					if (tap_release_in(sb_lx, sb_ly, BACK_X, BACK_Y, BACK_W, BACK_H)) {
						sfx_click();
						g_ui = UI_DASH;
						g_search_query[0] = '\0';
						touch_drain();
						continue;
					}
				}
			}

			vita2d_disable_clipping();
			vita2d_start_drawing();
			draw_search_screen(font);
			vita2d_end_drawing();
			vita2d_swap_buffers();
			sceDisplayWaitVblankStart();
			continue;
		}

		if (g_ui == UI_PICK) {
			if (touch.reportNum > 0) {
				float tx = touch.report[0].x / 2.0f;
				float ty = touch.report[0].y / 2.0f;

				if (back_down) {
					back_lx = tx;
					back_ly = ty;
				} else if (g_pick_dragging) {
					float dy = pick_sy - ty;
					if (fabsf(dy) > 12.0f || fabsf(tx - pick_sx) > 12.0f)
						pick_moved = 1;
					pick_lx = tx;
					pick_ly = ty;
					g_pick_scroll = pick_rubber_clamp(pick_scroll_start + dy, pick_max_scroll());
				} else if (touch_in(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
					back_down = 1;
					back_lx = tx;
					back_ly = ty;
				} else if (touch_in(tx, ty, PICK_X, PICK_Y, PICK_W, PICK_VIEW_H)) {
					g_pick_dragging = 1;
					pick_sx = pick_lx = tx;
					pick_sy = pick_ly = ty;
					pick_scroll_start = g_pick_scroll;
					pick_moved = 0;
					pick_down = -1;
					{
						int i;
						for (i = 0; i < g_geo_count; i++) {
							float y = PICK_Y + (float)i * pick_stride() - pick_visual_scroll();
							if (touch_in(tx, ty, PICK_X, y, PICK_W, PICK_ROW)) {
								pick_down = i;
								break;
							}
						}
					}
				}
			} else {
				if (back_down) {
					back_down = 0;
					if (tap_release_in(back_lx, back_ly, BACK_X, BACK_Y, BACK_W, BACK_H)) {
						sfx_click();
						g_ui = UI_DASH;
						g_pick_scroll = 0.0f;
						g_pick_dragging = 0;
						pick_down = -1;
						touch_drain();
						continue;
					}
				}
				if (g_pick_dragging) {
					int chosen = pick_down;
					g_pick_dragging = 0;
					if (!pick_moved && chosen >= 0 && chosen < g_geo_count) {
						float row_y = PICK_Y + (float)chosen * pick_stride() - pick_visual_scroll();
						if (tap_release_in(pick_lx, pick_ly, PICK_X, row_y, PICK_W, PICK_ROW)) {
							sfx_click();
							vita2d_disable_clipping();
							touch_drain();
							had_touch = 0;
							refresh_down = 0;
							city_down = 0;
							g_scroll_dragging = 0;
							apply_place(font, &g_geo[chosen]);
							pick_down = -1;
							continue;
						}
					}
					pick_down = -1;
				}
			}

			pick_settle_update();

			vita2d_disable_clipping();
			vita2d_start_drawing();
			draw_pick_list(font);
			vita2d_end_drawing();
			vita2d_swap_buffers();
			sceDisplayWaitVblankStart();
			continue;
		}

		/* --- Dashboard --- */
		if (touch.reportNum > 0) {
			float tx = touch.report[0].x / 2.0f;
			float ty = touch.report[0].y / 2.0f;

			if (city_down) {
				city_lx = tx;
				city_ly = ty;
			} else if (refresh_down) {
				refresh_lx = tx;
				refresh_ly = ty;
			} else if (!g_refreshing && touch_in(tx, ty, CITY_X, CITY_Y, CITY_W, CITY_H) &&
			           !had_touch) {
				city_down = 1;
				city_lx = tx;
				city_ly = ty;
			} else if (!g_refreshing && touch_in(tx, ty, REFRESH_X, REFRESH_Y, REFRESH_W, REFRESH_H) &&
			           !had_touch) {
				refresh_down = 1;
				refresh_lx = tx;
				refresh_ly = ty;
			} else if (touch_in(tx, ty, HOURLY_X, HOURLY_Y, HOURLY_W, HOURLY_H) &&
			           !refresh_down && !city_down) {
				touching_hourly = 1;
				drag_last_y = ty;
				if (!had_touch) {
					had_touch = 1;
					g_scroll_dragging = 1;
					drag_start_y = ty;
					drag_start_scroll = g_scroll_y;
				} else {
					g_scroll_y = rubber_clamp(drag_start_scroll + (drag_start_y - ty),
					                         hourly_max_scroll());
				}
			}
		} else {
			if (city_down) {
				city_down = 0;
				if (tap_release_in(city_lx, city_ly, CITY_X, CITY_Y, CITY_W, CITY_H)) {
					sfx_click();
					g_search_query[0] = '\0';
					ime_open("");
				}
			}
			if (refresh_down) {
				refresh_down = 0;
				if (tap_release_in(refresh_lx, refresh_ly, REFRESH_X, REFRESH_Y, REFRESH_W, REFRESH_H)) {
					sfx_click();
					do_weather_refresh(font);
				}
			}
		}

		if (!touching_hourly && had_touch) {
			float dy = drag_start_y - drag_last_y;
			float max_s = hourly_max_scroll();
			float start_page = snap_page(clampf(drag_start_scroll, 0.0f, max_s));
			g_scroll_dragging = 0;
			had_touch = 0;
			if (g_scroll_y < 0.0f) g_scroll_target = 0.0f;
			else if (g_scroll_y > max_s) g_scroll_target = max_s;
			else if (dy >= 36.0f) g_scroll_target = clampf(start_page + hourly_page_h(), 0, max_s);
			else if (dy <= -36.0f) g_scroll_target = clampf(start_page - hourly_page_h(), 0, max_s);
			else g_scroll_target = snap_page(g_scroll_y);
		}

		hourly_settle_update();
		g_anim += 0.035f;

		vita2d_start_drawing();
		if (g_weather.ok)
			draw_dashboard(font);
		else
			draw_message_screen(font, "Pas de donnees",
			                    g_weather.error[0] ? g_weather.error : "Home pour quitter");
		vita2d_end_drawing();
		vita2d_swap_buffers();
	}

	ime_close();
	sfx_term();
	http_term();
	vita2d_free_pgf(font);
	vita2d_fini();
	sceKernelExitProcess(0);
	return 0;
}
