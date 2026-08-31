#include "http.h"

#include <curl/curl.h>
#include <psp2/libssl.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} HttpBuf;

static void *s_net_mem = NULL;
static int s_http_ready = 0;

static size_t http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	HttpBuf *buf = (HttpBuf *)userdata;
	size_t n = size * nmemb;
	if (buf->len + n + 1 > buf->cap) {
		size_t ncap = buf->cap ? buf->cap * 2 : 8192;
		char *p;
		while (ncap < buf->len + n + 1)
			ncap *= 2;
		p = (char *)realloc(buf->data, ncap);
		if (!p)
			return 0;
		buf->data = p;
		buf->cap = ncap;
	}
	memcpy(buf->data + buf->len, ptr, n);
	buf->len += n;
	buf->data[buf->len] = '\0';
	return n;
}

int http_init(void)
{
	SceNetInitParam netParam;
	int ret;

	if (s_http_ready)
		return 0;

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);

	s_net_mem = malloc(1024 * 1024);
	if (!s_net_mem)
		return -1;

	memset(&netParam, 0, sizeof(netParam));
	netParam.memory = s_net_mem;
	netParam.size = 1024 * 1024;
	netParam.flags = 0;
	ret = sceNetInit(&netParam);
	if (ret < 0)
		return -2;

	ret = sceNetCtlInit();
	if (ret < 0)
		return -3;

	ret = sceSslInit(300 * 1024);
	if (ret < 0)
		return -4;

	ret = sceHttpInit(1024 * 1024);
	if (ret < 0)
		return -5;

	curl_global_init(CURL_GLOBAL_DEFAULT);
	s_http_ready = 1;
	return 0;
}

void http_term(void)
{
	if (!s_http_ready)
		return;
	curl_global_cleanup();
	sceHttpTerm();
	sceSslTerm();
	sceNetCtlTerm();
	sceNetTerm();
	free(s_net_mem);
	s_net_mem = NULL;
	sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
	sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
	sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
	s_http_ready = 0;
}

int http_get_header(const char *url, const char *header_name, const char *header_value,
                    char **out_body, size_t *out_len)
{
	CURL *curl;
	CURLcode rc;
	HttpBuf buf;
	long code = 0;
	struct curl_slist *hdrs = NULL;

	if (!url || !out_body)
		return -1;
	*out_body = NULL;
	if (out_len)
		*out_len = 0;

	memset(&buf, 0, sizeof(buf));
	curl = curl_easy_init();
	if (!curl)
		return -2;

	if (header_name && header_name[0] && header_value) {
		char line[2048];
		snprintf(line, sizeof(line), "%s: %s", header_name, header_value);
		hdrs = curl_slist_append(hdrs, line);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "MeteoVita/0.2");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

	rc = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK || code < 200 || code >= 300 || !buf.data) {
		free(buf.data);
		return -3;
	}

	*out_body = buf.data;
	if (out_len)
		*out_len = buf.len;
	return 0;
}

int http_get(const char *url, char **out_body, size_t *out_len)
{
	return http_get_header(url, NULL, NULL, out_body, out_len);
}
