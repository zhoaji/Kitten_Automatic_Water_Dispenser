#pragma once
#include "esp_err.h"

esp_err_t start_web_server(void);
void stop_web_server(void);

/** 获取当前电机占空比 (0-100) */
int web_server_get_motor_duty(void);

/** 获取当前设置的无操作自动关阀秒数 (0=关闭) */
int web_server_get_auto_stop_sec(void);

/** 设置水流量 (同时同步状态到 web 页面) */
void web_server_set_motor_duty(int duty);

/** 设置自动关闭时间 (0=关闭, 单位秒, 同时吁 NVS 保存) */
void web_server_set_auto_stop_sec(int sec);

/** 设置 LED 开关状态 (1=开, 0=关, 同时同步到 web 页面) */
void web_server_set_led_state(int state);
