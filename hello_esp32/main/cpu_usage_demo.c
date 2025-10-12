#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "CPU_USAGE";

uint64_t time_high = 0, time_low = 0;

void high_task(void *pvParameters)
{
    uint64_t start, end;
    while (1) {
        start = esp_timer_get_time();
        for (int i = 0; i < 200000; i++) {
            volatile int dummy = i * 2;
        }
        end = esp_timer_get_time();
        time_high += (end - start);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void low_task(void *pvParameters)
{
    uint64_t start, end;
    while (1) {
        start = esp_timer_get_time();
        for (int i = 0; i < 1000000; i++) {
            volatile int dummy = i + 1;
        }
        end = esp_timer_get_time();
        time_low += (end - start);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void monitor_task(void *pvParameters)
{
    while (1) {
        uint64_t total = time_high + time_low;
        if (total > 0) {
            float high_percent = (float)time_high / total * 100;
            float low_percent = (float)time_low / total * 100;
            ESP_LOGI(TAG, "CPU Usage -> High: %.1f%% | Low: %.1f%%", high_percent, low_percent);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== CPU Usage Monitor Demo ===");
    xTaskCreate(high_task, "HighTask", 3072, NULL, 5, NULL);
    xTaskCreate(low_task, "LowTask", 3072, NULL, 3, NULL);
    xTaskCreate(monitor_task, "Monitor", 4096, NULL, 2, NULL);
}
