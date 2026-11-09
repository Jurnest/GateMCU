#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

typedef enum {
    GATEWAY_WIFI_SETUP,
    GATEWAY_WIFI_CONNECTING,
    GATEWAY_WIFI_CONNECTED,
    GATEWAY_WIFI_FAILED
} gateway_wifi_state_t;

typedef struct {
    char private_key[45];
    char server_public_key[45];
    char preshared_key[45];
    char endpoint[80];
    char tunnel_ip[16];
    char tunnel_mask[16];
    char dns[64];
    char allowed_ips[64];
    uint16_t endpoint_port;
    uint16_t persistent_keepalive;
} gateway_wireguard_config_t;

typedef struct {
    uint8_t index;
    uint16_t listen_port;
    char target_ip[16];
    uint16_t target_port;
    bool enabled;
} gateway_forward_rule_t;

typedef struct {
    uint32_t tick;
    uint32_t bytes;
    uint32_t duration_ms;
    uint16_t listen_port;
    uint16_t target_port;
    uint8_t type;
    int16_t error;
    char source_ip[16];
    char target_ip[16];
} gateway_event_t;

void gateway_wifi_init(void);
esp_err_t gateway_wifi_connect(const char *ssid, const char *password);
esp_err_t gateway_wifi_connect_with_ip(const char *ssid, const char *password, bool use_static_ip, const char *ip, const char *gateway, const char *netmask);
void gateway_wifi_get_settings(char *ssid, size_t ssid_capacity, char *password, size_t password_capacity, bool *use_static_ip, char *ip, size_t ip_capacity, char *gateway, size_t gateway_capacity, char *netmask, size_t netmask_capacity);
gateway_wifi_state_t gateway_wifi_state(void);
void gateway_wifi_ip(char *out, size_t capacity);
uint8_t gateway_wifi_disconnect_reason(void);

esp_err_t gateway_wireguard_save(const gateway_wireguard_config_t *config);
bool gateway_wireguard_load(gateway_wireguard_config_t *config);
esp_err_t gateway_wireguard_set_enabled(bool enabled);
bool gateway_wireguard_is_enabled(void);
void gateway_wireguard_start(void);
const char *gateway_wireguard_status(void);
void gateway_wireguard_tunnel_ip(char *out, size_t capacity);

esp_err_t gateway_forward_add(uint16_t listen_port, const char *target_ip, uint16_t target_port);
esp_err_t gateway_forward_update(uint8_t index, uint16_t listen_port, const char *target_ip, uint16_t target_port);
esp_err_t gateway_forward_delete(uint8_t index);
size_t gateway_forward_list(gateway_forward_rule_t *out, size_t capacity);
void gateway_forward_start(const char *tunnel_ip);
void gateway_forward_stop(void);
void gateway_event_log_add(uint8_t type, const char *source_ip, uint16_t listen_port,
                           const char *target_ip, uint16_t target_port, uint32_t bytes,
                           uint32_t duration_ms, int error);
size_t gateway_event_log_list(gateway_event_t *out, size_t capacity);

void setup_web_start(void);
void captive_dns_start(void);
void reset_button_start(void);
esp_err_t gateway_auth_store(const char *password);
bool gateway_auth_verify(const char *password);
bool gateway_auth_configured(void);
