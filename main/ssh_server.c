#include "ssh_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "libssh/libssh.h"
#include "libssh/server.h"
#include "libssh/callbacks.h"
#include "cmd_handler.h"
#include "wifi_mgr.h"
#include "power_mgr.h"

static const char *TAG = "ssh_server";

#define SSH_PORT 22

// 使用更简短的 Ed25519 密钥
static const char *hardcoded_example_host_key =
	"-----BEGIN OPENSSH PRIVATE KEY-----\n"
	"b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW\n"
	"QyNTUxOQAAACA4KNdPYRmEWdP76SV4xN3/7JZjhf9wRzHBBBdV5DjlzAAAAIgG3kRkBt5E\n"
	"ZAAAAAtzc2gtZWQyNTUxOQAAACA4KNdPYRmEWdP76SV4xN3/7JZjhf9wRzHBBBdV5DjlzA\n"
	"AAAECRRkk05IyEuLqZwcP03tO7TrBqtdHLJdE0ezJP+B2x0zgo109hGYRZ0/vpJXjE3f/s\n"
	"lmOF/3BHMcEEF1XkOOXMAAAAAAECAwQF\n"
	"-----END OPENSSH PRIVATE KEY-----\n";

typedef struct {
    bool authenticated;
} ssh_session_data_t;



// SSH 输出回调

static void ssh_write_cmd_cb(const char *str, void *ctx)
{
    ssh_channel channel = (ssh_channel)ctx;
    if (channel) {
        // 由于 libssh 中通过 ssh_channel_write 写入
        // 直接写字符串会有换行表现不一致，客户端通常期待 \r\n
        // 所以这里简单替换一下 \n 为 \r\n 并一次发出
        const char *p = str;
        char buf[512];
        int idx = 0;
        while (*p && idx < sizeof(buf) - 2) {
            if (*p == '\n') {
                buf[idx++] = '\r';
            }
            buf[idx++] = *p++;
        }
        ssh_channel_write(channel, buf, idx);
    }
}

// 显示欢迎界面
static void show_welcome_screen(ssh_channel channel)
{
    const char *welcome = 
        "\r\n"
        "╔══════════════════════════════════════╗\r\n"
        "║      ESP32 设备管理终端 v1.0         ║\r\n"
        "║      通过 SSH 无密码远程控制         ║\r\n"
        "╚══════════════════════════════════════╝\r\n\r\n"
        "欢迎登录！设备状态正常。\r\n\r\n";
    ssh_channel_write(channel, welcome, strlen(welcome));

    // 调用 cmd_handler 发送 status 给用户
    cmd_handle("status", ssh_write_cmd_cb, channel);
    
    // 显示命令提示符
    const char *prompt = "\r\n> ";
    ssh_channel_write(channel, prompt, strlen(prompt));
}

