#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ==== GPIO Definitions ====
#define LED_RUNNING   GPIO_NUM_2
#define LED_READY     GPIO_NUM_4
#define LED_BLOCKED   GPIO_NUM_5
#define LED_SUSPENDED GPIO_NUM_18

#define BUTTON1_PIN GPIO_NUM_0   // Suspend / Resume
#define BUTTON2_PIN GPIO_NUM_34  // Give Semaphore (เปลี่ยนจาก 35 เพื่อหลีกเลี่ยง warning)

// ==== Globals ====
static const char *TAG = "TASK_STATES";
SemaphoreHandle_t demo_semaphore = NULL;
TaskHandle_t state_demo_task_handle = NULL;
TaskHandle_t control_task_handle = NULL;

volatile uint32_t state_changes[5] = {0}; // Running, Ready, Blocked, Suspended, Deleted

// ==== State Names ====
const char* state_names[] = {"Running", "Ready", "Blocked", "Suspended", "Deleted", "Invalid"};

const char* get_state_name(eTaskState state) {
    if (state <= eDeleted) return state_names[state];
    return state_names[5];
}

// ==== Exercise 1: State Counter ====
void count_state_change(eTaskState old_state, eTaskState new_state) {
    if (old_state != new_state && new_state <= eDeleted) {
        state_changes[new_state]++;
        ESP_LOGI(TAG, "State change: %s → %s (count=%d)",
                 get_state_name(old_state),
                 get_state_name(new_state),
                 state_changes[new_state]);
    }
}

// ==== Exercise 2: Custom LED Indicator ====
void update_state_display(eTaskState current_state) {
    gpio_set_level(LED_RUNNING, 0);
    gpio_set_level(LED_READY, 0);
    gpio_set_level(LED_BLOCKED, 0);
    gpio_set_level(LED_SUSPENDED, 0);

    switch (current_state) {
        case eRunning:
            gpio_set_level(LED_RUNNING, 1);
            break;
        case eReady:
            gpio_set_level(LED_READY, 1);
            break;
        case eBlocked:
            gpio_set_level(LED_BLOCKED, 1);
            break;
        case eSuspended:
            gpio_set_level(LED_SUSPENDED, 1);
            break;
        default:
            for (int i = 0; i < 2; i++) {
                gpio_set_level(LED_RUNNING, 1);
                gpio_set_level(LED_READY, 1);
                gpio_set_level(LED_BLOCKED, 1);
                gpio_set_level(LED_SUSPENDED, 1);
                vTaskDelay(pdMS_TO_TICKS(150));
                gpio_set_level(LED_RUNNING, 0);
                gpio_set_level(LED_READY, 0);
                gpio_set_level(LED_BLOCKED, 0);
                gpio_set_level(LED_SUSPENDED, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            break;
    }
}

// ==== Main Task ====
void state_demo_task(void *pvParameters) {
    ESP_LOGI(TAG, "State Demo Task started");

    eTaskState last_state = eReady;

    while (1) {
        // ตรวจสอบ state อย่างปลอดภัย
        eTaskState current_state = eInvalid;
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED && state_demo_task_handle != NULL) {
            current_state = eTaskGetState(state_demo_task_handle);
        }

        if (current_state == eInvalid) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        count_state_change(last_state, current_state);
        update_state_display(current_state);
        ESP_LOGI(TAG, "Task now in state: %s", get_state_name(current_state));

        // ทำงานจำลอง
        for (int i = 0; i < 300000; i++) { volatile int dummy = i * 3; }

        // ทดสอบ blocked state
        ESP_LOGI(TAG, "Waiting for semaphore (simulate Blocked)...");
        if (xSemaphoreTake(demo_semaphore, pdMS_TO_TICKS(3000)) == pdTRUE) {
            ESP_LOGI(TAG, "Semaphore received!");
        } else {
            ESP_LOGI(TAG, "Timeout waiting for semaphore...");
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(2000)); // slow down cycle
    }
}

// ==== Ready State Demo ====
void ready_state_demo_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Ready-state demo running...");
        for (int i = 0; i < 100000; i++) { volatile int dummy = i; }
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

// ==== Control Task ====
void control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Control Task started");
    bool suspended = false;

    while (1) {
        // ปุ่ม Suspend/Resume
        if (gpio_get_level(BUTTON1_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100)); // debounce
            if (!suspended) {
                ESP_LOGW(TAG, ">>> Suspending Demo Task");
                vTaskSuspend(state_demo_task_handle);
                gpio_set_level(LED_SUSPENDED, 1);
                suspended = true;
            } else {
                ESP_LOGW(TAG, ">>> Resuming Demo Task");
                vTaskResume(state_demo_task_handle);
                gpio_set_level(LED_SUSPENDED, 0);
                suspended = false;
            }
            while (gpio_get_level(BUTTON1_PIN) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }

        // ปุ่ม Semaphore
        if (gpio_get_level(BUTTON2_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, ">>> Giving Semaphore");
            xSemaphoreGive(demo_semaphore);
            while (gpio_get_level(BUTTON2_PIN) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ==== Monitor Task ====
void monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "System Monitor started");

    char *task_list_buffer = malloc(1024);
    char *stats_buffer = malloc(1024);

    while (1) {
        ESP_LOGI(TAG, "\n=== SYSTEM MONITOR ===");

        vTaskList(task_list_buffer);
        ESP_LOGI(TAG, "Name\t\tState\tPrio\tStack\tNum");
        ESP_LOGI(TAG, "%s", task_list_buffer);

        vTaskGetRunTimeStats(stats_buffer);
        ESP_LOGI(TAG, "\nRuntime Stats:");
        ESP_LOGI(TAG, "Task\t\tAbs Time\t%%Time");
        ESP_LOGI(TAG, "%s", stats_buffer);

        vTaskDelay(pdMS_TO_TICKS(7000));
    }
}

// ==== app_main ====
void app_main(void) {
    ESP_LOGI(TAG, "=== FreeRTOS Task States Demo (Slow & Safe) ===");

    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<LED_RUNNING)|(1ULL<<LED_READY)|(1ULL<<LED_BLOCKED)|(1ULL<<LED_SUSPENDED),
        .pull_down_en = 0, .pull_up_en = 0
    };
    gpio_config(&io_conf);

    gpio_config_t button_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL<<BUTTON1_PIN)|(1ULL<<BUTTON2_PIN),
        .pull_up_en = 1, .pull_down_en = 0
    };
    gpio_config(&button_conf);

    demo_semaphore = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "GPIO2=RUNNING | GPIO4=READY | GPIO5=BLOCKED | GPIO18=SUSPENDED");
    ESP_LOGI(TAG, "Button1(GPIO0)=Suspend/Resume | Button2(GPIO34)=Give Semaphore");

    xTaskCreate(state_demo_task, "StateDemo", 4096, NULL, 3, &state_demo_task_handle);
    xTaskCreate(ready_state_demo_task, "ReadyDemo", 2048, NULL, 3, NULL);
    xTaskCreate(control_task, "Control", 3072, NULL, 4, &control_task_handle);
    xTaskCreate(monitor_task, "Monitor", 4096, NULL, 1, NULL);
}
