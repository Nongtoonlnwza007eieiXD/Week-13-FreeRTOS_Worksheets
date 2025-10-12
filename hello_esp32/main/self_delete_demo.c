#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SELF_DELETE";

// ===== Temporary Task =====
void temporary_task(void *pvParameters)
{
    int *duration = (int *)pvParameters;

    ESP_LOGI(TAG, "Temporary task will run for %d seconds", *duration);

    for (int i = *duration; i > 0; i--) {
        ESP_LOGI(TAG, "Temporary task countdown: %d", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "Temporary task self-deleting now");
    vTaskDelete(NULL); // Delete itself
}

// ===== Main Entry =====
void app_main(void)
{
    static int temp_duration = 10;

    ESP_LOGI(TAG, "Creating temporary self-deleting task...");
    xTaskCreate(temporary_task, "TempTask", 2048, &temp_duration, 1, NULL);
}
