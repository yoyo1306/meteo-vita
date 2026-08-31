#include "i18n.h"

static LangId g_lang = LANG_FR;

static const char *const STRINGS[LANG_COUNT][STR_COUNT] = {
	/* French (default) */
	{
		"Météo Vita",
		"Localisation (GPS ou recherche)",
		"Rechercher",
		"GPS",
		"Temp. actuelle",
		"Max",
		"Min",
		"Prévisions par heure",
		"Humidité",
		"Vent",
		"Donnees fictives — etape 1",
		"START = quitter",
	},
	/* English (stub for community) */
	{
		"Météo Vita",
		"Location (GPS or search)",
		"Search",
		"GPS",
		"Current temp",
		"Max",
		"Min",
		"Hourly forecast",
		"Humidity",
		"Wind",
		"Placeholder data — step 1",
		"START = quit",
	},
};

void i18n_set_lang(LangId lang)
{
	if (lang >= 0 && lang < LANG_COUNT)
		g_lang = lang;
}

LangId i18n_get_lang(void)
{
	return g_lang;
}

const char *_(StrId id)
{
	if (id < 0 || id >= STR_COUNT)
		return "";
	return STRINGS[g_lang][id];
}
