#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化电源管理模块
 */
void power_mgr_init(void);

/**
 * @brief 关机：停止电机和LED，进入低功耗状态（WiFi保持连接，可远程唤醒）
 */
void power_mgr_shutdown(void);

/**
 * @brief 开机：恢复电机和LED到关机前的状态
 */
void power_mgr_wakeup(void);

/**
 * @brief 查询当前是否处于睡眠（关机）状态
 * @return true 表示已关机/休眠，false 表示正常运行
 */
bool power_mgr_is_sleeping(void);

/**
 * @brief 设置关机前保存的电机占空比（供web_server调用同步状态）
 * @param duty 0-100 的占空比
 */
void power_mgr_set_saved_duty(int duty);

/**
 * @brief 获取保存的电机占空比
 */
int power_mgr_get_saved_duty(void);
