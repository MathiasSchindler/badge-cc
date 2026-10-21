#include "http.h"

static uint32_t size(const char *text) {
    uint32_t n = 0u;
    while (text[n]) ++n;
    return n;
}

static int same(const uint8_t *data, uint32_t length, const char *text) {
    if (length != size(text)) return 0;
    for (uint32_t i = 0u; i < length; ++i)
        if (data[i] != (uint8_t)text[i]) return 0;
    return 1;
}

static int name(const uint8_t *data, uint32_t length, const char *text) {
    if (length != size(text)) return 0;
    for (uint32_t i = 0u; i < length; ++i) {
        uint8_t ch = data[i];
        if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch + 32u);
        if (ch != (uint8_t)text[i]) return 0;
    }
    return 1;
}

static int line_end(const uint8_t *data, uint32_t start, uint32_t length) {
    for (uint32_t i = start; i + 1u < length; ++i)
        if (data[i] == '\r' && data[i + 1u] == '\n') return (int)i;
    return -1;
}

int http_parse(const uint8_t *data, uint32_t length, const char *host,
                http_request_t *request) {
    if (length > HTTP_REQUEST_MAX) return -1;
    for (uint32_t i = 0u; i < length; ++i)
        if (data[i] == 0u || (data[i] < 32u && data[i] != '\r' &&
                              data[i] != '\n' && data[i] != '\t')) return -1;
    int end = line_end(data, 0u, length);
    if (end < 0) return length == HTTP_REQUEST_MAX ? -1 : 0;
    uint32_t start;
    if (end >= 4 && same(data, 4u, "GET ")) {
        request->post = 0u;
        start = 4u;
    } else if (end >= 5 && same(data, 5u, "POST ")) {
        request->post = 1u;
        start = 5u;
    } else return -1;
    uint32_t stop = start;
    while (stop < (uint32_t)end && data[stop] != ' ') ++stop;
    if (stop == start || stop - start >= sizeof(request->path) || data[start] != '/' ||
        !same(data + stop, (uint32_t)end - stop, " HTTP/1.1")) return -1;
    for (uint32_t i = start; i < stop; ++i) {
        if (data[i] <= 32u || data[i] >= 127u) return -1;
        request->path[i - start] = (char)data[i];
    }
    request->path[stop - start] = '\0';
    uint8_t host_seen = 0u, content_seen = 0u, type_seen = 0u, write_seen = 0u;
    uint32_t content = 0u;
    start = (uint32_t)end + 2u;
    while (1) {
        end = line_end(data, start, length);
        if (end < 0) return length == HTTP_REQUEST_MAX ? -1 : 0;
        if ((uint32_t)end == start) break;
        uint32_t colon = start;
        while (colon < (uint32_t)end && data[colon] != ':') ++colon;
        if (colon == start || colon == (uint32_t)end) return -1;
        for (uint32_t i = start; i < colon; ++i)
            if (!((data[i] >= 'a' && data[i] <= 'z') ||
                  (data[i] >= 'A' && data[i] <= 'Z') || data[i] == '-')) return -1;
        uint32_t value = colon + 1u, finish = (uint32_t)end;
        while (value < finish && (data[value] == ' ' || data[value] == '\t')) ++value;
        while (finish > value && (data[finish - 1u] == ' ' || data[finish - 1u] == '\t')) --finish;
        uint32_t n = finish - value;
        if (name(data + start, colon - start, "host")) {
            uint32_t host_length = size(host);
            if (host_seen || n < host_length) return -1;
            for (uint32_t i = 0u; i < host_length; ++i)
                if (data[value + i] != (uint8_t)host[i]) return -1;
            if (n != host_length && !same(data + value + host_length, n - host_length, ":80"))
                return -1;
            host_seen = 1u;
        } else if (name(data + start, colon - start, "content-length")) {
            if (content_seen || n == 0u || n > 4u) return -1;
            content_seen = 1u;
            for (uint32_t i = value; i < finish; ++i) {
                if (data[i] < '0' || data[i] > '9') return -1;
                content = content * 10u + data[i] - '0';
            }
            if (content > 256u) return -1;
        } else if (name(data + start, colon - start, "transfer-encoding") ||
                   name(data + start, colon - start, "expect")) return -1;
        else if (name(data + start, colon - start, "x-pico-request")) {
            if (write_seen) return -1;
            write_seen = 1u;
            request->write_allowed = same(data + value, n, "1");
        } else if (name(data + start, colon - start, "content-type")) {
            if (type_seen || !same(data + value, n, "application/x-www-form-urlencoded"))
                return -1;
            type_seen = 1u;
        }
        start = (uint32_t)end + 2u;
    }
    if (!write_seen) request->write_allowed = 0u;
    if (!host_seen || (request->post && (!content_seen || !type_seen)) ||
        (!request->post && content != 0u)) return -1;
    start = (uint32_t)end + 2u;
    if (length < start + content) return length == HTTP_REQUEST_MAX ? -1 : 0;
    if (length != start + content) return -1;
    request->body = data + start;
    request->body_size = content;
    return 1;
}

uint32_t http_response(uint8_t *out, uint32_t capacity, uint32_t status,
                        const char *type, const uint8_t *body, uint32_t length) {
    const char *reason = status == 200u ? "200 OK" :
                         status == 400u ? "400 Bad Request" :
                         status == 403u ? "403 Forbidden" :
                         status == 404u ? "404 Not Found" :
                         status == 409u ? "409 Conflict" :
                         status == 503u ? "503 Service Unavailable" :
                         "500 Internal Server Error";
    char digits[10];
    uint32_t count = 0u, number = length;
    do {
        digits[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0u);
    const char *parts[] = {
        "HTTP/1.1 ", reason, "\r\nContent-Type: ", type,
        "\r\nCache-Control: no-store\r\nConnection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline'; frame-ancestors 'none'\r\nContent-Length: "
    };
    uint32_t header = count + 4u;
    for (uint32_t i = 0u; i < sizeof(parts) / sizeof(parts[0]); ++i) header += size(parts[i]);
    if (header > capacity || length > capacity - header) return 0u;
    uint32_t position = 0u;
    for (uint32_t i = 0u; i < sizeof(parts) / sizeof(parts[0]); ++i)
        for (uint32_t j = 0u; parts[i][j]; ++j) out[position++] = (uint8_t)parts[i][j];
    while (count) out[position++] = (uint8_t)digits[--count];
    out[position++] = '\r';
    out[position++] = '\n';
    out[position++] = '\r';
    out[position++] = '\n';
    for (uint32_t i = 0u; i < length; ++i) out[position++] = body[i];
    return position;
}
