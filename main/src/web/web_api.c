#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gateway.h"
#include "web_internal.h"
#include "esp_system.h"
#include "esp_clk.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "esp_heap_caps.h"
#include "spi_flash.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint32_t current_cpu_load_percent(void)
{
    static TaskStatus_t tasks[32];
    static uint32_t previous_total, previous_idle;
    uint32_t total = 0, idle = 0;
    UBaseType_t count = uxTaskGetSystemState(tasks, sizeof(tasks) / sizeof(tasks[0]), &total);
    TaskHandle_t idle_handle = xTaskGetIdleTaskHandle();
    if (!count || !idle_handle) return 0;
    for (UBaseType_t i = 0; i < count; i++) {
        if (tasks[i].xHandle == idle_handle) {
            idle = tasks[i].ulRunTimeCounter;
            break;
        }
    }
    uint32_t elapsed = total - previous_total;
    uint32_t idle_elapsed = idle - previous_idle;
    previous_total = total;
    previous_idle = idle;
    if (!elapsed) return 0;
    if (idle_elapsed > elapsed) idle_elapsed = elapsed;
    return (uint32_t)(((uint64_t)(elapsed - idle_elapsed) * 100U) / elapsed);
}

esp_err_t web_login_post(httpd_req_t *req)
{
    char body[256] = {0}, password[64] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid login form");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len || !web_get_form_value(body, "password", password, sizeof(password))) return web_send_error(req, "Invalid login form");
    bool valid = gateway_auth_verify(password);
    memset(password, 0, sizeof(password));
    if (!valid) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, "Incorrect password", strlen("Incorrect password"));
    }
    web_session_set_cookie(req);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, "", 0);
}

esp_err_t web_logout_post(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    web_session_rotate();
    httpd_resp_set_hdr(req, "Set-Cookie", "gw_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/login");
    return httpd_resp_send(req, "", 0);
}

esp_err_t web_wifi_post_handler(httpd_req_t *req)
{
    char body[768] = {0}, ssid[33] = {0}, password[64] = {0}, admin_password[64] = {0}, admin_confirm[64] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid form");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len) return web_send_error(req, "Incomplete form");
    if (!web_get_form_value(body, "ssid", ssid, sizeof(ssid)) || !ssid[0] || !web_get_form_value(body, "password", password, sizeof(password)) ||
        !web_get_form_value(body, "admin_password", admin_password, sizeof(admin_password)) || !web_get_form_value(body, "admin_confirm", admin_confirm, sizeof(admin_confirm)))
        return web_send_error(req, "SSID, Wi-Fi password and admin password fields are required");
    if (strlen(admin_password) < 8 || strcmp(admin_password, admin_confirm) != 0) return web_send_error(req, "Admin password must be at least 8 characters and match confirmation");
    if (gateway_auth_configured() && !web_session_is_valid(req) && !gateway_auth_verify(admin_password))
        return web_send_error(req, "Admin password is incorrect");
    esp_err_t err = gateway_auth_store(admin_password);
    memset(admin_password, 0, sizeof(admin_password));
    memset(admin_confirm, 0, sizeof(admin_confirm));
    if (err != ESP_OK) return web_send_error(req, "Could not save admin password");
    err = gateway_wifi_connect(ssid, password);
    if (err != ESP_OK) return web_send_error(req, "Could not start Wi-Fi connection");
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_send(req, "Connecting", strlen("Connecting"));
}

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t') text++;
    size_t len = strlen(text);
    while (len && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r')) text[--len] = '\0';
    return text;
}

static bool copy_config_value(char *out, size_t capacity, const char *value)
{
    if (strlen(value) >= capacity) return false;
    strlcpy(out, value, capacity);
    return true;
}

