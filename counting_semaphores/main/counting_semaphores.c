#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "COUNTING_SEM";

// LED pins
#define LED_RESOURCE_1 GPIO_NUM_2
#define LED_RESOURCE_2 GPIO_NUM_4
#define LED_RESOURCE_3 GPIO_NUM_5
#define LED_RESOURCE_4 GPIO_NUM_16
#define LED_RESOURCE_5 GPIO_NUM_17
#define LED_PRODUCER GPIO_NUM_18
#define LED_SYSTEM GPIO_NUM_19

// === CONFIGURATION ===
#define MAX_RESOURCES 5   // 🔹 ทดลองที่ 2: เพิ่มจำนวน Resources เป็น 5
#define NUM_PRODUCERS 8   // 🔹 ทดลองที่ 3: เพิ่ม Producers เป็น 8
#define NUM_CONSUMERS 3   // (ยังคงเหมือนเดิม)

// Semaphore handle
SemaphoreHandle_t xCountingSemaphore;

// Resource management
typedef struct {
    int resource_id;
    bool in_use;
    char current_user[20];
    uint32_t usage_count;
    uint32_t total_usage_time;
} resource_t;

resource_t resources[MAX_RESOURCES] = {
    {1, false, "", 0, 0},
    {2, false, "", 0, 0},
    {3, false, "", 0, 0},
    {4, false, "", 0, 0},
    {5, false, "", 0, 0}
};

// Statistics
typedef struct {
    uint32_t total_requests;
    uint32_t successful_acquisitions;
    uint32_t failed_acquisitions;
    uint32_t resources_in_use;
} system_stats_t;

system_stats_t stats = {0, 0, 0, 0};

// --- Utility: LED map ---
void set_resource_led(int idx, int state) {
    gpio_num_t pins[MAX_RESOURCES] = {
        LED_RESOURCE_1, LED_RESOURCE_2, LED_RESOURCE_3, LED_RESOURCE_4, LED_RESOURCE_5};
    gpio_set_level(pins[idx], state);
}

// --- Acquire resource ---
int acquire_resource(const char *user_name) {
    for (int i = 0; i < MAX_RESOURCES; i++) {
        if (!resources[i].in_use) {
            resources[i].in_use = true;
            strcpy(resources[i].current_user, user_name);
            resources[i].usage_count++;
            set_resource_led(i, 1);
            stats.resources_in_use++;
            return i;
        }
    }
    return -1;
}

// --- Release resource ---
void release_resource(int idx, uint32_t usage_time) {
    if (idx >= 0 && idx < MAX_RESOURCES) {
        resources[idx].in_use = false;
        strcpy(resources[idx].current_user, "");
        resources[idx].total_usage_time += usage_time;
        set_resource_led(idx, 0);
        stats.resources_in_use--;
    }
}

// --- Producer task ---
void producer_task(void *pvParameters) {
    int producer_id = *((int *)pvParameters);
    char task_name[20];
    snprintf(task_name, sizeof(task_name), "Producer%d", producer_id);

    ESP_LOGI(TAG, "%s started", task_name);

    while (1) {
        stats.total_requests++;
        gpio_set_level(LED_PRODUCER, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_PRODUCER, 0);

        ESP_LOGI(TAG, "🏭 %s: Requesting resource...", task_name);
        uint32_t start = xTaskGetTickCount();

        if (xSemaphoreTake(xCountingSemaphore, pdMS_TO_TICKS(8000)) == pdTRUE) {
            stats.successful_acquisitions++;
            uint32_t wait_time = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;

            int res_idx = acquire_resource(task_name);
            if (res_idx >= 0) {
                ESP_LOGI(TAG, "✓ %s: Got resource %d (wait %lums)", task_name, res_idx + 1, wait_time);
                uint32_t use_time = 1000 + (esp_random() % 3000);
                vTaskDelay(pdMS_TO_TICKS(use_time));
                release_resource(res_idx, use_time);
                xSemaphoreGive(xCountingSemaphore);
                ESP_LOGI(TAG, "✓ %s: Released resource %d", task_name, res_idx + 1);
            } else {
                ESP_LOGE(TAG, "✗ %s: Semaphore ok but resource unavailable!", task_name);
                xSemaphoreGive(xCountingSemaphore);
            }
        } else {
            stats.failed_acquisitions++;
            ESP_LOGW(TAG, "⏰ %s: Timeout waiting for resource", task_name);
        }

        vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 3000)));
    }
}

