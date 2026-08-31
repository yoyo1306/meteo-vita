#include "geo.h"

#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cherche needle dans [start, end) */
static const char *find_range(const char *start, const char *end, const char *needle)
{
	size_t nlen;
	const char *p;

	if (!start || !end || start >= end || !needle)
		return NULL;
	nlen = strlen(needle);
	if (nlen == 0 || (size_t)(end - start) < nlen)
		return NULL;
	for (p = start; p + nlen <= end; p++) {
		if (memcmp(p, needle, nlen) == 0)
			return p;
	}
	return NULL;
}

static int extract_str(const char *start, const char *end, const char *key,
                       char *dst, size_t dstlen)
{
	char pat[64];
	const char *p;
	const char *q;
	size_t n;

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	p = find_range(start, end, pat);
	if (!p)
		return -1;
	p = find_range(p, end, ":");
	if (!p)
		return -1;
	p++;
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		p++;
	if (p >= end || *p != '"')
		return -1;
	p++;
	q = p;
	while (q < end && *q != '"') {
		if (*q == '\\' && q + 1 < end)
			q += 2;
		else
			q++;
	}
	n = (size_t)(q - p);
	if (n >= dstlen)
		n = dstlen - 1;
	memcpy(dst, p, n);
	dst[n] = '\0';
	return 0;
}

static int extract_float(const char *start, const char *end, const char *key, float *out)
{
	char pat[64];
	const char *p;
	char tmp[64];
	size_t n = 0;

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	p = find_range(start, end, pat);
	if (!p)
		return -1;
	p = find_range(p, end, ":");
	if (!p)
		return -1;
	p++;
	while (p < end && (*p == ' ' || *p == '\t'))
		p++;
	while (p < end && n + 1 < sizeof(tmp) &&
	       ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) {
		tmp[n++] = *p++;
	}
	tmp[n] = '\0';
	if (n == 0)
		return -1;
	*out = (float)atof(tmp);
	return 0;
}

/* Supprime les tableaux JSON nommé (ex. postcodes) — évite OOM / parse lourd */
static void strip_named_arrays(char *json, const char *key)
{
	char pat[72];
	char *p;

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	while ((p = strstr(json, pat)) != NULL) {
		char *from = p;
		char *lb;
		char *q;
		int depth = 0;

		lb = strchr(p, '[');
		if (!lb)
			break;
		q = lb;
		do {
			if (*q == '[')
				depth++;
			else if (*q == ']')
				depth--;
			q++;
		} while (*q && depth > 0);

		while (from > json && (from[-1] == ' ' || from[-1] == '\t' || from[-1] == '\n' || from[-1] == '\r'))
			from--;
		if (from > json && from[-1] == ',')
			from--;

		if (*q == ',')
			q++;

		memmove(from, q, strlen(q) + 1);
	}
}

/* Avance jusqu'à l'objet {..} suivant, retourne fin (après }), start via *out_start */
static const char *next_object(const char *p, const char *end, const char **out_start)
{
	int depth = 0;
	const char *start;

	while (p < end && *p != '{')
		p++;
	if (p >= end || *p != '{')
		return NULL;
	start = p;
	do {
		if (*p == '{')
			depth++;
		else if (*p == '}')
			depth--;
		p++;
	} while (p < end && depth > 0);

	*out_start = start;
	return p;
}

