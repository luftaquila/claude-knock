#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define NVS_NAMESPACE "ck_config"

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char mqtt_server[128];
    char mqtt_channel[65];
    char mqtt_username[26];
    char mqtt_password[65];
    bool configured;
} device_config_t;

esp_err_t config_load(device_config_t *cfg);
esp_err_t config_save(const device_config_t *cfg);
esp_err_t config_clear(void);
void config_generate_username(char *out, size_t len);
