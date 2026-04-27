#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static bool s_initial_connect_done = false;

static void sta_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_initial_connect_done) {
            if (s_retry_count < WIFI_RETRY_MAX) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_count, WIFI_RETRY_MAX);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGE(TAG, "Connection failed after %d retries", WIFI_RETRY_MAX);
            }
        } else {
            /* Post-initial drop: keep retrying forever so the device recovers
             * from AP reboots, roaming, DHCP lease loss, etc. */
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        if (!s_initial_connect_done) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void ap_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " connected", MAC2STR(event->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " disconnected", MAC2STR(event->mac));
    }
}

esp_err_t wifi_start_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Set DHCP option 114 for captive portal */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, sizeof(ip_addr));

    char captiveportal_uri[32];
    snprintf(captiveportal_uri, sizeof(captiveportal_uri), "http://%s", ip_addr);

    esp_netif_dhcps_stop(netif);
    esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                           captiveportal_uri, strlen(captiveportal_uri));
    esp_netif_dhcps_start(netif);

    ESP_LOGI(TAG, "AP started: SSID=%s, IP=%s", WIFI_AP_SSID, ip_addr);
    return ESP_OK;
}

esp_err_t wifi_start_sta(const device_config_t *cfg)
{
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, cfg->mqtt_username));

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &sta_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &sta_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, cfg->wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Disable WiFi power save — this is a wall-powered device, and MIN_MODEM
     * sleep can cause the AP to silently drop the STA / miss unicast packets
     * after long idle periods, which manifests as MQTT stalling. */
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "STA connecting to %s", cfg->wifi_ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        /* Keep the event handlers registered so the device auto-reconnects
         * on future disconnects. Flip the flag before any subsequent drop
         * so the handler follows the steady-state reconnect path. */
        s_initial_connect_done = true;
        ESP_LOGI(TAG, "Connected to %s", cfg->wifi_ssid);
        return ESP_OK;
    }

    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    vEventGroupDelete(s_wifi_event_group);

    ESP_LOGE(TAG, "Failed to connect to %s", cfg->wifi_ssid);
    return ESP_FAIL;
}
