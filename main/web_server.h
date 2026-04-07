#pragma once
#include "esp_err.h"

esp_err_t start_web_server(void);
void stop_web_server(void);

/** 获取当前电机占空比 (0-100) */
int web_server_get_motor_duty(void);

/** 获取当前设置的无操作自动关阀秒数 (0=关闭) */
int web_server_get_auto_stop_sec(void);