static bool parse_wireguard_conf(char *text, gateway_wireguard_config_t *config)
{
    char *line = text;
    bool have_address = false, have_endpoint = false;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        char *current = trim(line);
        char *equals = strchr(current, '=');
        if (*current && *current != '#' && *current != ';' && *current != '[' && equals) {
            *equals++ = '\0';
            char *key = trim(current), *value = trim(equals);
            if (strcmp(key, "PrivateKey") == 0) {
                if (!copy_config_value(config->private_key, sizeof(config->private_key), value)) return false;
            } else if (strcmp(key, "PublicKey") == 0) {
                if (!copy_config_value(config->server_public_key, sizeof(config->server_public_key), value)) return false;
            } else if (strcmp(key, "PresharedKey") == 0) {
                if (!copy_config_value(config->preshared_key, sizeof(config->preshared_key), value)) return false;
            } else if (strcmp(key, "DNS") == 0) {
                if (!copy_config_value(config->dns, sizeof(config->dns), value)) return false;
            } else if (strcmp(key, "AllowedIPs") == 0) {
                if (!copy_config_value(config->allowed_ips, sizeof(config->allowed_ips), value)) return false;
            } else if (strcmp(key, "Address") == 0) {
                char address[40];
                if (!copy_config_value(address, sizeof(address), value)) return false;
                char *comma = strchr(address, ','); if (comma) *comma = '\0';
                char *slash = strchr(address, '/');
                if (slash) *slash++ = '\0'; else slash = "32";
                unsigned prefix = strtoul(slash, NULL, 10);
                if (prefix > 32) return false;
                unsigned mask = prefix ? (0xffffffffU << (32 - prefix)) : 0;
                snprintf(config->tunnel_mask, sizeof(config->tunnel_mask), "%u.%u.%u.%u", (mask >> 24) & 255, (mask >> 16) & 255, (mask >> 8) & 255, mask & 255);
                if (!copy_config_value(config->tunnel_ip, sizeof(config->tunnel_ip), address)) return false;
                have_address = true;
            } else if (strcmp(key, "Endpoint") == 0) {
                char endpoint[100];
                if (!copy_config_value(endpoint, sizeof(endpoint), value)) return false;
                char *colon = strrchr(endpoint, ':');
                if (!colon) return false;
                *colon++ = '\0';
                unsigned port = strtoul(colon, NULL, 10);
                if (!port || port > 65535 || !copy_config_value(config->endpoint, sizeof(config->endpoint), endpoint)) return false;
                config->endpoint_port = port;
                have_endpoint = true;
            } else if (strcmp(key, "PersistentKeepalive") == 0) {
                unsigned value_num = strtoul(value, NULL, 10);
                if (value_num > 65535) return false;
                config->persistent_keepalive = value_num;
            }
        }
        line = next;
    }
    return config->private_key[0] && config->server_public_key[0] && have_address && have_endpoint;
}

