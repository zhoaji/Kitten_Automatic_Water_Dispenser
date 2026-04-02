idf_component_register(SRCS "blink_example_main.c" "led_mgr.c" "wifi_mgr.c" "web_server.c" "serial_cmd.c" "motor_mgr.c" "power_mgr.c" "cmd_handler.c" "ssh_server.c" "autostop_mgr.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash led_strip esp_http_server esp_timer)
