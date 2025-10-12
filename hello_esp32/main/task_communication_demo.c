#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "TASK_COMM";

// ตัวแปรแชร์ระหว่าง Tasks
volatile int shared_counter = 0;

// ===================== Producer Task =====================
void producer_task(void *pvParameters)
{
    while (1) {
        shared_counter++;
        ESP_LOGI(TAG, "Producer: counter = %d", shared_counter);
        vTaskDelay(pdMS_TO_TICKS(1000)); // delay 1 วินาที
    }
}

// ===================== Consumer Task =====================
void consumer_task(void *pvParameters)
{
    int last_value = 0;

    while (1) {
        if (shared_counter != last_value) {
            ESP_LOGI(TAG, "Consumer: received %d", shared_counter);
            last_value = shared_counter;
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // delay 0.5 วินาที
    }
}

// ===================== Main Function =====================
void app_main(void)
{
    ESP_LOGI(TAG, "=== FreeRTOS Task Communication Demo ===");

    // สร้าง Producer Task
    xTaskCreate(
        producer_task,        // Function
        "Producer_Task",      // Name
        2048,                 // Stack size
        NULL,                 // Parameter
        2,                    // Priority
        NULL                  // Handle
    );

    // สร้าง Consumer Task
    xTaskCreate(
        consumer_task,
        "Consumer_Task",
        2048,
        NULL,
        1,
        NULL
    );
}
