#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "wifi_provisioning.h"

static const char *TAG = "wifi_prov";

#define DNS_PORT 53

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static wifi_prov_cb_t s_prov_complete_cb = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_provisioning_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t my_handle;
    char ssid[33] = {0};
    size_t length = sizeof(ssid);
    ret = nvs_open("storage", NVS_READONLY, &my_handle);
    if (ret != ESP_OK) return false;

    ret = nvs_get_str(my_handle, "ssid", ssid, &length);
    nvs_close(my_handle);

    return (ret == ESP_OK && strlen(ssid) > 0);
}

void wifi_provisioning_set_callback(wifi_prov_cb_t cb)
{
    s_prov_complete_cb = cb;
}

/* HTML Page for Provisioning */
static const char* INDEX_HTML = 
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>body { font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; background: #f0f2f5; }"
".card { background: white; padding: 2rem; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width: 100%; max-width: 320px; }"
"h2 { color: #1a73e8; margin-bottom: 1.5rem; text-align: center; } input { width: 100%; padding: 10px; margin: 8px 0; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
"button { width: 100%; padding: 10px; background: #1a73e8; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 10px; }"
"button:hover { background: #1557b0; }</style><title>ESP32 WiFi 配网</title></head><body>"
"<div class=\"card\"><h2>智能设备配网</h2><form action=\"/save\" method=\"POST\">"
"<input type=\"text\" name=\"ssid\" placeholder=\"WiFi 名称 (SSID)\" required>"
"<input type=\"password\" name=\"password\" placeholder=\"WiFi 密码\">"
"<button type=\"submit\">保存并连接</button></form></div></body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[128];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char ssid[33] = {0};
    char pass[64] = {0};

    // Simple parser for form-urlencoded
    char *p = strtok(buf, "&");
    while (p != NULL) {
        if (strncmp(p, "ssid=", 5) == 0) strcpy(ssid, p + 5);
        if (strncmp(p, "password=", 9) == 0) strcpy(pass, p + 9);
        p = strtok(NULL, "&");
    }

    ESP_LOGI(TAG, "Received SSID: %s", ssid);

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "ssid", ssid);
        nvs_set_str(my_handle, "password", pass);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }

    httpd_resp_sendstr(req, "<h1>配置已保存，正在重启并连接...</h1>");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}

/* DNS Server Task to redirect all queries to 192.168.4.1 */
static void dns_server_task(void *pvParameters)
{
    uint8_t data[128];
    int len;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t addr_len = sizeof(cliaddr);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(DNS_PORT);
    bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    ESP_LOGI(TAG, "DNS Server started on port %d", DNS_PORT);

    while (1) {
        len = recvfrom(sockfd, data, sizeof(data), 0, (struct sockaddr *)&cliaddr, &addr_len);
        if (len > 0) {
            // Minimal DNS response: Answer with 192.168.4.1 for any query
            if (len > 12) {
                data[2] |= 0x80; // Flags: Response
                data[3] = 0x80;  // Flags: Recursive, No Error
                data[7] = 1;     // Answer count = 1
                
                // Append Answer section: Name(offset to query), Type(A), Class(IN), TTL(60), Len(4), IP(192.168.4.1)
                int offset = len;
                data[offset++] = 0xc0; data[offset++] = 0x0c; // Name offset to Question
                data[offset++] = 0x00; data[offset++] = 0x01; // Type A
                data[offset++] = 0x00; data[offset++] = 0x01; // Class IN
                data[offset++] = 0x00; data[offset++] = 0x00; data[offset++] = 0x00; data[offset++] = 0x3c; // TTL 60
                data[offset++] = 0x00; data[offset++] = 0x04; // Data length 4
                data[offset++] = 192; data[offset++] = 168; data[offset++] = 4; data[offset++] = 1; // IP
                
                sendto(sockfd, data, offset, 0, (struct sockaddr *)&cliaddr, addr_len);
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
        httpd_register_uri_handler(server, &index_uri);
        httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_handler };
        httpd_register_uri_handler(server, &save_uri);
    }
    return server;
}

void wifi_provisioning_start_portal(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32-S3-Setup",
            .ssid_len = strlen("ESP32-S3-Setup"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started. SSID: %s", wifi_config.ap.ssid);
    start_webserver();
    xTaskCreate(dns_server_task, "dns_task", 4096, NULL, 5, NULL);
}

bool wifi_provisioning_connect_saved(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    nvs_handle_t my_handle;
    char ssid[33] = {0};
    char pass[64] = {0};
    size_t s_len = sizeof(ssid), p_len = sizeof(pass);
    nvs_open("storage", NVS_READONLY, &my_handle);
    nvs_get_str(my_handle, "ssid", ssid, &s_len);
    nvs_get_str(my_handle, "password", pass, &p_len);
    nvs_close(my_handle);

    wifi_config_t wifi_config = {0};
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, pass);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        if (s_prov_complete_cb) s_prov_complete_cb();
        return true;
    } else {
        return false;
    }
}
