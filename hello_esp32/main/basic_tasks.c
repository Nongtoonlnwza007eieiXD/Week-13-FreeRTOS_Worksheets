#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

#define LED1_PIN GPIO_NUM_2
#define LED2_PIN GPIO_NUM_4

static const char *TAG = "BASIC_TASKS";

// ===============================
// Task 1: LED1 Blink (Basic Task)
// ===============================
void led1_task(void *pvParameters)
{
    int *task_id = (int *)pvParameters;
    ESP_LOGI(TAG, "LED1 Task started with ID: %d", *task_id);

    while (1) {
        ESP_LOGI(TAG, "LED1 ON");
        gpio_set_level(LED1_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "LED1 OFF");
        gpio_set_level(LED1_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    vTaskDelete(NULL);
}

// ===============================
// Task 2: LED2 Fast Blink
// ===============================
void led2_task(void *pvParameters)
{
    char *task_name = (char *)pvParameters;
    ESP_LOGI(TAG, "LED2 Task started: %s", task_name);

    while (1) {
        ESP_LOGI(TAG, "LED2 Blink Fast");

        for (int i = 0; i < 5; i++) {
            gpio_set_level(LED2_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED2_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // pause
    }
}

// ======================================
// Task 3: System Information Monitoring
// ======================================
void system_info_task(void *pvParameters)
{
    ESP_LOGI(TAG, "System Info Task started");

    while (1) {
        ESP_LOGI(TAG, "=== System Information ===");
        ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %d bytes", esp_get_minimum_free_heap_size());
        ESP_LOGI(TAG, "Number of tasks: %d", uxTaskGetNumberOfTasks());
        ESP_LOGI(TAG, "Uptime: %d seconds", xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ======================================
// Task 4: Task Manager (Control Tasks)
// ======================================
void task_manager(void *pvParameters)
{
    ESP_LOGI(TAG, "Task Manager started");

    TaskHandle_t *handles = (TaskHandle_t *)pvParameters;
    TaskHandle_t led1_handle = handles[0];
    TaskHandle_t led2_handle = handles[1];

    int command_counter = 0;

    while (1) {
        command_counter++;

        switch (command_counter % 6) {
            case 1:
                ESP_LOGI(TAG, "Manager: Suspending LED1");
                vTaskSuspend(led1_handle);
                break;
            case 2:
                ESP_LOGI(TAG, "Manager: Resuming LED1");
                vTaskResume(led1_handle);
                break;
            case 3:
                ESP_LOGI(TAG, "Manager: Suspending LED2");
                vTaskSuspend(led2_handle);
                break;
            case 4:
                ESP_LOGI(TAG, "Manager: Resuming LED2");
                vTaskResume(led2_handle);
                break;
            case 5:
                ESP_LOGI(TAG, "Manager: Getting task info");
                ESP_LOGI(TAG, "LED1 State: %s", eTaskGetState(led1_handle) == eRunning ? "Running" : "Not Running");
                ESP_LOGI(TAG, "LED2 State: %s", eTaskGetState(led2_handle) == eRunning ? "Running" : "Not Running");
                break;
            case 0:
                ESP_LOGI(TAG, "Manager: Reset cycle");
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ======================================
// Task 5: High Priority Task
// ======================================
void high_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "High Priority Task started");

    while (1) {
        ESP_LOGW(TAG, "HIGH PRIORITY TASK RUNNING!");

        for (int i = 0; i < 1000000; i++) {
            volatile int dummy = i;
        }

        ESP_LOGW(TAG, "High priority task yielding");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ======================================
// Task 6: Low Priority Task
// ======================================
void low_priority_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Low Priority Task started");

    while (1) {
        ESP_LOGI(TAG, "Low priority task running");

        for (int i = 0; i < 100; i++) {
            ESP_LOGI(TAG, "Low priority work: %d/100", i + 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// ======================================
// Task 7: Runtime Statistics Task
// ======================================
void runtime_stats_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Runtime Stats Task started");

    char *buffer = malloc(1024);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer for runtime stats");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        ESP_LOGI(TAG, "\n=== Runtime Statistics ===");
        vTaskGetRunTimeStats(buffer);
        ESP_LOGI(TAG, "Task\t\tAbs Time\tPercent Time");
        ESP_LOGI(TAG, "%s", buffer);

        ESP_LOGI(TAG, "\n=== Task List ===");
        vTaskList(buffer);
        ESP_LOGI(TAG, "Name\t\tState\tPriority\tStack\tNum");
        ESP_LOGI(TAG, "%s", buffer);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    free(buffer);
    vTaskDelete(NULL);
}

// ======================================
// Main Function
// ======================================
void app_main(void)
{
    ESP_LOGI(TAG, "=== FreeRTOS Basic Tasks Demo ===");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED1_PIN) | (1ULL << LED2_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    static int led1_id = 1;
    static char led2_name[] = "FastBlinker";

    TaskHandle_t led1_handle = NULL;
    TaskHandle_t led2_handle = NULL;
    TaskHandle_t info_handle = NULL;

    // Create LED1 Task
    if (xTaskCreate(led1_task, "LED1_Task", 2048, &led1_id, 2, &led1_handle) == pdPASS)
        ESP_LOGI(TAG, "LED1 Task created successfully");

    // Create LED2 Task
    if (xTaskCreate(led2_task, "LED2_Task", 2048, led2_name, 2, &led2_handle) == pdPASS)
        ESP_LOGI(TAG, "LED2 Task created successfully");

    // Create System Info Task
    if (xTaskCreate(system_info_task, "SysInfo_Task", 3072, NULL, 1, &info_handle) == pdPASS)
        ESP_LOGI(TAG, "System Info Task created successfully");

    // Create Task Manager
    TaskHandle_t task_handles[2] = {led1_handle, led2_handle};
    xTaskCreate(task_manager, "TaskManager", 2048, task_handles, 3, NULL);

    // Create High/Low Priority Tasks
    xTaskCreate(high_priority_task, "HighPriority", 4096, NULL, 4, NULL);
    xTaskCreate(low_priority_task, "LowPriority", 4096, NULL, 1, NULL);

    // Create Runtime Stats Task
    xTaskCreate(runtime_stats_task, "RuntimeStats", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created. Main task will now idle.");
    ESP_LOGI(TAG, "Task handles - LED1: %p, LED2: %p, Info: %p", led1_handle, led2_handle, info_handle);

    while (1) {
        ESP_LOGI(TAG, "Main task heartbeat");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
