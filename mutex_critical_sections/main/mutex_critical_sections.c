#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "MUTEX_LAB";

// ------------------------- CONFIG -------------------------
// ตั้งค่าทดลองที่นี่ (1=ปกติ, 2=ปิด Mutex, 3=ปรับ Priority)
#define TEST_MODE 3
// ----------------------------------------------------------

// LED pins
#define LED_TASK1 GPIO_NUM_2
#define LED_TASK2 GPIO_NUM_4
#define LED_TASK3 GPIO_NUM_5
#define LED_CRITICAL GPIO_NUM_18

SemaphoreHandle_t xMutex;

// Shared resource
typedef struct {
    uint32_t counter;
    char shared_buffer[100];
    uint32_t checksum;
    uint32_t access_count;
} shared_resource_t;

shared_resource_t shared_data = {0, "", 0, 0};

// Statistics
typedef struct {
    uint32_t successful_access;
    uint32_t failed_access;
    uint32_t corruption_detected;
} access_stats_t;

access_stats_t stats = {0, 0, 0};

// ---------------- UTILITY ----------------
uint32_t calculate_checksum(const char *data, uint32_t counter) {
    uint32_t sum = counter;
    for (int i = 0; data[i] != '\0'; i++) sum += (uint32_t)data[i] * (i + 1);
    return sum;
}

// ---------------- CRITICAL SECTION ----------------
void access_shared_resource(const char *task_name, gpio_num_t led_pin) {
    char temp_buffer[100];
    uint32_t temp_counter, expected_checksum;

    ESP_LOGI(TAG, "[%s] Request access to shared data...", task_name);

#if TEST_MODE == 2
    // 🔬 TEST 2: ปิดการใช้ Mutex
    ESP_LOGW(TAG, "[%s] ⚠️ MUTEX DISABLED - UNSAFE ACCESS MODE", task_name);
    gpio_set_level(led_pin, 1);
    gpio_set_level(LED_CRITICAL, 1);

    temp_counter = shared_data.counter;
    strcpy(temp_buffer, shared_data.shared_buffer);
    expected_checksum = shared_data.checksum;

    uint32_t calc = calculate_checksum(temp_buffer, temp_counter);
    if (calc != expected_checksum && shared_data.access_count > 0) {
        ESP_LOGE(TAG, "[%s] ❌ Data corruption detected!", task_name);
        stats.corruption_detected++;
    }

    vTaskDelay(pdMS_TO_TICKS(300 + (esp_random() % 500)));
    shared_data.counter = temp_counter + 1;
    snprintf(shared_data.shared_buffer, sizeof(shared_data.shared_buffer),
             "Modified by %s #%lu", task_name, shared_data.counter);
    shared_data.checksum =
        calculate_checksum(shared_data.shared_buffer, shared_data.counter);
    shared_data.access_count++;
    stats.successful_access++;
    ESP_LOGI(TAG, "[%s] Updated Counter=%lu", task_name, shared_data.counter);

    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(led_pin, 0);
    gpio_set_level(LED_CRITICAL, 0);

#else
    // 🔐 TEST 1 / TEST 3: ใช้ Mutex ปกติ
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        ESP_LOGI(TAG, "[%s] ✓ Mutex acquired", task_name);
        gpio_set_level(led_pin, 1);
        gpio_set_level(LED_CRITICAL, 1);

        temp_counter = shared_data.counter;
        strcpy(temp_buffer, shared_data.shared_buffer);
        expected_checksum = shared_data.checksum;

        uint32_t calc = calculate_checksum(temp_buffer, temp_counter);
        if (calc != expected_checksum && shared_data.access_count > 0) {
            ESP_LOGE(TAG, "[%s] ❌ Data corruption detected!", task_name);
            stats.corruption_detected++;
        }

        vTaskDelay(pdMS_TO_TICKS(300 + (esp_random() % 500)));
        shared_data.counter = temp_counter + 1;
        snprintf(shared_data.shared_buffer, sizeof(shared_data.shared_buffer),
                 "Modified by %s #%lu", task_name, shared_data.counter);
        shared_data.checksum =
            calculate_checksum(shared_data.shared_buffer, shared_data.counter);
        shared_data.access_count++;
        stats.successful_access++;
        ESP_LOGI(TAG, "[%s] Updated Counter=%lu", task_name, shared_data.counter);

        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(led_pin, 0);
        gpio_set_level(LED_CRITICAL, 0);
        xSemaphoreGive(xMutex);
        ESP_LOGI(TAG, "[%s] Mutex released", task_name);
    } else {
        ESP_LOGW(TAG, "[%s] ✗ Failed to acquire mutex (timeout)", task_name);
        stats.failed_access++;
    }
