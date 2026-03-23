#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the LED hardware and start the control task
 */
void led_mgr_init(void);

/**
 * @brief Turn the LED on/off
 * @param state 1 for enabled, 0 for disabled
 */
void led_mgr_set_state(uint8_t state);

/**
 * @brief Set the LED color
 */
void led_mgr_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set the blink period in milliseconds
 */
void led_mgr_set_blink_period(uint32_t period);

// Getters for status API
bool led_mgr_get_state(void);
void led_mgr_get_color(uint8_t *r, uint8_t *g, uint8_t *b);
uint32_t led_mgr_get_blink_period(void);
