#include "wifi_mgr.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi_mgr";

#define ESP_WIFI_SSID      "ESP32-Config"
#define ESP_WIFI_PASS      "12345678"
#define ESP_WIFI_CHANNEL   1
#define MAX_STA_CONN       4
#define MAX_RETRY_COUNT   5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static uint8_t wifi_connected = 0;
static uint8_t wifi_connecting = 0;
static char saved_ssid[32] = {0};
static char saved_password[64] = {0};
static char saved_ip[16] = {0};

bool wifi_mgr_is_connected(void) { return wifi_connected != 0; }
const char* wifi_mgr_get_saved_ssid(void) { return saved_ssid; }
const char* wifi_mgr_get_ip(void) { return saved_ip; }
int wifi_mgr_get_retry_num(void) { return s_retry_num; }
int wifi_mgr_get_max_retry(void) { return MAX_RETRY_COUNT; }

void wifi_mgr_clear_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, "ssid");
        nvs_erase_key(nvs_handle, "password");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        
        memset(saved_ssid, 0, sizeof(saved_ssid));
        memset(saved_password, 0, sizeof(saved_password));
        
        ESP_LOGI(TAG, "Wi-Fi配置已清除");
    }
}

void wifi_mgr_save_config(const char* ssid, const char* password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "ssid", ssid);
        nvs_set_str(nvs_handle, "password", password);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Wi-Fi配置已保存: SSID=%s", ssid);
    }
    
    strncpy(saved_ssid, ssid, sizeof(saved_ssid) - 1);
    strncpy(saved_password, password, sizeof(saved_password) - 1);
}

static void load_wifi_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    
    if (err == ESP_OK) {
        size_t len = sizeof(saved_ssid);
        nvs_get_str(nvs_handle, "ssid", saved_ssid, &len);
        
        len = sizeof(saved_password);
        nvs_get_str(nvs_handle, "password", saved_password, &len);
        
        nvs_close(nvs_handle);
        
        if (strlen(saved_ssid) > 0) {
            ESP_LOGI(TAG, "从NVS加载Wi-Fi配置: SSID=%s", saved_ssid);
        }
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_connecting = 1;
        esp_wifi_connect();
        ESP_LOGI(TAG, "开始连接Wi-Fi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_connecting) {
            s_retry_num++;
            ESP_LOGI(TAG, "Wi-Fi断开连接，重试次数: %d/%d", s_retry_num, MAX_RETRY_COUNT);
            
            if (s_retry_num >= MAX_RETRY_COUNT) {
                ESP_LOGI(TAG, "连接失败次数过多，准备切换到AP模式...");
                wifi_connecting = 0;
                wifi_connected = 0;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            } else {
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_wifi_connect();
            }
        }
        wifi_connected = 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "获得IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(saved_ip, sizeof(saved_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connecting = 0;
        wifi_connected = 1;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "站点已连接到AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "站点已断开");
    }
}

// 清理 STA 模式申请的所有 Wi-Fi 资源，使 AP 模式可以安全地重新初始化
static void wifi_sta_cleanup(void)
{
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "STA 资源已释放");
}

static void wifi_monitor_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        if (!wifi_connected && strlen(saved_ssid) > 0 && s_retry_num >= MAX_RETRY_COUNT) {
            ESP_LOGI(TAG, "Wi-Fi连接失败，准备切换到AP模式，重启...");
            wifi_sta_cleanup();
            esp_restart();
        }
    }
}

void wifi_mgr_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    load_wifi_config();
}

void wifi_mgr_start_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi SoftAP启动完成! SSID: %s 密码: %s", ESP_WIFI_SSID, ESP_WIFI_PASS);
    ESP_LOGI(TAG, "访问 http://192.168.4.1 进行配置");
}

void wifi_mgr_start_sta(void)
{
    s_retry_num = 0;

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, saved_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, saved_password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi STA模式启动，尝试连接SSID: %s", saved_ssid);

    xTaskCreate(wifi_monitor_task, "wifi_monitor", 4096, NULL, 5, NULL);
    
    int wait_count = 0;
    const int max_wait = 300; // 60s
    
    while (wait_count < max_wait) {
        vTaskDelay(pdMS_TO_TICKS(200));
        wait_count++;
        
        if (wifi_connected) {
            ESP_LOGI(TAG, "Wi-Fi连接成功!");
            break;
        }
        
        if (s_retry_num >= MAX_RETRY_COUNT) {
            ESP_LOGI(TAG, "连接失败次数过多 (%d/%d)，准备切换到AP模式", s_retry_num, MAX_RETRY_COUNT);
            wifi_connected = 0;
            wifi_connecting = 0;
            break;
        }
    }
    
    if (wait_count >= max_wait) {
        ESP_LOGI(TAG, "连接超时，准备切换到AP模式");
        wifi_connected = 0;
        wifi_connecting = 0;
    }
    
    // STA 连接最终失败：释放资源，让 AP 模式可以干净地调用 esp_wifi_init()
    if (!wifi_connected) {
        wifi_sta_cleanup();
        if (strlen(saved_ssid) > 0) {
            ESP_LOGI(TAG, "Wi-Fi连接暂时失败，保留配置以备下次重启重试");
            // wifi_mgr_clear_config(); // 不再自动删除配置
        }
    }
}
