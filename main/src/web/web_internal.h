#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_http_server.h"
#include "esp_err.h"

void web_session_rotate(void);
bool web_session_is_valid(httpd_req_t *req);
bool web_require_session(httpd_req_t *req);
void web_session_set_cookie(httpd_req_t *req);
bool web_get_form_value(const char *body, const char *name, char *out, size_t capacity);
esp_err_t web_send_error(httpd_req_t *req, const char *message);

esp_err_t web_login_page(httpd_req_t *req);
esp_err_t web_login_post(httpd_req_t *req);
esp_err_t web_logout_post(httpd_req_t *req);
esp_err_t web_dashboard_page(httpd_req_t *req);
esp_err_t web_setup_page_handler(httpd_req_t *req);
esp_err_t web_home_handler(httpd_req_t *req);
esp_err_t web_wifi_post_handler(httpd_req_t *req);
esp_err_t web_status_handler(httpd_req_t *req);
esp_err_t web_setup_status_handler(httpd_req_t *req);
esp_err_t web_wireguard_post_handler(httpd_req_t *req);
esp_err_t web_wireguard_toggle_handler(httpd_req_t *req);
esp_err_t web_forward_post_handler(httpd_req_t *req);
esp_err_t web_settings_wifi_handler(httpd_req_t *req);
esp_err_t web_password_post_handler(httpd_req_t *req);
esp_err_t web_restart_post_handler(httpd_req_t *req);
esp_err_t web_portal_redirect(httpd_req_t *req);
