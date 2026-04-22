#include "solenoid.h"
#include "config.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define SOLENOID_PWM_FREQ_HZ   5000
#define SOLENOID_PWM_TIMER     LEDC_TIMER_0
#define SOLENOID_PWM_CHANNEL   LEDC_CHANNEL_0
#define SOLENOID_PWM_RES_BITS  LEDC_TIMER_10_BIT
#define SOLENOID_PWM_DUTY_MAX  1023

static const char *TAG = "solenoid";
static esp_timer_handle_t s_led_timer = NULL;
static status_mode_t s_status = STATUS_AP_MODE;
static QueueHandle_t s_pulse_queue = NULL;
static uint8_t s_strength = STRENGTH_DEFAULT;
static uint8_t s_strength2 = STRENGTH_DEFAULT;
static uint16_t s_delay_ms = DELAY_MS_DEFAULT;
static uint8_t s_boost_ms = BOOST_MS_DEFAULT;
static uint8_t s_hold_duty = HOLD_DUTY_DEFAULT;

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

/* Solenoid pulse task — reads from queue to serialize pulses.
 *
 * Each pulse has two phases, both driven via PWM (LEDC):
 *   boost : s_boost_ms at 100% duty (kick plunger past static threshold)
 *   hold  : s_strength (1st) / s_strength2 (2nd+) ms at s_hold_duty % (reduced accel → lower impact velocity)
 *
 * Total pulse length = boost_ms + hold_ms. Either phase may be zero.
 */
static void pulse_task(void *arg)
{
    int count;
    while (1) {
        if (xQueueReceive(s_pulse_queue, &count, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < count; i++) {
                uint8_t boost = s_boost_ms;
                uint8_t hold = (i == 0) ? s_strength : s_strength2;
                uint32_t hold_raw = (uint32_t)s_hold_duty * SOLENOID_PWM_DUTY_MAX / 100;

                if (boost > 0) {
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, SOLENOID_PWM_CHANNEL, SOLENOID_PWM_DUTY_MAX);
                    ledc_update_duty(LEDC_LOW_SPEED_MODE, SOLENOID_PWM_CHANNEL);
                    vTaskDelay(pdMS_TO_TICKS(boost));
                }
                if (hold > 0 && hold_raw > 0) {
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, SOLENOID_PWM_CHANNEL, hold_raw);
                    ledc_update_duty(LEDC_LOW_SPEED_MODE, SOLENOID_PWM_CHANNEL);
                    vTaskDelay(pdMS_TO_TICKS(hold));
                }
                if (boost > 0 || (hold > 0 && hold_raw > 0)) {
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, SOLENOID_PWM_CHANNEL, 0);
                    ledc_update_duty(LEDC_LOW_SPEED_MODE, SOLENOID_PWM_CHANNEL);
                }
                if (i < count - 1) {
                    vTaskDelay(pdMS_TO_TICKS(s_delay_ms));
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

    /* Configure LEDC PWM for solenoid drive */
    ledc_timer_config_t timer_cfg = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = SOLENOID_PWM_TIMER,
        .duty_resolution  = SOLENOID_PWM_RES_BITS,
        .freq_hz          = SOLENOID_PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chan_cfg = {
        .gpio_num       = GPIO_SOLENOID,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = SOLENOID_PWM_CHANNEL,
        .timer_sel      = SOLENOID_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .duty           = 0,
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));

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

    ESP_LOGI(TAG, "Solenoid GPIO=%d (PWM %dHz), LED GPIO=%d, Reset GPIO=%d",
             GPIO_SOLENOID, SOLENOID_PWM_FREQ_HZ, GPIO_STATUS_LED, GPIO_RESET_BUTTON);
}

void solenoid_pulse(int count)
{
    if (count < 1) count = 1;
    if (count > 10) count = 10;
    ESP_LOGI(TAG, "Pulse x%d (first=%ums, rest=%ums)", count, s_strength, s_strength2);
    xQueueSend(s_pulse_queue, &count, 0);
}

void solenoid_set_status(status_mode_t mode)
{
    s_status = mode;
    update_led_timer();
}

void solenoid_set_strength(uint8_t percent)
{
    if (percent > STRENGTH_MAX) percent = STRENGTH_MAX;
    s_strength = percent;
    ESP_LOGI(TAG, "Pulse (first) set to %u ms", s_strength);
}

void solenoid_set_strength2(uint8_t percent)
{
    if (percent > STRENGTH_MAX) percent = STRENGTH_MAX;
    s_strength2 = percent;
    ESP_LOGI(TAG, "Pulse (rest) set to %u ms", s_strength2);
}

void solenoid_set_delay(uint16_t ms)
{
    if (ms < DELAY_MS_MIN) ms = DELAY_MS_MIN;
    if (ms > DELAY_MS_MAX) ms = DELAY_MS_MAX;
    s_delay_ms = ms;
    ESP_LOGI(TAG, "Delay set to %u ms", s_delay_ms);
}

void solenoid_set_boost(uint8_t ms)
{
    if (ms > BOOST_MS_MAX) ms = BOOST_MS_MAX;
    s_boost_ms = ms;
    ESP_LOGI(TAG, "Boost set to %u ms", s_boost_ms);
}

void solenoid_set_hold(uint8_t duty_percent)
{
    if (duty_percent > HOLD_DUTY_MAX) duty_percent = HOLD_DUTY_MAX;
    s_hold_duty = duty_percent;
    ESP_LOGI(TAG, "Hold duty set to %u%%", s_hold_duty);
}
