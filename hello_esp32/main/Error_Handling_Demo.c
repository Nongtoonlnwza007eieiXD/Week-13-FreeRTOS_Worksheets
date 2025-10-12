#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "ERROR_DEMO";   // Tag สำหรับแสดงชื่อโมดูลใน log

void error_handling_demo(void)
{
    ESP_LOGI(TAG, "=== Error Handling Demo ===");

    esp_err_t result;

    // ✅ Case 1: Success (ไม่มี error)
    result = ESP_OK;
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Operation completed successfully");
    }

    // ✅ Case 2: Memory allocation failure
    result = ESP_ERR_NO_MEM;
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Error: %s (0x%x)", esp_err_to_name(result), result);
    }

    // ✅ Case 3: Invalid argument (non-fatal)
    result = ESP_ERR_INVALID_ARG;
    ESP_ERROR_CHECK_WITHOUT_ABORT(result); // แสดง error แต่ไม่หยุดระบบ
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Non-fatal error: %s (0x%x)", esp_err_to_name(result), result);
    }

    // ✅ Case 4: Fatal error simulation
    result = ESP_FAIL;
    if (result == ESP_FAIL) {
        ESP_LOGE(TAG, "Fatal error simulated (demo only, continuing execution)");
    }

    ESP_LOGI(TAG, "Error handling demo completed successfully.\n");
}