#endif
}

// ---------------- TASKS ----------------
void high_priority_task(void *pvParameters) {
    ESP_LOGI(TAG, "High Priority Task started (Prio: %d)",
             uxTaskPriorityGet(NULL));
    while (1) {
        access_shared_resource("HIGH_PRI", LED_TASK1);
        vTaskDelay(pdMS_TO_TICKS(5000 + (esp_random() % 3000)));
    }
}

void medium_priority_task(void *pvParameters) {
    ESP_LOGI(TAG, "Medium Priority Task started (Prio: %d)",
             uxTaskPriorityGet(NULL));
    while (1) {
        access_shared_resource("MED_PRI", LED_TASK2);
        vTaskDelay(pdMS_TO_TICKS(3000 + (esp_random() % 2000)));
    }
}

void low_priority_task(void *pvParameters) {
    ESP_LOGI(TAG, "Low Priority Task started (Prio: %d)",
             uxTaskPriorityGet(NULL));
    while (1) {
        access_shared_resource("LOW_PRI", LED_TASK3);
        vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 1000)));
    }
}

void monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "System monitor started");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "\n═══ MUTEX MONITOR ═══");
        ESP_LOGI(TAG, "Counter: %lu", shared_data.counter);
        ESP_LOGI(TAG, "Buffer: '%s'", shared_data.shared_buffer);
        ESP_LOGI(TAG, "Access Count: %lu", shared_data.access_count);
        ESP_LOGI(TAG, "Successful: %lu | Failed: %lu | Corrupted: %lu",
                 stats.successful_access, stats.failed_access,
                 stats.corruption_detected);
        ESP_LOGI(TAG, "═══════════════════════\n");
    }
}

// ---------------- MAIN ----------------
void app_main(void) {
    ESP_LOGI(TAG, "Mutex Lab Starting (TEST_MODE=%d)...", TEST_MODE);

    gpio_set_direction(LED_TASK1, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_TASK2, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_TASK3, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_CRITICAL, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_TASK1, 0);
    gpio_set_level(LED_TASK2, 0);
    gpio_set_level(LED_TASK3, 0);
    gpio_set_level(LED_CRITICAL, 0);

#if TEST_MODE != 2
    xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        return;
    }
#endif

    shared_data.counter = 0;
    strcpy(shared_data.shared_buffer, "Initial state");
    shared_data.checksum =
        calculate_checksum(shared_data.shared_buffer, shared_data.counter);

#if TEST_MODE == 3
    // 🧩 TEST 3: ปรับ Priority (Low สูงกว่า High)
    xTaskCreate(high_priority_task, "HighPri", 3072, NULL, 2, NULL);
    xTaskCreate(medium_priority_task, "MedPri", 3072, NULL, 3, NULL);
    xTaskCreate(low_priority_task, "LowPri", 3072, NULL, 5, NULL);
#else
    // 🧩 TEST 1–2: Priority ปกติ
    xTaskCreate(high_priority_task, "HighPri", 3072, NULL, 5, NULL);
    xTaskCreate(medium_priority_task, "MedPri", 3072, NULL, 3, NULL);
    xTaskCreate(low_priority_task, "LowPri", 3072, NULL, 2, NULL);
#endif

    xTaskCreate(monitor_task, "Monitor", 3072, NULL, 1, NULL);
    ESP_LOGI(TAG, "All tasks created successfully.");
}
