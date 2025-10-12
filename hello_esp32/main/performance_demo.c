#include "esp_log.h"      // ✅ เพิ่มไฟล์ header สำหรับ logging
#include "esp_timer.h"    // ✅ สำหรับฟังก์ชันจับเวลา esp_timer_get_time()

static const char *TAG = "PERF_MON";  // ✅ ประกาศ TAG สำหรับ log

void performance_demo(void)
{
    ESP_LOGI(TAG, "=== Performance Monitoring ===");

    uint64_t start_time = esp_timer_get_time();

    // งานจำลอง (CPU Loop)
    for (int i = 0; i < 1000000; i++) {
        volatile int dummy = i * 2;
    }

    uint64_t end_time = esp_timer_get_time();
    uint64_t execution_time = end_time - start_time;

    ESP_LOGI(TAG, "Execution time: %lld microseconds", execution_time);
    ESP_LOGI(TAG, "Execution time: %.2f milliseconds", execution_time / 1000.0);
}
