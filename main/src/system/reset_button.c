#include "gateway.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

#define RESET_BUTTON GPIO_NUM_0
#define RESET_LED GPIO_NUM_2
#define RESET_MIN_HOLD_MS 5000
#define RESET_MAX_HOLD_MS 7000
#define BUTTON_SAMPLE_MS 20
#define WIFI_NAMESPACE "gateway"

static void erase_settings_and_restart(void)
{
    ESP_LOGW("reset-button", "Clearing settings after a 5-7 second press and release");
    nvs_handle handle;
    if (nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    if (nvs_open("wireguard", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    for (int i = 0; i < 3; i++) {
        gpio_set_level(RESET_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(180));
        gpio_set_level(RESET_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(180));
    }
    esp_restart();
}

static void reset_task(void *arg)
{
    gpio_config_t button = {.pin_bit_mask = 1ULL << RESET_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    gpio_config_t led = {.pin_bit_mask = 1ULL << RESET_LED, .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_ERROR_CHECK(gpio_config(&led));
    gpio_set_level(RESET_LED, 1); /* onboard LED is active low */

    unsigned held_ms = 0;
    for (;;) {
        if (gpio_get_level(RESET_BUTTON) == 0) {
            held_ms += BUTTON_SAMPLE_MS;
        } else {
            if (held_ms >= RESET_MIN_HOLD_MS && held_ms <= RESET_MAX_HOLD_MS) erase_settings_and_restart();
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_SAMPLE_MS));
    }
}

void reset_button_start(void)
{
    xTaskCreate(reset_task, "reset_button", 2048, NULL, 4, NULL);
}
