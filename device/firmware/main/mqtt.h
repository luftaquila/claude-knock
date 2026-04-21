#pragma once

#include "config.h"
#include "esp_err.h"

esp_err_t mqtt_start(const device_config_t *cfg);
void mqtt_stop(void);
