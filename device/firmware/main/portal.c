#include "portal.h"
#include "config.h"
#include "solenoid.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "portal";
static httpd_handle_t s_server = NULL;

extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t len = portal_html_end - portal_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, portal_html_start, len);
    return ESP_OK;
}

/* JSON-escape into dst; on overflow returns ESP_FAIL */
static esp_err_t json_escape(const char *src, char *dst, size_t dst_len)
{
    size_t di = 0;
    for (const char *p = src; *p; p++) {
        const char *esc = NULL;
        char buf[8];
        switch (*p) {
        case '"':  esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        default:
            if ((unsigned char)*p < 0x20) {
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
                esc = buf;
            }
            break;
        }
        if (esc) {
            size_t l = strlen(esc);
            if (di + l >= dst_len) return ESP_FAIL;
            memcpy(dst + di, esc, l);
            di += l;
        } else {
            if (di + 1 >= dst_len) return ESP_FAIL;
            dst[di++] = *p;
        }
    }
    dst[di] = '\0';
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    device_config_t cfg;
    /* config_load always initializes cfg (tuning from NVS or defaults, username from MAC).
     * Returns NOT_FOUND only to signal incomplete credentials; we still render the struct. */
    config_load(&cfg);

    char ssid_esc[sizeof(cfg.wifi_ssid) * 2];
    char wifi_pass_esc[sizeof(cfg.wifi_password) * 2];
    char server_esc[sizeof(cfg.mqtt_server) * 2];
    char channel_esc[sizeof(cfg.mqtt_channel) * 2];
    char mqtt_pass_esc[sizeof(cfg.mqtt_password) * 2];
    ssid_esc[0] = wifi_pass_esc[0] = server_esc[0] = channel_esc[0] = mqtt_pass_esc[0] = '\0';

    if (cfg.configured) {
        if (json_escape(cfg.wifi_ssid, ssid_esc, sizeof(ssid_esc)) != ESP_OK ||
            json_escape(cfg.wifi_password, wifi_pass_esc, sizeof(wifi_pass_esc)) != ESP_OK ||
            json_escape(cfg.mqtt_server, server_esc, sizeof(server_esc)) != ESP_OK ||
            json_escape(cfg.mqtt_channel, channel_esc, sizeof(channel_esc)) != ESP_OK ||
            json_escape(cfg.mqtt_password, mqtt_pass_esc, sizeof(mqtt_pass_esc)) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON escape overflow");
            return ESP_FAIL;
        }
    }

    char json[1536];
    snprintf(json, sizeof(json),
             "{\"username\":\"%s\",\"configured\":%s,"
             "\"wifi_ssid\":\"%s\",\"wifi_pass\":\"%s\","
             "\"mqtt_server\":\"%s\",\"mqtt_channel\":\"%s\",\"mqtt_pass\":\"%s\","
             "\"strength\":%u,\"strength2\":%u,\"delay_ms\":%u,"
             "\"boost_ms\":%u,\"hold_duty\":%u}",
             cfg.mqtt_username,
             cfg.configured ? "true" : "false",
             ssid_esc, wifi_pass_esc,
             server_esc, channel_esc, mqtt_pass_esc,
             cfg.strength, cfg.strength2, cfg.delay_ms,
             cfg.boost_ms, cfg.hold_duty);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

/* URL-decode a string in place */
static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* Extract a form field value from URL-encoded body */
static esp_err_t get_form_value(const char *body, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;

    while ((p = strstr(p, key)) != NULL) {
        /* Ensure this is a full key match (preceded by start-of-string or '&') */
        if (p != body && *(p - 1) != '&') {
            p += key_len;
            continue;
        }
        if (p[key_len] != '=') {
            p += key_len;
            continue;
        }
        p += key_len + 1;
        const char *end = strchr(p, '&');
        size_t vlen = end ? (size_t)(end - p) : strlen(p);
        if (vlen >= out_len) vlen = out_len - 1;
        memcpy(out, p, vlen);
        out[vlen] = '\0';
        url_decode(out);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[1200];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    device_config_t cfg = {0};
    cfg.strength = STRENGTH_DEFAULT;
    cfg.strength2 = STRENGTH_DEFAULT;
    cfg.delay_ms = DELAY_MS_DEFAULT;
    cfg.boost_ms = BOOST_MS_DEFAULT;
    cfg.hold_duty = HOLD_DUTY_DEFAULT;
    config_generate_username(cfg.mqtt_username, sizeof(cfg.mqtt_username));

    /* Preserve tuning fields across reboots — config_load always populates
     * tuning from NVS (falling back to defaults) regardless of configured state. */
    device_config_t existing;
    config_load(&existing);
    cfg.strength = existing.strength;
    cfg.strength2 = existing.strength2;
    cfg.delay_ms = existing.delay_ms;
    cfg.boost_ms = existing.boost_ms;
    cfg.hold_duty = existing.hold_duty;

    if (get_form_value(body, "wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid)) != ESP_OK ||
        strlen(cfg.wifi_ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wi-Fi SSID is required");
        return ESP_FAIL;
    }

    get_form_value(body, "wifi_pass", cfg.wifi_password, sizeof(cfg.wifi_password));

    if (get_form_value(body, "mqtt_server", cfg.mqtt_server, sizeof(cfg.mqtt_server)) != ESP_OK ||
        strlen(cfg.mqtt_server) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MQTT server is required");
        return ESP_FAIL;
    }

    if (get_form_value(body, "mqtt_channel", cfg.mqtt_channel, sizeof(cfg.mqtt_channel)) != ESP_OK ||
        strlen(cfg.mqtt_channel) == 0) {
        strncpy(cfg.mqtt_channel, "claude-knock", sizeof(cfg.mqtt_channel) - 1);
    }

    if (get_form_value(body, "mqtt_pass", cfg.mqtt_password, sizeof(cfg.mqtt_password)) != ESP_OK ||
        strlen(cfg.mqtt_password) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MQTT password is required");
        return ESP_FAIL;
    }

    esp_err_t err = config_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    const char *resp = "{\"ok\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Config saved, rebooting in 2s...");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t test_post_handler(httpd_req_t *req)
{
    char body[32] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received > 0) body[received] = '\0';

    int count = 1;
    char val[8];
    if (received > 0 && get_form_value(body, "count", val, sizeof(val)) == ESP_OK) {
        long v = strtol(val, NULL, 10);
        if (v < 1) v = 1;
        if (v > 10) v = 10;
        count = (int)v;
    }

    solenoid_pulse(count);
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"count\":%d}", count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t strength2_post_handler(httpd_req_t *req)
{
    char body[32];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char val[8];
    if (get_form_value(body, "strength2", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "strength2 field required");
        return ESP_FAIL;
    }
    long v = strtol(val, NULL, 10);
    if (v < STRENGTH_MIN) v = STRENGTH_MIN;
    if (v > STRENGTH_MAX) v = STRENGTH_MAX;

    solenoid_set_strength2((uint8_t)v);
    config_save_strength2((uint8_t)v);

    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"strength2\":%ld}", v);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t delay_post_handler(httpd_req_t *req)
{
    char body[32];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char val[8];
    if (get_form_value(body, "delay_ms", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "delay_ms field required");
        return ESP_FAIL;
    }
    long v = strtol(val, NULL, 10);
    if (v < DELAY_MS_MIN) v = DELAY_MS_MIN;
    if (v > DELAY_MS_MAX) v = DELAY_MS_MAX;

    solenoid_set_delay((uint16_t)v);
    config_save_delay((uint16_t)v);

    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"delay_ms\":%ld}", v);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t strength_post_handler(httpd_req_t *req)
{
    char body[32];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char val[8];
    if (get_form_value(body, "strength", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "strength field required");
        return ESP_FAIL;
    }
    long v = strtol(val, NULL, 10);
    if (v < STRENGTH_MIN) v = STRENGTH_MIN;
    if (v > STRENGTH_MAX) v = STRENGTH_MAX;

    solenoid_set_strength((uint8_t)v);
    config_save_strength((uint8_t)v);

    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"strength\":%ld}", v);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t boost_post_handler(httpd_req_t *req)
{
    char body[32];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char val[8];
    if (get_form_value(body, "boost_ms", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "boost_ms field required");
        return ESP_FAIL;
    }
    long v = strtol(val, NULL, 10);
    if (v < BOOST_MS_MIN) v = BOOST_MS_MIN;
    if (v > BOOST_MS_MAX) v = BOOST_MS_MAX;

    solenoid_set_boost((uint8_t)v);
    config_save_boost((uint8_t)v);

    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"boost_ms\":%ld}", v);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t hold_post_handler(httpd_req_t *req)
{
    char body[32];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char val[8];
    if (get_form_value(body, "hold_duty", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "hold_duty field required");
        return ESP_FAIL;
    }
    long v = strtol(val, NULL, 10);
    if (v < HOLD_DUTY_MIN) v = HOLD_DUTY_MIN;
    if (v > HOLD_DUTY_MAX) v = HOLD_DUTY_MAX;

    solenoid_set_hold((uint8_t)v);
    config_save_hold((uint8_t)v);

    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"hold_duty\":%ld}", v);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect to captive portal", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static const httpd_uri_t uri_status = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_get_handler,
};

static const httpd_uri_t uri_save = {
    .uri = "/save",
    .method = HTTP_POST,
    .handler = save_post_handler,
};

static const httpd_uri_t uri_strength = {
    .uri = "/strength",
    .method = HTTP_POST,
    .handler = strength_post_handler,
};

static const httpd_uri_t uri_test = {
    .uri = "/test",
    .method = HTTP_POST,
    .handler = test_post_handler,
};

static const httpd_uri_t uri_delay = {
    .uri = "/delay",
    .method = HTTP_POST,
    .handler = delay_post_handler,
};

static const httpd_uri_t uri_strength2 = {
    .uri = "/strength2",
    .method = HTTP_POST,
    .handler = strength2_post_handler,
};

static const httpd_uri_t uri_boost = {
    .uri = "/boost",
    .method = HTTP_POST,
    .handler = boost_post_handler,
};

static const httpd_uri_t uri_hold = {
    .uri = "/hold",
    .method = HTTP_POST,
    .handler = hold_post_handler,
};

esp_err_t portal_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* Leave enough LwIP sockets (LWIP_MAX_SOCKETS=16) for MQTT, DNS and
     * DHCP renew. 7 is plenty for the captive portal (a phone fetches a
     * handful of URLs during provisioning) without starving the rest. */
    config.max_open_sockets = 7;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_save);
    httpd_register_uri_handler(s_server, &uri_strength);
    httpd_register_uri_handler(s_server, &uri_test);
    httpd_register_uri_handler(s_server, &uri_delay);
    httpd_register_uri_handler(s_server, &uri_strength2);
    httpd_register_uri_handler(s_server, &uri_boost);
    httpd_register_uri_handler(s_server, &uri_hold);
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_handler);

    ESP_LOGI(TAG, "Captive portal started");
    return ESP_OK;
}

void portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
