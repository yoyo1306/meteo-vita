#ifndef METEO_I18N_H
#define METEO_I18N_H

/* Simple string table — FR by default, ready for community translations later. */

typedef enum {
	LANG_FR = 0,
	LANG_EN = 1,
	LANG_COUNT
} LangId;

typedef enum {
	STR_APP_TITLE = 0,
	STR_LOCATION_HINT,
	STR_SEARCH,
	STR_GPS,
	STR_CURRENT_TEMP,
	STR_MAX,
	STR_MIN,
	STR_HOURLY,
	STR_HUMIDITY,
	STR_WIND,
	STR_PLACEHOLDER_NOTE,
	STR_EXIT_HINT,
	STR_COUNT
} StrId;

void i18n_set_lang(LangId lang);
LangId i18n_get_lang(void);
const char *_(StrId id);

#endif
