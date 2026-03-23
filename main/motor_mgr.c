#include "motor_mgr.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define MOTOR_PWM_TIMER         LEDC_TIMER_0
#define MOTOR_PWM_MODE          LEDC_LOW_SPEED_MODE // Use low speed mode to avoid high speed mode constraints if any
#define MOTOR_PWM_OUTPUT_IO     (4) // Set output GPIO to 4
#define MOTOR_PWM_CHANNEL       LEDC_CHANNEL_0
#define MOTOR_PWM_DUTY_RES      LEDC_TIMER_10_BIT // Set duty resolution to 10 bits
#define MOTOR_PWM_FREQUENCY     (25000) // Frequency in Hertz. Set frequency at 25 kHz

static const char *TAG = "motor_mgr";

esp_err_t motor_mgr_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = MOTOR_PWM_MODE,
        .timer_num        = MOTOR_PWM_TIMER,
        .duty_resolution  = MOTOR_PWM_DUTY_RES,
        .freq_hz          = MOTOR_PWM_FREQUENCY,  // Set output frequency at 25 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = MOTOR_PWM_MODE,
        .channel        = MOTOR_PWM_CHANNEL,
        .timer_sel      = MOTOR_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_PWM_OUTPUT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Motor PWM initialized on GPIO %d at %d Hz", MOTOR_PWM_OUTPUT_IO, MOTOR_PWM_FREQUENCY);
    return ESP_OK;
}

esp_err_t motor_mgr_set_duty(uint32_t duty_percent)
{
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    // 10-bit resolution -> max duty is (2^10) - 1 = 1023
    uint32_t duty = (duty_percent * 1023) / 100;
    
    ESP_ERROR_CHECK(ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL, duty));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL));
    
    ESP_LOGI(TAG, "Motor duty set to %lu%% (val: %lu)", duty_percent, duty);
    return ESP_OK;
}
