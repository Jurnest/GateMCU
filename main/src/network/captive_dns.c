#include <string.h>
#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(sock); vTaskDelete(NULL); return; }

    uint8_t packet[512];
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int length = recvfrom(sock, packet, sizeof(packet), 0, (struct sockaddr *)&peer, &peer_len);
        if (length < 17) continue;
        size_t end = 12;
        while (end < (size_t)length && packet[end] != 0) {
            size_t label = packet[end++];
            if (label > 63 || end + label >= (size_t)length) break;
            end += label;
        }
        if (end + 5 >= (size_t)length || packet[end] != 0) continue;
        size_t question_end = end + 5;
        bool answer_a = packet[end + 1] == 0 && packet[end + 2] == 1 && packet[end + 3] == 0 && packet[end + 4] == 1;
        packet[2] = 0x81; packet[3] = 0x80;
        packet[6] = 0; packet[7] = answer_a ? 1 : 0;
        packet[8] = packet[9] = packet[10] = packet[11] = 0;
        if (answer_a) {
            const uint8_t record[] = {0xc0,0x0c,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x3c,0x00,0x04,192,168,4,1};
            memcpy(packet + question_end, record, sizeof(record));
            question_end += sizeof(record);
        }
        sendto(sock, packet, question_end, 0, (struct sockaddr *)&peer, peer_len);
    }
}

void captive_dns_start(void)
{
    xTaskCreate(dns_task, "captive_dns", 2048, NULL, 4, NULL);
}
