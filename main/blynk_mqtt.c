#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "blynk_mqtt.h"
#include "web_server.h"
#include "power_mgr.h"

static const char *TAG = "blynk_mqtt";
static esp_mqtt_client_handle_t client;

/**
 * @brief 解析并处理来自 Blynk 的指令
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to Blynk");
            // 订阅写指令主题 (ds/write/引脚名)
            esp_mqtt_client_subscribe(client, "downlink/ds/#", 0);
            // 启动时同步一次状态
            blynk_mqtt_report_all();
            break;
        case MQTT_EVENT_DATA:
            {
                char topic[64] = {0};
                int t_len = event->topic_len < 63 ? event->topic_len : 63;
                strncpy(topic, event->topic, t_len);

                char data[64] = {0};
                int d_len = event->data_len < 63 ? event->data_len : 63;
                strncpy(data, event->data, d_len);

                ESP_LOGI(TAG, "Received -> Topic: %s, Data: %s", topic, data);

                // 根据主题关键字分发逻辑
                if (strstr(topic, "swich") != NULL) {
                    int state = atoi(data);
                    ESP_LOGI(TAG, "Blynk Command: swich (V1) -> %d", state);
                    web_server_set_led_state(state);
                } 
                else if (strstr(topic, "flow") != NULL) {
                    int duty = atoi(data);
                    ESP_LOGI(TAG, "Blynk Command: flow (V3) -> %d%%", duty);
                    web_server_set_motor_duty(duty);
                } 
                else if (strstr(topic, "power") != NULL) {
                    int power_state = atoi(data);
                    ESP_LOGI(TAG, "Blynk Command: power (V4) -> %d", power_state);
                    if (power_state == 0) {
                        power_mgr_shutdown();
                    } else {
                        power_mgr_wakeup();
                    }
                } 
                else if (strstr(topic, "autostop") != NULL) {
                    int sec = atoi(data);
                    ESP_LOGI(TAG, "Blynk Command: autostop (V5) -> %d sec", sec);
                    web_server_set_auto_stop_sec(sec);
                }
                else if (strstr(topic, "redirect") != NULL) {
                    ESP_LOGI(TAG, "Redirecting to: %s", data);
                    esp_mqtt_client_config_t new_cfg = {
                        .broker.address.uri = data,
                        .credentials.username = "device",
                        .credentials.authentication.password = BLYNK_AUTH_TOKEN,
                        .credentials.client_id = BLYNK_TEMPLATE_ID,
                    };
                    esp_mqtt_set_config(client, &new_cfg);
                    esp_mqtt_client_reconnect(client);
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
        default:
            break;
    }
}

void blynk_mqtt_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://blynk.cloud",
        .broker.address.port = 1883,
        .credentials.username = "device",
        .credentials.authentication.password = BLYNK_AUTH_TOKEN,
        .credentials.client_id = BLYNK_TEMPLATE_ID,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "Blynk MQTT Client Started");
}

void blynk_mqtt_report_int(const char* ds_name, int value) {
    if (!client) return;
    char topic[64];
    char data[16];
    // 使用 ds/update/引脚名 主题上报数据
    snprintf(topic, sizeof(topic), "ds/update/%s", ds_name);
    snprintf(data, sizeof(data), "%d", value);
    esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
}

void blynk_mqtt_report_all(void) {
    // 同步流量
    blynk_mqtt_report_int("flow", web_server_get_motor_duty());
    // 同步LED（这里建议先获取LED真实状态，假设 web_server_get_led_state 存在）
    // 由于 web_server.h 没定义获取 LED 状态的接口，我们暂时用 1 (或者可以加一个)
    // 根据 led_mgr.h 我们可以获取
#include "led_mgr.h"
    blynk_mqtt_report_int("swich", led_mgr_get_state() ? 1 : 0);
    // 同步自动关闭时间
    blynk_mqtt_report_int("autostop", web_server_get_auto_stop_sec());
    // 同步电源状态
    blynk_mqtt_report_int("power", power_mgr_is_sleeping() ? 0 : 1);
}
