#include "motor_mgr.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define MOTOR_PWM_TIMER         LEDC_TIMER_0
#define MOTOR_PWM_MODE          LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_OUTPUT_IO     (4)
#define MOTOR_PWM_CHANNEL       LEDC_CHANNEL_0
#define MOTOR_PWM_DUTY_RES      LEDC_TIMER_10_BIT
#define MOTOR_PWM_FREQUENCY     (25000)

static const char *TAG = "motor_mgr";

esp_err_t motor_mgr_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = MOTOR_PWM_MODE,
        .timer_num        = MOTOR_PWM_TIMER,
        .duty_resolution  = MOTOR_PWM_DUTY_RES,
        .freq_hz          = MOTOR_PWM_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = MOTOR_PWM_MODE,
        .channel        = MOTOR_PWM_CHANNEL,
        .timer_sel      = MOTOR_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_PWM_OUTPUT_IO,
        .duty           = 0,
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
    
    // 你的电机：占空比越低转速越快，所以反转关系
    // 用户界面 0% -> 实际占空比 100% (最快)
    // 用户界面 100% -> 实际占空比 0% (最慢/停止)
    uint32_t actual_duty = 100 - duty_percent;
    
    // 限制最大占空比为90%（超过90%电机停止）
    if (actual_duty > 90) {
        actual_duty = 90;
    }
    
    // 最小占空比设为5%，避免完全停止
    if (actual_duty < 5) {
        actual_duty = 5;
    }
    
    // 10-bit resolution -> max duty is 1023
    uint32_t duty = (actual_duty * 1023) / 100;
    
    ESP_ERROR_CHECK(ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL));
    
    ESP_LOGI(TAG, "UI: %lu%% -> Actual duty: %lu%% (PWM: %lu)", duty_percent, actual_duty, duty);
    return ESP_OK;
}
