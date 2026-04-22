#include "config.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";

void config_generate_username(char *out, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "claude-knock-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

esp_err_t config_load(device_config_t *cfg)
{
    memset(cfg, 0, sizeof(device_config_t));
    cfg->strength = STRENGTH_DEFAULT;
    cfg->strength2 = STRENGTH_DEFAULT;
    cfg->delay_ms = DELAY_MS_DEFAULT;
    cfg->boost_ms = BOOST_MS_DEFAULT;
    cfg->hold_duty = HOLD_DUTY_DEFAULT;
    config_generate_username(cfg->mqtt_username, sizeof(cfg->mqtt_username));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No config found in NVS");
        return ESP_ERR_NVS_NOT_FOUND;
    }

    /* Load tuning params first so they persist even if credentials are incomplete */
    uint8_t strength;
    if (nvs_get_u8(handle, "strength", &strength) == ESP_OK) {
        if (strength > STRENGTH_MAX) strength = STRENGTH_MAX;
        cfg->strength = strength;
        cfg->strength2 = strength;
    }
    if (nvs_get_u8(handle, "strength2", &strength) == ESP_OK) {
        if (strength > STRENGTH_MAX) strength = STRENGTH_MAX;
        cfg->strength2 = strength;
    }

    uint16_t delay_ms;
    if (nvs_get_u16(handle, "delay_ms", &delay_ms) == ESP_OK) {
        if (delay_ms < DELAY_MS_MIN) delay_ms = DELAY_MS_MIN;
        if (delay_ms > DELAY_MS_MAX) delay_ms = DELAY_MS_MAX;
        cfg->delay_ms = delay_ms;
    }

    uint8_t boost_ms;
    if (nvs_get_u8(handle, "boost_ms", &boost_ms) == ESP_OK) {
        if (boost_ms > BOOST_MS_MAX) boost_ms = BOOST_MS_MAX;
        cfg->boost_ms = boost_ms;
    }

    uint8_t hold_duty;
    if (nvs_get_u8(handle, "hold_duty", &hold_duty) == ESP_OK) {
        if (hold_duty > HOLD_DUTY_MAX) hold_duty = HOLD_DUTY_MAX;
        cfg->hold_duty = hold_duty;
    }

    /* Credentials — missing any of these leaves tuning intact but marks unconfigured */
    size_t len;

    len = sizeof(cfg->wifi_ssid);
    err = nvs_get_str(handle, "wifi_ssid", cfg->wifi_ssid, &len);
    if (err != ESP_OK) goto incomplete;

    len = sizeof(cfg->wifi_password);
    err = nvs_get_str(handle, "wifi_pass", cfg->wifi_password, &len);
    if (err != ESP_OK) goto incomplete;

    len = sizeof(cfg->mqtt_server);
    err = nvs_get_str(handle, "mqtt_server", cfg->mqtt_server, &len);
    if (err != ESP_OK) goto incomplete;

    len = sizeof(cfg->mqtt_channel);
    err = nvs_get_str(handle, "mqtt_channel", cfg->mqtt_channel, &len);
    if (err != ESP_OK) {
        strncpy(cfg->mqtt_channel, "claude-knock", sizeof(cfg->mqtt_channel) - 1);
    }

    len = sizeof(cfg->mqtt_password);
    err = nvs_get_str(handle, "mqtt_pass", cfg->mqtt_password, &len);
    if (err != ESP_OK) goto incomplete;

    cfg->configured = true;
    nvs_close(handle);
    ESP_LOGI(TAG, "Config loaded: SSID=%s, MQTT=%s, channel=%s, user=%s, pulse=%u/%ums, delay=%ums, boost=%ums, hold=%u%%",
             cfg->wifi_ssid, cfg->mqtt_server, cfg->mqtt_channel, cfg->mqtt_username,
             cfg->strength, cfg->strength2, cfg->delay_ms,
             cfg->boost_ms, cfg->hold_duty);
    return ESP_OK;

incomplete:
    nvs_close(handle);
    ESP_LOGI(TAG, "Incomplete credentials in NVS (tuning preserved)");
    /* Wipe any partially-filled credential strings but keep tuning fields loaded above */
    cfg->wifi_ssid[0] = '\0';
    cfg->wifi_password[0] = '\0';
    cfg->mqtt_server[0] = '\0';
    cfg->mqtt_channel[0] = '\0';
    cfg->mqtt_password[0] = '\0';
    cfg->configured = false;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t config_save(const device_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "wifi_ssid", cfg->wifi_ssid);
    if (err != ESP_OK) goto fail;
    err = nvs_set_str(handle, "wifi_pass", cfg->wifi_password);
    if (err != ESP_OK) goto fail;
    err = nvs_set_str(handle, "mqtt_server", cfg->mqtt_server);
    if (err != ESP_OK) goto fail;
    err = nvs_set_str(handle, "mqtt_channel", cfg->mqtt_channel);
    if (err != ESP_OK) goto fail;
    err = nvs_set_str(handle, "mqtt_pass", cfg->mqtt_password);
    if (err != ESP_OK) goto fail;
    err = nvs_set_u8(handle, "strength", cfg->strength);
    if (err != ESP_OK) goto fail;
    err = nvs_set_u8(handle, "strength2", cfg->strength2);
    if (err != ESP_OK) goto fail;
    err = nvs_set_u16(handle, "delay_ms", cfg->delay_ms);
    if (err != ESP_OK) goto fail;
    err = nvs_set_u8(handle, "boost_ms", cfg->boost_ms);
    if (err != ESP_OK) goto fail;
    err = nvs_set_u8(handle, "hold_duty", cfg->hold_duty);
    if (err != ESP_OK) goto fail;

    err = nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Config saved");
    return err;

fail:
    nvs_close(handle);
    ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
    return err;
}

esp_err_t config_save_strength(uint8_t strength)
{
    if (strength > STRENGTH_MAX) strength = STRENGTH_MAX;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, "strength", strength);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist strength: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_save_strength2(uint8_t strength)
{
    if (strength > STRENGTH_MAX) strength = STRENGTH_MAX;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, "strength2", strength);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist strength2: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_save_delay(uint16_t delay_ms)
{
    if (delay_ms < DELAY_MS_MIN) delay_ms = DELAY_MS_MIN;
    if (delay_ms > DELAY_MS_MAX) delay_ms = DELAY_MS_MAX;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u16(handle, "delay_ms", delay_ms);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist delay: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_save_boost(uint8_t boost_ms)
{
    if (boost_ms > BOOST_MS_MAX) boost_ms = BOOST_MS_MAX;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, "boost_ms", boost_ms);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist boost: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_save_hold(uint8_t hold_duty)
{
    if (hold_duty > HOLD_DUTY_MAX) hold_duty = HOLD_DUTY_MAX;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, "hold_duty", hold_duty);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist hold: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Config cleared");
    return err;
}
