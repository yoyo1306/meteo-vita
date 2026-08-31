#include "weather.h"

#include "http.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATIONS_PATH "ux0:data/meteo_vita/stations.csv"
#define MF_LISTE_URL  "https://public-api.meteofrance.fr/public/DPObs/v2/liste-stations"
#define MF_OBS_URL    "https://public-api.meteofrance.fr/public/DPObs/v2/station/infrahoraire-6m"
/* Au-delà : hors réseau MF (ex. New York) → Open-Meteo pour la temp actuelle */
#define MF_MAX_STATION_KM 80.0f

static char s_mf_apikey[2048];

void weather_set_mf_apikey(const char *apikey)
{
	if (!apikey)
		apikey = "";
	snprintf(s_mf_apikey, sizeof(s_mf_apikey), "%s", apikey);
}

const char *weather_condition_fr(int wmo_code)
{
	/*
	 * Traduction littérale des weather_code WMO Open-Meteo
	 * https://open-meteo.com/en/docs (WMO Weather interpretation codes)
	 */
	switch (wmo_code) {
	case 0:  return "Ciel clair";
	case 1:  return "Peu nuageux";
	case 2:  return "Partiellement nuageux";
	case 3:  return "Couvert";
	case 45: return "Brouillard";
	case 48: return "Brouillard givrant";
	case 51: return "Bruine faible";
	case 53: return "Bruine";
	case 55: return "Bruine forte";
	case 56: return "Bruine verglacante faible";
	case 57: return "Bruine verglacante";
	case 61: return "Pluie faible";
	case 63: return "Pluie";
	case 65: return "Pluie forte";
	case 66: return "Pluie verglacante faible";
	case 67: return "Pluie verglacante";
	case 71: return "Neige faible";
	case 73: return "Neige";
	case 75: return "Neige forte";
	case 77: return "Granules de neige";
	case 80: return "Averses faibles";
	case 81: return "Averses";
	case 82: return "Averses violentes";
	case 85: return "Averses de neige faibles";
	case 86: return "Averses de neige";
	case 95: return "Orage";
	case 96: return "Orage et grele";
	case 99: return "Orage et forte grele";
	default: return "Code inconnu";
	}
}

void weather_set_place(WeatherData *w, const char *city, float lat, float lon)
{
	if (!w)
		return;
	memset(w, 0, sizeof(*w));
	if (city)
		snprintf(w->city, sizeof(w->city), "%s", city);
	w->lat = lat;
	w->lon = lon;
}

static const char *find_key(const char *json, const char *key)
{
	char pat[96];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	return strstr(json, pat);
}

static int parse_number_after_key(const char *json, const char *key, float *out)
{
	const char *p = find_key(json, key);
	if (!p)
		return -1;
	p = strchr(p + 1, ':');
	if (!p)
		return -1;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (*p == 'n') /* null */
		return -1;
	*out = (float)atof(p);
	return 0;
}

static int parse_int_after_key(const char *json, const char *key, int *out)
{
	float f;
	if (parse_number_after_key(json, key, &f) != 0)
		return -1;
	*out = (int)(f + (f >= 0.0f ? 0.5f : -0.5f));
	return 0;
}

static int parse_float_array(const char *json, const char *key, float *out, int maxn, int *count)
{
	const char *p = find_key(json, key);
	const char *lb;
	int n = 0;

	if (!p)
		return -1;
	lb = strchr(p, '[');
	if (!lb)
		return -1;
	p = lb + 1;
	while (*p && *p != ']' && n < maxn) {
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
			p++;
		if (*p == ']')
			break;
		out[n++] = (float)atof(p);
		while (*p && *p != ',' && *p != ']')
			p++;
	}
	*count = n;
	return n > 0 ? 0 : -1;
}

static int parse_iso_hour(const char *iso)
{
	const char *t = strchr(iso, 'T');
	if (!t || strlen(t) < 3)
		return -1;
	return atoi(t + 1);
}

static int parse_iso_ymd(const char *iso, int *y, int *m, int *d)
{
	if (strlen(iso) < 10)
		return -1;
	*y = atoi(iso);
	*m = atoi(iso + 5);
	*d = atoi(iso + 8);
	return 0;
}

