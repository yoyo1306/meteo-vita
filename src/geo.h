#ifndef METEO_GEO_H
#define METEO_GEO_H

#define GEO_MAX_RESULTS 8

typedef struct {
	char name[64];
	char label[96]; /* "Lyon, Rhône-Alpes, France" */
	float lat;
	float lon;
} GeoResult;

/* Recherche Open-Meteo Geocoding. Retourne 0 si OK. */
int geo_search(const char *query, GeoResult *out, int maxn, int *count);

/* Nom de lieu depuis lat/lon (Nominatim). Retourne 0 si OK. */
int geo_reverse(float lat, float lon, GeoResult *out);

#endif