esp_err_t web_status_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    bool authenticated = true;
    char ip[16], tunnel_ip[16] = "", response[1280], wifi_ssid[33] = "", static_ip[16] = "", wifi_gateway[16] = "", netmask[16] = "", ssid_json[100] = "";
    bool use_static_ip = false;
    gateway_wifi_ip(ip, sizeof(ip));
    if (authenticated) gateway_wireguard_tunnel_ip(tunnel_ip, sizeof(tunnel_ip));
    if (authenticated) gateway_wifi_get_settings(wifi_ssid, sizeof(wifi_ssid), NULL, 0, &use_static_ip, static_ip, sizeof(static_ip), wifi_gateway, sizeof(wifi_gateway), netmask, sizeof(netmask));
    for (size_t i = 0, j = 0; wifi_ssid[i] && j + 2 < sizeof(ssid_json); i++) {
        if (wifi_ssid[i] == '"' || wifi_ssid[i] == '\\') ssid_json[j++] = '\\';
        if ((unsigned char)wifi_ssid[i] >= 0x20) ssid_json[j++] = wifi_ssid[i];
        ssid_json[j] = '\0';
    }
    gateway_forward_rule_t rules[3];
    size_t count = authenticated ? gateway_forward_list(rules, 3) : 0;
    const esp_partition_t *app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    const esp_partition_t *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    uint32_t free_heap = authenticated ? esp_get_free_heap_size() : 0;
    uint32_t min_heap = authenticated ? esp_get_minimum_free_heap_size() : 0;
    uint32_t cpu_load = authenticated ? current_cpu_load_percent() : 0;
    uint32_t cpu_mhz = authenticated ? (uint32_t)(esp_clk_cpu_freq() / 1000000) : 0;
    uint32_t task_count = authenticated ? (uint32_t)uxTaskGetNumberOfTasks() : 0;
    uint32_t flash_size = authenticated ? (uint32_t)spi_flash_get_chip_size() : 0;
    static uint32_t app_image_size;
    static bool app_image_checked;
    if (authenticated && app && !app_image_checked) {
        esp_partition_pos_t position = {.offset = app->address, .size = app->size};
        esp_image_metadata_t metadata = {0};
        if (esp_image_load(ESP_IMAGE_VERIFY_SILENT, &position, &metadata) == ESP_OK) app_image_size = metadata.image_len;
        app_image_checked = true;
    }
    extern heap_region_t g_heap_region[];
    extern size_t g_heap_region_num;
    uint32_t heap_total = 0;
    if (authenticated) for (size_t i = 0; i < g_heap_region_num; i++) if (g_heap_region[i].caps & MALLOC_CAP_8BIT) heap_total += g_heap_region[i].total_size;
    uint32_t app_capacity = authenticated && app ? app->size : 0;
    uint32_t app_size = authenticated ? app_image_size : 0;
    uint32_t nvs_size = authenticated && nvs ? nvs->size : 0;
    uint32_t uptime = authenticated ? (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000) : 0;
    const char *state = gateway_wifi_state() == GATEWAY_WIFI_CONNECTED ? "connected" :
                        gateway_wifi_state() == GATEWAY_WIFI_CONNECTING ? "connecting" :
                        gateway_wifi_state() == GATEWAY_WIFI_FAILED ? "failed" : "setup";
    int length = snprintf(response, sizeof(response), "{\"state\":\"%s\",\"wifi_reason\":%u,\"ip\":\"%s\",\"ssid\":\"%s\",\"ip_mode\":\"%s\",\"static_ip\":\"%s\",\"gateway\":\"%s\",\"netmask\":\"%s\",\"tunnel_ip\":\"%s\",\"wg\":\"%s\",\"wg_enabled\":%s,\"heap_free\":%u,\"heap_total\":%u,\"heap_min\":%u,\"cpu_load\":%u,\"cpu_mhz\":%u,\"cpu_tasks\":%u,\"flash_bytes\":%u,\"app_bytes\":%u,\"app_capacity\":%u,\"nvs_bytes\":%u,\"uptime\":%u,\"rules\":[", state, gateway_wifi_disconnect_reason(), ip, ssid_json, use_static_ip ? "static" : "dhcp", static_ip, wifi_gateway, netmask, tunnel_ip, authenticated ? gateway_wireguard_status() : "", gateway_wireguard_is_enabled() ? "true" : "false", free_heap, heap_total, min_heap, cpu_load, cpu_mhz, task_count, flash_size, app_size, app_capacity, nvs_size, uptime);
    for (size_t i = 0; i < count && length > 0 && length < sizeof(response); i++) {
        length += snprintf(response + length, sizeof(response) - length, "%s{\"listen_port\":%u,\"target_ip\":\"%s\",\"target_port\":%u}", i ? "," : "", rules[i].listen_port, rules[i].target_ip, rules[i].target_port);
    }
    if (length > 0 && length < sizeof(response)) length += snprintf(response + length, sizeof(response) - length, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, length);
}

esp_err_t web_setup_status_handler(httpd_req_t *req)
{
    char ip[16] = "";
    gateway_wifi_ip(ip, sizeof(ip));
    const char *state = gateway_wifi_state() == GATEWAY_WIFI_CONNECTED ? "connected" :
                        gateway_wifi_state() == GATEWAY_WIFI_CONNECTING ? "connecting" :
                        gateway_wifi_state() == GATEWAY_WIFI_FAILED ? "failed" : "setup";
    char response[96];
    int length = snprintf(response, sizeof(response), "{\"state\":\"%s\",\"wifi_reason\":%u,\"ip\":\"%s\"}", state, gateway_wifi_disconnect_reason(), ip);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, response, length);
}

esp_err_t web_wireguard_post_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    char body[768] = {0}, config_text[640] = {0};
    gateway_wireguard_config_t config = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid WireGuard form");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len) return web_send_error(req, "Incomplete form");
    if (!web_get_form_value(body, "config", config_text, sizeof(config_text)) || !parse_wireguard_conf(config_text, &config))
        return web_send_error(req, "Config must include PrivateKey, Address, Peer PublicKey and Endpoint");
    esp_err_t err = gateway_wireguard_save(&config);
    if (err != ESP_OK) return web_send_error(req, "Invalid WireGuard configuration");
    gateway_wireguard_start();
    return httpd_resp_send(req, "WireGuard configuration saved", strlen("WireGuard configuration saved"));
}

