#pragma once

#include "config.h"
#include "esp_err.h"

#define WIFI_AP_SSID "claude-knock"
#define WIFI_RETRY_MAX 10

esp_err_t wifi_start_ap(void);
esp_err_t wifi_start_sta(const device_config_t *cfg);
