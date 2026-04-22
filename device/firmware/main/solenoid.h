#pragma once

#include <stdint.h>

#include "esp_err.h"

#define GPIO_SOLENOID     6
#define GPIO_STATUS_LED   8
#define GPIO_RESET_BUTTON 9

#define RESET_HOLD_MS      3000

typedef enum {
    STATUS_AP_MODE,
    STATUS_CONNECTING,
    STATUS_NORMAL,
} status_mode_t;

void solenoid_init(void);
void solenoid_pulse(int count);
void solenoid_set_status(status_mode_t mode);
void solenoid_set_strength(uint8_t percent);
void solenoid_set_strength2(uint8_t percent);
void solenoid_set_delay(uint16_t ms);
void solenoid_set_boost(uint8_t ms);
void solenoid_set_hold(uint8_t duty_percent);
