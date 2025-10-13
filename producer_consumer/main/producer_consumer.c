#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "PROD_CONS";

// ==== LED Pin Mapping ====
#define LED_PRODUCER_1 GPIO_NUM_2
#define LED_PRODUCER_2 GPIO_NUM_4
#define LED_PRODUCER_3 GPIO_NUM_5
#define LED_PRODUCER_4 GPIO_NUM_21   // ✅ เพิ่ม Producer คนที่ 4
#define LED_CONSUMER_1 GPIO_NUM_18
#define LED_CONSUMER_2 GPIO_NUM_19

// ==== Queue & Mutex ====
QueueHandle_t xProductQueue;
SemaphoreHandle_t xPrintMutex; // สำหรับ print ปลอดภัยข้าม task

// ==== สถิติระบบ ====
typedef struct {
    uint32_t produced;
    uint32_t consumed;
    uint32_t dropped;
} stats_t;

stats_t global_stats = {0, 0, 0};

// ==== โครงสร้างสินค้า ====
typedef struct {
    int producer_id;
    int product_id;
    char product_name[30];
    uint32_t production_time;
    int processing_time_ms;
} product_t;

// ==== ฟังก์ชันพิมพ์แบบปลอดภัย ====
void safe_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (xSemaphoreTake(xPrintMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        vprintf(format, args);
        xSemaphoreGive(xPrintMutex);
    }
    va_end(args);
}

