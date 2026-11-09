#include <stddef.h>
#include "gateway.h"
#include "web_internal.h"
#include "esp_http_server.h"

esp_err_t web_portal_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    return httpd_resp_send(req, "", 0);
}

void setup_web_start(void)
{
    web_session_rotate();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 26;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) return;
    httpd_uri_t home = {.uri = "/", .method = HTTP_GET, .handler = web_home_handler};
    httpd_uri_t login_get = {.uri = "/login", .method = HTTP_GET, .handler = web_login_page};
    httpd_uri_t login_post_uri = {.uri = "/login", .method = HTTP_POST, .handler = web_login_post};
    httpd_uri_t logout = {.uri = "/logout", .method = HTTP_POST, .handler = web_logout_post};
    httpd_uri_t wireguard_page = {.uri = "/wireguard", .method = HTTP_GET, .handler = web_dashboard_page};
    httpd_uri_t forwards_page = {.uri = "/forwards", .method = HTTP_GET, .handler = web_dashboard_page};
    httpd_uri_t settings_page = {.uri = "/settings", .method = HTTP_GET, .handler = web_dashboard_page};
    httpd_uri_t system_page = {.uri = "/system", .method = HTTP_GET, .handler = web_dashboard_page};
    httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_POST, .handler = web_wifi_post_handler};
    httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = web_status_handler};
    httpd_uri_t setup_status = {.uri = "/setup/status", .method = HTTP_GET, .handler = web_setup_status_handler};
    httpd_uri_t wireguard = {.uri = "/wireguard", .method = HTTP_POST, .handler = web_wireguard_post_handler};
    httpd_uri_t wireguard_toggle = {.uri = "/wireguard/toggle", .method = HTTP_POST, .handler = web_wireguard_toggle_handler};
    httpd_uri_t forward = {.uri = "/forward", .method = HTTP_POST, .handler = web_forward_post_handler};
    httpd_uri_t settings_wifi = {.uri = "/settings/wifi", .method = HTTP_POST, .handler = web_settings_wifi_handler};
    httpd_uri_t password = {.uri = "/password", .method = HTTP_POST, .handler = web_password_post_handler};
    httpd_uri_t restart = {.uri = "/restart", .method = HTTP_POST, .handler = web_restart_post_handler};
    httpd_register_uri_handler(server, &home);
    httpd_register_uri_handler(server, &login_get);
    httpd_register_uri_handler(server, &login_post_uri);
    httpd_register_uri_handler(server, &logout);
    httpd_register_uri_handler(server, &wireguard_page);
    httpd_register_uri_handler(server, &forwards_page);
    httpd_register_uri_handler(server, &settings_page);
    httpd_register_uri_handler(server, &system_page);
    httpd_register_uri_handler(server, &wifi);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &setup_status);
    httpd_register_uri_handler(server, &wireguard);
    httpd_register_uri_handler(server, &wireguard_toggle);
    httpd_register_uri_handler(server, &forward);
    httpd_register_uri_handler(server, &settings_wifi);
    httpd_register_uri_handler(server, &password);
    httpd_register_uri_handler(server, &restart);
    static const char *probes[] = {
        "/generate_204", "/gen_204", "/hotspot-detect.html", "/connecttest.txt",
        "/ncsi.txt", "/canonical.html", "/success.txt", "/redirect", "/library/test/success.html"
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        httpd_uri_t probe = {.uri = probes[i], .method = HTTP_GET, .handler = web_portal_redirect};
        httpd_register_uri_handler(server, &probe);
    }
}
