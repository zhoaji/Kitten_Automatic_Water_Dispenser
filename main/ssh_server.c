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

// 借鉴 example-master 的硬编码主机密钥
static const char *hardcoded_example_host_key =
	"-----BEGIN OPENSSH PRIVATE KEY-----\n"
	"b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAABlwAAAAdzc2gtcn\n"
	"NhAAAAAwEAAQAAAYEA7bUljOjNKb26WbxV4DQEZlfCIjiKM2uYpoRugv6mR7lCYyFKZNy9\n"
	"3oxeKo/eVy4RVklKbiiFp6mcYBo9BUjVtTQ5gZZJ/rSw8oVvsJp8t5i7zazKLhraF9E1NA\n"
	"9yRoaZLPAl+K1R+Sa/2lkx/rQSFelnrHVKCjXSFkqr1z4llg47qZVBo6Q3f7L5DLRB+B5e\n"
	"r1nNiEvGwlUDP6k/43p8oKAoGLGGReUpanE3FRCy6Fj7rJ7lz3Hp6vBKWJoiwP2hllpr1J\n"
	"ZngxzxSs5pthhChPpYl/tF4bDBVfS94IwKSiP0Vnl5jPdidm7Vrt+p2xjnUNkNtmos2qPf\n"
	"8epkWJl76cyGRnzgcTjHa/3TUIVNBQwXAqmJHHzX+ZLdymOwYmVSiquvLmlJO3ZJAnKpHL\n"
	"eFjeLqxGyb/iVJsN7qgQB77FcU1jijR6+Alv4ksqmqVGgTCeJUgmnLkIvu7sGxgi0+BPsm\n"
	"ozPRLR1zACmIxdMLOWv6WvPS9kT0abvROQSFrlvvAAAFmIUydiGFMnYhAAAAB3NzaC1yc2\n"
	"EAAAGBAO21JYzozSm9ulm8VeA0BGZXwiI4ijNrmKaEboL+pke5QmMhSmTcvd6MXiqP3lcu\n"
	"EVZJSm4ohaepnGAaPQVI1bU0OYGWSf60sPKFb7CafLeYu82syi4a2hfRNTQPckaGmSzwJf\n"
	"itUfkmv9pZMf60EhXpZ6x1Sgo10hZKq9c+JZYOO6mVQaOkN3+y+Qy0QfgeXq9ZzYhLxsJV\n"
	"Az+pP+N6fKCgKBixhkXlKWpxNxUQsuhY+6ye5c9x6erwSliaIsD9oZZaa9SWZ4Mc8UrOab\n"
	"YYQoT6WJf7ReGwwVX0veCMCkoj9FZ5eYz3YnZu1a7fqdsY51DZDbZqLNqj3/HqZFiZe+nM\n"
	"hkZ84HE4x2v901CFTQUMFwKpiRx81/mS3cpjsGJlUoqrry5pSTt2SQJyqRy3hY3i6sRsm/\n"
	"4lSbDe6oEAe+xXFNY4o0evgJb+JLKpqlRoEwniVIJpy5CL7u7BsYItPgT7JqMz0S0dcwAp\n"
	"iMXTCzlr+lrz0vZE9Gm70TkEha5b7wAAAAMBAAEAAAGBAOD1XBIch30HRwKBkCvcToWka9\n"
	"8C7xd2rkJ4djWWVTrvgnpaGROXLEEfSkaxXNPYjyO/vKa/xq1DgPAaJMGJimYwhHO1DVX1\n"
	"HriFu4vAyGLgMmuVKMm1M8zyeo1ISPehjfjPVMAhFsDaARrc6smHFM6T0z+MyIMdKDNce3\n"
	"/6GowF8ESvMi1xzewWLkftl7j+1NDSBgcE35ct6SMoQ4Q+eQ9yQkAMUWx4UVegyWYwJYBq\n"
	"JdPZlNdbkOp8eX+cb2OBIsYjJd0sl38RqCiPxzrRADv0g+A8vEwvX1T8+zNRbacS1PSAed\n"
	"Tyo/0sqYZui4i2JuulLQV8t1tX8mRr4FbvWNxf0KyTNhk7cFntB/M2TQS1RecKrbPOR2fH\n"
	"SQ0stok4U+nakwmlyq7vV9/NJaN/md+InkUZqary7D1y3lK2mwN6q39aUcJqLN+Fbb6Phn\n"
	"z/sW/hz9lUKHd1+vMUs/UIV5RP6Rorq2Q4E6SKttBlbQ0lQKozNrzeLBOt4iVTz9+cAQAA\n"
	"AMBxCtacS7jK8RLTXSBkuHA6SjaF8XCgloiUuzGKiQMamCCG4t7WNmNNrCzl43uX5x2HyA\n"
	"gllzdib0H7qBBeV+AhstXEaorshLpkvCVLAIMY18PL8VVIhAcyM4nwE0rT2DeKuU2UZyEe\n"
	"2vBbV1XQgJQtS9cOjrTkOMTgumqwDzzdgUb0CzXeadm+YSWJ7FtQuTtE/zl5AUma2uJ2pX\n"
	"JkPlCUQld8Sj8g8UYPOAhQItGOYCL1M0BRE8GhSSbTHyBHB28AAADBAPyCba9q8pOw3ISg\n"
	"1SmNLoYOz6KrzeXEC5m+87uXMvTZ470DxRs8YKOWFoUIfdl9Eham8n8ylFT85Skw5R4xjP\n"
	"pDRlcfWqgO63u/x6FU3AFDe8QivQn/FbRv7Jjln/yQoNUxtkEVSAU49OdWIvVNXXkhj1c9\n"
	"lK+d5gwLzVULZtsiUAFidzHOIFA1slnaLRlKbaLN/U1WGiSY+k4wbIpXNn43fS8Y8jzQnW\n"
	"/mQfBtGO2O1AgyV3od56ztVyQUNOyG7wAAAMEA8P5WfYXIHYnzkBUYraD/2WwRcg87t8FO\n"
	"b+xRcd3t2/6e/J1UAHwOz0k4VgerxA0tbRA/ztcfb313NDau1yXE70ki02fY1KPa8TPXwi\n"
	"7pztm5nRQWx3oWrbfLmxW4aBS3YSG4ptNr35wtPGqrYcgYJvjsWtUzhMuyEOvMOTxsnu59\n"
	"JubTlEItwZ4/28ocWtCVJmltbOolU0oDNaxTUQ5q7puV7ge2Ze4ELX80EKkttuYQ50heDh\n"
	"l9rTiUsxla43sBAAAAHHRubkB0MzYxMC5yeW1kZmFydHN2ZXJrZXQuc2UBAgMEBQY=\n"
	"-----END OPENSSH PRIVATE KEY-----\n";

