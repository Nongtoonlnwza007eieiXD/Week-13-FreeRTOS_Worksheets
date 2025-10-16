// memory_pools.c  — safe version for ESP-IDF v5.x (no-PSRAM friendly)
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"   // <-- IMPORTANT with IDF v5.x
#include "driver/gpio.h"

static const char *TAG = "MEM_POOLS";

/* ---------------- GPIO indicators ---------------- */
#define LED_SMALL_POOL     GPIO_NUM_2
#define LED_MEDIUM_POOL    GPIO_NUM_4
#define LED_LARGE_POOL     GPIO_NUM_5
#define LED_POOL_FULL      GPIO_NUM_18
#define LED_POOL_ERROR     GPIO_NUM_19

/* ---------------- Pool sizes (safe defaults) ----------------
   ปรับให้เหมาะกับบอร์ดไม่มี PSRAM; ถ้ามี PSRAM โค้ดจะขยาย Huge pool ให้อัตโนมัติ */
#define SMALL_POOL_BLOCK_SIZE    64
#define SMALL_POOL_BLOCK_COUNT   16

#define MEDIUM_POOL_BLOCK_SIZE   256
#define MEDIUM_POOL_BLOCK_COUNT  8

#define LARGE_POOL_BLOCK_SIZE    1024
#define LARGE_POOL_BLOCK_COUNT   4

#define HUGE_POOL_BLOCK_SIZE     4096
#define HUGE_POOL_BLOCK_COUNT    2     // จะเพิ่มอัตโนมัติถ้ามี PSRAMเยอะ

/* ---------------- Internal structures ---------------- */
typedef struct memory_block {
    struct memory_block* next;
    uint32_t magic;
    uint32_t pool_id;
    uint64_t alloc_time;
} memory_block_t;

typedef struct {
    const char* name;
    size_t block_size;
    size_t block_count;
    size_t alignment;
    uint32_t caps;

    void* pool_memory;
    memory_block_t* free_list;
    uint8_t* usage_bitmap;   // 1 bit per block

    size_t allocated_blocks;
    size_t peak_usage;
    uint64_t total_allocations;
    uint64_t total_deallocations;
    uint64_t allocation_time_total;
    uint64_t deallocation_time_total;
    uint32_t allocation_failures;

    SemaphoreHandle_t mutex;
    uint32_t pool_id;
} memory_pool_t;

typedef enum {
    POOL_SMALL = 0,
    POOL_MEDIUM,
    POOL_LARGE,
    POOL_HUGE,
    POOL_COUNT
} pool_type_t;

typedef struct {
    const char* name;
    size_t block_size;
    size_t block_count;
    uint32_t caps;
    gpio_num_t led_pin;
} pool_config_t;

/* ---------------- Magic for corruption checks ---------------- */
#define POOL_MAGIC_FREE   0xDEADBEEF
#define POOL_MAGIC_ALLOC  0xCAFEBABE

/* ---------------- Globals ---------------- */
static memory_pool_t pools[POOL_COUNT] = {0};

/* ---------------- Helpers ---------------- */
static inline size_t aligned_size(size_t size, size_t align) {
    return (size + align - 1) & ~(align - 1);
}

static bool try_init_pool(memory_pool_t* pool, const pool_config_t* cfg, uint32_t pool_id, size_t block_count)
{
    memset(pool, 0, sizeof(*pool));
    pool->name       = cfg->name;
    pool->block_size = cfg->block_size;
    pool->block_count= block_count;
    pool->alignment  = 4;
    pool->caps       = cfg->caps;
    pool->pool_id    = pool_id;

    const size_t hdr   = sizeof(memory_block_t);
    const size_t data  = aligned_size(pool->block_size, pool->alignment);
    const size_t stride= hdr + data;
    const size_t total = stride * pool->block_count;

    ESP_LOGI(TAG, "%s: requesting pool memory %u blocks × %uB (stride %uB) = %uB (caps 0x%X)",
             pool->name, (unsigned)pool->block_count, (unsigned)pool->block_size,
             (unsigned)stride, (unsigned)total, (unsigned)pool->caps);

    pool->pool_memory = heap_caps_malloc(total, pool->caps);
    if (!pool->pool_memory) {
        ESP_LOGW(TAG, "%s: heap_caps_malloc(%uB, caps=0x%X) FAILED", pool->name, (unsigned)total, (unsigned)pool->caps);
        return false;
    }

    const size_t bitmap_bytes = (pool->block_count + 7) / 8;
    pool->usage_bitmap = (uint8_t*) heap_caps_calloc(bitmap_bytes, 1, MALLOC_CAP_INTERNAL);
    if (!pool->usage_bitmap) {
        ESP_LOGW(TAG, "%s: bitmap alloc (%uB) FAILED", pool->name, (unsigned)bitmap_bytes);
        heap_caps_free(pool->pool_memory);
        pool->pool_memory = NULL;
        return false;
    }

    pool->free_list = NULL;
    uint8_t* p = (uint8_t*)pool->pool_memory;
    for (size_t i = 0; i < pool->block_count; i++) {
        memory_block_t* blk = (memory_block_t*)(p + i * stride);
        blk->magic = POOL_MAGIC_FREE;
        blk->pool_id = pool->pool_id;
        blk->alloc_time = 0;
        blk->next = pool->free_list;
        pool->free_list = blk;
    }

    pool->mutex = xSemaphoreCreateMutex();
    if (!pool->mutex) {
        heap_caps_free(pool->usage_bitmap);
        heap_caps_free(pool->pool_memory);
        pool->usage_bitmap = NULL;
        pool->pool_memory = NULL;
        ESP_LOGE(TAG, "%s: failed to create mutex", pool->name);
        return false;
    }

    ESP_LOGI(TAG, "✅ %s pool initialized: %u blocks × %uB = %uB", pool->name,
             (unsigned)pool->block_count, (unsigned)pool->block_size,
             (unsigned)(stride * pool->block_count));
    return true;
}

