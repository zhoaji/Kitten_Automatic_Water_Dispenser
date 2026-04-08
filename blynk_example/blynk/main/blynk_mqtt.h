#ifndef BLYNK_MQTT_H
#define BLYNK_MQTT_H

#include <stdint.h>

// Blynk Credentials provided by User
#define BLYNK_TEMPLATE_ID   "TMPL4LYGAq1Jk"
#define BLYNK_TEMPLATE_NAME "esp32s3"
#define BLYNK_AUTH_TOKEN    "l2-UukivG3CEwpjs0dJq-wyKiO42UBvw"

// Callback types for Blynk events
typedef void (*blynk_switch_cb_t)(int state);
typedef void (*blynk_rgb_cb_t)(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Initialize and start Blynk MQTT client
 */
void blynk_mqtt_start(void);

/**
 * @brief Register switch (V1) callback
 */
void blynk_mqtt_set_switch_cb(blynk_switch_cb_t cb);

/**
 * @brief Register RGB (V2) callback
 */
void blynk_mqtt_set_rgb_cb(blynk_rgb_cb_t cb);

#endif