// ==== Producer Task ====
void producer_task(void *pvParameters) {
    int producer_id = *((int*)pvParameters);
    product_t product;
    int product_counter = 0;
    gpio_num_t led_pin;

    // Mapping LED สำหรับแต่ละ Producer
    switch (producer_id) {
        case 1: led_pin = LED_PRODUCER_1; break;
        case 2: led_pin = LED_PRODUCER_2; break;
        case 3: led_pin = LED_PRODUCER_3; break;
        case 4: led_pin = LED_PRODUCER_4; break;
        default: led_pin = LED_PRODUCER_1;
    }

    safe_printf("Producer %d started\n", producer_id);

    while (1) {
        product.producer_id = producer_id;
        product.product_id = product_counter++;
        snprintf(product.product_name, sizeof(product.product_name),
                 "Product-P%d-#%d", producer_id, product.product_id);
        product.production_time = xTaskGetTickCount();
        product.processing_time_ms = 500 + (esp_random() % 2000);

        BaseType_t xStatus = xQueueSend(xProductQueue, &product, pdMS_TO_TICKS(100));

        if (xStatus == pdPASS) {
            global_stats.produced++;
            safe_printf("✓ Producer %d: Created %s (proc: %d ms)\n",
                        producer_id, product.product_name, product.processing_time_ms);
            gpio_set_level(led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_level(led_pin, 0);
        } else {
            global_stats.dropped++;
            safe_printf("✗ Producer %d: Queue full → Dropped %s\n",
                        producer_id, product.product_name);
        }

        int delay = 1000 + (esp_random() % 2000); // 1–3 s
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

// ==== Consumer Task ====
void consumer_task(void *pvParameters) {
    int consumer_id = *((int*)pvParameters);
    product_t product;
    gpio_num_t led_pin = (consumer_id == 1) ? LED_CONSUMER_1 : LED_CONSUMER_2;

    safe_printf("Consumer %d started\n", consumer_id);

    while (1) {
        BaseType_t xStatus = xQueueReceive(xProductQueue, &product, pdMS_TO_TICKS(5000));

        if (xStatus == pdPASS) {
            global_stats.consumed++;
            uint32_t queue_time = xTaskGetTickCount() - product.production_time;
            safe_printf("→ Consumer %d: Processing %s (wait %lums)\n",
                        consumer_id, product.product_name, queue_time * portTICK_PERIOD_MS);

            gpio_set_level(led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(product.processing_time_ms));
            gpio_set_level(led_pin, 0);

            safe_printf("✓ Consumer %d: Finished %s\n",
                        consumer_id, product.product_name);
        } else {
            safe_printf("⏰ Consumer %d: Timeout waiting for product\n", consumer_id);
        }
    }
}

// ==== Statistics Task ====
void statistics_task(void *pvParameters) {
    safe_printf("Statistics task started\n");

    while (1) {
        UBaseType_t queue_items = uxQueueMessagesWaiting(xProductQueue);
        safe_printf("\n═══ SYSTEM STATISTICS ═══\n");
        safe_printf("Produced : %lu\n", global_stats.produced);
        safe_printf("Consumed : %lu\n", global_stats.consumed);
        safe_printf("Dropped  : %lu\n", global_stats.dropped);
        safe_printf("Queue    : %d items\n", queue_items);
        safe_printf("Efficiency : %.1f %%\n",
                    global_stats.produced > 0 ?
                    (float)global_stats.consumed / global_stats.produced * 100 : 0);

        printf("Queue: [");
        for (int i = 0; i < 10; i++)
            printf(i < queue_items ? "■" : "□");
        printf("]\n═══════════════════════════\n\n");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ==== Load Balancer Task ====
void load_balancer_task(void *pvParameters) {
    const int MAX_QUEUE_SIZE = 8;
    safe_printf("Load balancer started\n");

    while (1) {
        UBaseType_t queue_items = uxQueueMessagesWaiting(xProductQueue);
        if (queue_items > MAX_QUEUE_SIZE) {
            safe_printf("⚠️  HIGH LOAD → Queue = %d / 10\n", queue_items);
            safe_printf("💡 Suggest adding more consumers!\n");

            gpio_set_level(LED_PRODUCER_1, 1);
            gpio_set_level(LED_PRODUCER_2, 1);
            gpio_set_level(LED_PRODUCER_3, 1);
            gpio_set_level(LED_PRODUCER_4, 1);
            gpio_set_level(LED_CONSUMER_1, 1);
            gpio_set_level(LED_CONSUMER_2, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_PRODUCER_1, 0);
            gpio_set_level(LED_PRODUCER_2, 0);
            gpio_set_level(LED_PRODUCER_3, 0);
            gpio_set_level(LED_PRODUCER_4, 0);
            gpio_set_level(LED_CONSUMER_1, 0);
            gpio_set_level(LED_CONSUMER_2, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ==== app_main ====
void app_main(void) {
    ESP_LOGI(TAG, "Producer–Consumer Lab (Experiment 2 & 3)");

    // ตั้งค่า GPIO ทั้งหมด
    gpio_set_direction(LED_PRODUCER_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PRODUCER_2, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PRODUCER_3, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PRODUCER_4, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_CONSUMER_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_CONSUMER_2, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PRODUCER_1, 0);
    gpio_set_level(LED_PRODUCER_2, 0);
    gpio_set_level(LED_PRODUCER_3, 0);
    gpio_set_level(LED_PRODUCER_4, 0);
    gpio_set_level(LED_CONSUMER_1, 0);
    gpio_set_level(LED_CONSUMER_2, 0);

    // สร้าง Queue และ Mutex
    xProductQueue = xQueueCreate(10, sizeof(product_t));
    xPrintMutex = xSemaphoreCreateMutex();

    if (xProductQueue == NULL || xPrintMutex == NULL) {
        ESP_LOGE(TAG, "❌ Failed to create Queue/Mutex!");
        return;
    }

    ESP_LOGI(TAG, "✅ Queue & Mutex created.");

    // IDs สำหรับ Task ทั้งหมด
    static int producer1_id = 1, producer2_id = 2, producer3_id = 3, producer4_id = 4;
    static int consumer1_id = 1, consumer2_id = 2;

    // ==== Producers (ทดลอง 2 เพิ่ม คนที่ 4) ====
    xTaskCreate(producer_task, "Producer1", 3072, &producer1_id, 3, NULL);
    xTaskCreate(producer_task, "Producer2", 3072, &producer2_id, 3, NULL);
    xTaskCreate(producer_task, "Producer3", 3072, &producer3_id, 3, NULL);
    xTaskCreate(producer_task, "Producer4", 3072, &producer4_id, 3, NULL); // ✅ เพิ่มใหม่

    // ==== Consumers (ทดลอง 3 : ปิด Consumer 2 ชั่วคราว) ====
    xTaskCreate(consumer_task, "Consumer1", 3072, &consumer1_id, 2, NULL);
    // ❌ ปิด Consumer 2 เพื่อดูผลเมื่อผู้บริโภคลดลง
    // xTaskCreate(consumer_task, "Consumer2", 3072, &consumer2_id, 2, NULL);

    // ==== Monitoring Tasks ====
    xTaskCreate(statistics_task, "Statistics", 4096, NULL, 1, NULL);
    xTaskCreate(load_balancer_task, "LoadBalancer", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "System ready: 4 Producers + 1 Consumer (Experiment 2 & 3).");
}
