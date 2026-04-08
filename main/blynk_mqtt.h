#ifndef BLYNK_MQTT_H
#define BLYNK_MQTT_H

#include <stdint.h>
#include <stdbool.h>

// Blynk Credentials (从 blynk_example 移植)
#define BLYNK_TEMPLATE_ID   "TMPL4LYGAq1Jk"
#define BLYNK_TEMPLATE_NAME "esp32s3"
#define BLYNK_AUTH_TOKEN    "l2-UukivG3CEwpjs0dJq-wyKiO42UBvw"

/**
 * @brief 初始化并启动 Blynk MQTT 客户端
 */
void blynk_mqtt_start(void);

/**
 * @brief 上报所有状态至 Blynk (流量, LED状态, 自动停机设置等)
 */
void blynk_mqtt_report_all(void);

/**
 * @brief 仅上报特定数据流的状态
 * @param ds_name 数据流名称 (如 "flow", "swich", "autostop")
 * @param value 整数值
 */
void blynk_mqtt_report_int(const char* ds_name, int value);

#endif // BLYNK_MQTT_H
