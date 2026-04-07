#include "cmd_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "wifi_mgr.h"
#include "led_mgr.h"
#include "motor_mgr.h"
#include "power_mgr.h"
#include "power_mgr.h"
#include "web_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void _printf_wrapper(cmd_write_fn_t write_fn, void *ctx, const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (write_fn) {
        write_fn(buf, ctx);
    }
}

void cmd_handle(const char *cmd_line, cmd_write_fn_t write_fn, void *ctx)
{
    // 跳过前导空格
    while (*cmd_line == ' ' || *cmd_line == '\t') cmd_line++;
    if (strlen(cmd_line) == 0) return;

    // 分割命令
    char cmd[128];
    strncpy(cmd, cmd_line, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    
    // 我们手动拆分前几个token用来判断
    char *argv[10];
    int argc = 0;
    char *p = cmd;
    char *token_start = NULL;
    int in_token = 0;
    
    for (; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (in_token) {
                *p = '\0';
                in_token = 0;
            }
        } else {
            if (!in_token) {
                if (argc < 10) {
                    argv[argc++] = p;
                }
                in_token = 1;
            }
        }
    }

    if (argc == 0) return;

    // 每次执行有效命令，如果有设置自动关停时间，就喂狗
    // 为串口/终端命令活动做记录（预留，如果需要联动自动关停）

    if (strcmp(argv[0], "status") == 0) {
        _printf_wrapper(write_fn, ctx, "=== 设备状态 ===\n");
        _printf_wrapper(write_fn, ctx, "Wi-Fi: %s\n", wifi_mgr_is_connected() ? "已连接" : "未连接");
        _printf_wrapper(write_fn, ctx, "SSID: %s\n", wifi_mgr_get_saved_ssid());
        if (wifi_mgr_is_connected()) {
            _printf_wrapper(write_fn, ctx, "IP地址: %s\n", wifi_mgr_get_ip());
        }
        _printf_wrapper(write_fn, ctx, "电源状态: %s\n", power_mgr_is_sleeping() ? "休眠中 😴" : "正常运行 🟢");
        _printf_wrapper(write_fn, ctx, "LED指示灯: %s\n", led_mgr_get_state() ? "开启 💡" : "关闭 🌑");
        uint8_t r, g, b;
        led_mgr_get_color(&r, &g, &b);
        _printf_wrapper(write_fn, ctx, "LED颜色: R:%d G:%d B:%d\n", r, g, b);
        // 水流量与自动关闭设置
        int duty = web_server_get_motor_duty();
        int auto_stop = web_server_get_auto_stop_sec();
        if (duty == 0) {
            _printf_wrapper(write_fn, ctx, "水流量: 停止 (关闭)\n");
        } else {
            _printf_wrapper(write_fn, ctx, "水流量: %d%%\n", duty);
        }
        if (auto_stop == 0) {
            _printf_wrapper(write_fn, ctx, "自动关闭: 未开启\n");
        } else if (auto_stop < 60) {
            _printf_wrapper(write_fn, ctx, "自动关闭: 无操作 %d 秒后将停止\n", auto_stop);
        } else {
            _printf_wrapper(write_fn, ctx, "自动关闭: 无操作 %d 分钟后将停止\n", auto_stop / 60);
        }
    } else if (strcmp(argv[0], "led") == 0) {
        if (argc > 1) {
            if (strcmp(argv[1], "on") == 0) {
                web_server_set_led_state(1);
                _printf_wrapper(write_fn, ctx, "LED 已开启\n");
            } else if (strcmp(argv[1], "off") == 0) {
                web_server_set_led_state(0);
                _printf_wrapper(write_fn, ctx, "LED 已关闭\n");
            } else if (strcmp(argv[1], "color") == 0 && argc == 5) {
                int r = atoi(argv[2]);
                int g = atoi(argv[3]);
                int b = atoi(argv[4]);
                led_mgr_set_color(r, g, b);
                web_server_set_led_state(1); // 设置颜色时同步开启并更新 web 状态
                _printf_wrapper(write_fn, ctx, "LED 颜色已设置为 R:%d G:%d B:%d\n", r, g, b);
            } else {
                _printf_wrapper(write_fn, ctx, "参数错误, 示例: led on / led off / led color 255 0 0\n");
            }
        } else {
            _printf_wrapper(write_fn, ctx, "缺少参数 (on / off / color)\n");
        }
    } else if (strcmp(argv[0], "flow") == 0) {
        if (argc > 1) {
            int duty = atoi(argv[1]);
            web_server_set_motor_duty(duty); // 统一通过 web_server 设置，确保 web页面和自动关闭计时器同步
            if (duty <= 0) {
                _printf_wrapper(write_fn, ctx, "水流量已设置为 0%% (停止)\n");
            } else {
                _printf_wrapper(write_fn, ctx, "水流量已设置为 %d%%\n", web_server_get_motor_duty());
            }
        } else {
            _printf_wrapper(write_fn, ctx, "缺少流量百分比 (示例: flow 50)\n");
        }
    } else if (strcmp(argv[0], "autostop") == 0) {
        if (argc > 1) {
            int sec = atoi(argv[1]);
            if (sec < 0) sec = 0;
            web_server_set_auto_stop_sec(sec);
            if (sec == 0) {
                _printf_wrapper(write_fn, ctx, "自动关闭已禁用\n");
            } else if (sec < 60) {
                _printf_wrapper(write_fn, ctx, "自动关闭已设定为无操作 %d 秒后停止\n", sec);
            } else {
                _printf_wrapper(write_fn, ctx, "自动关闭已设定为无操作 %d 分钟后停止\n", sec / 60);
            }
        } else {
            int cur = web_server_get_auto_stop_sec();
            if (cur == 0) {
                _printf_wrapper(write_fn, ctx, "当前自动关闭: 未开启\n用法: autostop <秒数> (例: autostop 300)  0=关闭\n");
            } else {
                _printf_wrapper(write_fn, ctx, "当前自动关闭: 无操作 %d 秒后停止\n用法: autostop <秒数>  0=关闭\n", cur);
            }
        }
    } else if (strcmp(argv[0], "sleep") == 0) {
        _printf_wrapper(write_fn, ctx, "设备即将进入低功耗休眠模式\n");
        power_mgr_shutdown();
    } else if (strcmp(argv[0], "wakeup") == 0) {
        power_mgr_wakeup();
        _printf_wrapper(write_fn, ctx, "设备已被唤醒\n");
    } else if (strcmp(argv[0], "restart") == 0) {
        _printf_wrapper(write_fn, ctx, "重启 ESP32...\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(argv[0], "reset") == 0) {
        _printf_wrapper(write_fn, ctx, "清除 Wi-Fi 配置并重启...\n");
        wifi_mgr_clear_config();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(argv[0], "ap") == 0) {
        _printf_wrapper(write_fn, ctx, "切换至配置 AP 模式...\n");
        wifi_mgr_clear_config();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        _printf_wrapper(write_fn, ctx, "可用命令：\n");
        _printf_wrapper(write_fn, ctx, "  status              - 查看设备运行状态\n");
        _printf_wrapper(write_fn, ctx, "  led on/off          - 开启 / 关闭 LED 灯\n");
        _printf_wrapper(write_fn, ctx, "  led color R G B     - 设置 LED 颜色 (0-255)\n");
        _printf_wrapper(write_fn, ctx, "  flow <0-100>        - 设置水流量 (百分比)\n");
        _printf_wrapper(write_fn, ctx, "  autostop <秒数>    - 设置无操作自动关阀 (0=关闭, 例: autostop 300)\n");
        _printf_wrapper(write_fn, ctx, "  sleep               - 进入休眠模式\n");
        _printf_wrapper(write_fn, ctx, "  wakeup              - 从休眠状态唤醒\n");
        _printf_wrapper(write_fn, ctx, "  restart             - 重启设备\n");
        _printf_wrapper(write_fn, ctx, "  reset               - 清空所有设置并重启\n");
        _printf_wrapper(write_fn, ctx, "  ap                  - 切换并强行进入配网 AP 模式\n");
        _printf_wrapper(write_fn, ctx, "  help                - 显示此帮助信息\n");
    } else {
        _printf_wrapper(write_fn, ctx, "未知命令: %s (输入 'help' 查看清单)\n", argv[0]);
    }
}
