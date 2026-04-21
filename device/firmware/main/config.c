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
    config_generate_username(cfg->mqtt_username, sizeof(cfg->mqtt_username));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No config found in NVS");
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t len;

    len = sizeof(cfg->wifi_ssid);
    err = nvs_get_str(handle, "wifi_ssid", cfg->wifi_ssid, &len);
    if (err != ESP_OK) goto not_found;

    len = sizeof(cfg->wifi_password);
    err = nvs_get_str(handle, "wifi_pass", cfg->wifi_password, &len);
    if (err != ESP_OK) goto not_found;

    len = sizeof(cfg->mqtt_server);
    err = nvs_get_str(handle, "mqtt_server", cfg->mqtt_server, &len);
    if (err != ESP_OK) goto not_found;

    len = sizeof(cfg->mqtt_channel);
    err = nvs_get_str(handle, "mqtt_channel", cfg->mqtt_channel, &len);
    if (err != ESP_OK) {
        strncpy(cfg->mqtt_channel, "claude-knock", sizeof(cfg->mqtt_channel) - 1);
    }

    len = sizeof(cfg->mqtt_password);
    err = nvs_get_str(handle, "mqtt_pass", cfg->mqtt_password, &len);
    if (err != ESP_OK) goto not_found;

    cfg->configured = true;
    nvs_close(handle);
    ESP_LOGI(TAG, "Config loaded: SSID=%s, MQTT=%s, channel=%s, user=%s",
             cfg->wifi_ssid, cfg->mqtt_server, cfg->mqtt_channel, cfg->mqtt_username);
    return ESP_OK;

not_found:
    nvs_close(handle);
    ESP_LOGI(TAG, "Incomplete config in NVS");
    memset(cfg, 0, sizeof(device_config_t));
    config_generate_username(cfg->mqtt_username, sizeof(cfg->mqtt_username));
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

    err = nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Config saved");
    return err;

fail:
    nvs_close(handle);
    ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
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
