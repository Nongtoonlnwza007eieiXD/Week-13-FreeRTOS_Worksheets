#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_HIGH_PIN GPIO_NUM_2
#define LED_MED_PIN  GPIO_NUM_4
#define LED_LOW_PIN  GPIO_NUM_5
#define BUTTON_PIN   GPIO_NUM_0

static const char *TAG = "PRIORITY_LAB";

volatile bool priority_test_running = false;
volatile uint32_t high_task_count = 0, med_task_count = 0, low_task_count = 0;
volatile bool shared_resource_busy = false;

// ===== Helper macro เพื่อใช้คำนวณ dummy =====
#define DO_DUMMY_WORK(limit, expr)     \
    do {                               \
        for (int i = 0; i < (limit); i++) { \
            (void)(expr);              \
            if (i % 50000 == 0) vTaskDelay(1); \
        }                              \
    } while (0)

// ===== STEP 1 : Basic Priority Demonstration =====
void high_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "High Priority Task started (Priority 5, Core %d)", xPortGetCoreID());

    while (1) {
        if (priority_test_running) {
            high_task_count++;
            ESP_LOGI(TAG, "[HIGH] Running (%d)", high_task_count);
            gpio_set_level(LED_HIGH_PIN, 1);

            DO_DUMMY_WORK(200000, i * 3);  // แทน dummy variable

            gpio_set_level(LED_HIGH_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void medium_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Medium Priority Task started (Priority 3, Core %d)", xPortGetCoreID());

    while (1) {
        if (priority_test_running) {
            med_task_count++;
            ESP_LOGI(TAG, "[MEDIUM] Running (%d)", med_task_count);
            gpio_set_level(LED_MED_PIN, 1);

            DO_DUMMY_WORK(250000, i + 10);

            gpio_set_level(LED_MED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(400));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void low_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Low Priority Task started (Priority 1, Core %d)", xPortGetCoreID());

    while (1) {
        if (priority_test_running) {
            low_task_count++;
            ESP_LOGI(TAG, "[LOW] Running (%d)", low_task_count);
            gpio_set_level(LED_LOW_PIN, 1);

            DO_DUMMY_WORK(400000, i - 50);

            gpio_set_level(LED_LOW_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Control Task started");

    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            if (!priority_test_running) {
                ESP_LOGW(TAG, "=== STARTING PRIORITY TEST ===");
                high_task_count = med_task_count = low_task_count = 0;
                priority_test_running = true;
                vTaskDelay(pdMS_TO_TICKS(10000));
                priority_test_running = false;

                ESP_LOGW(TAG, "=== PRIORITY TEST RESULTS ===");
                ESP_LOGI(TAG, "High: %d  Medium: %d  Low: %d", 
                         high_task_count, med_task_count, low_task_count);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ===== STEP 2 : Round-Robin Scheduling =====
void equal_priority_task1(void *pvParameters)
{
    while (1) {
        if (priority_test_running) {
            ESP_LOGI(TAG, "Equal Priority Task 1 running");
            DO_DUMMY_WORK(100000, i);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void equal_priority_task2(void *pvParameters)
{
    while (1) {
        if (priority_test_running) {
            ESP_LOGI(TAG, "Equal Priority Task 2 running");
            DO_DUMMY_WORK(100000, i);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void equal_priority_task3(void *pvParameters)
{
    while (1) {
        if (priority_test_running) {
            ESP_LOGI(TAG, "Equal Priority Task 3 running");
            DO_DUMMY_WORK(100000, i);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ===== STEP 3 : Priority Inversion Demo =====
void priority_inversion_high(void *pvParameters)
{
    while (1) {
        if (priority_test_running) {
            ESP_LOGW(TAG, "[INV_HIGH] Needs shared resource...");
            while (shared_resource_busy) {
                ESP_LOGW(TAG, "High priority waiting...");
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            ESP_LOGI(TAG, "[INV_HIGH] Got the resource!");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void priority_inversion_low(void *pvParameters)
{
    while (1) {
        if (priority_test_running) {
            ESP_LOGI(TAG, "[INV_LOW] Using shared resource...");
            shared_resource_busy = true;
            vTaskDelay(pdMS_TO_TICKS(2000));
            shared_resource_busy = false;
            ESP_LOGI(TAG, "[INV_LOW] Released resource");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ===== STEP 4 : Dynamic Priority Change =====
void dynamic_priority_demo(void *pvParameters)
{
    TaskHandle_t low_handle = (TaskHandle_t)pvParameters;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGW(TAG, "[Dynamic] Boosting Low Task to Priority 4");
        vTaskPrioritySet(low_handle, 4);

        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGW(TAG, "[Dynamic] Restoring Low Task to Priority 1");
        vTaskPrioritySet(low_handle, 1);
    }
}

// ===== MAIN =====
void app_main(void)
{
    ESP_LOGI(TAG, "=== FreeRTOS Priority / Round-Robin / Dynamic / Dual-Core Lab ===");

    // ตั้งค่า GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<LED_HIGH_PIN)|(1ULL<<LED_MED_PIN)|(1ULL<<LED_LOW_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    gpio_config_t btn_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .pull_up_en = 1,
        .pull_down_en = 0,
    };
    gpio_config(&btn_conf);

    // สร้าง Tasks และกำหนด Core
    TaskHandle_t low_handle = NULL;

    xTaskCreatePinnedToCore(high_priority_task, "HighPrio", 3072, NULL, 5, NULL, 0); // Core 0
    xTaskCreatePinnedToCore(medium_priority_task, "MedPrio", 3072, NULL, 3, NULL, 0); // Core 0
    xTaskCreatePinnedToCore(low_priority_task, "LowPrio", 3072, NULL, 1, &low_handle, 1); // Core 1
    xTaskCreatePinnedToCore(control_task, "Control", 3072, NULL, 4, NULL, 1); // Core 1

    // Round-Robin Tasks
    xTaskCreatePinnedToCore(equal_priority_task1, "Equal1", 2048, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(equal_priority_task2, "Equal2", 2048, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(equal_priority_task3, "Equal3", 2048, NULL, 2, NULL, 1);

    // Priority Inversion Tasks
    xTaskCreatePinnedToCore(priority_inversion_high, "InvHigh", 2048, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(priority_inversion_low, "InvLow", 2048, NULL, 1, NULL, 1);

    // Dynamic Priority Controller
    xTaskCreatePinnedToCore(dynamic_priority_demo, "DynamicPrio", 3072, low_handle, 3, NULL, 0);

    ESP_LOGI(TAG, "Press button (GPIO0) to start Priority Test");
}