// --- Resource monitor ---
void resource_monitor_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        int avail = uxSemaphoreGetCount(xCountingSemaphore);

        ESP_LOGI(TAG, "\n📊 RESOURCE POOL STATUS");
        ESP_LOGI(TAG, "Available: %d/%d  In-use: %lu", avail, MAX_RESOURCES, stats.resources_in_use);

        for (int i = 0; i < MAX_RESOURCES; i++) {
            if (resources[i].in_use)
                ESP_LOGI(TAG, "  Resource %d: BUSY (User: %s)", i + 1, resources[i].current_user);
            else
                ESP_LOGI(TAG, "  Resource %d: FREE (Used %lu times)", i + 1, resources[i].usage_count);
        }

        printf("Pool: [");
        for (int i = 0; i < MAX_RESOURCES; i++) printf(resources[i].in_use ? "■" : "□");
        printf("] Available: %d\n", avail);
    }
}

// --- Statistics task ---
void statistics_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(12000));
        float success_rate = stats.total_requests > 0
                                 ? (float)stats.successful_acquisitions / stats.total_requests * 100
                                 : 0;
        ESP_LOGI(TAG, "\n📈 SYSTEM STATS");
        ESP_LOGI(TAG, "Requests=%lu | Success=%lu | Fail=%lu | Active=%lu",
                 stats.total_requests, stats.successful_acquisitions, stats.failed_acquisitions,
                 stats.resources_in_use);
        ESP_LOGI(TAG, "Success rate: %.1f%%", success_rate);
    }
}

// --- Load generator (stress test) ---
void load_generator_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20000));
        gpio_set_level(LED_SYSTEM, 1);
        ESP_LOGW(TAG, "🚀 LOAD GENERATOR: Starting burst test...");
        for (int i = 0; i < MAX_RESOURCES + 3; i++) {
            if (xSemaphoreTake(xCountingSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
                int idx = acquire_resource("LoadGen");
                if (idx >= 0) {
                    ESP_LOGI(TAG, "LoadGen: Acquired %d", idx + 1);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    release_resource(idx, 300);
                    xSemaphoreGive(xCountingSemaphore);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        gpio_set_level(LED_SYSTEM, 0);
        ESP_LOGI(TAG, "Load burst completed");
    }
}

// --- Main ---
void app_main(void) {
    ESP_LOGI(TAG, "Counting Semaphore Lab Starting...");

    gpio_num_t all_leds[MAX_RESOURCES + 2] = {
        LED_RESOURCE_1, LED_RESOURCE_2, LED_RESOURCE_3, LED_RESOURCE_4, LED_RESOURCE_5, LED_PRODUCER, LED_SYSTEM};
    for (int i = 0; i < MAX_RESOURCES + 2; i++) {
        gpio_set_direction(all_leds[i], GPIO_MODE_OUTPUT);
        gpio_set_level(all_leds[i], 0);
    }

    xCountingSemaphore = xSemaphoreCreateCounting(MAX_RESOURCES, MAX_RESOURCES);
    if (xCountingSemaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create counting semaphore!");
        return;
    }

    // Create producers
    static int producer_ids[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_ids[i] = i + 1;
        char name[20];
        snprintf(name, sizeof(name), "Producer%d", i + 1);
        xTaskCreate(producer_task, name, 3072, &producer_ids[i], 3, NULL);
    }

    // Create system tasks
    xTaskCreate(resource_monitor_task, "Monitor", 3072, NULL, 2, NULL);
    xTaskCreate(statistics_task, "Stats", 3072, NULL, 1, NULL);
    xTaskCreate(load_generator_task, "LoadGen", 2048, NULL, 4, NULL);

    ESP_LOGI(TAG, "System Ready: %d Resources, %d Producers", MAX_RESOURCES, NUM_PRODUCERS);
}
