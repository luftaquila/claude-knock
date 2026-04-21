#include "portal.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
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
    esp_err_t err = config_load(&cfg);
    if (err != ESP_OK) {
        memset(&cfg, 0, sizeof(cfg));
        config_generate_username(cfg.mqtt_username, sizeof(cfg.mqtt_username));
    }

    char ssid_esc[sizeof(cfg.wifi_ssid) * 2];
    char server_esc[sizeof(cfg.mqtt_server) * 2];
    char channel_esc[sizeof(cfg.mqtt_channel) * 2];
    ssid_esc[0] = server_esc[0] = channel_esc[0] = '\0';

    if (cfg.configured) {
        if (json_escape(cfg.wifi_ssid, ssid_esc, sizeof(ssid_esc)) != ESP_OK ||
            json_escape(cfg.mqtt_server, server_esc, sizeof(server_esc)) != ESP_OK ||
            json_escape(cfg.mqtt_channel, channel_esc, sizeof(channel_esc)) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON escape overflow");
            return ESP_FAIL;
        }
    }

    char json[1024];
    snprintf(json, sizeof(json),
             "{\"username\":\"%s\",\"configured\":%s,"
             "\"wifi_ssid\":\"%s\",\"mqtt_server\":\"%s\",\"mqtt_channel\":\"%s\"}",
             cfg.mqtt_username,
             cfg.configured ? "true" : "false",
             ssid_esc, server_esc, channel_esc);

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

    device_config_t existing;
    bool has_existing = (config_load(&existing) == ESP_OK && existing.configured);

    device_config_t cfg = {0};
    config_generate_username(cfg.mqtt_username, sizeof(cfg.mqtt_username));

    if (get_form_value(body, "wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid)) != ESP_OK ||
        strlen(cfg.wifi_ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "WiFi SSID is required");
        return ESP_FAIL;
    }

    /* wifi_pass: form value takes precedence; empty means "keep existing" */
    if (get_form_value(body, "wifi_pass", cfg.wifi_password, sizeof(cfg.wifi_password)) != ESP_OK ||
        strlen(cfg.wifi_password) == 0) {
        if (has_existing) {
            strncpy(cfg.wifi_password, existing.wifi_password, sizeof(cfg.wifi_password) - 1);
        }
    }

    if (get_form_value(body, "mqtt_server", cfg.mqtt_server, sizeof(cfg.mqtt_server)) != ESP_OK ||
        strlen(cfg.mqtt_server) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MQTT server is required");
        return ESP_FAIL;
    }

    if (get_form_value(body, "mqtt_channel", cfg.mqtt_channel, sizeof(cfg.mqtt_channel)) != ESP_OK ||
        strlen(cfg.mqtt_channel) == 0) {
        strncpy(cfg.mqtt_channel, "claude-knock", sizeof(cfg.mqtt_channel) - 1);
    }

    /* mqtt_pass: form value takes precedence; empty means "keep existing" */
    if (get_form_value(body, "mqtt_pass", cfg.mqtt_password, sizeof(cfg.mqtt_password)) != ESP_OK ||
        strlen(cfg.mqtt_password) == 0) {
        if (has_existing && strlen(existing.mqtt_password) > 0) {
            strncpy(cfg.mqtt_password, existing.mqtt_password, sizeof(cfg.mqtt_password) - 1);
        } else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "MQTT password is required");
            return ESP_FAIL;
        }
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

esp_err_t portal_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_save);
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