static int parse_hourly_times(const char *hourly_section, int *hours, int *ymd_day, int maxn, int *count)
{
	const char *p = find_key(hourly_section, "time");
	const char *lb;
	int n = 0;

	if (!p)
		return -1;
	lb = strchr(p, '[');
	if (!lb)
		return -1;
	p = lb + 1;
	while (*p && *p != ']' && n < maxn) {
		const char *q;
		char iso[24];
		int len;
		while (*p && *p != '"')
			p++;
		if (*p != '"')
			break;
		p++;
		q = strchr(p, '"');
		if (!q)
			break;
		len = (int)(q - p);
		if (len >= (int)sizeof(iso))
			len = (int)sizeof(iso) - 1;
		memcpy(iso, p, (size_t)len);
		iso[len] = '\0';
		hours[n] = parse_iso_hour(iso);
		{
			int y, m, d;
			if (parse_iso_ymd(iso, &y, &m, &d) == 0)
				ymd_day[n] = y * 10000 + m * 100 + d;
			else
				ymd_day[n] = 0;
		}
		n++;
		p = q + 1;
	}
	*count = n;
	return n > 0 ? 0 : -1;
}

static void ensure_data_dir(void)
{
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/meteo_vita", 0777);
}

static int load_file(const char *path, char **out, size_t *out_len)
{
	int fd;
	SceIoStat st;
	char *buf;
	int rd;

	*out = NULL;
	if (out_len)
		*out_len = 0;
	if (sceIoGetstat(path, &st) < 0 || st.st_size <= 0)
		return -1;
	buf = (char *)malloc((size_t)st.st_size + 1);
	if (!buf)
		return -2;
	fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) {
		free(buf);
		return -3;
	}
	rd = sceIoRead(fd, buf, (SceSize)st.st_size);
	sceIoClose(fd);
	if (rd <= 0) {
		free(buf);
		return -4;
	}
	buf[rd] = '\0';
	*out = buf;
	if (out_len)
		*out_len = (size_t)rd;
	return 0;
}

static int save_file(const char *path, const char *data, size_t len)
{
	int fd;
	ensure_data_dir();
	fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0)
		return -1;
	sceIoWrite(fd, data, (SceSize)len);
	sceIoClose(fd);
	return 0;
}

static int ensure_stations_csv(char **out_csv)
{
	char *body = NULL;
	size_t len = 0;

	*out_csv = NULL;
	if (load_file(STATIONS_PATH, out_csv, &len) == 0 && *out_csv && len > 64)
		return 0;

	free(*out_csv);
	*out_csv = NULL;

	if (!s_mf_apikey[0])
		return -1;
	if (http_get_header(MF_LISTE_URL, "apikey", s_mf_apikey, &body, &len) != 0)
		return -2;
	if (!body || len < 64) {
		free(body);
		return -3;
	}
	save_file(STATIONS_PATH, body, len);
	*out_csv = body;
	return 0;
}

/* Distance approximative au carré (km²) */
static float dist2_km(float lat1, float lon1, float lat2, float lon2)
{
	float mid = (lat1 + lat2) * 0.5f * (float)(M_PI / 180.0);
	float dlat = (lat1 - lat2) * 111.0f;
	float dlon = (lon1 - lon2) * 111.0f * cosf(mid);
	return dlat * dlat + dlon * dlon;
}