esp_err_t web_wireguard_toggle_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    char body[32] = {0}, value[8] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid WireGuard action");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len || !web_get_form_value(body, "enabled", value, sizeof(value))) return web_send_error(req, "WireGuard action is required");
    if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) return web_send_error(req, "Invalid WireGuard action");
    esp_err_t err = gateway_wireguard_set_enabled(value[0] == '1');
    if (err != ESP_OK) return web_send_error(req, "Could not change WireGuard state");
    return httpd_resp_send(req, "WireGuard state updated", strlen("WireGuard state updated"));
}

esp_err_t web_forward_post_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    char body[192] = {0}, value[16] = {0}, target_ip[16] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid forwarding form");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len) return web_send_error(req, "Incomplete form");
    if (!web_get_form_value(body, "listen_port", value, sizeof(value))) return web_send_error(req, "Listen port is required");
    unsigned long listen_port = strtoul(value, NULL, 10);
    if (!web_get_form_value(body, "target_port", value, sizeof(value))) return web_send_error(req, "Target port is required");
    unsigned long target_port = strtoul(value, NULL, 10);
    if (!web_get_form_value(body, "target_ip", target_ip, sizeof(target_ip)) || !listen_port || listen_port > 65535 || !target_port || target_port > 65535)
        return web_send_error(req, "Invalid forwarding rule");
    esp_err_t err = gateway_forward_add(listen_port, target_ip, target_port);
    if (err == ESP_ERR_NO_MEM) return web_send_error(req, "Maximum of three rules reached");
    if (err != ESP_OK) return web_send_error(req, "Could not save forwarding rule");
    return httpd_resp_send(req, "TCP forwarding rule saved", strlen("TCP forwarding rule saved"));
}

esp_err_t web_settings_wifi_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    char body[384] = {0}, ssid[33] = {0}, password[64] = {0}, mode[12] = {0};
    char ip[16] = {0}, gateway[16] = {0}, netmask[16] = {0};
    char old_ssid[33] = {0}, old_password[64] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid Wi-Fi form");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len) return web_send_error(req, "Incomplete form");
    if (!web_get_form_value(body, "ssid", ssid, sizeof(ssid)) || !ssid[0] || !web_get_form_value(body, "password", password, sizeof(password)))
        return web_send_error(req, "SSID and Wi-Fi password fields are required");
    gateway_wifi_get_settings(old_ssid, sizeof(old_ssid), old_password, sizeof(old_password), NULL,
                              NULL, 0, NULL, 0, NULL, 0);
    if (!password[0] && strcmp(ssid, old_ssid) == 0) strlcpy(password, old_password, sizeof(password));
    web_get_form_value(body, "ip_mode", mode, sizeof(mode));
    bool use_static = strcmp(mode, "static") == 0;
    if (use_static) {
        ip4_addr_t parsed;
        if (!web_get_form_value(body, "ip", ip, sizeof(ip)) || !web_get_form_value(body, "gateway", gateway, sizeof(gateway)) ||
            !web_get_form_value(body, "netmask", netmask, sizeof(netmask)) || !ip4addr_aton(ip, &parsed) ||
            !ip4addr_aton(gateway, &parsed) || !ip4addr_aton(netmask, &parsed))
            return web_send_error(req, "Enter valid IP, gateway and subnet mask values");
    }
    esp_err_t err = gateway_wifi_connect_with_ip(ssid, password, use_static, ip, gateway, netmask);
    if (err != ESP_OK) return web_send_error(req, "Could not start Wi-Fi connection");
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_send(req, "Connecting", strlen("Connecting"));
}

esp_err_t web_password_post_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    char body[256] = {0}, password[64] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) return web_send_error(req, "Invalid password form");
    int received = httpd_req_recv(req, body, req->content_len);
    if (received != req->content_len) return web_send_error(req, "Incomplete form");
    if (!web_get_form_value(body, "password", password, sizeof(password)) || strlen(password) < 8)
        return web_send_error(req, "Password must contain at least 8 characters");
    esp_err_t err = gateway_auth_store(password);
    memset(password, 0, sizeof(password));
    if (err != ESP_OK) return web_send_error(req, "Could not save password");
    web_session_rotate();
    web_session_set_cookie(req);
    return httpd_resp_send(req, "Password updated", strlen("Password updated"));
}

esp_err_t web_restart_post_handler(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    httpd_resp_send(req, "Restarting", strlen("Restarting"));
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

