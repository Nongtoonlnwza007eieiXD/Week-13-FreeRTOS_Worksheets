#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "SW_TIMERS";

// LED pins
#define LED_BLINK GPIO_NUM_2
#define LED_HEARTBEAT GPIO_NUM_4
#define LED_STATUS GPIO_NUM_5
#define LED_ONESHOT GPIO_NUM_18

// Timer handles
TimerHandle_t xBlinkTimer;
TimerHandle_t xHeartbeatTimer;
TimerHandle_t xStatusTimer;
TimerHandle_t xOneShotTimer;
TimerHandle_t xDynamicTimer;

// Timer periods
#define BLINK_PERIOD     500
#define HEARTBEAT_PERIOD 2000
#define STATUS_PERIOD    5000
#define ONESHOT_DELAY    3000

// Statistics
typedef struct {
    uint32_t blink_count;
    uint32_t heartbeat_count;
    uint32_t status_count;
    uint32_t oneshot_count;
    uint32_t dynamic_count;
    uint32_t extra_count;
} timer_stats_t;

timer_stats_t stats = {0, 0, 0, 0, 0, 0};
bool led_blink_state = false;

// ==== TIMER CALLBACKS ====

void blink_timer_callback(TimerHandle_t xTimer) {
    stats.blink_count++;
    led_blink_state = !led_blink_state;
    gpio_set_level(LED_BLINK, led_blink_state);
    ESP_LOGI(TAG, "💫 Blink Timer: Toggle #%lu (%s)", stats.blink_count, led_blink_state ? "ON" : "OFF");

    if (stats.blink_count % 20 == 0) {
        ESP_LOGI(TAG, "🚀 Starting one-shot timer...");
        xTimerStart(xOneShotTimer, 0);
    }
}

void heartbeat_timer_callback(TimerHandle_t xTimer) {
    stats.heartbeat_count++;
    ESP_LOGI(TAG, "💓 Heartbeat Timer: Beat #%lu", stats.heartbeat_count);

    gpio_set_level(LED_HEARTBEAT, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LED_HEARTBEAT, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LED_HEARTBEAT, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LED_HEARTBEAT, 0);
}

void status_timer_callback(TimerHandle_t xTimer) {
    stats.status_count++;
    ESP_LOGI(TAG, "📊 Status Timer: Update #%lu", stats.status_count);

    gpio_set_level(LED_STATUS, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_STATUS, 0);

    ESP_LOGI(TAG, "═══ TIMER STATS ═══");
    ESP_LOGI(TAG, "Blink: %lu | Heartbeat: %lu | Status: %lu | One-shot: %lu | Dynamic: %lu | Extra: %lu",
             stats.blink_count, stats.heartbeat_count, stats.status_count,
             stats.oneshot_count, stats.dynamic_count, stats.extra_count);
}

void dynamic_timer_callback(TimerHandle_t xTimer) {
    stats.dynamic_count++;
    ESP_LOGI(TAG, "🌟 Dynamic Timer Event #%lu", stats.dynamic_count);

    gpio_set_level(LED_BLINK, 1);
    gpio_set_level(LED_HEARTBEAT, 1);
    gpio_set_level(LED_STATUS, 1);
    gpio_set_level(LED_ONESHOT, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_BLINK, led_blink_state);
    gpio_set_level(LED_HEARTBEAT, 0);
    gpio_set_level(LED_STATUS, 0);
    gpio_set_level(LED_ONESHOT, 0);

    xTimerDelete(xTimer, 100);
}

