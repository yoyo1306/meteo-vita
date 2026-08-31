#include "config.h"

#include "mf_secret.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_DIR   "ux0:data/meteo_vita"
#define CONFIG_PATH  "ux0:data/meteo_vita/config.txt"
#define APIKEY_PATH  "ux0:data/meteo_vita/mf_apikey.txt"

void config_set_defaults(AppConfig *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->city, sizeof(cfg->city), "Paris");
	cfg->lat = 48.8566f;
	cfg->lon = 2.3522f;
	snprintf(cfg->mf_apikey, sizeof(cfg->mf_apikey), "%s", MF_APIKEY_DEFAULT);
	cfg->valid = 1;
}

static void ensure_dir(void)
{
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(CONFIG_DIR, 0777);
}

static int save_apikey(const char *key)
{
	int fd;
	size_t n;

	if (!key || !key[0])
		return -1;
	ensure_dir();
	n = strlen(key);
	fd = sceIoOpen(APIKEY_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0)
		return -2;
	sceIoWrite(fd, key, (SceSize)n);
	sceIoClose(fd);
	return 0;
}

static int load_apikey(char *out, size_t out_sz)
{
	char buf[MF_APIKEY_MAX];
	int fd;
	int rd;
	int i;

	if (!out || out_sz < 8)
		return -1;
	fd = sceIoOpen(APIKEY_PATH, SCE_O_RDONLY, 0);
	if (fd < 0)
		return -2;
	memset(buf, 0, sizeof(buf));
	rd = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	if (rd <= 0)
		return -3;
	buf[rd] = '\0';
	/* trim CR/LF/spaces */
	while (rd > 0 && (buf[rd - 1] == '\n' || buf[rd - 1] == '\r' ||
	                  buf[rd - 1] == ' ' || buf[rd - 1] == '\t')) {
		buf[--rd] = '\0';
	}
	i = 0;
	while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r')
		i++;
	if (!buf[i])
		return -4;
	snprintf(out, out_sz, "%s", buf + i);
	return 0;
}

int config_save(const AppConfig *cfg)
{
	char buf[256];
	int fd;
	int n;

	if (!cfg || !cfg->city[0])
		return -1;

	ensure_dir();
	n = snprintf(buf, sizeof(buf), "city=%s\nlat=%.6f\nlon=%.6f\n",
	             cfg->city, (double)cfg->lat, (double)cfg->lon);
	if (n <= 0 || n >= (int)sizeof(buf))
		return -2;

	fd = sceIoOpen(CONFIG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0)
		return -3;
	sceIoWrite(fd, buf, (SceSize)n);
	sceIoClose(fd);

	if (cfg->mf_apikey[0])
		save_apikey(cfg->mf_apikey);
	return 0;
}

int config_load(AppConfig *cfg)
{
	char buf[256];
	int fd;
	int rd;
	char *p;
	char city[64];
	float lat = 0.0f, lon = 0.0f;
	int got_city = 0, got_lat = 0, got_lon = 0;

	if (!cfg)
		return -1;

	config_set_defaults(cfg);

	fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
	if (fd < 0)
		return -2;

	memset(buf, 0, sizeof(buf));
	rd = sceIoRead(fd, buf, sizeof(buf) - 1);
	sceIoClose(fd);
	if (rd <= 0)
		return -3;
	buf[rd] = '\0';

	memset(city, 0, sizeof(city));
	p = buf;
	while (p && *p) {
		char *line = p;
		char *nl = strchr(p, '\n');
		if (nl) {
			*nl = '\0';
			p = nl + 1;
		} else {
			p = NULL;
		}
		if (strncmp(line, "city=", 5) == 0) {
			snprintf(city, sizeof(city), "%s", line + 5);
			got_city = 1;
		} else if (strncmp(line, "lat=", 4) == 0) {
			lat = (float)atof(line + 4);
			got_lat = 1;
		} else if (strncmp(line, "lon=", 4) == 0) {
			lon = (float)atof(line + 4);
			got_lon = 1;
		}
	}

	if (!got_city || !got_lat || !got_lon || !city[0])
		return -4;

	snprintf(cfg->city, sizeof(cfg->city), "%s", city);
	cfg->lat = lat;
	cfg->lon = lon;
	if (load_apikey(cfg->mf_apikey, sizeof(cfg->mf_apikey)) != 0)
		snprintf(cfg->mf_apikey, sizeof(cfg->mf_apikey), "%s", MF_APIKEY_DEFAULT);
	cfg->valid = 1;
	return 0;
}
