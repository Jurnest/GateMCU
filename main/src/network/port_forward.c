#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "nvs.h"

#define FORWARD_NS "gateway"
#define MAX_RULES 3

static gateway_forward_rule_t rules[MAX_RULES];
static bool listener_started[MAX_RULES];
static int listener_fd[MAX_RULES] = {-1, -1, -1};
static int client_fd[MAX_RULES] = {-1, -1, -1};
static int target_fd[MAX_RULES] = {-1, -1, -1};
static volatile bool forwarding_enabled;
static char active_tunnel_ip[16];

static void send_all(int fd, const uint8_t *buf, size_t length)
{
    while (length) {
        int sent = send(fd, buf, length, 0);
        if (sent <= 0) return;
        buf += sent;
        length -= sent;
    }
}

static uint32_t relay(int left, int right)
{
    uint8_t buf[TCP_MSS];
    uint32_t total = 0;
    bool prefer_left = true;
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds); FD_SET(left, &readfds); FD_SET(right, &readfds);
        int maxfd = left > right ? left : right;
        struct timeval timeout = {.tv_sec = 120, .tv_usec = 0};
        int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready <= 0) break;
        bool left_ready = FD_ISSET(left, &readfds);
        bool right_ready = FD_ISSET(right, &readfds);
        int source;
        if (left_ready && right_ready) {
            source = prefer_left ? left : right;
            prefer_left = !prefer_left;
        } else {
            source = left_ready ? left : right;
        }
        int dest = source == left ? right : left;
        int received = recv(source, buf, sizeof(buf), 0);
        if (received <= 0) break;
        total += (uint32_t)received;
        send_all(dest, buf, received);
    }
    return total;
}

static void rule_task(void *arg)
{
    unsigned index = (unsigned)(uintptr_t)arg;
    gateway_forward_rule_t rule = rules[index];
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server < 0) { listener_started[index] = false; vTaskDelete(NULL); return; }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(rule.listen_port);
    local.sin_addr.s_addr = inet_addr(active_tunnel_ip);
    if (bind(server, (struct sockaddr *)&local, sizeof(local)) < 0 || listen(server, 1) < 0) {
        close(server); listener_started[index] = false; vTaskDelete(NULL); return;
    }
    listener_fd[index] = server;
    while (forwarding_enabled) {
        int incoming = accept(server, NULL, NULL);
        if (incoming < 0) { if (!forwarding_enabled) break; continue; }
        client_fd[index] = incoming;
        struct sockaddr_in peer = {0};
        socklen_t peer_len = sizeof(peer);
        char source_ip[16] = "";
        if (getpeername(incoming, (struct sockaddr *)&peer, &peer_len) == 0)
            strlcpy(source_ip, inet_ntoa(peer.sin_addr), sizeof(source_ip));
        gateway_event_log_add(1, source_ip, rule.listen_port, rule.target_ip, rule.target_port, 0, 0, 0);
        int outgoing = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        target_fd[index] = outgoing;
        struct sockaddr_in target = {0};
        target.sin_family = AF_INET;
        target.sin_port = htons(rule.target_port);
        target.sin_addr.s_addr = inet_addr(rule.target_ip);
        uint32_t bytes = 0;
        int connect_error = 0;
        bool connected = false;
        if (outgoing < 0) connect_error = errno;
        else if (connect(outgoing, (struct sockaddr *)&target, sizeof(target)) == 0) connected = true;
        else connect_error = errno;
        gateway_event_log_add(connected ? 2 : 3, source_ip, rule.listen_port, rule.target_ip,
                              rule.target_port, 0, 0, connect_error);
        TickType_t transfer_start = xTaskGetTickCount();
        if (connected) bytes = relay(incoming, outgoing);
        if (connected) gateway_event_log_add(4, source_ip, rule.listen_port, rule.target_ip,
                                             rule.target_port, bytes,
                                             (uint32_t)(xTaskGetTickCount() - transfer_start) * portTICK_PERIOD_MS, 0);
        if (outgoing >= 0) close(outgoing);
        target_fd[index] = -1;
        close(incoming);
        client_fd[index] = -1;
    }
    listener_fd[index] = -1;
    close(server);
    listener_started[index] = false;
    vTaskDelete(NULL);
}

static void start_listeners(void)
{
    if (!active_tunnel_ip[0] || strcmp(gateway_wireguard_status(), "connected") != 0) return;
    forwarding_enabled = true;
    for (unsigned i = 0; i < MAX_RULES; i++) {
        rules[i].index = i;
        if (rules[i].enabled && !listener_started[i]) {
            listener_started[i] = true;
            if (xTaskCreate(rule_task, "tcp_forward", 3072, (void *)(uintptr_t)i, 3, NULL) != pdPASS) listener_started[i] = false;
        }
    }
}

static esp_err_t stop_listeners_for_change(void)
{
    gateway_forward_stop();
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        bool running = false;
        for (unsigned i = 0; i < MAX_RULES; i++) running |= listener_started[i];
        if (!running) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_TIMEOUT;
}

static bool port_is_used(uint8_t except_index, uint16_t port)
{
    for (unsigned i = 0; i < MAX_RULES; i++)
        if (i != except_index && rules[i].enabled && rules[i].listen_port == port) return true;
    return false;
}