/* ลด block_count ลงครึ่งหนึ่งเรื่อย ๆ จนจองได้ (กันรีบูต) */
static bool init_memory_pool_safely(memory_pool_t* pool, const pool_config_t* cfg, uint32_t pool_id)
{
    size_t count = cfg->block_count;
    while (count >= 1) {
        if (try_init_pool(pool, cfg, pool_id, count)) return true;
        count /= 2;
        ESP_LOGW(TAG, "%s: retry with smaller block_count = %u", cfg->name, (unsigned)count);
    }
    return false;
}

/* ---------------- Allocation/Free ---------------- */
static void* pool_malloc(memory_pool_t* pool)
{
    if (!pool || !pool->mutex) return NULL;
    uint64_t t0 = esp_timer_get_time();
    void* out = NULL;

    if (xSemaphoreTake(pool->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (pool->free_list) {
            memory_block_t* blk = pool->free_list;
            pool->free_list = blk->next;

            if (blk->magic != POOL_MAGIC_FREE || blk->pool_id != pool->pool_id) {
                ESP_LOGE(TAG, "🚨 %s: corruption on alloc blk=%p", pool->name, blk);
                gpio_set_level(LED_POOL_ERROR, 1);
                xSemaphoreGive(pool->mutex);
                return NULL;
            }

            blk->magic = POOL_MAGIC_ALLOC;
            blk->alloc_time = esp_timer_get_time();
            blk->next = NULL;

            pool->allocated_blocks++;
            if (pool->allocated_blocks > pool->peak_usage) pool->peak_usage = pool->allocated_blocks;
            pool->total_allocations++;

            /* set bitmap */
            const size_t stride = sizeof(memory_block_t) + aligned_size(pool->block_size, pool->alignment);
            size_t idx = ((uint8_t*)blk - (uint8_t*)pool->pool_memory) / stride;
            if (idx < pool->block_count) pool->usage_bitmap[idx >> 3] |= (uint8_t)(1u << (idx & 7));

            out = (uint8_t*)blk + sizeof(memory_block_t);
        } else {
            pool->allocation_failures++;
            gpio_set_level(LED_POOL_FULL, 1);
            ESP_LOGW(TAG, "🔴 %s: pool exhausted %u/%u", pool->name,
                     (unsigned)pool->allocated_blocks, (unsigned)pool->block_count);
        }
        xSemaphoreGive(pool->mutex);
    }

    pool->allocation_time_total += (esp_timer_get_time() - t0);
    return out;
}

static bool pool_free(memory_pool_t* pool, void* ptr)
{
    if (!pool || !ptr || !pool->mutex) return false;
    uint64_t t0 = esp_timer_get_time();
    bool ok = false;

    if (xSemaphoreTake(pool->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memory_block_t* blk = (memory_block_t*)((uint8_t*)ptr - sizeof(memory_block_t));

        /* verify bounds */
        const size_t stride = sizeof(memory_block_t) + aligned_size(pool->block_size, pool->alignment);
        uint8_t* start = (uint8_t*)pool->pool_memory;
        uint8_t* end   = start + stride * pool->block_count;

        if ((uint8_t*)blk < start || (uint8_t*)blk >= end ||
            blk->magic != POOL_MAGIC_ALLOC || blk->pool_id != pool->pool_id) {
            ESP_LOGE(TAG, "🚨 invalid free %p for %s (magic=0x%08X pid=%lu)",
                     ptr, pool->name, blk->magic, blk->pool_id);
            gpio_set_level(LED_POOL_ERROR, 1);
        } else {
            size_t idx = ((uint8_t*)blk - start) / stride;
            pool->usage_bitmap[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
            blk->magic = POOL_MAGIC_FREE;
            blk->next = pool->free_list;
            pool->free_list = blk;

            if (pool->allocated_blocks) pool->allocated_blocks--;
            pool->total_deallocations++;
            ok = true;
        }
        xSemaphoreGive(pool->mutex);
    }

    pool->deallocation_time_total += (esp_timer_get_time() - t0);
    return ok;
}

/* ---------------- Smart API ---------------- */
static void* smart_pool_malloc(size_t size)
{
    size_t need = size + 16; // headroom
    for (int i = 0; i < POOL_COUNT; i++) {
        if (need <= pools[i].block_size) {
            void* p = pool_malloc(&pools[i]);
            if (p) {
                /* LED pulse */
                gpio_set_level((i==POOL_SMALL)?LED_SMALL_POOL:(i==POOL_MEDIUM)?LED_MEDIUM_POOL:LED_LARGE_POOL, 1);
                vTaskDelay(pdMS_TO_TICKS(30));
                gpio_set_level((i==POOL_SMALL)?LED_SMALL_POOL:(i==POOL_MEDIUM)?LED_MEDIUM_POOL:LED_LARGE_POOL, 0);
                return p;
            }
        }
    }
    ESP_LOGW(TAG, "no suitable pool for %uB -> fallback heap", (unsigned)size);
    return heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}

static bool smart_pool_free(void* ptr)
{
    if (!ptr) return false;
    for (int i = 0; i < POOL_COUNT; i++) {
        if (pool_free(&pools[i], ptr)) return true;
    }
    heap_caps_free(ptr); // fallback
    return true;
}

/* ---------------- Monitoring ---------------- */
static void print_pool_statistics(void)
{
    ESP_LOGI(TAG, "\n📊 POOL STATS");
    for (int i = 0; i < POOL_COUNT; i++) {
        memory_pool_t* p = &pools[i];
        if (!p->mutex) continue;
        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "%s: used %u/%u (peak %u) fail %u alloc %llu free %llu",
                     p->name,
                     (unsigned)p->allocated_blocks, (unsigned)p->block_count,
                     (unsigned)p->peak_usage, (unsigned)p->allocation_failures,
                     p->total_allocations, p->total_deallocations);
            xSemaphoreGive(p->mutex);
        }
    }
}

static void visualize_pool_usage(void)
{
    char bar[33]; bar[32]='\0';
    for (int i = 0; i < POOL_COUNT; i++) {
        memory_pool_t* p = &pools[i];
        if (!p->mutex) continue;
        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            int filled = (p->block_count==0) ? 0 : (int)( (p->allocated_blocks * 32) / p->block_count );
            for (int j=0;j<32;j++) bar[j] = (j<filled)?'█':'░';
            ESP_LOGI(TAG, "%s: [%s] %u/%u", p->name, bar, (unsigned)p->allocated_blocks, (unsigned)p->block_count);
            xSemaphoreGive(p->mutex);
        }
    }
}

