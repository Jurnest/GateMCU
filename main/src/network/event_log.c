#include <string.h>
#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EVENT_CAPACITY 24
#define EVENT_MAX_AGE_SECONDS (30 * 60)

static gateway_event_t events[EVENT_CAPACITY];
static size_t next_event;
static size_t event_count;

void gateway_event_log_add(uint8_t type, const char *source_ip, uint16_t listen_port,
                           const char *target_ip, uint16_t target_port, uint32_t bytes,
                           uint32_t duration_ms, int error)
{
    portENTER_CRITICAL();
    gateway_event_t *event = &events[next_event];
    memset(event, 0, sizeof(*event));
    event->tick = xTaskGetTickCount();
    event->type = type;
    event->listen_port = listen_port;
    event->target_port = target_port;
    event->bytes = bytes;
    event->duration_ms = duration_ms;
    event->error = (int16_t)error;
    if (source_ip) strlcpy(event->source_ip, source_ip, sizeof(event->source_ip));
    if (target_ip) strlcpy(event->target_ip, target_ip, sizeof(event->target_ip));
    next_event = (next_event + 1) % EVENT_CAPACITY;
    if (event_count < EVENT_CAPACITY) event_count++;
    portEXIT_CRITICAL();
}

size_t gateway_event_log_list(gateway_event_t *out, size_t capacity)
{
    size_t count;
    TickType_t now = xTaskGetTickCount();
    const TickType_t max_age = pdMS_TO_TICKS(EVENT_MAX_AGE_SECONDS * 1000U);
    portENTER_CRITICAL();
    count = event_count;
    size_t written = 0;
    for (size_t n = 0; n < count; n++) {
        size_t index = (next_event + EVENT_CAPACITY - 1 - n) % EVENT_CAPACITY;
        if ((TickType_t)(now - events[index].tick) > max_age) continue;
        if (out && written < capacity) out[written] = events[index];
        written++;
    }
    portEXIT_CRITICAL();
    return written;
}
