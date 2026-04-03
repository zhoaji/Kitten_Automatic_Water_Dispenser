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

// 硬编码密钥已改为 RSA 动态生成，此处数组不再使用
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

// 身份验证回调
static int auth_none_cb(ssh_session session, const char *user, void *userdata) {
    ssh_session_data_t *data = (ssh_session_data_t *)userdata;
    ESP_LOGI(TAG, "接受无密码用户登录: %s", user ? user : "unknown");
    if (data) data->authenticated = true;
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
    
    ssh_session_data_t session_data = { .authenticated = false };
    
    struct ssh_server_callbacks_struct cb = {
        .userdata = &session_data,
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
    while (ssh_is_connected(session) && !session_data.authenticated) {
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
    
    // 使用预置的 ED25519 私钥，避免在 ESP32 上动态生成失败（rc=-1）
    // 替换掉之前那个 ED25519 的密钥
// 替换成新生成的 OpenSSH 格式 RSA 密钥 Base64
  const char *privkey_pem = 
        "-----BEGIN RSA PRIVATE KEY-----"
        "MIIEogIBAAKCAQEArEWqT6tsug8WEZ+aPd1qR54ih22dUZ4KFednFCzR1VDh7fJ3"
        "sp1a1yWjEB3R9MBljKI8INftBrKVsRyIGYAV56V8srRas4U9AlSYjJpCz7+ZhOj2"
        "o59qh9l9d05B10Htpq8wRoxp1yiQNkXH7iaLfeE2XYwjtN31mMexi3X07TsY5YlB"
        "qwCgPmeCpgHFFrmZQ0fNsI72stUjjHGXHZoE+/ADE9qi69NMFnIDj0Sd0xwbUCZr"
        "ScBuZPCg36pplUt299eHaX+shI0PjOOeuYmCj3kNkV6zTGa7ouiUL9CuADAYLXz1"
        "abfvXTScVeFw3k2zGJsWi3JFJpE9yFTZk6Q4cwIDAQABAoIBACqbfFkCC/0kmA+5"
        "yrs8VPnrmZynNr6l+NacCfmKcEdzHr3sN0Cc/IezzlXBGlmPcE5NHdP9s6jxaGaK"
        "qPqtnD1Tx7inNLur24AFDknQKXAackzWFZI4bm+1Efv9BfnIW4/bSnRYbCED7k8O"
        "CTnUnLGAjyKp83bbYs/rq/TTMsWtbPkrmIm+KlyduDVNemYquN3Ia6JZIPxKgi3p"
        "yOabD2th1+wU3k0qXb5ehFSO7iZAA4a8inMt6+f0/RYQXTmtk0bOEYezHOBwgquA"
        "unYBN7f6mRqWmkMZBEXQpiS2+AE5yLeUR4KZ4vyST9ukaPTwCWT2yLuEAuYjhqDp"
        "s5g23xECgYEA4mwAnbWnW9RvR1PfkTN/zkKeCZ46yTZyHWSVQtwibj5Hyj1BLsKy"
        "59EefmljB8ix8GjDc0tevnEYRpQTP00ylDgnReHgl1GmmZtOoOtOd8baVg1CIgnc"
        "THq7WsJz7oJKsJRgF/dV7np7xeaop1powIlHFcj9lL16VOX1TULjRKkCgYEAwsbJ"
        "msLYW4H9lqvUuTk8fkDH8OneflvYEE3sjVwMbZ90lm7Shq+ljL0J0NB7BsXyTAb1"
        "Bp+AxJjHxMaJi+LQvajAJ6nSBi+j/8ZpWqEc9aH/2MZOvxD91TfsmODoPgsog1N6"
        "eQxVYAD6X+tXGgKbdEYCz/fXsXdgRmxjnWfSKbsCgYBS/GqtYurYCWBPsDn8qfdp"
        "zZjGxaueG8pvY3IhczVbWpBNW24MiWew90BJ7K5TKAevqXYZR8KN4j2XgKYdSVoE"
        "YSBjyIncbBy3p+iFqji0Rbm4WFuoxhxsG3+XoDWFcVOWrIsbvZdNNK8wtX2S+Nvz"
        "1Vysa2Ilpdy0SSRDEQTjIQKBgCoj3gxYqXyq1BWcGYr1Yiwikd+Cibum3UkxwsMW"
        "ri2teQju8ydmqxeW8p+161gczX47ZxnGupJOR7JADhQwv165OtGaATGLbxzwbWzJ"
        "PL28DeF1jiXyZCiUT+EHj9eUjHBVSEMWMwZxT7oe7ZpYBBAU8ZjTE1x26mJyIt80"
        "ThjvAoGAfyZuieaBb71FdTt1L6TvFiJmj6dmEC6stET/4QTUOS8aF9TT+v8+EywM"
        "ikgBK8YHMj3sn8+UjWaxiLmNm4FjTCB574hXlrLkiNMp1JSBdqjIATHEi+7ZEmJO"
        "pc7FnfCX6EQjuaRewLd0/Z6UvjU4VFEPMGGH6+Bn8SQPaV9Moz8="
        "-----END RSA PRIVATE KEY-----";


    ssh_key pk = NULL;
    // 依然使用这个函数，把带有换行和头尾的完整 PEM 字符串传给它
    int rc_import = ssh_pki_import_privkey_base64(privkey_pem, NULL, NULL, NULL, &pk);

    if (rc_import == SSH_OK && pk != NULL) {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_IMPORT_KEY, pk);
        ESP_LOGI(TAG, "终于成功啦！已导入主机密钥 (RSA PEM)");
    } else {
        ESP_LOGE(TAG, "导入主机密钥失败, rc=%d", rc_import);
        goto end;
    }
////////////////////////////////////////////////////////////////////////////
    if (rc_import == SSH_OK && pk != NULL) {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_IMPORT_KEY, pk);
        ESP_LOGI(TAG, "已成功导入硬编码的主机密钥 (ED25519)");
    } else {
        ESP_LOGE(TAG, "导入主机密钥失败, rc=%d", rc_import);
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
    xTaskCreate(ssh_server_listener_task, "ssh_server_task", 16384, NULL, 5, NULL);
}
