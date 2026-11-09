#include <stdio.h>
#include <string.h>
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

static void relay(int left, int right)
{
    uint8_t buf[512];
    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds); FD_SET(left, &readfds); FD_SET(right, &readfds);
        int maxfd = left > right ? left : right;
        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) <= 0) break;
        int source = FD_ISSET(left, &readfds) ? left : right;
        int dest = source == left ? right : left;
        int received = recv(source, buf, sizeof(buf), 0);
        if (received <= 0) break;
        send_all(dest, buf, received);
    }
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
        int outgoing = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in target = {0};
        target.sin_family = AF_INET;
        target.sin_port = htons(rule.target_port);
        target.sin_addr.s_addr = inet_addr(rule.target_ip);
        if (outgoing >= 0 && connect(outgoing, (struct sockaddr *)&target, sizeof(target)) == 0) relay(incoming, outgoing);
        if (outgoing >= 0) close(outgoing);
        close(incoming);
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
        if (rules[i].enabled && !listener_started[i]) {
            listener_started[i] = true;
            if (xTaskCreate(rule_task, "tcp_forward", 3072, (void *)(uintptr_t)i, 3, NULL) != pdPASS) listener_started[i] = false;
        }
    }
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
    char in_key[12], out_key[12], ip_key[12];
    snprintf(in_key, sizeof(in_key), "in%u", index);
    snprintf(out_key, sizeof(out_key), "out%u", index);
    snprintf(ip_key, sizeof(ip_key), "ip%u", index);
    err = nvs_set_u16(handle, in_key, listen_port);
    if (err == ESP_OK) err = nvs_set_u16(handle, out_key, target_port);
    if (err == ESP_OK) err = nvs_set_str(handle, ip_key, target_ip);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        rules[index].listen_port = listen_port;
        rules[index].target_port = target_port;
        strlcpy(rules[index].target_ip, target_ip, sizeof(rules[index].target_ip));
        rules[index].enabled = true;
        start_listeners();
    }
    return err;
}

size_t gateway_forward_list(gateway_forward_rule_t *out, size_t capacity)
{
    nvs_handle handle;
    if (nvs_open(FORWARD_NS, NVS_READONLY, &handle) == ESP_OK) {
        for (unsigned i = 0; i < MAX_RULES; i++) {
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
    for (unsigned i = 0; i < MAX_RULES; i++)
        if (listener_fd[i] >= 0) shutdown(listener_fd[i], SHUT_RDWR);
}