static esp_err_t save_rule(uint8_t index, uint16_t listen_port, const char *target_ip, uint16_t target_port)
{
    char in_key[12], out_key[12], ip_key[12];
    snprintf(in_key, sizeof(in_key), "in%u", index);
    snprintf(out_key, sizeof(out_key), "out%u", index);
    snprintf(ip_key, sizeof(ip_key), "ip%u", index);
    nvs_handle handle;
    esp_err_t err = nvs_open(FORWARD_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, in_key, listen_port);
    if (err == ESP_OK) err = nvs_set_u16(handle, out_key, target_port);
    if (err == ESP_OK) err = nvs_set_str(handle, ip_key, target_ip);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t gateway_forward_add(uint16_t listen_port, const char *target_ip, uint16_t target_port)
{
    if (!listen_port || !target_port || !target_ip || !ip4addr_aton(target_ip, &(ip4_addr_t){0})) return ESP_ERR_INVALID_ARG;
    nvs_handle handle;
    esp_err_t err = nvs_open(FORWARD_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    unsigned index;
    for (index = 0; index < MAX_RULES; index++) {
        char key[12]; uint16_t existing = 0;
        snprintf(key, sizeof(key), "in%u", index);
        if (nvs_get_u16(handle, key, &existing) != ESP_OK) break;
    }
    if (index == MAX_RULES) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    nvs_close(handle);
    gateway_forward_list(NULL, 0);
    if (port_is_used(MAX_RULES, listen_port)) return ESP_ERR_INVALID_STATE;
    esp_err_t stop_err = stop_listeners_for_change();
    if (stop_err != ESP_OK) return stop_err;
    char in_key[12], out_key[12], ip_key[12];
    snprintf(in_key, sizeof(in_key), "in%u", index);
    snprintf(out_key, sizeof(out_key), "out%u", index);
    snprintf(ip_key, sizeof(ip_key), "ip%u", index);
    err = nvs_open(FORWARD_NS, NVS_READWRITE, &handle);
    bool opened = err == ESP_OK;
    if (opened) err = nvs_set_u16(handle, in_key, listen_port);
    if (err == ESP_OK) err = nvs_set_u16(handle, out_key, target_port);
    if (err == ESP_OK) err = nvs_set_str(handle, ip_key, target_ip);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    if (err == ESP_OK) {
        rules[index].index = index;
        rules[index].listen_port = listen_port;
        rules[index].target_port = target_port;
        strlcpy(rules[index].target_ip, target_ip, sizeof(rules[index].target_ip));
        rules[index].enabled = true;
    }
    gateway_forward_list(NULL, 0);
    start_listeners();
    return err;
}

esp_err_t gateway_forward_update(uint8_t index, uint16_t listen_port, const char *target_ip, uint16_t target_port)
{
    ip4_addr_t parsed;
    if (index >= MAX_RULES || !listen_port || !target_port || !target_ip || !ip4addr_aton(target_ip, &parsed)) return ESP_ERR_INVALID_ARG;
    gateway_forward_list(NULL, 0);
    if (!rules[index].enabled) return ESP_ERR_NOT_FOUND;
    if (port_is_used(index, listen_port)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = stop_listeners_for_change();
    if (err != ESP_OK) return err;
    err = save_rule(index, listen_port, target_ip, target_port);
    gateway_forward_list(NULL, 0);
    start_listeners();
    return err;
}

esp_err_t gateway_forward_delete(uint8_t index)
{
    if (index >= MAX_RULES) return ESP_ERR_INVALID_ARG;
    gateway_forward_list(NULL, 0);
    if (!rules[index].enabled) return ESP_ERR_NOT_FOUND;
    esp_err_t err = stop_listeners_for_change();
    if (err != ESP_OK) return err;
    char in_key[12], out_key[12], ip_key[12];
    snprintf(in_key, sizeof(in_key), "in%u", index);
    snprintf(out_key, sizeof(out_key), "out%u", index);
    snprintf(ip_key, sizeof(ip_key), "ip%u", index);
    nvs_handle handle;
    err = nvs_open(FORWARD_NS, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_key(handle, in_key);
        nvs_erase_key(handle, out_key);
        nvs_erase_key(handle, ip_key);
        err = nvs_commit(handle);
        nvs_close(handle);
    }
    gateway_forward_list(NULL, 0);
    start_listeners();
    return err;
}

size_t gateway_forward_list(gateway_forward_rule_t *out, size_t capacity)
{
    nvs_handle handle;
    if (nvs_open(FORWARD_NS, NVS_READONLY, &handle) == ESP_OK) {
        for (unsigned i = 0; i < MAX_RULES; i++) {
            rules[i].index = i;
            char in_key[12], out_key[12], ip_key[12];
            snprintf(in_key, sizeof(in_key), "in%u", i);
            snprintf(out_key, sizeof(out_key), "out%u", i);
            snprintf(ip_key, sizeof(ip_key), "ip%u", i);
            size_t len = sizeof(rules[i].target_ip);
            rules[i].enabled = nvs_get_u16(handle, in_key, &rules[i].listen_port) == ESP_OK &&
                               nvs_get_u16(handle, out_key, &rules[i].target_port) == ESP_OK &&
                               nvs_get_str(handle, ip_key, rules[i].target_ip, &len) == ESP_OK;
        }
        nvs_close(handle);
    }
    size_t count = 0;
    for (unsigned i = 0; i < MAX_RULES; i++) if (rules[i].enabled) {
        if (out && count < capacity) out[count] = rules[i];
        count++;
    }
    return count;
}

void gateway_forward_start(const char *tunnel_ip)
{
    strlcpy(active_tunnel_ip, tunnel_ip, sizeof(active_tunnel_ip));
    gateway_forward_list(NULL, 0);
    start_listeners();
}

void gateway_forward_stop(void)
{
    forwarding_enabled = false;
    for (unsigned i = 0; i < MAX_RULES; i++) {
        if (listener_fd[i] >= 0) shutdown(listener_fd[i], SHUT_RDWR);
        if (client_fd[i] >= 0) shutdown(client_fd[i], SHUT_RDWR);
        if (target_fd[i] >= 0) shutdown(target_fd[i], SHUT_RDWR);
    }
}
