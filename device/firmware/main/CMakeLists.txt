idf_component_register(
    SRCS "main.c" "config.c" "wifi.c" "portal.c" "mqtt.c" "solenoid.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_event nvs_flash esp_wifi esp_http_server esp_timer dns_server esp_netif esp_driver_gpio esp-tls
    EMBED_FILES "portal.html"
)
