#pragma once

#include <stdint.h>

/**
 * @brief 命令输出回调函数类型
 * @param str 输出的字符串
 * @param ctx 用户上下文（比如串口的句柄）
 */
typedef void (*cmd_write_fn_t)(const char *str, void *ctx);

/**
 * @brief 处理一行命令
 * @param cmd_line 完整的命令字符串
 * @param write_fn 输出回调
 * @param ctx 传递给输出回调的上下文
 */
void cmd_handle(const char *cmd_line, cmd_write_fn_t write_fn, void *ctx);
