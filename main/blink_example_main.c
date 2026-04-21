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

#include "blynk_mqtt.h"
#include "driver/gpio.h"

static const char *TAG = "main";

#define CONFIG_BUTTON_GPIO 0
#define LONG_PRESS_TIME_MS 5000

static void button_monitor_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    uint32_t press_start_tick = 0;
    bool is_pressing = false;

    while (1) {
        // GPIO 0 通常是低电平触发 (按下为 0)
        if (gpio_get_level(CONFIG_BUTTON_GPIO) == 0) {
            if (!is_pressing) {
                is_pressing = true;
                press_start_tick = xTaskGetTickCount();
                ESP_LOGI(TAG, "检测到按键按下...");
            } else {
                uint32_t duration_ms = (xTaskGetTickCount() - press_start_tick) * portTICK_PERIOD_MS;
                if (duration_ms >= LONG_PRESS_TIME_MS) {
                    ESP_LOGW(TAG, "检测到长按 5 秒！准备清除配置并重启...");
                    wifi_mgr_clear_config();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                }
            }
        } else {
            if (is_pressing) {
                is_pressing = false;
                ESP_LOGI(TAG, "按键已松开");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

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
    xTaskCreate(button_monitor_task, "button_monitor", 4096, NULL, 5, NULL);
    motor_mgr_init();
    serial_cmd_init();
    wifi_mgr_init();
    power_mgr_init();
    
    // Check if we have Wi-Fi config
    if (strlen(wifi_mgr_get_saved_ssid()) > 0) {
        // Try to connect to saved Wi-Fi
        wifi_mgr_start_sta();
        
        if (wifi_mgr_is_connected()) {
            // 连接成功，恢复默认颜色并停止闪烁
            led_mgr_set_blink_period(0);
            led_mgr_set_color(200, 150, 16);
            led_mgr_set_state(1);

            // Start web server in STA mode
            start_web_server();

            blynk_mqtt_start();
            
            // Keep app_main running
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }
    
    // If no Wi-Fi config or connection failed, start AP mode
    led_mgr_set_color(255, 0, 0);
    led_mgr_set_blink_period(500);
    led_mgr_set_state(1);
    
    wifi_mgr_start_ap();
    start_web_server();

    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
