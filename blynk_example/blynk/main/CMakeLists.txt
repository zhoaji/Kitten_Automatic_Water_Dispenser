idf_component_register(SRCS "blink_example_main.c" "wifi_provisioning.c" "blynk_mqtt.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_wifi esp_http_server mqtt led_strip)
