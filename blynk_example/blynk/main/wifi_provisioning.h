#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <stdbool.h>

/**
 * @brief Initialize NVS and check for saved WiFi credentials
 * 
 * @return true if credentials exist and loaded
 * @return false if no credentials found
 */
bool wifi_provisioning_init_nvs(void);

/**
 * @brief Start the captive portal (AP Mode + HTTP Server)
 */
void wifi_provisioning_start_portal(void);

/**
 * @brief Try to connect to the saved WiFi
 * 
 * @return true if connection successful
 * @return false if connection failed
 */
bool wifi_provisioning_connect_saved(void);

/**
 * @brief Register a callback for when provisioning is complete
 */
typedef void (*wifi_prov_cb_t)(void);
void wifi_provisioning_set_callback(wifi_prov_cb_t cb);

#endif
