#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"     // ✅ เพิ่มบรรทัดนี้เพื่อแก้ esp_random()
#include "driver/gpio.h"

static const char *TAG = "HEAP_MGMT";

// GPIO สำหรับแสดงสถานะ
#define LED_MEMORY_OK       GPIO_NUM_2   // Memory system OK
#define LED_LOW_MEMORY      GPIO_NUM_4   // Low memory warning
#define LED_MEMORY_ERROR    GPIO_NUM_5   // Memory error/leak
#define LED_FRAGMENTATION   GPIO_NUM_18  // High fragmentation
#define LED_SPIRAM_ACTIVE   GPIO_NUM_19  // SPIRAM usage

// Memory thresholds
#define LOW_MEMORY_THRESHOLD    50000    // 50KB
#define CRITICAL_MEMORY_THRESHOLD 20000  // 20KB
#define FRAGMENTATION_THRESHOLD 0.3      // 30% fragmentation
#define MAX_ALLOCATIONS         100

// Memory allocation tracking
typedef struct {
    void* ptr;
    size_t size;
    uint32_t caps;
    const char* description;
    uint64_t timestamp;
    bool is_active;
} memory_allocation_t;

// Memory statistics
typedef struct {
    uint32_t total_allocations;
    uint32_t total_deallocations;
    uint32_t current_allocations;
    uint64_t total_bytes_allocated;
    uint64_t total_bytes_deallocated;
    uint64_t peak_usage;
    uint32_t allocation_failures;
    uint32_t fragmentation_events;
    uint32_t low_memory_events;
} memory_stats_t;

// Global variables
static memory_allocation_t allocations[MAX_ALLOCATIONS];
static memory_stats_t stats = {0};
static SemaphoreHandle_t memory_mutex;
static bool memory_monitoring_enabled = true;

// ================= Memory Tracking =================

int find_free_allocation_slot(void) {
    for (int i = 0; i < MAX_ALLOCATIONS; i++) {
        if (!allocations[i].is_active) {
            return i;
        }
    }
    return -1;
}

int find_allocation_by_ptr(void* ptr) {
    for (int i = 0; i < MAX_ALLOCATIONS; i++) {
        if (allocations[i].is_active && allocations[i].ptr == ptr) {
            return i;
        }
    }
    return -1;
}

void* tracked_malloc(size_t size, uint32_t caps, const char* description) {
    void* ptr = heap_caps_malloc(size, caps);
    
    if (memory_monitoring_enabled && memory_mutex) {
        if (xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (ptr) {
                int slot = find_free_allocation_slot();
                if (slot >= 0) {
                    allocations[slot].ptr = ptr;
                    allocations[slot].size = size;
                    allocations[slot].caps = caps;
                    allocations[slot].description = description;
                    allocations[slot].timestamp = esp_timer_get_time();
                    allocations[slot].is_active = true;
                    
                    stats.total_allocations++;
                    stats.current_allocations++;
                    stats.total_bytes_allocated += size;
                    
                    size_t current_usage = stats.total_bytes_allocated - stats.total_bytes_deallocated;
                    if (current_usage > stats.peak_usage) {
                        stats.peak_usage = current_usage;
                    }
                    
                    ESP_LOGI(TAG, "✅ Allocated %d bytes at %p (%s) - Slot %d", 
                             size, ptr, description, slot);
                } else {
                    ESP_LOGW(TAG, "⚠️ Allocation tracking full!");
                }
            } else {
                stats.allocation_failures++;
                ESP_LOGE(TAG, "❌ Failed to allocate %d bytes (%s)", size, description);
            }
            xSemaphoreGive(memory_mutex);
        }
    }
    return ptr;
}

void tracked_free(void* ptr, const char* description) {
    if (!ptr) return;
    
    if (memory_monitoring_enabled && memory_mutex) {
        if (xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int slot = find_allocation_by_ptr(ptr);
            if (slot >= 0) {
                allocations[slot].is_active = false;
                stats.total_deallocations++;
                stats.current_allocations--;
                stats.total_bytes_deallocated += allocations[slot].size;
                ESP_LOGI(TAG, "🗑️ Freed %d bytes at %p (%s) - Slot %d", 
                         allocations[slot].size, ptr, description, slot);
            } else {
                ESP_LOGW(TAG, "⚠️ Freeing untracked pointer %p (%s)", ptr, description);
            }
            xSemaphoreGive(memory_mutex);
        }
    }
    heap_caps_free(ptr);
}

// ================= Memory Analysis =================

