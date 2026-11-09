#include <stdio.h>
#include <string.h>
#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "tcpip_adapter.h"
#include "lwip/inet.h"
#include "mdns.h"

#define WIFI_NAMESPACE "gateway"

static const char *TAG = "wifi-manager";
static gateway_wifi_state_t state = GATEWAY_WIFI_SETUP;
static char current_ip[16] = "";
static unsigned retries;
static uint8_t last_disconnect_reason;
static bool ap_shutdown_scheduled;
static uint32_t ap_generation;
static bool pending_credentials;
static bool ignore_next_disconnect;
static char pending_ssid[33];
static char pending_password[64];
static bool pending_use_static_ip;
static char pending_ip[16], pending_gateway[16], pending_netmask[16];

static esp_err_t save_credentials(const char *ssid, const char *password, bool use_static_ip, const char *ip, const char *gateway, const char *netmask)
{
    nvs_handle handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_pass", password);
    if (err == ESP_OK) err = nvs_set_u8(handle, "wifi_static", use_static_ip ? 1 : 0);
    if (err == ESP_OK && use_static_ip) err = nvs_set_str(handle, "wifi_ip", ip);
    if (err == ESP_OK && use_static_ip) err = nvs_set_str(handle, "wifi_gw", gateway);
    if (err == ESP_OK && use_static_ip) err = nvs_set_str(handle, "wifi_mask", netmask);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static bool read_setting(const char *key, char *out, size_t capacity)
{
    nvs_handle handle;
    size_t length = capacity;
    if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_get_str(handle, key, out, &length);
    nvs_close(handle);
    return err == ESP_OK && out[0] != '\0';
}

static void close_setup_ap_task(void *arg)
{
    uint32_t generation = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(30000));
    if (generation == ap_generation && state == GATEWAY_WIFI_CONNECTED && ap_shutdown_scheduled) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) ESP_LOGW(TAG, "Could not close setup AP: %s", esp_err_to_name(err));
        ap_shutdown_scheduled = false;
    }
    vTaskDelete(NULL);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        last_disconnect_reason = event ? event->reason : 0;
        if (ignore_next_disconnect) {
            ignore_next_disconnect = false;
        } else if (state == GATEWAY_WIFI_CONNECTED) {
            ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u); reconnecting", last_disconnect_reason);
            state = GATEWAY_WIFI_CONNECTING;
            current_ip[0] = '\0';
            retries = 0;
            ap_shutdown_scheduled = false;
            ap_generation++;
            esp_wifi_set_mode(WIFI_MODE_APSTA);
            esp_wifi_connect();
        } else if (state == GATEWAY_WIFI_CONNECTING && retries++ < 10) {
            esp_wifi_connect();
        } else if (state == GATEWAY_WIFI_CONNECTING) {
            state = GATEWAY_WIFI_FAILED;
            ESP_LOGW(TAG, "Wi-Fi connection failed (reason %u); setup AP remains available", last_disconnect_reason);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        retries = 0;
        last_disconnect_reason = 0;
        if (pending_credentials) {
            esp_err_t save_err = save_credentials(pending_ssid, pending_password, pending_use_static_ip, pending_ip, pending_gateway, pending_netmask);
            if (save_err != ESP_OK) ESP_LOGE(TAG, "Could not save Wi-Fi settings: %s", esp_err_to_name(save_err));
            pending_credentials = false;
        }
        state = GATEWAY_WIFI_CONNECTED;
        // Re-announce once the STA interface is fully up. On ESP8266 RTOS SDK,
        // mDNS may be initialized before Wi-Fi but has no active STA PCB yet.
        esp_err_t mdns_err = mdns_hostname_set("gatemcu");
        if (mdns_err != ESP_OK) ESP_LOGW(TAG, "Could not announce mDNS hostname: %s", esp_err_to_name(mdns_err));
        gateway_wireguard_start();
        ESP_LOGI(TAG, "Connected; address is %s", current_ip);
        if (!ap_shutdown_scheduled) {
            ap_shutdown_scheduled = true;
            uint32_t generation = ++ap_generation;
            if (xTaskCreate(close_setup_ap_task, "close_setup_ap", 1024, (void *)(uintptr_t)generation, 3, NULL) != pdPASS) {
                ap_shutdown_scheduled = false;
                ESP_LOGW(TAG, "Could not schedule setup AP shutdown");
            }
        }
    }
}

