#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "gateway.h"
#include "web_internal.h"
#include "esp_system.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

static void decode_form_value(char *value)
{
    char *read = value, *write = value;
    while (*read) {
        if (*read == '+') { *write++ = ' '; read++; }
        else if (*read == '%' && read[1] && read[2]) {
            int hi = hex_value(read[1]), lo = hex_value(read[2]);
            if (hi >= 0 && lo >= 0) { *write++ = (char)((hi << 4) | lo); read += 3; }
            else *write++ = *read++;
        } else *write++ = *read++;
    }
    *write = '\0';
}

bool web_get_form_value(const char *body, const char *name, char *out, size_t capacity)
{
    size_t name_len = strlen(name);
    const char *field = body;
    while (field && *field) {
        const char *end = strchr(field, '&');
        if (!end) end = field + strlen(field);
        const char *equals = memchr(field, '=', end - field);
        if (equals && (size_t)(equals - field) == name_len && memcmp(field, name, name_len) == 0) {
            size_t len = (size_t)(end - equals - 1);
            if (len >= capacity) return false;
            memcpy(out, equals + 1, len);
            out[len] = '\0';
            decode_form_value(out);
            return true;
        }
        field = *end ? end + 1 : NULL;
    }
    return false;
}

bool web_read_body(httpd_req_t *req, char *out, size_t capacity)
{
    if (!req || !out || req->content_len <= 0 || (size_t)req->content_len >= capacity) return false;
    size_t received_total = 0;
    unsigned timeouts = 0;
    while (received_total < (size_t)req->content_len) {
        int received = httpd_req_recv(req, out + received_total, req->content_len - received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && timeouts++ < 3) continue;
        if (received <= 0) return false;
        received_total += (size_t)received;
    }
    out[received_total] = '\0';
    return true;
}

esp_err_t web_send_error(httpd_req_t *req, const char *message)
{
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, message, strlen(message));
}

static char session_token[33];
static char session_cookie[96];

void web_session_rotate(void)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); i++) {
        session_token[i * 2] = hex[random[i] >> 4];
        session_token[i * 2 + 1] = hex[random[i] & 15];
    }
    session_token[32] = '\0';
}

bool web_session_is_valid(httpd_req_t *req)
{
    size_t len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (!len || len >= 160) return false;
    char cookie[160];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) return false;
    const char *value = strstr(cookie, "gw_session=");
    if (!value) return false;
    value += strlen("gw_session=");
    unsigned diff = 0;
    for (size_t i = 0; i < 32; i++) {
        if (!value[i] || value[i] == ';') return false;
        diff |= (unsigned char)value[i] ^ (unsigned char)session_token[i];
    }
    return diff == 0 && (value[32] == '\0' || value[32] == ';');
}

bool web_require_session(httpd_req_t *req)
{
    if (gateway_auth_configured() && web_session_is_valid(req)) return true;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, "", 0);
    return false;
}

void web_session_set_cookie(httpd_req_t *req)
{
    snprintf(session_cookie, sizeof(session_cookie), "gw_session=%s; Path=/; HttpOnly; SameSite=Strict", session_token);
    httpd_resp_set_hdr(req, "Set-Cookie", session_cookie);
}