typedef struct {
    bool authenticated;
} ssh_session_data_t;

/*
 * 从 example-master 中借鉴的热修复方法：暴力指针修改绑定的主机密钥
 * 因为上游并不支持直接通过内存加载主机私钥，这个函数用来修改 struct ssh_bind_struct 的内存
 */
static int import_embedded_host_key(ssh_bind sshbind, const char *base64_key)
{
	size_t ptralign = sizeof(void*);
	char buf[2048];
	char *p, *q, *e;
	ssh_key *target;
	int error;
	ssh_key probe;
	enum ssh_keytypes_e type;

	ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, "");
	memcpy(buf, sshbind, sizeof(buf));
	ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR,
			     "0123456789ABCDEF0123456789ABCDEF");
	p = buf;
	e = p + sizeof(buf);
	q = (char*)sshbind;
	while (p < e) {
		if (memcmp(p, q, ptralign) != 0)
			break;
		p += ptralign;
		q += ptralign;
	}
	if (p >= e)
		return SSH_ERROR;
	probe = ssh_key_new();
	if (probe == NULL)
		return SSH_ERROR;
	error = ssh_pki_import_privkey_base64(base64_key, NULL, NULL, NULL,
					      &probe);
	type = ssh_key_type(probe);
	ssh_key_free(probe);
	if (error != SSH_OK)
		return error;
	switch (type) {
	case SSH_KEYTYPE_ECDSA_P256:
	case SSH_KEYTYPE_ECDSA_P521:
		target = (ssh_key*)((uintptr_t)sshbind + (p - buf)
				    - 4 * ptralign);
		break;
	case SSH_KEYTYPE_DSS:
		target = (ssh_key*)((uintptr_t)sshbind + (p - buf)
				    - 3 * ptralign);
		break;
	case SSH_KEYTYPE_RSA:
		target = (ssh_key*)((uintptr_t)sshbind + (p - buf)
				    - 2 * ptralign);
		break;
	case SSH_KEYTYPE_ED25519:
		target = (ssh_key*)((uintptr_t)sshbind + (p - buf)
				    - 1 * ptralign);
		break;
	default:
		return SSH_ERROR;
	}
	error = ssh_pki_import_privkey_base64(base64_key, NULL, NULL, NULL,
					      target);
	return error;
}

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
    
    // 借鉴 example-master，利用 API Hack 将硬编码的主机密钥写入
    if (import_embedded_host_key(sshbind, hardcoded_example_host_key) != SSH_OK) {
        ESP_LOGE(TAG, "Failed to import embedded host key");
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
    ssh_bind_free(sshbind);
    ssh_finalize();
    vTaskDelete(NULL);
}

void ssh_server_init(void)
{
    ESP_LOGI(TAG, "Starting SSH Server Task...");
    xTaskCreate(ssh_server_listener_task, "ssh_server_task", 16384, NULL, 5, NULL);
}
