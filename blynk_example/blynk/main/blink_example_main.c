#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "wifi_provisioning.h"
#include "blynk_mqtt.h"

static const char *TAG = "app_main";

#define BLINK_GPIO CONFIG_BLINK_GPIO

static led_strip_handle_t led_strip;
static uint8_t s_r = 16, s_g = 16, s_b = 16;
static bool s_led_on = false;

static void update_led(void)
{
    if (s_led_on) {
        led_strip_set_pixel(led_strip, 0, s_r, s_g, s_b);
        led_strip_refresh(led_strip);
    } else {
        led_strip_clear(led_strip);
    }
}

static void on_blynk_switch(int state)
{
    ESP_LOGI(TAG, "Blynk Switch: %d", state);
    s_led_on = (state != 0);
    update_led();
}

static void on_blynk_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_LOGI(TAG, "Blynk RGB: %d, %d, %d", r, g, b);
    s_r = r;
    s_g = g;
    s_b = b;
    if (s_led_on) update_led();
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring LED Strip on GPIO %d", BLINK_GPIO);
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#endif
    led_strip_clear(led_strip);
}

void app_main(void)
{
    configure_led();

    // 1. Initialize NVS and check for saved WiFi
    if (!wifi_provisioning_init_nvs()) {
        ESP_LOGI(TAG, "No WiFi credentials found. Starting Captive Portal...");
        wifi_provisioning_start_portal();
        // The portal will restart the ESP on success
        return;
    }

    // 2. WiFi credentials found, try to connect
    ESP_LOGI(TAG, "Connecting to saved WiFi...");
    if (wifi_provisioning_connect_saved()) {
        ESP_LOGI(TAG, "Connected! Starting Blynk...");
        
        // 3. Register callbacks and start Blynk
        blynk_mqtt_set_switch_cb(on_blynk_switch);
        blynk_mqtt_set_rgb_cb(on_blynk_rgb);
        blynk_mqtt_start();
    } else {
        ESP_LOGE(TAG, "Failed to connect to saved WiFi. Restarting portal...");
        wifi_provisioning_start_portal();
    }

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
