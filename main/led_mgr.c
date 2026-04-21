#include "led_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "led_mgr";

#define BLINK_GPIO CONFIG_BLINK_GPIO

static volatile uint8_t led_r = 200;
static volatile uint8_t led_g = 150;
static volatile uint8_t led_b = 16;
static volatile uint8_t led_on = 0;
static volatile uint32_t blink_period = 0;

static led_strip_handle_t led_strip = NULL;

static void configure_led(void)
{
#ifdef CONFIG_BLINK_LED_RMT
    ESP_LOGI(TAG, "配置可寻址LED (RMT)");
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
#elif CONFIG_BLINK_LED_GPIO
    ESP_LOGI(TAG, "配置GPIO LED");
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
#endif
}

static void led_control_task(void *pvParameters)
{
    configure_led();
    uint8_t toggle = 0;
    
    while (1) {
        if (led_on) {
            bool current_state = true;
            if (blink_period > 0) {
                current_state = (toggle == 0);
                toggle = !toggle;
            }

            if (current_state) {
#ifdef CONFIG_BLINK_LED_RMT
                led_strip_set_pixel(led_strip, 0, led_r, led_g, led_b);
                led_strip_refresh(led_strip);
#elif CONFIG_BLINK_LED_GPIO
                gpio_set_level(BLINK_GPIO, 1);
#endif
            } else {
#ifdef CONFIG_BLINK_LED_RMT
                led_strip_clear(led_strip);
#elif CONFIG_BLINK_LED_GPIO
                gpio_set_level(BLINK_GPIO, 0);
#endif
            }
        } else {
#ifdef CONFIG_BLINK_LED_RMT
            led_strip_clear(led_strip);
#elif CONFIG_BLINK_LED_GPIO
            gpio_set_level(BLINK_GPIO, 0);
#endif
        }
        
        if (blink_period > 0 && led_on) {
            vTaskDelay(pdMS_TO_TICKS(blink_period / 2));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void led_mgr_init(void)
{
    xTaskCreate(led_control_task, "led_control", 4096, NULL, 3, NULL);
}

void led_mgr_set_state(uint8_t state)
{
    led_on = state;
}

void led_mgr_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    led_r = r;
    led_g = g;
    led_b = b;
}

void led_mgr_set_blink_period(uint32_t period)
{
    blink_period = period;
}

bool led_mgr_get_state(void)
{
    return led_on != 0;
}

void led_mgr_get_color(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (r) *r = led_r;
    if (g) *g = led_g;
    if (b) *b = led_b;
}

uint32_t led_mgr_get_blink_period(void)
{
    return blink_period;
}
