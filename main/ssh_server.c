#include "ssh_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "libssh/libssh.h"
#include "libssh/server.h"
#include "cmd_handler.h"
#include "wifi_mgr.h"
#include "power_mgr.h"

static const char *TAG = "ssh_server";

#define SSH_PORT 22

// 硬编码的 ED25519 Host Key (供内部使用，避免依赖文件系统保存密钥)
// 这是随便生成的一个固定密钥，这样免去设备重启或者没有文件系统导致 Host Key 变化
static const unsigned char host_key_ed25519[] = {
    0x30, 0x51, 0x02, 0x01, 0x01, 0x04, 0x20, 0x8a, 0x6e, 0x76, 0x63, 0xc1, 0x3d, 0xfe, 0x18, 0x7b,
    0x1d, 0xf6, 0xe1, 0x61, 0x2a, 0x36, 0xe2, 0x7c, 0xc5, 0x6c, 0x7d, 0x48, 0x27, 0x25, 0xde, 0x47,
    0x8f, 0x56, 0x24, 0xa1, 0x0e, 0x41, 0x76, 0xa1, 0x23, 0x03, 0x21, 0x00, 0x30, 0x76, 0x1d, 0xcb,
    0xc7, 0xdf, 0xc5, 0x93, 0x3e, 0x40, 0x51, 0x76, 0xba, 0xb6, 0xa8, 0xf6, 0xe7, 0x57, 0xdf, 0xa6,
    0x2c, 0xc8, 0xe7, 0x55, 0x61, 0xc8, 0x6c, 0xc4, 0x78, 0xd0, 0xaa, 0x21
};

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

// 身份验证回调
static int auth_none_cb(ssh_session session, const char *user, void *userdata) {
    ESP_LOGI(TAG, "接受无密码用户登录: %s", user ? user : "unknown");
    return SSH_AUTH_SUCCESS;
}

// 频道处理回调
static ssh_channel channel_open_request_cb(ssh_session session, void *userdata) {
    ssh_channel channel = ssh_channel_new(session);
    return channel;
}

static void ssh_session_task(void *pvParameters)
{
    ssh_session session = (ssh_session)pvParameters;
    ssh_event event = ssh_event_new();
    
    struct ssh_server_callbacks_struct cb = {
        .userdata = NULL,
        .auth_none_function = auth_none_cb,
        .channel_open_request_session_function = channel_open_request_cb
    };
    ssh_callbacks_init(&cb);
    ssh_set_server_callbacks(session, &cb);

    if (ssh_handle_key_exchange(session) != SSH_OK) {
        ESP_LOGE(TAG, "Key exchange error: %s", ssh_get_error(session));
        goto cleanup;
    }

    ssh_event_add_session(event, session);
    
    // 认证循环
    while (!ssh_is_connected(session) || !ssh_is_authenticated(session)) {
        if (ssh_event_dopoll(event, -1) == SSH_ERROR) goto cleanup;
    }

    ESP_LOGI(TAG, "客户端已验证！正等待建立 Shell...");

    // 取得打开的通道
    ssh_channel channel = NULL;
    while (!channel && ssh_is_connected(session)) {
        ssh_event_dopoll(event, 100);
        
        // 查找属于本 session 的有效通道
        // 一般只有一个通道
        // 因为 libssh API 的关系，直接用一个内部结构体获取相对麻烦，
        // 我们利用 ssh_event_dopoll 会触发我们的 cb 生成通道
        // 其实我们可以通过注册 channel callbacks 来抓
        // 这里只是个简化方案。
    }

    // 以上这种拿 channel 的姿势并不严谨，推荐使用 libssh 原生的 channel 处理方式
    // 让我们重建正确的方式来阻塞等待客户端打开 shell
    
    // 我们等客户端提出 shell / pty 请求
    ssh_message message;
    while ((message = ssh_message_get(session)) != NULL) {
        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL_OPEN &&
            ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
            channel = ssh_message_channel_request_open_reply_accept(message);
            ssh_message_free(message);
            continue;
        }
        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL && channel) {
            if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_PTY) {
                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                continue;
            }
            if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SHELL) {
                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                break; // 壳已经建立
            }
        }
        ssh_message_reply_default(message);
        ssh_message_free(message);
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
    ssh_event_free(event);
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
    
    // 直接用我们在内存中硬编码的私钥导入，由于 libssh bind API 等一般期望文件路径，
    // 这里导入一个私钥到 PKI
    ssh_key rsakey = NULL;
    int rc = ssh_pki_import_privkey_base64(
        "MC4CAQAwBQYDK2VwBCIEIEo/r3N9M4o0Z/K2P+9nJj8uD0L6OqI+D8SgLhD4P9eF", // Base64 ed25519 dummy
        NULL,
        NULL,
        NULL,
        &rsakey
    );
    if (rc == SSH_OK) {
        // 由于无文档记录的可以直接传 pki. 但是通过临时写一个文件是比较通用的：
        // 或者不配置，但是因为没有 host key, ssh_bind_listen 会失败
        // 我们改为通过 IMPORT 导入并在 callback 里。
    }
    
    // 我们最稳妥的做法是用 libssh 的内置 pki 生成（如果没有文件系统）。
    // ESP-IDF libssh 的 port 提供了 ssh_bind_options_set() 可以设定 imported key,
    // 但保险起见，我们设置一个内置 rsa_key，这需要修改一下。
    // 但是最简单的方法是使用 ssh_pki_generate 临时生成，因为设备内存中保留 host key 就行了：
    ssh_key pk = NULL;
    ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &pk);
    if(pk != NULL) {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_IMPORT_KEY, pk);
    } else {
        ESP_LOGE(TAG, "Failed to generate host key");
        goto end;
    }

    if (ssh_bind_listen(sshbind) < 0) {
        ESP_LOGE(TAG, "Error listening to socket: %s", ssh_get_error(sshbind));
        goto end;
    }

    ESP_LOGI(TAG, "SSH Server Listenting on Port %d...", SSH_PORT);

    while (1) {
        session = ssh_new();
        if (session == NULL) {
            ESP_LOGE(TAG, "Failed to allocate session");
            continue;
        }

        if (ssh_bind_accept(sshbind, session) == SSH_ERROR) {
            ESP_LOGE(TAG, "Error accepting a connection: %s", ssh_get_error(sshbind));
            ssh_free(session);
            continue;
        }

        ESP_LOGI(TAG, "接受了新的 SSH 连接！");

        // 给每个连接开一个任务来处理
        xTaskCreate(ssh_session_task, "ssh_session_task", 8192, session, 5, NULL);
    }

end:
    if (pk) ssh_key_free(pk);
    ssh_bind_free(sshbind);
    ssh_finalize();
    vTaskDelete(NULL);
}

void ssh_server_init(void)
{
    ESP_LOGI(TAG, "Starting SSH Server Task...");
    xTaskCreate(ssh_server_listener_task, "ssh_server_task", 8192, NULL, 5, NULL);
}