void analyze_memory_status(void) {
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_free = esp_get_free_heap_size();
    
    float internal_fragmentation = 0.0;
    if (internal_free > 0) {
        internal_fragmentation = 1.0 - ((float)internal_largest / (float)internal_free);
    }
    
    ESP_LOGI(TAG, "\n📊 ═══ MEMORY STATUS ═══");
    ESP_LOGI(TAG, "Internal Free: %d bytes | Largest Block: %d bytes", internal_free, internal_largest);
    ESP_LOGI(TAG, "SPIRAM Free: %d bytes | Total Free: %d bytes", spiram_free, total_free);
    ESP_LOGI(TAG, "Fragmentation: %.1f%%", internal_fragmentation * 100);
    
    if (internal_free < CRITICAL_MEMORY_THRESHOLD) {
        gpio_set_level(LED_MEMORY_ERROR, 1);
        gpio_set_level(LED_LOW_MEMORY, 1);
        gpio_set_level(LED_MEMORY_OK, 0);
        stats.low_memory_events++;
        ESP_LOGW(TAG, "🚨 CRITICAL: Very low memory!");
    } else if (internal_free < LOW_MEMORY_THRESHOLD) {
        gpio_set_level(LED_LOW_MEMORY, 1);
        gpio_set_level(LED_MEMORY_OK, 0);
        stats.low_memory_events++;
        ESP_LOGW(TAG, "⚠️ WARNING: Low memory");
    } else {
        gpio_set_level(LED_MEMORY_OK, 1);
        gpio_set_level(LED_LOW_MEMORY, 0);
        gpio_set_level(LED_MEMORY_ERROR, 0);
    }
    
    if (internal_fragmentation > FRAGMENTATION_THRESHOLD) {
        gpio_set_level(LED_FRAGMENTATION, 1);
        stats.fragmentation_events++;
        ESP_LOGW(TAG, "⚠️ High fragmentation detected!");
    } else {
        gpio_set_level(LED_FRAGMENTATION, 0);
    }
    
    gpio_set_level(LED_SPIRAM_ACTIVE, spiram_free > 0);
}

// ================= Tasks =================

void memory_stress_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "🧪 Memory stress test started");
    void* test_ptrs[20] = {NULL};
    int allocation_count = 0;
    
    while (1) {
        int action = esp_random() % 3;  // ✅ ใช้งานได้แล้ว
        
        if (action == 0 && allocation_count < 20) {
            size_t size = 100 + (esp_random() % 2000);
            uint32_t caps = (esp_random() % 2) ? MALLOC_CAP_INTERNAL : MALLOC_CAP_DEFAULT;
            test_ptrs[allocation_count] = tracked_malloc(size, caps, "StressTest");
            if (test_ptrs[allocation_count]) {
                memset(test_ptrs[allocation_count], 0xAA, size);
                allocation_count++;
            }
        } else if (action == 1 && allocation_count > 0) {
            int index = esp_random() % allocation_count;
            if (test_ptrs[index]) {
                tracked_free(test_ptrs[index], "StressTest");
                for (int i = index; i < allocation_count - 1; i++) {
                    test_ptrs[i] = test_ptrs[i + 1];
                }
                allocation_count--;
            }
        } else {
            analyze_memory_status();
        }
        vTaskDelay(pdMS_TO_TICKS(1000 + (esp_random() % 2000)));
    }
}

void memory_monitor_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        analyze_memory_status();
        ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
    }
}

// ================= Initialization =================

void app_main(void) {
    ESP_LOGI(TAG, "🚀 Heap Management Lab Starting...");
    
    gpio_set_direction(LED_MEMORY_OK, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_LOW_MEMORY, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_MEMORY_ERROR, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_FRAGMENTATION, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_SPIRAM_ACTIVE, GPIO_MODE_OUTPUT);
    
    gpio_set_level(LED_MEMORY_OK, 0);
    gpio_set_level(LED_LOW_MEMORY, 0);
    gpio_set_level(LED_MEMORY_ERROR, 0);
    gpio_set_level(LED_FRAGMENTATION, 0);
    gpio_set_level(LED_SPIRAM_ACTIVE, 0);
    
    memory_mutex = xSemaphoreCreateMutex();
    memset(allocations, 0, sizeof(allocations));
    
    analyze_memory_status();
    
    xTaskCreate(memory_stress_test_task, "StressTest", 4096, NULL, 5, NULL);
    xTaskCreate(memory_monitor_task, "Monitor", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "✅ System running — check LEDs for memory status.");
}
