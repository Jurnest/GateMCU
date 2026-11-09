#include <stdio.h>
#include <string.h>
#include <time.h>
#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "esp_wireguard.h"

#define WG_NS "wireguard"

static const char *TAG = "wireguard-manager";
static char status_text[20] = "not_configured";
static gateway_wireguard_config_t saved;
static wireguard_ctx_t context;
static volatile bool desired_enabled;
static volatile bool task_running;
static bool configuration_loaded;

static bool read_string(nvs_handle handle, const char *key, char *out, size_t cap)
{
    size_t len = cap;
    return nvs_get_str(handle, key, out, &len) == ESP_OK;
}

esp_err_t gateway_wireguard_save(const gateway_wireguard_config_t *config)
{
    if (!config || !config->private_key[0] || !config->server_public_key[0] || !config->endpoint[0] || !config->tunnel_ip[0] || !config->tunnel_mask[0] || !config->endpoint_port)
        return ESP_ERR_INVALID_ARG;
    if (task_running) {
        desired_enabled = false;
        strlcpy(status_text, "disconnecting", sizeof(status_text));
        gateway_forward_stop();
        for (unsigned i = 0; i < 30 && task_running; i++) vTaskDelay(pdMS_TO_TICKS(100));
        if (task_running) return ESP_ERR_TIMEOUT;
    }
    nvs_handle handle;
    esp_err_t err = nvs_open(WG_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "private", config->private_key);
    if (err == ESP_OK) err = nvs_set_str(handle, "public", config->server_public_key);
    if (err == ESP_OK) err = nvs_set_str(handle, "psk", config->preshared_key);
    if (err == ESP_OK) err = nvs_set_str(handle, "endpoint", config->endpoint);
    if (err == ESP_OK) err = nvs_set_str(handle, "tunnel_ip", config->tunnel_ip);
    if (err == ESP_OK) err = nvs_set_str(handle, "mask", config->tunnel_mask);
    if (err == ESP_OK) err = nvs_set_str(handle, "dns", config->dns);
    if (err == ESP_OK) err = nvs_set_str(handle, "allowed", config->allowed_ips);
    if (err == ESP_OK) err = nvs_set_u16(handle, "port", config->endpoint_port);
    if (err == ESP_OK) err = nvs_set_u16(handle, "keepalive", config->persistent_keepalive);
    if (err == ESP_OK) err = nvs_set_u8(handle, "enabled", 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        saved = *config;
        desired_enabled = true;
        configuration_loaded = true;
        strlcpy(status_text, "connecting", sizeof(status_text));
    }
    return err;
}

static void wireguard_task(void *arg)
{
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now); localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2020 - 1900)) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        for (int i = 0; i < 20 && sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET; i++) vTaskDelay(pdMS_TO_TICKS(1500));
    }

    wireguard_config_t config = ESP_WIREGUARD_CONFIG_DEFAULT();
    config.private_key = saved.private_key;
    config.public_key = saved.server_public_key;
    config.preshared_key = saved.preshared_key[0] ? saved.preshared_key : NULL;
    config.endpoint = saved.endpoint;
    config.port = saved.endpoint_port;
    config.allowed_ip = saved.tunnel_ip;
    config.allowed_ip_mask = saved.tunnel_mask;
    config.persistent_keepalive = saved.persistent_keepalive;
    esp_err_t err = esp_wireguard_init(&config, &context);
    if (err == ESP_OK) err = esp_wireguard_connect(&context);
    if (err != ESP_OK) {
        strlcpy(status_text, "failed", sizeof(status_text));
        ESP_LOGE(TAG, "Unable to start WireGuard: %s", esp_err_to_name(err));
        task_running = false;
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        if (!desired_enabled) {
            gateway_forward_stop();
            if (context.netif) esp_wireguard_disconnect(&context);
            strlcpy(status_text, "inactive", sizeof(status_text));
            task_running = false;
            vTaskDelete(NULL);
            return;
        }
        if (esp_wireguardif_peer_is_up(&context) == ESP_OK) {
            strlcpy(status_text, "connected", sizeof(status_text));
            gateway_forward_start(saved.tunnel_ip);
            ESP_LOGI(TAG, "WireGuard tunnel is up");
        } else if (strcmp(status_text, "connected") == 0) {
            strlcpy(status_text, "connecting", sizeof(status_text));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

bool gateway_wireguard_load(gateway_wireguard_config_t *config)
{
    if (!config) return false;
    nvs_handle handle;
    if (nvs_open(WG_NS, NVS_READONLY, &handle) != ESP_OK) return false;
    memset(config, 0, sizeof(*config));
    bool ok = read_string(handle, "private", config->private_key, sizeof(config->private_key)) &&
              read_string(handle, "public", config->server_public_key, sizeof(config->server_public_key)) &&
              (read_string(handle, "psk", config->preshared_key, sizeof(config->preshared_key)) || (config->preshared_key[0] = '\0', true)) &&
              read_string(handle, "endpoint", config->endpoint, sizeof(config->endpoint)) &&
              read_string(handle, "tunnel_ip", config->tunnel_ip, sizeof(config->tunnel_ip)) &&
              read_string(handle, "mask", config->tunnel_mask, sizeof(config->tunnel_mask)) &&
              nvs_get_u16(handle, "port", &config->endpoint_port) == ESP_OK;
    read_string(handle, "dns", config->dns, sizeof(config->dns));
    read_string(handle, "allowed", config->allowed_ips, sizeof(config->allowed_ips));
    nvs_get_u16(handle, "keepalive", &config->persistent_keepalive);
    uint8_t enabled = 1;
    if (nvs_get_u8(handle, "enabled", &enabled) != ESP_OK) enabled = 1;
    nvs_close(handle);
    if (!ok) return false;
    saved = *config;
    configuration_loaded = true;
    desired_enabled = enabled != 0;
    return true;
}

bool gateway_wireguard_is_enabled(void)
{
    return desired_enabled;
}

esp_err_t gateway_wireguard_set_enabled(bool enabled)
{
    gateway_wireguard_config_t config;
    if (!configuration_loaded && !gateway_wireguard_load(&config)) return ESP_ERR_INVALID_STATE;
    nvs_handle handle;
    esp_err_t err = nvs_open(WG_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "enabled", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    desired_enabled = enabled;
    if (enabled) {
        strlcpy(status_text, "connecting", sizeof(status_text));
        gateway_wireguard_start();
    } else {
        gateway_forward_stop();
        strlcpy(status_text, task_running ? "disconnecting" : "inactive", sizeof(status_text));
        if (!task_running) gateway_forward_stop();
    }
    return ESP_OK;
}

void gateway_wireguard_start(void)
{
    if (gateway_wifi_state() != GATEWAY_WIFI_CONNECTED || task_running) return;
    gateway_wireguard_config_t config;
    if (!gateway_wireguard_load(&config)) { strlcpy(status_text, "not_configured", sizeof(status_text)); return; }
    if (!desired_enabled) { strlcpy(status_text, "inactive", sizeof(status_text)); return; }
    task_running = true;
    strlcpy(status_text, "connecting", sizeof(status_text));
    if (xTaskCreate(wireguard_task, "wireguard", 4096, NULL, 4, NULL) != pdPASS) {
        task_running = false;
        strlcpy(status_text, "failed", sizeof(status_text));
    }
}

const char *gateway_wireguard_status(void)
{
    return status_text;
}

void gateway_wireguard_tunnel_ip(char *out, size_t capacity)
{
    if (capacity) strlcpy(out, saved.tunnel_ip, capacity);
}
