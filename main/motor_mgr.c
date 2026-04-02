#include "motor_mgr.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define MOTOR_PWM_TIMER         LEDC_TIMER_0
#define MOTOR_PWM_MODE          LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_OUTPUT_IO     (4)
#define MOTOR_PWM_CHANNEL       LEDC_CHANNEL_0
#define MOTOR_PWM_DUTY_RES      LEDC_TIMER_10_BIT
#define MOTOR_PWM_FREQUENCY     (30000)

static const char *TAG = "motor_mgr";

esp_err_t motor_mgr_init(void)
{
    // ── 第一步：上电复位时先用 GPIO 驱动拉高引脚 ──
    // LEDC 初始化前 GPIO 默认为输入浮空，可能导致电机意外启动
    // 先强制输出高电平（高电平 = 电机停止）
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << MOTOR_PWM_OUTPUT_IO),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(MOTOR_PWM_OUTPUT_IO, 1);  // HIGH → 电机停止

    // ── 第二步：初始化 LEDC 定时器 ──
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = MOTOR_PWM_MODE,
        .timer_num        = MOTOR_PWM_TIMER,
        .duty_resolution  = MOTOR_PWM_DUTY_RES,
        .freq_hz          = MOTOR_PWM_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // ── 第三步：初始化 LEDC 通道（LEDC 接管 GPIO）──
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

    // ── 第四步：LEDC 接管后立即停止输出，保持 GPIO 高电平 ──
    // ledc_stop(idle_level=1) 强制 GPIO 输出高电平，电机保持停止
    ledc_stop(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL, 1);

    ESP_LOGI(TAG, "Motor PWM initialized on GPIO %d at %d Hz, startup safe (GPIO HIGH)",
             MOTOR_PWM_OUTPUT_IO, MOTOR_PWM_FREQUENCY);
    return ESP_OK;
}

esp_err_t motor_mgr_set_duty(uint32_t duty_percent)
{
    if (duty_percent > 100) {
        duty_percent = 100;
    }

    if (duty_percent == 0) {
        // 停止：ledc_stop 强制 GPIO 输出高电平（idle_level=1）
        // 该电机驱动为高电平停止：GPIO HIGH → 电机停止，GPIO LOW/PWM → 电机运行
        ledc_stop(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL, 1);
        ESP_LOGI(TAG, "UI: 0%% -> Motor STOPPED (GPIO HIGH)");
        return ESP_OK;
    }

    // ── 根据 PWM 调速流量曲线图──
    //
    //  PWM占空比(%) │ 流量(L/min)
    //  ────────────┼────────────
    //      0%      │  最大流量  ← 纯低电平，全力运行
    //     30%      │  ~1.08
    //     45%      │  ~0.84
    //     60%      │  ~0.62
    //     75%      │  ~0.20
    //     88%      │  ~0.02   ← 刚好能转动
    //     90%+     │   0      ← 电机停止
    //
    // UI 映射（反向线性，UI越大流量越大）：
    //   UI   1% → PWM  88%（最小流量，刚好启动）
    //   UI  50% → PWM ~44%（中等流量）
    //   UI 100% → PWM   0%（最大流量，纯低电平）
    //
    // 公式：pwm = 88 - (duty_percent - 1) * 88 / 99

    uint32_t pwm_percent = 88 - ((duty_percent - 1) * 88) / 99;

    // 安全边界（防止整数溢出）
    if (pwm_percent > 88) pwm_percent = 88;

    // 10-bit resolution -> 满量程 1023
    uint32_t duty = (pwm_percent * 1023) / 100;

    // 重新绑定 GPIO 到 LEDC 通道，模拟复位后的启动序列
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = MOTOR_PWM_MODE,
        .channel        = MOTOR_PWM_CHANNEL,
        .timer_sel      = MOTOR_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_PWM_OUTPUT_IO,
        .duty           = duty,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_ERROR_CHECK(ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CHANNEL));

    ESP_LOGI(TAG, "UI: %lu%% -> PWM: %lu%% (reg: %lu)", duty_percent, pwm_percent, duty);
    return ESP_OK;
}



