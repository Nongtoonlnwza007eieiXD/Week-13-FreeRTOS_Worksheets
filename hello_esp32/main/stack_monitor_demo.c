#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"

// -------------------- CONFIG --------------------
#define BAUD_RATE 115200
static const char *TAG = "STACK_MONITOR";

// ตัวอย่าง task สำหรับทดสอบ
void heavy_dynamic_task(void *pvParameters)
{
    ESP_LOGI(TAG, "HeavyTask_Dynamic started");

    int counter = 0;
    while (1)
    {
        counter++;
        ESP_LOGI(TAG, "Running dynamic stack task... %d", counter);

        // ใช้งาน stack บ้างเพื่อจำลองโหลด
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "Dynamic data %d", counter);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// -------------------- MAIN --------------------
void app_main(void)
{
    // 🟢 ตั้งค่า UART Console ให้ Baud rate ถูกต้อง
    const uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    // 🟢 ตั้งค่า log system ให้ใช้ stdout ที่ถูกต้อง
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_set_vprintf(vprintf);

    ESP_LOGI(TAG, "=== FreeRTOS Stack Size Optimization Demo (Safe Mode) ===");

    // สร้าง task ทดสอบ dynamic stack monitoring
    xTaskCreate(heavy_dynamic_task, "HeavyTask_Dynamic", 2048, NULL, 3, NULL);

    // main loop (กัน watchdog reset)
    while (1)
    {
        ESP_LOGI(TAG, "Main loop alive...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
