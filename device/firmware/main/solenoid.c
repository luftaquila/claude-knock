#include "solenoid.h"
#include "config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "solenoid";
static esp_timer_handle_t s_led_timer = NULL;
static status_mode_t s_status = STATUS_AP_MODE;
static QueueHandle_t s_pulse_queue = NULL;

/* LED blink timer callback */
static void led_timer_cb(void *arg)
{
    static bool led_state = false;
    led_state = !led_state;
    gpio_set_level(GPIO_STATUS_LED, led_state ? 1 : 0);
}

static void update_led_timer(void)
{
    if (!s_led_timer) {
        return;
    }

    esp_timer_stop(s_led_timer);

    switch (s_status) {
    case STATUS_AP_MODE:
        /* 1 Hz blink (500ms toggle) */
        esp_timer_start_periodic(s_led_timer, 500000);
        break;
    case STATUS_CONNECTING:
        /* 4 Hz blink (125ms toggle) */
        esp_timer_start_periodic(s_led_timer, 125000);
        break;
    case STATUS_NORMAL:
        /* Solid on */
        gpio_set_level(GPIO_STATUS_LED, 1);
        break;
    }
}

/* Solenoid pulse task — reads from queue to serialize pulses */
static void pulse_task(void *arg)
{
    int count;
    while (1) {
        if (xQueueReceive(s_pulse_queue, &count, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < count; i++) {
                gpio_set_level(GPIO_SOLENOID, 1);
                vTaskDelay(pdMS_TO_TICKS(SOLENOID_PULSE_MS));
                gpio_set_level(GPIO_SOLENOID, 0);
                if (i < count - 1) {
                    vTaskDelay(pdMS_TO_TICKS(SOLENOID_GAP_MS));
                }
            }
        }
    }
}

/* Reset button monitoring task */
static void reset_monitor_task(void *arg)
{
    while (1) {
        /* Wait for button press (active low) */
        if (gpio_get_level(GPIO_RESET_BUTTON) == 0) {
            ESP_LOGI(TAG, "Reset button pressed, hold for %d ms...", RESET_HOLD_MS);
            vTaskDelay(pdMS_TO_TICKS(RESET_HOLD_MS));

            /* Check if still held */
            if (gpio_get_level(GPIO_RESET_BUTTON) == 0) {
                ESP_LOGW(TAG, "Factory reset triggered!");
                /* Blink LED rapidly to indicate reset */
                for (int i = 0; i < 6; i++) {
                    gpio_set_level(GPIO_STATUS_LED, i % 2);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                config_clear();
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void solenoid_init(void)
{
    /* Configure solenoid GPIO */
    gpio_config_t sol_conf = {
        .pin_bit_mask = (1ULL << GPIO_SOLENOID),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sol_conf);
    gpio_set_level(GPIO_SOLENOID, 0);

    /* Configure LED GPIO */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_STATUS_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    /* Configure reset button GPIO */
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << GPIO_RESET_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    /* Create LED blink timer */
    const esp_timer_create_args_t timer_args = {
        .callback = led_timer_cb,
        .name = "led_blink",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_led_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED timer: %s", esp_err_to_name(err));
    }

    /* Create pulse queue and task */
    s_pulse_queue = xQueueCreate(4, sizeof(int));
    xTaskCreate(pulse_task, "pulse", 2048, NULL, 10, NULL);

    /* Start reset button monitor */
    xTaskCreate(reset_monitor_task, "reset_mon", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Solenoid GPIO=%d, LED GPIO=%d, Reset GPIO=%d",
             GPIO_SOLENOID, GPIO_STATUS_LED, GPIO_RESET_BUTTON);
}

void solenoid_pulse(int count)
{
    if (count < 1) count = 1;
    if (count > 10) count = 10;
    ESP_LOGI(TAG, "Pulse x%d", count);
    xQueueSend(s_pulse_queue, &count, 0);
}

void solenoid_set_status(status_mode_t mode)
{
    s_status = mode;
    update_led_timer();
}