esp_err_t gateway_wifi_connect(const char *ssid, const char *password)
{
    return gateway_wifi_connect_with_ip(ssid, password, false, "", "", "");
}

esp_err_t gateway_wifi_connect_with_ip(const char *ssid, const char *password, bool use_static_ip, const char *ip, const char *gateway, const char *netmask)
{
    if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 63) return ESP_ERR_INVALID_ARG;
    bool disconnect_current = state == GATEWAY_WIFI_CONNECTED;
    if (use_static_ip) {
        ip4_addr_t parsed;
        if (!ip || !gateway || !netmask || !ip4addr_aton(ip, &parsed) || !ip4addr_aton(gateway, &parsed) || !ip4addr_aton(netmask, &parsed)) return ESP_ERR_INVALID_ARG;
    }
    strlcpy(pending_ssid, ssid, sizeof(pending_ssid));
    strlcpy(pending_password, password, sizeof(pending_password));
    pending_use_static_ip = use_static_ip;
    strlcpy(pending_ip, ip ? ip : "", sizeof(pending_ip));
    strlcpy(pending_gateway, gateway ? gateway : "", sizeof(pending_gateway));
    strlcpy(pending_netmask, netmask ? netmask : "", sizeof(pending_netmask));
    pending_credentials = true;

    wifi_config_t station = {0};
    strlcpy((char *)station.sta.ssid, ssid, sizeof(station.sta.ssid));
    strlcpy((char *)station.sta.password, password, sizeof(station.sta.password));
    station.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    retries = 0;
    last_disconnect_reason = 0;
    state = GATEWAY_WIFI_CONNECTING;
    current_ip[0] = '\0';
    ap_shutdown_scheduled = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        pending_credentials = false;
        state = GATEWAY_WIFI_FAILED;
        return err;
    }
    if (disconnect_current) {
        ignore_next_disconnect = true;
        if (esp_wifi_disconnect() != ESP_OK) ignore_next_disconnect = false;
    }
    err = esp_wifi_set_config(ESP_IF_WIFI_STA, &station);
    if (err != ESP_OK) {
        pending_credentials = false;
        state = GATEWAY_WIFI_FAILED;
        return err;
    }
    if (use_static_ip) {
        tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
        tcpip_adapter_ip_info_t info = {0};
        ip4addr_aton(ip, &info.ip);
        ip4addr_aton(gateway, &info.gw);
        ip4addr_aton(netmask, &info.netmask);
        esp_err_t ip_err = tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &info);
        if (ip_err != ESP_OK) {
            pending_credentials = false;
            state = GATEWAY_WIFI_FAILED;
            return ip_err;
        }
        // Static IP disables DHCP, so the adapter will not learn a DNS
        // resolver automatically. Use the configured LAN gateway as DNS.
        tcpip_adapter_dns_info_t dns = {0};
        dns.ip.type = IPADDR_TYPE_V4;
        if (!ip4addr_aton(gateway, ip_2_ip4(&dns.ip))) {
            pending_credentials = false;
            state = GATEWAY_WIFI_FAILED;
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t dns_err = tcpip_adapter_set_dns_info(TCPIP_ADAPTER_IF_STA, TCPIP_ADAPTER_DNS_MAIN, &dns);
        if (dns_err != ESP_OK) ESP_LOGW(TAG, "Could not set static DNS to gateway: %s", esp_err_to_name(dns_err));
    } else {
        esp_err_t dhcp_err = tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
        if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STARTED) {
            pending_credentials = false;
            state = GATEWAY_WIFI_FAILED;
            return dhcp_err;
        }
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        pending_credentials = false;
        state = GATEWAY_WIFI_FAILED;
    }
    return err;
}