/* CSV MF : Id_station;Id_omm;Nom_usuel;Latitude;Longitude;Altitude;Date_ouverture;Pack */
static int find_nearest_station(const char *csv, float lat, float lon,
                                char *id_out, size_t id_sz,
                                char *name_out, size_t name_sz)
{
	const char *p;
	float best_radome = 1.0e30f;
	float best_any = 1.0e30f;
	char radome_id[16] = {0}, radome_name[48] = {0};
	char any_id[16] = {0}, any_name[48] = {0};
	int found_radome = 0, found_any = 0;
	float max2 = MF_MAX_STATION_KM * MF_MAX_STATION_KM;

	if (!csv || !id_out)
		return -1;

	p = csv;
	if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
		p += 3;
	{
		const char *nl = strchr(p, '\n');
		if (!nl)
			return -2;
		p = nl + 1;
	}

	while (*p) {
		char id[16], name[48], pack[24];
		float slat = 0.0f, slon = 0.0f;
		const char *line = p;
		const char *nl = strchr(p, '\n');
		const char *c1, *c2, *c3, *c4, *c5, *c6, *c7, *c8;
		size_t line_len;
		char row[256];
		float d2;
		int nlen;
		int is_radome;

		if (nl) {
			line_len = (size_t)(nl - line);
			p = nl + 1;
		} else {
			line_len = strlen(line);
			p += line_len;
		}
		if (line_len < 10)
			continue;
		if (line_len >= sizeof(row))
			line_len = sizeof(row) - 1;
		memcpy(row, line, line_len);
		row[line_len] = '\0';
		if (row[line_len - 1] == '\r')
			row[line_len - 1] = '\0';

		c1 = row;
		c2 = strchr(c1, ';');
		if (!c2)
			continue;
		c3 = strchr(c2 + 1, ';');
		if (!c3)
			continue;
		c4 = strchr(c3 + 1, ';');
		if (!c4)
			continue;
		c5 = strchr(c4 + 1, ';');
		if (!c5)
			continue;
		c6 = strchr(c5 + 1, ';');
		c7 = c6 ? strchr(c6 + 1, ';') : NULL;
		c8 = c7 ? strchr(c7 + 1, ';') : NULL;

		nlen = (int)(c2 - c1);
		if (nlen <= 0 || nlen >= (int)sizeof(id))
			continue;
		memcpy(id, c1, (size_t)nlen);
		id[nlen] = '\0';

		nlen = (int)(c4 - (c3 + 1));
		if (nlen < 0)
			nlen = 0;
		if (nlen >= (int)sizeof(name))
			nlen = (int)sizeof(name) - 1;
		memcpy(name, c3 + 1, (size_t)nlen);
		name[nlen] = '\0';

		slat = (float)atof(c4 + 1);
		slon = (float)atof(c5 + 1);
		if (slat == 0.0f && slon == 0.0f)
			continue;

		pack[0] = '\0';
		if (c8) {
			const char *pend = c8 + 1;
			nlen = 0;
			while (pend[nlen] && pend[nlen] != ';' && pend[nlen] != '\r' && nlen < (int)sizeof(pack) - 1) {
				pack[nlen] = pend[nlen];
				nlen++;
			}
			pack[nlen] = '\0';
		}

		is_radome = (strcmp(pack, "RADOME") == 0);
		d2 = dist2_km(lat, lon, slat, slon);
		if (d2 > max2)
			continue;

		if (!found_any || d2 < best_any) {
			best_any = d2;
			snprintf(any_id, sizeof(any_id), "%s", id);
			snprintf(any_name, sizeof(any_name), "%s", name);
			found_any = 1;
		}
		if (is_radome && (!found_radome || d2 < best_radome)) {
			best_radome = d2;
			snprintf(radome_id, sizeof(radome_id), "%s", id);
			snprintf(radome_name, sizeof(radome_name), "%s", name);
			found_radome = 1;
		}
	}

	if (found_radome) {
		snprintf(id_out, id_sz, "%s", radome_id);
		if (name_out && name_sz)
			snprintf(name_out, name_sz, "%s", radome_name);
		return 0;
	}
	if (found_any) {
		snprintf(id_out, id_sz, "%s", any_id);
		if (name_out && name_sz)
			snprintf(name_out, name_sz, "%s", any_name);
		return 0;
	}
	return -3;
}

static int fetch_mf_observation(WeatherData *w, int *have_hum, int *have_wind)
{
	char *csv = NULL;
	char *body = NULL;
	size_t blen = 0;
	char sid[16];
	char url[256];
	float t_k = 0.0f, ff = 0.0f;
	int u = 0;

	if (have_hum)
		*have_hum = 0;
	if (have_wind)
		*have_wind = 0;

	if (!s_mf_apikey[0]) {
		snprintf(w->error, sizeof(w->error), "Cle API Meteo-France absente");
		return -1;
	}

	if (ensure_stations_csv(&csv) != 0) {
		snprintf(w->error, sizeof(w->error), "Liste stations MF indisponible");
		return -2;
	}

	if (find_nearest_station(csv, w->lat, w->lon, sid, sizeof(sid),
	                         w->station, sizeof(w->station)) != 0) {
		free(csv);
		snprintf(w->error, sizeof(w->error), "Aucune station MF proche");
		return -3;
	}
	free(csv);

	snprintf(url, sizeof(url), "%s?id_station=%s&format=json", MF_OBS_URL, sid);
	if (http_get_header(url, "apikey", s_mf_apikey, &body, &blen) != 0 || !body) {
		snprintf(w->error, sizeof(w->error), "Observation MF indisponible");
		return -4;
	}

	if (parse_number_after_key(body, "t", &t_k) != 0 || t_k < 200.0f || t_k > 340.0f) {
		free(body);
		snprintf(w->error, sizeof(w->error), "Temp MF invalide");
		return -5;
	}

	w->temp_now = t_k - 273.15f;
	if (parse_int_after_key(body, "u", &u) == 0) {
		w->humidity = u;
		if (have_hum)
			*have_hum = 1;
	}
	if (parse_number_after_key(body, "ff", &ff) == 0) {
		w->wind_kmh = ff * 3.6f; /* m/s → km/h */
		if (have_wind)
			*have_wind = 1;
	}

	free(body);
	return 0;
}

