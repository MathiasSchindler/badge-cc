#ifndef PICO2W_HTTP_H
#define PICO2W_HTTP_H

#include "types.h"

#define HTTP_REQUEST_MAX 1024u
#define HTTP_RESPONSE_MAX 16384u

typedef struct {
    uint8_t post;
    uint8_t write_allowed;
    char path[48];
    const uint8_t *body;
    uint32_t body_size;
} http_request_t;

/* 0 incomplete, 1 complete, -1 malformed/unsupported. No pipelining. */
int http_parse(const uint8_t *data, uint32_t length, const char *host,
                http_request_t *request);
uint32_t http_response(uint8_t *out, uint32_t capacity, uint32_t status,
                        const char *type, const uint8_t *body, uint32_t length);

#endif
