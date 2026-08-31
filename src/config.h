#ifndef METEO_CONFIG_H
#define METEO_CONFIG_H

#define MF_APIKEY_MAX 2048

typedef struct {
	char city[64];
	float lat;
	float lon;
	char mf_apikey[MF_APIKEY_MAX];
	int valid;
} AppConfig;

void config_set_defaults(AppConfig *cfg);
int config_load(AppConfig *cfg);
int config_save(const AppConfig *cfg);

#endif