void gateway_wifi_get_settings(char *ssid, size_t ssid_capacity, char *password, size_t password_capacity, bool *use_static_ip, char *ip, size_t ip_capacity, char *gateway, size_t gateway_capacity, char *netmask, size_t netmask_capacity)
{
    nvs_handle handle;
    if (ssid_capacity) ssid[0] = '\0';
    if (password_capacity) password[0] = '\0';
    if (ip_capacity) ip[0] = '\0';
    if (gateway_capacity) gateway[0] = '\0';
    if (netmask_capacity) netmask[0] = '\0';
    if (use_static_ip) *use_static_ip = false;
    if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    if (ssid_capacity) nvs_get_str(handle, "wifi_ssid", ssid, &ssid_capacity);
    if (password_capacity) nvs_get_str(handle, "wifi_pass", password, &password_capacity);
    uint8_t static_value = 0;
    if (nvs_get_u8(handle, "wifi_static", &static_value) == ESP_OK && use_static_ip) *use_static_ip = static_value != 0;
    if (ip_capacity) nvs_get_str(handle, "wifi_ip", ip, &ip_capacity);
    if (gateway_capacity) nvs_get_str(handle, "wifi_gw", gateway, &gateway_capacity);
    if (netmask_capacity) nvs_get_str(handle, "wifi_mask", netmask, &netmask_capacity);
    nvs_close(handle);
}

gateway_wifi_state_t gateway_wifi_state(void)
{
    return state;
}

void gateway_wifi_ip(char *out, size_t capacity)
{
    if (capacity) strlcpy(out, current_ip, capacity);
}

uint8_t gateway_wifi_disconnect_reason(void)
{
    return last_disconnect_reason;
}

void gateway_wifi_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    // Start mDNS before Wi-Fi obtains an address so its IP event handler is
    // already registered when the station joins the LAN.
    err = mdns_init();
    if (err == ESP_OK) {
        err = mdns_hostname_set("gatemcu");
        if (err == ESP_OK) err = mdns_instance_name_set("GateMCU Gateway");
        if (err == ESP_OK) err = mdns_service_add("GateMCU", "_http", "_tcp", 80, NULL, 0);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "mDNS startup failed: %s", esp_err_to_name(err));

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, CONFIG_GATEWAY_SETUP_AP_NAME, sizeof(ap.ap.ssid));
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    strlcpy((char *)ap.ap.password, "GateMCU-Setup", sizeof(ap.ap.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    char ssid[33] = {0}, password[65] = {0}, ip[16] = {0}, gateway[16] = {0}, netmask[16] = {0};
    bool use_static_ip = false;
    if (read_setting("wifi_ssid", ssid, sizeof(ssid))) {
        read_setting("wifi_pass", password, sizeof(password));
        nvs_handle handle;
        if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
            uint8_t v = 0;
            if (nvs_get_u8(handle, "wifi_static", &v) == ESP_OK) use_static_ip = v != 0;
            nvs_close(handle);
        }
        if (use_static_ip) {
            read_setting("wifi_ip", ip, sizeof(ip));
            read_setting("wifi_gw", gateway, sizeof(gateway));
            read_setting("wifi_mask", netmask, sizeof(netmask));
            /* Repair the previously saved reversed values for this LAN. */
            if (strcmp(ip, "192.168.1.1") == 0 && strcmp(gateway, "192.168.1.111") == 0 &&
                strcmp(netmask, "255.255.255.0") == 0) {
                strlcpy(ip, "192.168.1.111", sizeof(ip));
                strlcpy(gateway, "192.168.1.1", sizeof(gateway));
                esp_err_t repair_err = save_credentials(ssid, password, true, ip, gateway, netmask);
                if (repair_err == ESP_OK) ESP_LOGW(TAG, "Repaired reversed static IP and gateway settings");
                else ESP_LOGE(TAG, "Could not save repaired IP settings: %s", esp_err_to_name(repair_err));
            }
        }
        gateway_wifi_connect_with_ip(ssid, password, use_static_ip, ip, gateway, netmask);
    }
}