int geo_search(const char *query, GeoResult *out, int maxn, int *count)
{
	CURL *curl;
	char *enc = NULL;
	char url[512];
	char *body = NULL;
	size_t body_len = 0;
	const char *p;
	const char *end;
	const char *results;
	int n = 0;
	int i;
	GeoResult tmp[GEO_MAX_RESULTS];
	int is_fr[GEO_MAX_RESULTS];
	int ntmp = 0;

	if (count)
		*count = 0;
	if (!query || !query[0] || !out || maxn <= 0)
		return -1;
	if (maxn > GEO_MAX_RESULTS)
		maxn = GEO_MAX_RESULTS;

	curl = curl_easy_init();
	if (!curl)
		return -2;
	enc = curl_easy_escape(curl, query, 0);
	curl_easy_cleanup(curl);
	if (!enc)
		return -3;

	/* count limité pour réponses plus légères */
	snprintf(url, sizeof(url),
	         "https://geocoding-api.open-meteo.com/v1/search"
	         "?name=%s&count=%d&language=fr&format=json",
	         enc, maxn);
	curl_free(enc);

	if (http_get(url, &body, &body_len) != 0)
		return -4;

	/* Les postcodes FR font exploser la RAM / le parse */
	strip_named_arrays(body, "postcodes");

	results = strstr(body, "\"results\"");
	if (!results) {
		free(body);
		return -5;
	}
	p = strchr(results, '[');
	if (!p) {
		free(body);
		return -5;
	}
	p++;
	end = body + strlen(body);

	while (ntmp < maxn) {
		const char *obj_start = NULL;
		const char *obj_end;
		char name[64], admin1[64], country[64], cc[8];
		float lat = 0.0f, lon = 0.0f;

		obj_end = next_object(p, end, &obj_start);
		if (!obj_end || !obj_start)
			break;
		p = obj_end;

		memset(name, 0, sizeof(name));
		memset(admin1, 0, sizeof(admin1));
		memset(country, 0, sizeof(country));
		memset(cc, 0, sizeof(cc));

		if (extract_str(obj_start, obj_end, "name", name, sizeof(name)) != 0)
			continue;
		extract_str(obj_start, obj_end, "admin1", admin1, sizeof(admin1));
		extract_str(obj_start, obj_end, "country", country, sizeof(country));
		extract_str(obj_start, obj_end, "country_code", cc, sizeof(cc));
		if (extract_float(obj_start, obj_end, "latitude", &lat) != 0)
			continue;
		if (extract_float(obj_start, obj_end, "longitude", &lon) != 0)
			continue;

		memset(&tmp[ntmp], 0, sizeof(tmp[ntmp]));
		snprintf(tmp[ntmp].name, sizeof(tmp[ntmp].name), "%s", name);
		tmp[ntmp].lat = lat;
		tmp[ntmp].lon = lon;
		if (admin1[0] && country[0])
			snprintf(tmp[ntmp].label, sizeof(tmp[ntmp].label), "%s, %s", admin1, country);
		else if (country[0])
			snprintf(tmp[ntmp].label, sizeof(tmp[ntmp].label), "%s", country);
		else
			snprintf(tmp[ntmp].label, sizeof(tmp[ntmp].label), "%.2f, %.2f",
			         (double)lat, (double)lon);
		is_fr[ntmp] = (cc[0] == 'F' && cc[1] == 'R') ? 1 : 0;
		ntmp++;
	}

	free(body);

	/* FR en premier */
	n = 0;
	for (i = 0; i < ntmp && n < maxn; i++) {
		if (is_fr[i])
			out[n++] = tmp[i];
	}
	for (i = 0; i < ntmp && n < maxn; i++) {
		if (!is_fr[i])
			out[n++] = tmp[i];
	}

	if (count)
		*count = n;
	return n > 0 ? 0 : -5;
}

static int pick_address_name(const char *addr_start, const char *addr_end, char *name, size_t namelen)
{
	const char *keys[] = {"city", "town", "village", "municipality", "hamlet", "suburb"};
	size_t i;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (extract_str(addr_start, addr_end, keys[i], name, namelen) == 0 && name[0])
			return 0;
	}
	return -1;
}

int geo_reverse(float lat, float lon, GeoResult *out)
{
	char url[512];
	char *body = NULL;
	size_t body_len = 0;
	const char *addr;
	const char *addr_end;
	const char *end;
	char name[64], state[64], country[64];

	if (!out)
		return -1;

	memset(out, 0, sizeof(*out));
	out->lat = lat;
	out->lon = lon;

	snprintf(url, sizeof(url),
	         "https://nominatim.openstreetmap.org/reverse?lat=%.6f&lon=%.6f"
	         "&format=json&accept-language=fr",
	         (double)lat, (double)lon);

	if (http_get(url, &body, &body_len) != 0 || !body)
		goto fallback;

	end = body + strlen(body);
	addr = find_range(body, end, "\"address\"");
	if (!addr)
		goto fallback;
	addr = find_range(addr, end, "{");
	if (!addr)
		goto fallback;

	addr_end = addr;
	{
		int depth = 0;
		do {
			if (*addr_end == '{')
				depth++;
			else if (*addr_end == '}')
				depth--;
			addr_end++;
		} while (addr_end < end && depth > 0);
	}

	memset(name, 0, sizeof(name));
	memset(state, 0, sizeof(state));
	memset(country, 0, sizeof(country));

	if (pick_address_name(addr, addr_end, name, sizeof(name)) != 0) {
		if (extract_str(body, end, "display_name", out->label, sizeof(out->label)) == 0) {
			char *comma = strchr(out->label, ',');
			if (comma)
				*comma = '\0';
			snprintf(name, sizeof(name), "%.63s", out->label);
		}
	}

	extract_str(addr, addr_end, "state", state, sizeof(state));
	extract_str(addr, addr_end, "country", country, sizeof(country));

	if (name[0])
		snprintf(out->name, sizeof(out->name), "%s", name);
	else
		snprintf(out->name, sizeof(out->name), "Position GPS");

	if (state[0] && country[0])
		snprintf(out->label, sizeof(out->label), "%s, %s", state, country);
	else if (country[0])
		snprintf(out->label, sizeof(out->label), "%s", country);
	else
		snprintf(out->label, sizeof(out->label), "%.4f, %.4f", (double)lat, (double)lon);

	free(body);
	return out->name[0] ? 0 : -1;

fallback:
	free(body);
	snprintf(out->name, sizeof(out->name), "Position GPS");
	snprintf(out->label, sizeof(out->label), "%.4f, %.4f", (double)lat, (double)lon);
	return 0;
}
