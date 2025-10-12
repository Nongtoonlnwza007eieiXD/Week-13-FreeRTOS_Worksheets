#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"    // ✅ ต้องมี
#include "esp_flash.h"        // ✅ ใช้แทน spi_flash_get_chip_size()
#include "nvs_flash.h"

// TAG สำหรับ logging
static const char *TAG = "LOGGING_DEMO";

// ====== ฟังก์ชันย่อยต่าง ๆ ======

void demonstrate_logging_levels(void)
{
    ESP_LOGE(TAG, "This is an ERROR message - highest priority");
    ESP_LOGW(TAG, "This is a WARNING message");
    ESP_LOGI(TAG, "This is an INFO message - default level");
    ESP_LOGD(TAG, "This is a DEBUG message - needs debug level");
    ESP_LOGV(TAG, "This is a VERBOSE message - needs verbose level");
}

void demonstrate_formatted_logging(void)
{
    int temperature = 25;
    float voltage = 3.3;
    const char* status = "OK";

    ESP_LOGI(TAG, "Sensor readings:");
    ESP_LOGI(TAG, "  Temperature: %d°C", temperature);
    ESP_LOGI(TAG, "  Voltage: %.2fV", voltage);
    ESP_LOGI(TAG, "  Status: %s", status);

    // แสดงข้อมูลแบบ Hex dump
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ESP_LOGI(TAG, "Data dump:");
    ESP_LOG_BUFFER_HEX(TAG, data, sizeof(data));
}

void demonstrate_conditional_logging(void)
{
    int error_code = 0;

    if (error_code != 0) {
        ESP_LOGE(TAG, "Error occurred: code %d", error_code);
    } else {
        ESP_LOGI(TAG, "System is running normally");
    }

    // การใช้งาน ESP_ERROR_CHECK กับ NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");
}

// ====== ฟังก์ชันหลัก ======
void app_main(void)
{
    // ✅ ต้องอยู่ในฟังก์ชัน ไม่ใช่นอก main
    esp_log_level_set("LOGGING_DEMO", ESP_LOG_DEBUG);
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "=== ESP32 Hello World Demo ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip Model: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "Free Heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min Free Heap: %d bytes", esp_get_minimum_free_heap_size());

    // ✅ ข้อมูลชิป
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip cores: %d", chip_info.cores);

    // ✅ ใช้ API ใหม่ esp_flash_get_size()
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "Flash size: %ldMB %s",
             flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    // ====== เรียกแต่ละฟังก์ชันสาธิต ======
    ESP_LOGI(TAG, "\n--- Logging Levels Demo ---");
    demonstrate_logging_levels();

    ESP_LOGI(TAG, "\n--- Formatted Logging Demo ---");
    demonstrate_formatted_logging();

    ESP_LOGI(TAG, "\n--- Conditional Logging Demo ---");
    demonstrate_conditional_logging();

    // ====== ลูปหลัก ======
    int counter = 0;
    while (1) {
        ESP_LOGI(TAG, "Main loop iteration: %d", counter++);

        if (counter % 10 == 0) {
            ESP_LOGI(TAG, "Memory status - Free: %d bytes", esp_get_free_heap_size());
        }

        if (counter % 20 == 0) {
            ESP_LOGW(TAG, "Warning: Counter reached %d", counter);
        }

        if (counter > 50) {
            ESP_LOGE(TAG, "Error simulation: Counter exceeded 50!");
            counter = 0; // reset counter
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // หน่วงเวลา 2 วินาที
    }
}
