#include "mqtt.h"
#include "solenoid.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static char s_topic[65];

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to MQTT broker");
        esp_mqtt_client_subscribe(s_client, s_topic, 1);
        solenoid_set_status(STATUS_NORMAL);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from MQTT broker");
        solenoid_set_status(STATUS_CONNECTING);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribed to %s", s_topic);
        break;

    case MQTT_EVENT_DATA: {
        /* Ignore fragmented messages (topic is NULL on continuation) */
        if (!event->topic) {
            break;
        }

        char payload[32] = {0};
        size_t len = event->data_len < sizeof(payload) - 1 ? event->data_len : sizeof(payload) - 1;
        memcpy(payload, event->data, len);

        ESP_LOGI(TAG, "Message: topic=%.*s, data=%s", event->topic_len, event->topic, payload);

        /* Parse knock:N pattern */
        if (strncmp(payload, "knock:", 6) == 0) {
            int count = atoi(payload + 6);
            if (count < 1) count = 1;
            if (count > 10) count = 10;
            solenoid_pulse(count);
        } else if (strcmp(payload, "__reset__") == 0) {
            ESP_LOGW(TAG, "Remote reset requested");
            config_clear();
            esp_restart();
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type: %d", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

esp_err_t mqtt_start(const device_config_t *cfg)
{
    strncpy(s_topic, cfg->mqtt_channel, sizeof(s_topic) - 1);

    /* Tight keepalive + TCP-level keep-alive so a silently-dead connection
     * (NAT timeout, broker crash, Wi-Fi fast-retry loop) is detected and
     * reconnected within ~1 minute instead of ~2+ minutes. */
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->mqtt_server,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = cfg->mqtt_username,
        .credentials.authentication.password = cfg->mqtt_password,
        .session.keepalive = 30,
        .network.disable_auto_reconnect = false,
        .network.reconnect_timeout_ms = 5000,
        .network.timeout_ms = 10000,
        .network.tcp_keep_alive_cfg = {
            .keep_alive_enable = true,
            .keep_alive_idle = 30,
            .keep_alive_interval = 10,
            .keep_alive_count = 3,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started: server=%s, user=%s, topic=%s",
             cfg->mqtt_server, cfg->mqtt_username, s_topic);
    return ESP_OK;
}

void mqtt_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}