void oneshot_timer_callback(TimerHandle_t xTimer) {
    stats.oneshot_count++;
    ESP_LOGI(TAG, "⚡ One-shot Timer Event #%lu", stats.oneshot_count);

    for (int i = 0; i < 5; i++) {
        gpio_set_level(LED_ONESHOT, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_ONESHOT, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uint32_t period = 1000 + (esp_random() % 2000);
    xDynamicTimer = xTimerCreate("DynamicTimer", pdMS_TO_TICKS(period),
                                 pdFALSE, NULL, dynamic_timer_callback);
    if (xDynamicTimer) xTimerStart(xDynamicTimer, 0);
}

// ==== EXTRA LOAD TIMER CALLBACK ====
void extra_timer_callback(TimerHandle_t xTimer) {
    stats.extra_count++;
    int id = (int)pvTimerGetTimerID(xTimer);
    ESP_LOGI(TAG, "🧩 ExtraTimer #%d fired! Count=%lu", id, stats.extra_count);
}

// ==== CONTROL TASK ====
void timer_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Timer Control Task Running...");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        ESP_LOGI(TAG, "🎛️ Maintenance: Random Timer Adjustment");

        switch (esp_random() % 3) {
            case 0:
                ESP_LOGI(TAG, "⏸️ Pause heartbeat for 5s");
                xTimerStop(xHeartbeatTimer, 100);
                vTaskDelay(pdMS_TO_TICKS(5000));
                xTimerStart(xHeartbeatTimer, 100);
                break;
            case 1:
                xTimerReset(xStatusTimer, 100);
                ESP_LOGI(TAG, "🔁 Reset Status Timer");
                break;
            case 2: {
                uint32_t new_period = 300 + (esp_random() % 500);
                xTimerChangePeriod(xBlinkTimer, pdMS_TO_TICKS(new_period), 100);
                ESP_LOGI(TAG, "⚙️ Blink Timer changed to %lums", new_period);
                break;
            }
        }
    }
}

// ==== MAIN ====
void app_main(void) {
    ESP_LOGI(TAG, "Software Timers Lab Starting...");

    gpio_set_direction(LED_BLINK, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_HEARTBEAT, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_STATUS, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ONESHOT, GPIO_MODE_OUTPUT);

    gpio_set_level(LED_BLINK, 0);
    gpio_set_level(LED_HEARTBEAT, 0);
    gpio_set_level(LED_STATUS, 0);
    gpio_set_level(LED_ONESHOT, 0);

    // Create main timers
    xBlinkTimer = xTimerCreate("Blink", pdMS_TO_TICKS(BLINK_PERIOD), pdTRUE, NULL, blink_timer_callback);
    xHeartbeatTimer = xTimerCreate("Heartbeat", pdMS_TO_TICKS(HEARTBEAT_PERIOD), pdTRUE, NULL, heartbeat_timer_callback);
    xStatusTimer = xTimerCreate("Status", pdMS_TO_TICKS(STATUS_PERIOD), pdTRUE, NULL, status_timer_callback);
    xOneShotTimer = xTimerCreate("OneShot", pdMS_TO_TICKS(ONESHOT_DELAY), pdFALSE, NULL, oneshot_timer_callback);

    if (xBlinkTimer && xHeartbeatTimer && xStatusTimer && xOneShotTimer) {
        ESP_LOGI(TAG, "✅ Timers created successfully");
        xTimerStart(xBlinkTimer, 0);
        xTimerStart(xHeartbeatTimer, 0);
        xTimerStart(xStatusTimer, 0);
        xTaskCreate(timer_control_task, "TimerCtrl", 2048, NULL, 2, NULL);

        // ==== EXPERIMENT 3: ADD EXTRA TIMERS ====
        ESP_LOGW(TAG, "🧪 Experiment 3: Adding 10 Extra Timers for load test...");
        for (int i = 0; i < 10; i++) {
            TimerHandle_t extra_timer = xTimerCreate("ExtraTimer",
                pdMS_TO_TICKS(100 + i * 50), pdTRUE, (void*)i, extra_timer_callback);
            if (extra_timer) {
                xTimerStart(extra_timer, 0);
            }
        }
        ESP_LOGI(TAG, "All extra timers started successfully!");
    } else {
        ESP_LOGE(TAG, "❌ Failed to create main timers! Check menuconfig (CONFIG_FREERTOS_USE_TIMERS=y)");
    }
}
                        