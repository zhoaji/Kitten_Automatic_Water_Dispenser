#include "power_mgr.h"
#include "led_mgr.h"
#include "motor_mgr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_mgr";

// 是否处于睡眠（关机）状态
static volatile bool s_sleeping = false;

// 关机前保存的电机占空比，默认0（唤醒后不自动恢复）
static volatile int s_saved_duty = 0;

void power_mgr_init(void)
{
    s_sleeping = false;
    s_saved_duty = 0;
    ESP_LOGI(TAG, "电源管理模块初始化完成");
}

void power_mgr_shutdown(void)
{
    if (s_sleeping) {
        ESP_LOGI(TAG, "设备已处于休眠状态，忽略关机命令");
        return;
    }

    ESP_LOGI(TAG, "执行关机操作：停止电机、关闭LED，进入低功耗模式...");

    // 1. 停止电机（流量设为0，实际PWM占空比最低）
    motor_mgr_set_duty(0);

    // 2. 关闭LED
    led_mgr_set_state(0);

    // 3. 标记为睡眠状态（WiFi和Web服务器继续运行以接收唤醒命令）
    s_sleeping = true;

    // 4. 启用WiFi Modem Sleep以降低功耗（WiFi仍保持连接）
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    ESP_LOGI(TAG, "设备已进入低功耗休眠状态（WiFi保持连接，等待远程唤醒）");
}

void power_mgr_wakeup(void)
{
    if (!s_sleeping) {
        ESP_LOGI(TAG, "设备未处于休眠状态，忽略唤醒命令");
        return;
    }

    ESP_LOGI(TAG, "执行唤醒操作：恢复WiFi性能和LED...");

    // 1. 退出Modem Sleep（恢复正常WiFi性能）
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 2. 清除睡眠标志
    s_sleeping = false;

    // 3. 电机保持停止（GPIO 高电平），等待用户主动设置水流
    //    不调用 motor_mgr_set_duty()，确保唤醒后电机不会自动启动
    ESP_LOGI(TAG, "电机保持停止，等待用户设置水流");

    // 4. 恢复LED状态
    led_mgr_set_state(1);
    ESP_LOGI(TAG, "LED已恢复");

    ESP_LOGI(TAG, "设备已唤醒，电机停止等待用户操作");
}

bool power_mgr_is_sleeping(void)
{
    return s_sleeping;
}

void power_mgr_set_saved_duty(int duty)
{
    if (!s_sleeping) {
        // 只在正常运行时更新保存的占空比（关机状态下不更新）
        s_saved_duty = duty;
    }
}

int power_mgr_get_saved_duty(void)
{
    return s_saved_duty;
}