/* ---------------- Tasks (short & safe) ---------------- */
static void pool_monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        print_pool_statistics();
        visualize_pool_usage();

        bool exhausted = false;
        for (int i=0;i<POOL_COUNT;i++) {
            if (pools[i].block_count && pools[i].allocated_blocks >= pools[i].block_count) {
                exhausted = true; break;
            }
        }
        gpio_set_level(LED_POOL_FULL, exhausted ? 1 : 0);

        ESP_LOGI(TAG, "Free heap: %u bytes", (unsigned)esp_get_free_heap_size());
    }
}

static void pool_stress_test_task(void *arg)
{
    void* ptrs[64]={0};
    size_t sizes[64]={0};
    int n=0;

    while (1) {
        int action = (int)(esp_random()%3);

        if (action==0 && n<64) {
            size_t sz = 16 + (esp_random()%1536); // 16..1551
            void* p = smart_pool_malloc(sz);
            if (p) {
                memset(p, 0xAA, sz);
                ptrs[n]=p; sizes[n]=sz; n++;
            }
        } else if (action==1 && n>0) {
            int idx = (int)(esp_random()%n);
            if (ptrs[idx]) {
                // quick verify
                uint8_t* d=(uint8_t*)ptrs[idx];
                bool ok=true;
                for (size_t i=0;i<sizes[idx];i++){ if (d[i]!=0xAA){ ok=false;break; } }
                if (!ok) { ESP_LOGE(TAG, "🚨 data corruption detected"); gpio_set_level(LED_POOL_ERROR,1); }
                smart_pool_free(ptrs[idx]);
                // compact
                for (int j=idx;j<n-1;j++){ ptrs[j]=ptrs[j+1]; sizes[j]=sizes[j+1]; }
                n--;
            }
        } else {
            print_pool_statistics();
        }

        vTaskDelay(pdMS_TO_TICKS(400 + (esp_random()%600)));
    }
}

