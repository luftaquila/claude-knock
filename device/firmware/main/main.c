#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "config.h"
#include "wifi.h"
#include "portal.h"
#include "mqtt.h"
#include "solenoid.h"
#include "dns_server.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "claude-knock starting...");

    /* Suppress noisy HTTP server logs from captive portal redirects */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialize networking */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize hardware */
    solenoid_init();

    /* Load config */
    device_config_t cfg;
    err = config_load(&cfg);

    if (!cfg.configured) {
        /* Provisioning mode: start AP + captive portal */
        ESP_LOGI(TAG, "No config found, entering AP provisioning mode");
        solenoid_set_status(STATUS_AP_MODE);

        wifi_start_ap();
        portal_start();

        dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
        start_dns_server(&dns_cfg);

        ESP_LOGI(TAG, "Captive portal ready at http://192.168.4.1");
    } else {
        /* Normal mode: connect WiFi + MQTT */
        ESP_LOGI(TAG, "Config found, connecting to WiFi...");
        solenoid_set_status(STATUS_CONNECTING);

        err = wifi_start_sta(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi connection failed, falling back to AP mode");
            solenoid_set_status(STATUS_AP_MODE);

            esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (sta_netif) {
                esp_netif_destroy(sta_netif);
            }
            esp_wifi_stop();
            esp_wifi_deinit();

            wifi_start_ap();
            portal_start();

            dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
            start_dns_server(&dns_cfg);
            return;
        }

        mqtt_start(&cfg);
        portal_start();
    }
}
