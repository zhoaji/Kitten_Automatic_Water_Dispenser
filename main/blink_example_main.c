#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "led_mgr.h"
#include "wifi_mgr.h"
#include "web_server.h"
#include "serial_cmd.h"
#include "motor_mgr.h"
#include "power_mgr.h"
#include "ssh_server.h"
#include "blynk_mqtt.h"

static const char *TAG = "main";

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP32 LED Control System Starting...");
    
    // Initialize common modules
    led_mgr_init();
    motor_mgr_init();
    serial_cmd_init();
    wifi_mgr_init();
    power_mgr_init();
    
    // Check if we have Wi-Fi config
    if (strlen(wifi_mgr_get_saved_ssid()) > 0) {
        // Try to connect to saved Wi-Fi
        wifi_mgr_start_sta();
        
        if (wifi_mgr_is_connected()) {
            // Start web server in STA mode
            start_web_server();
            ssh_server_init();
            blynk_mqtt_start();
            
            // Keep app_main running
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }
    
    // If no Wi-Fi config or connection failed, start AP mode
    wifi_mgr_start_ap();
    start_web_server();
    ssh_server_init();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
