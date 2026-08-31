#ifndef METEO_HTTP_H
#define METEO_HTTP_H

#include <stddef.h>

/* Init réseau Vita + libcurl. Retourne 0 si OK. */
int http_init(void);
void http_term(void);

/*
 * GET HTTPS → buffer alloué (free par l'appelant).
 * Retourne 0 si OK, <0 sinon. *out_len = taille sans '\0'.
 * header_name/value optionnels (ex. "apikey" pour Météo-France).
 */
int http_get(const char *url, char **out_body, size_t *out_len);
int http_get_header(const char *url, const char *header_name, const char *header_value,
                    char **out_body, size_t *out_len);

#endif
