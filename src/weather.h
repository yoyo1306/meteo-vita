#ifndef METEO_WEATHER_H
#define METEO_WEATHER_H

#define WEATHER_HOURLY_MAX 24

typedef struct {
	char city[64];
	float lat;
	float lon;

	float temp_now;
	float temp_max;
	float temp_min;
	int humidity;     /* % */
	float wind_kmh;
	int weather_code; /* WMO (Open-Meteo, pour le libellé) */
	char condition[48];
	char station[48]; /* station MF utilisée pour l'obs */

	int hourly_hour[WEATHER_HOURLY_MAX]; /* 0..23 */
	float hourly_temp[WEATHER_HOURLY_MAX];
	int hourly_count;

	int ok;
	char error[96];
} WeatherData;

void weather_set_place(WeatherData *w, const char *city, float lat, float lon);
void weather_set_mf_apikey(const char *apikey);

/*
 * Current : observations Météo-France (station la plus proche).
 * Prévisions horaires / min-max / libellé ciel : Open-Meteo.
 */
int weather_fetch(WeatherData *w);

const char *weather_condition_fr(int wmo_code);

#endif
