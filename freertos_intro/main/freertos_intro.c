#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "FREERTOS_INTRO";

void app_main(void)
{
    ESP_LOGI(TAG, "FreeRTOS Intro Running on Core %d", xPortGetCoreID());

    // ตรวจว่ามี TWDT อยู่แล้วหรือไม่
    esp_err_t wdt_err = esp_task_wdt_deinit(); // ปิดของเก่า (ถ้ามี)
    if (wdt_err == ESP_OK) {
        ESP_LOGI(TAG, "Existing TWDT deinitialized");
    }

    // ตั้งค่า TWDT ใหม่
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 10000,    // timeout 10 วินาที
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,  // ทุก core
        .trigger_panic = true,  // ให้ panic ถ้า timeout
    };
    esp_task_wdt_init(&twdt_config);
    esp_task_wdt_add(NULL);  // เพิ่ม task ปัจจุบันเข้า watchdog

    // ===== Monitor Loop =====
    while (1)
    {
        size_t free_heap = esp_get_free_heap_size();
        size_t min_heap = esp_get_minimum_free_heap_size();
        ESP_LOGI(TAG, "💾 Heap Free: %d bytes | Min Free: %d bytes",
                 free_heap, min_heap);

        esp_task_wdt_reset(); // รีเซ็ต watchdog ป้องกัน timeout

        vTaskDelay(pdMS_TO_TICKS(5000)); // ทุก 5 วิ
    }
}