static void pool_perf_task(void *arg)
{
    const int N=400;
    const size_t sizes[] = {32,128,512,2048};
    while (1) {
        ESP_LOGI(TAG, "⚡ benchmark start");
        for (int s=0;s<4;s++) {
            size_t sz=sizes[s];
            uint64_t t0=esp_timer_get_time();
            void* a[N];
            for (int i=0;i<N;i++) a[i]=smart_pool_malloc(sz);
            uint64_t t1=esp_timer_get_time();
            for (int i=0;i<N;i++) if (a[i]) smart_pool_free(a[i]);
            uint64_t t2=esp_timer_get_time();
            ESP_LOGI(TAG, "size %u: alloc %.2f us/obj, free %.2f us/obj",
                     (unsigned)sz,
                     (double)(t1-t0)/N, (double)(t2-t1)/N);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* ---------------- App init ---------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "🚀 Memory Pools Lab Starting...");

    // GPIO
    gpio_set_direction(LED_SMALL_POOL,  GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_MEDIUM_POOL, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_LARGE_POOL,  GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_POOL_FULL,   GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_POOL_ERROR,  GPIO_MODE_OUTPUT);
    gpio_set_level(LED_SMALL_POOL, 0);
    gpio_set_level(LED_MEDIUM_POOL,0);
    gpio_set_level(LED_LARGE_POOL, 0);
    gpio_set_level(LED_POOL_FULL,  0);
    gpio_set_level(LED_POOL_ERROR, 0);

    /* ตรวจ PSRAM */
    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    bool has_psram = spiram_free > 0;
    ESP_LOGI(TAG, "PSRAM: %s (free=%u bytes)", has_psram? "YES":"NO", (unsigned)spiram_free);

    /* config พูล (huge pool จะใช้ SPIRAM ถ้ามี) */
    pool_config_t cfgs[POOL_COUNT] = {
        { "Small",  SMALL_POOL_BLOCK_SIZE,  SMALL_POOL_BLOCK_COUNT,  MALLOC_CAP_INTERNAL, LED_SMALL_POOL  },
        { "Medium", MEDIUM_POOL_BLOCK_SIZE, MEDIUM_POOL_BLOCK_COUNT, MALLOC_CAP_INTERNAL, LED_MEDIUM_POOL },
        { "Large",  LARGE_POOL_BLOCK_SIZE,  LARGE_POOL_BLOCK_COUNT,  MALLOC_CAP_DEFAULT,  LED_LARGE_POOL  },
        { "Huge",   HUGE_POOL_BLOCK_SIZE,   HUGE_POOL_BLOCK_COUNT,   has_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT, LED_POOL_FULL }
    };

    /* ถ้าไม่มี PSRAM และ heap ค่อนข้างจำกัด ให้ลด huge ลงไปอีก */
    if (!has_psram) {
        cfgs[POOL_HUGE].block_count = 1;  // ป้องกัน OOM
        ESP_LOGW(TAG, "No PSRAM -> limiting Huge pool to %u block", (unsigned)cfgs[POOL_HUGE].block_count);
    }

    /* init pools แบบ safe (จะลด block_count ลงถ้าไม่พอ) */
    for (int i=0;i<POOL_COUNT;i++) {
        if (!init_memory_pool_safely(&pools[i], &cfgs[i], (uint32_t)(i+1))) {
            ESP_LOGE(TAG, "Failed to init %s pool — continuing without it", cfgs[i].name);
            // ปล่อยว่าง pool นี้ (smart allocator จะตกไป heap ปกติ)
        }
    }

    print_pool_statistics();

    // tasks
    xTaskCreate(pool_monitor_task,     "PoolMonitor", 4096, NULL, 5, NULL);
    xTaskCreate(pool_stress_test_task, "PoolStress",  4096, NULL, 5, NULL);
    xTaskCreate(pool_perf_task,        "PoolPerf",    4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "\n🎯 LEDs:");
    ESP_LOGI(TAG, "  GPIO2  Small (64B) activity");
    ESP_LOGI(TAG, "  GPIO4  Medium (256B) activity");
    ESP_LOGI(TAG, "  GPIO5  Large (1KB) activity");
    ESP_LOGI(TAG, "  GPIO18 Pool FULL");
    ESP_LOGI(TAG, "  GPIO19 Pool ERROR");
    ESP_LOGI(TAG, "✅ Memory Pool System operational");
}
