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
#include "esp_random.h"       // ✅ เพิ่มเพื่อแก้ error esp_random()
#include "esp_attr.h"
#include "driver/gpio.h"
#include "soc/soc_memory_layout.h"

static const char *TAG = "MEM_OPT";

// GPIO สำหรับแสดงสถานะ optimization
#define LED_STATIC_ALLOC    GPIO_NUM_2
#define LED_ALIGNMENT_OPT   GPIO_NUM_4
#define LED_PACKING_OPT     GPIO_NUM_5
#define LED_MEMORY_SAVING   GPIO_NUM_18
#define LED_OPTIMIZATION    GPIO_NUM_19

#define ALIGN_UP(num, align) (((num) + (align) - 1) & ~((align) - 1))
#define IS_ALIGNED(ptr, align) (((uintptr_t)(ptr) & ((align) - 1)) == 0)

#define STATIC_BUFFER_SIZE   4096
#define STATIC_BUFFER_COUNT  8
#define TASK_STACK_SIZE      2048
#define MAX_TASKS            4

static uint8_t static_buffers[STATIC_BUFFER_COUNT][STATIC_BUFFER_SIZE] __attribute__((aligned(4)));
static bool static_buffer_used[STATIC_BUFFER_COUNT] = {false};
static SemaphoreHandle_t static_buffer_mutex;

static StackType_t task_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(8)));
static StaticTask_t task_buffers[MAX_TASKS];
static int next_task_slot = 0;

// ────────────────────────────────────────────────
typedef struct {
    size_t static_allocations;
    size_t dynamic_allocations;
    size_t alignment_optimizations;
    size_t packing_optimizations;
    size_t memory_saved_bytes;
    uint64_t allocation_time_saved;
} optimization_stats_t;

static optimization_stats_t opt_stats = {0};

// ────────────────────────────────────────────────
// Struct ตัวอย่างการจัดเรียงหน่วยความจำ
typedef struct {
    char a;
    int b;
    char c;
    double d;
    char e;
} __attribute__((packed)) bad_struct_t;

typedef struct {
    double d;
    int b;
    char a;
    char c;
    char e;
} __attribute__((aligned(8))) good_struct_t;

// ────────────────────────────────────────────────
void* allocate_static_buffer(void) {
    void* buffer = NULL;
    if (xSemaphoreTake(static_buffer_mutex, pdMS_TO_TICKS(100))) {
        for (int i = 0; i < STATIC_BUFFER_COUNT; i++) {
            if (!static_buffer_used[i]) {
                static_buffer_used[i] = true;
                buffer = static_buffers[i];
                opt_stats.static_allocations++;
                gpio_set_level(LED_STATIC_ALLOC, 1);
                break;
            }
        }
        xSemaphoreGive(static_buffer_mutex);
    }
    return buffer;
}

void free_static_buffer(void* buffer) {
    if (!buffer) return;
    if (xSemaphoreTake(static_buffer_mutex, pdMS_TO_TICKS(100))) {
        for (int i = 0; i < STATIC_BUFFER_COUNT; i++) {
            if (buffer == static_buffers[i]) {
                static_buffer_used[i] = false;
                break;
            }
        }
        gpio_set_level(LED_STATIC_ALLOC, 0);
        xSemaphoreGive(static_buffer_mutex);
    }
}

// ────────────────────────────────────────────────
void* aligned_malloc(size_t size, size_t alignment) {
    size_t total = size + alignment + sizeof(void*);
    void* raw = malloc(total);
    if (!raw) return NULL;
    uintptr_t aligned_addr = ALIGN_UP((uintptr_t)raw + sizeof(void*), alignment);
    void** ptr_store = (void**)aligned_addr - 1;
    *ptr_store = raw;
    opt_stats.alignment_optimizations++;
    gpio_set_level(LED_ALIGNMENT_OPT, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LED_ALIGNMENT_OPT, 0);
    return (void*)aligned_addr;
}

void aligned_free(void* ptr) {
    if (!ptr) return;
    void** orig = (void**)ptr - 1;
    free(*orig);
}

// ────────────────────────────────────────────────
void demonstrate_struct_optimization(void) {
    ESP_LOGI(TAG, "\n🏗️ STRUCT OPTIMIZATION DEMO");

    bad_struct_t bad_example;
    good_struct_t good_example;

    bad_example.a = 'A';
    bad_example.b = 0x12345678;
    bad_example.c = 'C';
    bad_example.d = 3.14;
    bad_example.e = 'E';

    good_example.a = 'A';
    good_example.b = 0x12345678;
    good_example.c = 'C';
    good_example.d = 3.14;
    good_example.e = 'E';

    ESP_LOGI(TAG, "Bad struct size:  %d bytes", sizeof(bad_struct_t));
    ESP_LOGI(TAG, "Good struct size: %d bytes", sizeof(good_struct_t));
    ESP_LOGI(TAG, "Memory saved:     %d bytes", sizeof(bad_struct_t) - sizeof(good_struct_t));

    (void)bad_example;   // ✅ ปิด warning unused variable
    (void)good_example;  // ✅ ปิด warning unused variable
}

// ────────────────────────────────────────────────
void optimize_memory_access_patterns(void) {
    ESP_LOGI(TAG, "\n⚡ MEMORY ACCESS OPTIMIZATION");

    const size_t array_size = 1024;
    uint32_t* array = aligned_malloc(array_size * sizeof(uint32_t), 32);
    if (!array) return;

    for (size_t i = 0; i < array_size; i++) array[i] = i;

    uint64_t start = esp_timer_get_time();
    volatile uint32_t sum = 0;
    for (size_t i = 0; i < array_size; i++) sum += array[i];
    uint64_t seq_time = esp_timer_get_time() - start;

    start = esp_timer_get_time();
    sum = 0;
    for (size_t i = 0; i < array_size; i++) {
        size_t idx = esp_random() % array_size;  // ✅ ใช้ esp_random ได้แล้ว
        sum += array[idx];
    }
    uint64_t rand_time = esp_timer_get_time() - start;

    ESP_LOGI(TAG, "Sequential: %llu µs | Random: %llu µs | Speedup: %.2fx",
             seq_time, rand_time, (float)rand_time / seq_time);

    aligned_free(array);
}

// ────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "🚀 Memory Optimization Lab Starting...");

    gpio_set_direction(LED_STATIC_ALLOC, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ALIGNMENT_OPT, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PACKING_OPT, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_MEMORY_SAVING, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_OPTIMIZATION, GPIO_MODE_OUTPUT);

    static_buffer_mutex = xSemaphoreCreateMutex();
    if (!static_buffer_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        return;
    }

    demonstrate_struct_optimization();
    optimize_memory_access_patterns();

    ESP_LOGI(TAG, "Memory Optimization System operational!");
}