static void ssh_session_task(void *pvParameters)
{
    ssh_session session = (ssh_session)pvParameters;
    ssh_channel channel = NULL;
    ssh_message message;
    int authenticated = 0;
    int shell_opened = 0;

    ESP_LOGI(TAG, "开始 Key Exchange (KEX)...");
    int kex_rc = ssh_handle_key_exchange(session);
    if (kex_rc != SSH_OK) {
        const char *err = ssh_get_error(session);
        ESP_LOGE(TAG, "Key exchange error (rc=%d): %s", kex_rc, err ? err : "Unknown");
        goto cleanup;
    }
    ESP_LOGI(TAG, "Key Exchange 成功完成!");

    // 1. 认证循环 (无密码认证)
    while (!authenticated && (message = ssh_message_get(session)) != NULL) {
        if (ssh_message_type(message) == SSH_REQUEST_SERVICE) {
            ESP_LOGI(TAG, "接受客户端 Service 请求: %s", ssh_message_subtype(message) == SSH_AUTH_METHOD_UNKNOWN ? "unknown" : "ssh-userauth (或其他)");
            ssh_message_service_reply_success(message);
            ssh_message_free(message);
            continue;
        }
        if (ssh_message_type(message) == SSH_REQUEST_AUTH) {
            if (ssh_message_subtype(message) == SSH_AUTH_METHOD_NONE) {
                ESP_LOGI(TAG, "接受无密码用户登录");
                authenticated = 1;
                ssh_message_auth_reply_success(message, 0);
            } else {
                ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_NONE);
                ssh_message_reply_default(message);
            }
            ssh_message_free(message);
            continue;
        }
        ssh_message_reply_default(message);
        ssh_message_free(message);
    }

    if (!authenticated) {
        ESP_LOGE(TAG, "客户端认证失败或断开连接");
        goto cleanup;
    }
    ESP_LOGI(TAG, "客户端已验证！正等待建立 Shell...");

    // 2. 通道申请与建立
    while (!channel && (message = ssh_message_get(session)) != NULL) {
        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL_OPEN &&
            ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
            channel = ssh_message_channel_request_open_reply_accept(message);
            ssh_message_free(message);
            break; // 通道已获得
        }
        ssh_message_reply_default(message);
        ssh_message_free(message);
    }

    if (!channel) {
        ESP_LOGE(TAG, "无法建立 SSH 通道");
        goto cleanup;
    }

    // 3. PTY 与 Shell 请求处理
    while (!shell_opened && (message = ssh_message_get(session)) != NULL) {
        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL) {
            
            if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_PTY) {
                ESP_LOGI(TAG, "收到 pty 请求，已接收");
                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                continue;
            }
            if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SHELL) {
                ESP_LOGI(TAG, "收到 shell 请求，正在打开");
                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                shell_opened = 1;
                break;
            }
        }
        ssh_message_reply_default(message);
        ssh_message_free(message);
    }

    if (!shell_opened) {
        ESP_LOGE(TAG, "无法开启 Shell");
        goto cleanup;
    }

    
    if (channel) {
        show_welcome_screen(channel);

        char cmd_buffer[128];
        int cmd_idx = 0;
        char buf[32];

        while (ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
            int len = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), 0);
            if (len > 0) {
                for (int i = 0; i < len; i++) {
                    char c = buf[i];
                    
                    if (c == '\r' || c == '\n') {
                        ssh_channel_write(channel, "\r\n", 2);
                        cmd_buffer[cmd_idx] = '\0';
                        if (cmd_idx > 0) {
                            cmd_handle(cmd_buffer, ssh_write_cmd_cb, channel);
                            cmd_idx = 0;
                        }
                        const char *prompt = "> ";
                        ssh_channel_write(channel, prompt, strlen(prompt));
                    } 
                    else if (c == 127 || c == '\b') { // 退格处理
                        if (cmd_idx > 0) {
                            cmd_idx--;
                            ssh_channel_write(channel, "\b \b", 3);
                        }
                    } 
                    else if (c >= 32 && c <= 126) {
                        if (cmd_idx < sizeof(cmd_buffer) - 1) {
                            cmd_buffer[cmd_idx++] = c;
                            ssh_channel_write(channel, &c, 1); // 字符回显
                        }
                    } else if (c == 3) {
                         // Ctrl+C 退出
                        ssh_channel_close(channel);
                        break;
                    }
                }
            } else if (len < 0) {
                break; // 连接丢失
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
    }

cleanup:
    ESP_LOGI(TAG, "SSH 回话结束");
    ssh_disconnect(session);
    ssh_free(session);
    vTaskDelete(NULL);
}


static void ssh_server_listener_task(void *pvParameters)
{
    ssh_bind sshbind;
    ssh_session session;

    ssh_init();
    sshbind = ssh_bind_new();

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, "22");
    
    // 借鉴 example-master，利用 API Hack 将硬编码的主机密钥写入
    // 使用 libssh 0.11.0 原生支持的导入配置选项直接从内存注入 Private Key
    // 这取代了之前由于内存结构体偏移不一致导致的非法指针 hack 方法
    int rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_IMPORT_KEY_STR, hardcoded_example_host_key);
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "Failed to import embedded host key using SSH_BIND_OPTIONS_IMPORT_KEY_STR (rc = %d)", rc);
        goto end;
    }

    if (ssh_bind_listen(sshbind) < 0) {
        ESP_LOGE(TAG, "Error listening to socket: %s", ssh_get_error(sshbind));
        goto end;
    }

    ESP_LOGI(TAG, "SSH Server Listenting on Port %d...", SSH_PORT);

    while (1) {
        ESP_LOGI(TAG, "Waiting for new connection...");
        session = ssh_new();
        if (session == NULL) {
            ESP_LOGE(TAG, "Failed to allocate session");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int accept_rc = ssh_bind_accept(sshbind, session);
        if (accept_rc == SSH_ERROR) {
            const char *err = ssh_get_error(sshbind);
            ESP_LOGE(TAG, "Error accepting a connection: %s", err ? err : "Unknown Error");
            ssh_free(session);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG, "接受了新的 SSH 连接！分配任务处理...");

        // 给每个连接开一个任务来处理
        xTaskCreate(ssh_session_task, "ssh_session_task", 8192, session, 5, NULL);
    }

end:
    ssh_bind_free(sshbind);
    ssh_finalize();
    vTaskDelete(NULL);
}

void ssh_server_init(void)
{
    ESP_LOGI(TAG, "Starting SSH Server Task...");
    xTaskCreate(ssh_server_listener_task, "ssh_server_task", 16384, NULL, 5, NULL);
}
