#pragma once

#include <stdbool.h>

void wifi_mgr_init(void);
void wifi_mgr_start_sta(void);
void wifi_mgr_start_ap(void);
void wifi_mgr_clear_config(void);
void wifi_mgr_save_config(const char* ssid, const char* password);

bool wifi_mgr_is_connected(void);
const char* wifi_mgr_get_saved_ssid(void);
const char* wifi_mgr_get_ip(void);
int wifi_mgr_get_retry_num(void);
int wifi_mgr_get_max_retry(void);
