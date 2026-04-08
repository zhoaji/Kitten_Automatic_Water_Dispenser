#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "blynk_mqtt.h"

static const char *TAG = "blynk_mqtt";

static esp_mqtt_client_handle_t client;
static blynk_switch_cb_t s_switch_cb = NULL;
static blynk_rgb_cb_t s_rgb_cb = NULL;

static void parse_rgb(const char* data, int len) {
    char buf[32];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    strncpy(buf, data, len);
    buf[len] = '\0';

    uint32_t r, g, b;
    if (sscanf(buf, "%lu,%lu,%lu", &r, &g, &b) == 3) {
        if (s_rgb_cb) s_rgb_cb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    } else {
        // Some widgets send hex or other formats, but usually color picker sends "R,G,B"
        ESP_LOGW(TAG, "Failed to parse RGB data: %s", buf);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected to Blynk");
            esp_mqtt_client_subscribe(client, "ds/write/#", 0);
            esp_mqtt_client_subscribe(client, "downlink/#", 0);
            break;
        case MQTT_EVENT_DATA:
            {
                // Create null-terminated strings for searching
                char topic[64] = {0};
                int t_len = event->topic_len < 63 ? event->topic_len : 63;
                strncpy(topic, event->topic, t_len);

                char data[64] = {0};
                int d_len = event->data_len < 63 ? event->data_len : 63;
                strncpy(data, event->data, d_len);

                ESP_LOGI(TAG, "Received -> Topic: %s, Data: %s", topic, data);

                if (strstr(topic, "swich") != NULL) {
                    int state = atoi(data);
                    ESP_LOGI(TAG, "Detected swich (V1) -> State: %d", state);
                    if (s_switch_cb) s_switch_cb(state);
                } else if (strstr(topic, "RGB") != NULL) {
                    ESP_LOGI(TAG, "Detected RGB (V2) -> Data: %s", data);
                    parse_rgb(data, d_len);
                } else if (strstr(topic, "redirect") != NULL) {
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
        .credentials.client_id = BLYNK_TEMPLATE_ID, // Use Template ID as client ID or anything unique
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void blynk_mqtt_set_switch_cb(blynk_switch_cb_t cb) {
    s_switch_cb = cb;
}

void blynk_mqtt_set_rgb_cb(blynk_rgb_cb_t cb) {
    s_rgb_cb = cb;
}
