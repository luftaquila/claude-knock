#pragma once

#include "esp_err.h"

#define GPIO_SOLENOID     6
#define GPIO_STATUS_LED   8
#define GPIO_RESET_BUTTON 9

#define SOLENOID_PULSE_MS  100
#define SOLENOID_GAP_MS    200
#define RESET_HOLD_MS      3000

typedef enum {
    STATUS_AP_MODE,
    STATUS_CONNECTING,
    STATUS_NORMAL,
} status_mode_t;

void solenoid_init(void);
void solenoid_pulse(int count);
void solenoid_set_status(status_mode_t mode);
