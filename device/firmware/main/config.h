#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define NVS_NAMESPACE "ck_config"

#define STRENGTH_MIN     0
#define STRENGTH_MAX     100
#define STRENGTH_DEFAULT 80

#define DELAY_MS_MIN     50
#define DELAY_MS_MAX     1000
#define DELAY_MS_DEFAULT 200

#define BOOST_MS_MIN     0
#define BOOST_MS_MAX     30
#define BOOST_MS_DEFAULT 0

#define HOLD_DUTY_MIN     0
#define HOLD_DUTY_MAX     100
#define HOLD_DUTY_DEFAULT 100

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char mqtt_server[128];
    char mqtt_channel[65];
    char mqtt_username[26];
    char mqtt_password[65];
    uint8_t strength;
    uint8_t strength2;
    uint16_t delay_ms;
    uint8_t boost_ms;
    uint8_t hold_duty;
    bool configured;
} device_config_t;

esp_err_t config_load(device_config_t *cfg);
esp_err_t config_save(const device_config_t *cfg);
esp_err_t config_save_strength(uint8_t strength);
esp_err_t config_save_strength2(uint8_t strength);
esp_err_t config_save_delay(uint16_t delay_ms);
esp_err_t config_save_boost(uint8_t boost_ms);
esp_err_t config_save_hold(uint8_t hold_duty);
esp_err_t config_clear(void);
void config_generate_username(char *out, size_t len);