static int fetch_open_meteo_forecast(WeatherData *w, int have_temp, int have_hum, int have_wind)
{
	char url[512];
	char *body = NULL;
	size_t body_len = 0;
	const char *current;
	const char *daily;
	const char *hourly;
	float temps[64];
	int hours[64];
	int days[64];
	int ntemp = 0, ntime = 0;
	int i, start_i = -1;
	SceDateTime now;
	int next_h;
	int today_key;

	snprintf(url, sizeof(url),
	         "https://api.open-meteo.com/v1/forecast"
	         "?latitude=%.5f&longitude=%.5f"
	         "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
	         "&hourly=temperature_2m"
	         "&daily=temperature_2m_max,temperature_2m_min"
	         "&timezone=auto"
	         "&forecast_days=2"
	         "&temperature_unit=celsius"
	         "&wind_speed_unit=kmh",
	         (double)w->lat, (double)w->lon);

	if (http_get(url, &body, &body_len) != 0) {
		snprintf(w->error, sizeof(w->error), "Reseau / API indisponible");
		return -1;
	}

	current = strstr(body, "\"current\"");
	daily = strstr(body, "\"daily\"");
	hourly = strstr(body, "\"hourly\"");
	if (!current || !daily || !hourly) {
		snprintf(w->error, sizeof(w->error), "Reponse API invalide");
		free(body);
		return -2;
	}

	{
		char cur_buf[1024];
		size_t n = (size_t)(daily - current);
		if (n >= sizeof(cur_buf))
			n = sizeof(cur_buf) - 1;
		memcpy(cur_buf, current, n);
		cur_buf[n] = '\0';
		parse_int_after_key(cur_buf, "weather_code", &w->weather_code);
		if (!have_temp)
			parse_number_after_key(cur_buf, "temperature_2m", &w->temp_now);
		if (!have_hum)
			parse_int_after_key(cur_buf, "relative_humidity_2m", &w->humidity);
		if (!have_wind)
			parse_number_after_key(cur_buf, "wind_speed_10m", &w->wind_kmh);
	}

	snprintf(w->condition, sizeof(w->condition), "%s", weather_condition_fr(w->weather_code));

	{
		float maxa[4], mina[4];
		int nc = 0, nn = 0;
		parse_float_array(daily, "temperature_2m_max", maxa, 4, &nc);
		parse_float_array(daily, "temperature_2m_min", mina, 4, &nn);
		if (nc > 0)
			w->temp_max = maxa[0];
		if (nn > 0)
			w->temp_min = mina[0];
	}

	parse_float_array(hourly, "temperature_2m", temps, 64, &ntemp);
	parse_hourly_times(hourly, hours, days, 64, &ntime);
	if (ntemp <= 0 || ntime <= 0 || ntemp != ntime) {
		snprintf(w->error, sizeof(w->error), "Horaires API incomplets");
		free(body);
		return -3;
	}

	memset(&now, 0, sizeof(now));
	sceRtcGetCurrentClockLocalTime(&now);
	next_h = ((int)now.hour + 1) % 24;
	today_key = (int)now.year * 10000 + (int)now.month * 100 + (int)now.day;

	for (i = 0; i < ntime; i++) {
		if (hours[i] != next_h)
			continue;
		if (next_h == 0) {
			if (days[i] >= today_key)
				start_i = i;
		} else if (days[i] == today_key && hours[i] > (int)now.hour) {
			start_i = i;
		} else if (days[i] > today_key) {
			start_i = i;
		}
		if (start_i >= 0)
			break;
	}
	if (start_i < 0) {
		for (i = 0; i < ntime; i++) {
			if (days[i] > today_key ||
			    (days[i] == today_key && hours[i] > (int)now.hour)) {
				start_i = i;
				break;
			}
		}
	}
	if (start_i < 0)
		start_i = 0;

	w->hourly_count = 0;
	for (i = start_i; i < ntime && w->hourly_count < WEATHER_HOURLY_MAX; i++) {
		w->hourly_hour[w->hourly_count] = hours[i];
		w->hourly_temp[w->hourly_count] = temps[i];
		w->hourly_count++;
	}

	free(body);
	return 0;
}

int weather_fetch(WeatherData *w)
{
	int have_temp = 0, have_hum = 0, have_wind = 0;

	if (!w)
		return -1;
	w->ok = 0;
	w->error[0] = '\0';
	w->hourly_count = 0;
	w->station[0] = '\0';
	w->temp_now = 0.0f;
	w->humidity = 0;
	w->wind_kmh = 0.0f;
	w->weather_code = -1;
	w->condition[0] = '\0';

	if (fetch_mf_observation(w, &have_hum, &have_wind) == 0)
		have_temp = 1;

	if (fetch_open_meteo_forecast(w, have_temp, have_hum, have_wind) != 0) {
		if (!have_temp)
			return -1;
		snprintf(w->condition, sizeof(w->condition), "Observation MF");
		w->ok = 1;
		w->error[0] = '\0';
		return 0;
	}

	w->error[0] = '\0';
	w->ok = 1;
	return 0;
}
