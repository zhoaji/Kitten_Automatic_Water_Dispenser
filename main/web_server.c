#include "web_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_mgr.h"
#include "led_mgr.h"
#include "motor_mgr.h"

static const char *TAG = "web_server";
static httpd_handle_t http_server = NULL;

// config_page_html is unused, removed to prevent -Werror=unused-variable

static const char *config_page_full_html = 
    "<!DOCTYPE html><html>"
    "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>ESP32 Wi-Fi 配置</title><style>"
    "body { font-family: Arial; margin: 20px; background: #f0f0f0; }"
    ".container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
    "h1 { text-align: center; color: #333; }"
    "input { width: 100%; padding: 10px; margin: 10px 0; box-sizing: border-box; border: 1px solid #ccc; border-radius: 5px; }"
    "button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }"
    "button:hover { background: #45a049; }"
    ".status { margin-top: 20px; padding: 10px; border-radius: 5px; text-align: center; }"
    ".success { background: #dff0d8; color: #3c763d; }"
    ".error { background: #f2dede; color: #a94442; }"
    ".info { background: #d9edf7; color: #31708f; }"
    ".btn-reset { background: #f44336; margin-top: 10px; }"
    "</style></head>"
    "<body><div class=\"container\">"
    "<h1>Wi-Fi 网络配置</h1>"
    "<p>请输入要连接的Wi-Fi信息：</p>"
    "<input type=\"text\" id=\"ssid\" placeholder=\"Wi-Fi名称 (SSID)\">"
    "<input type=\"password\" id=\"password\" placeholder=\"Wi-Fi密码\">"
    "<button onclick=\"submitConfig()\">提交配置</button>"
    "<button class=\"btn-reset\" onclick=\"resetConfig()\">重置Wi-Fi配置</button>"
    "<div id=\"status\" class=\"status info\">准备就绪</div>"
    "</div><script>"
    "function submitConfig() {"
    "  var ssid = document.getElementById('ssid').value;"
    "  var password = document.getElementById('password').value;"
    "  if (!ssid) { alert('请输入Wi-Fi名称'); return; }"
    "  var status = document.getElementById('status');"
    "  status.className = 'status info'; status.innerHTML = '正在提交配置...';"
    "  var xhr = new XMLHttpRequest(); xhr.open('POST', '/save_wifi', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4) {"
    "      if (xhr.status == 200) { status.className = 'status success'; status.innerHTML = '配置成功！ESP32正在连接Wi-Fi...<br>如连接失败，将自动返回热点模式'; }"
    "      else { status.className = 'status error'; status.innerHTML = '配置失败，请重试'; }"
    "    }"
    "  };"
    "  xhr.send('ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password));"
    "}"
    "function resetConfig() {"
    "  if (!confirm('确定要重置Wi-Fi配置吗？')) return;"
    "  var xhr = new XMLHttpRequest(); xhr.open('POST', '/reset_wifi', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) { alert('Wi-Fi配置已重置，ESP32即将重启'); }"
    "  };"
    "  xhr.send('reset=1');"
    "}"
    "</script></body></html>";

static const char *control_page_html = 
    "<!DOCTYPE html><html>"
    "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>ESP32 LED 控制</title><style>"
    "body { font-family: Arial; margin: 20px; background: #f0f0f0; }"
    ".container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
    "h1 { text-align: center; color: #333; }"
    ".section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }"
    ".section h2 { margin-top: 0; color: #555; }"
    "label { display: block; margin: 10px 0 5px; }"
    "input[type=\"range\"] { width: 100%; }"
    "input[type=\"color\"] { width: 100%; height: 40px; cursor: pointer; }"
    "button { padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; font-size: 14px; }"
    ".btn-on { background: #4CAF50; color: white; }"
    ".btn-off { background: #f44336; color: white; }"
    ".btn-submit { background: #2196F3; color: white; width: 100%; font-size: 16px; }"
    ".btn-submit:hover { background: #1976D2; }"
    ".status { padding: 10px; border-radius: 5px; text-align: center; margin: 10px 0; }"
    ".connected { background: #dff0d8; color: #3c763d; }"
    ".disconnected { background: #f2dede; color: #a94442; }"
    ".color-preview { width: 100%; height: 50px; border-radius: 5px; margin-top: 10px; border: 1px solid #ddd; }"
    ".info { font-size: 12px; color: #666; text-align: center; margin-top: 10px; }"
    "</style></head>"
    "<body><div class=\"container\">"
    "<h1>ESP32 LED 控制</h1>"
    "<div id=\"wifiStatus\" class=\"status disconnected\">Wi-Fi未连接</div>"
    "<div class=\"section\"><h2>LED 开关</h2>"
    "<button class=\"btn-on\" onclick=\"setLedState(1)\">开启</button>"
    "<button class=\"btn-off\" onclick=\"setLedState(0)\">关闭</button></div>"
    "<div class=\"section\"><h2>LED 颜色</h2>"
    "<input type=\"color\" id=\"colorPicker\" value=\"#c89610\" onchange=\"updateColorPreview()\">"
    "<div id=\"colorPreview\" class=\"color-preview\" style=\"background: #c89610;\"></div>"
    "<button class=\"btn-submit\" onclick=\"applyColor()\">应用颜色</button></div>"
    "<div class=\"section\"><h2>闪烁频率</h2>"
    "<label>闪烁周期 (毫秒): <span id=\"periodValue\">1000</span>ms</label>"
    "<input type=\"range\" id=\"blinkPeriod\" min=\"100\" max=\"5000\" step=\"100\" value=\"1000\" oninput=\"document.getElementById('periodValue').innerText = this.value\">"
    "<button class=\"btn-submit\" onclick=\"applyBlinkPeriod()\">应用频率</button></div>"
    "<div class=\"section\"><h2>电机控制 (PWM 25kHz)</h2>"
    "<label>电机占空比: <span id=\"motorDutyValue\">0</span>%</label>"
    "<input type=\"range\" id=\"motorDuty\" min=\"0\" max=\"100\" step=\"1\" value=\"0\" oninput=\"updateMotorDuty(this.value)\" onchange=\"updateMotorDuty(this.value)\">"
    "</div>"
    "<div class=\"section\"><h2>Wi-Fi 状态</h2>"
    "<button class=\"btn-submit\" onclick=\"reconnectWiFi()\">重新配网</button></div>"
    "<p class=\"info\">ESP32-S3 LED Control</p>"
    "</div><script>"
    "function setLedState(state) {"
    "  var xhr = new XMLHttpRequest(); xhr.open('POST', '/led_state', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.send('state=' + state);"
    "}"
    "function updateColorPreview() {"
    "  var color = document.getElementById('colorPicker').value;"
    "  document.getElementById('colorPreview').style.background = color;"
    "}"
    "function applyColor() {"
    "  var color = document.getElementById('colorPicker').value;"
    "  var r = parseInt(color.substr(1,2), 16); var g = parseInt(color.substr(3,2), 16); var b = parseInt(color.substr(5,2), 16);"
    "  var xhr = new XMLHttpRequest(); xhr.open('POST', '/led_color', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.send('r=' + r + '&g=' + g + '&b=' + b);"
    "}"
    "function applyBlinkPeriod() {"
    "  var period = document.getElementById('blinkPeriod').value;"
    "  var xhr = new XMLHttpRequest(); xhr.open('POST', '/led_blink', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.send('period=' + period);"
    "}"
    "function updateMotorDuty(duty) {"
    "  document.getElementById('motorDutyValue').innerText = duty;"
    "  var xhr = new XMLHttpRequest(); xhr.open('POST', '/motor_speed', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.send('duty=' + duty);"
    "}"
    "function reconnectWiFi() { window.location.href = 'http://192.168.4.1/config'; }"
    "function updateStatus() {"
    "  var xhr = new XMLHttpRequest(); xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) {"
    "      try { var data = JSON.parse(xhr.responseText); var status = document.getElementById('wifiStatus');"
    "        if (data.wifi_connected) { status.className = 'status connected'; status.innerHTML = '已连接到路由器: ' + data.ssid + '<br>IP: ' + data.ip; }"
    "        else if (data.mode === 'AP') { status.className = 'status disconnected'; status.innerHTML = '当前为热点配置模式 (无互联网)<br><a href=\"http://192.168.4.1/config\">点击这里配网</a>'; }"
    "        else { status.className = 'status disconnected'; status.innerHTML = 'Wi-Fi连接中...<br><a href=\"http://192.168.4.1/config\">重新配置</a>'; }"
    "      } catch(e) {}"
    "    }"
    "  };"
    "  xhr.open('GET', '/status?t=' + new Date().getTime(), true); xhr.send();"
    "}"
    "setInterval(updateStatus, 2000); updateStatus();"
    "</script></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, control_page_html, strlen(control_page_html));
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, config_page_full_html, strlen(config_page_full_html));
    return ESP_OK;
}

static esp_err_t reset_wifi_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    if (strncmp(content, "reset=1", 7) == 0) {
        wifi_mgr_clear_config();
        httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    httpd_resp_send(req, "{\"status\":\"error\"}", 18);
    return ESP_OK;
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    char content[256];
    char ssid[32] = {0};
    char password[64] = {0};
    
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    char *p = strtok(content, "&");
    while (p) {
        if (strncmp(p, "ssid=", 5) == 0) {
            strncpy(ssid, p + 5, sizeof(ssid) - 1);
            char *s = ssid; char *d = ssid;
            while (*s) {
                if (*s == '+') *s = ' ';
                if (*s == '%' && *(s+1) && *(s+2)) {
                    char hex[3] = {*(s+1), *(s+2), 0};
                    *d++ = strtol(hex, NULL, 16);
                    s += 3;
                } else { *d++ = *s++; }
            }
            *d = '\0';
        } else if (strncmp(p, "password=", 9) == 0) {
            strncpy(password, p + 9, sizeof(password) - 1);
            char *s = password; char *d = password;
            while (*s) {
                if (*s == '+') *s = ' ';
                if (*s == '%' && *(s+1) && *(s+2)) {
                    char hex[3] = {*(s+1), *(s+2), 0};
                    *d++ = strtol(hex, NULL, 16);
                    s += 3;
                } else { *d++ = *s++; }
            }
            *d = '\0';
        }
        p = strtok(NULL, "&");
    }
    
    if (strlen(ssid) > 0) {
        wifi_mgr_save_config(ssid, password);
        httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        httpd_resp_send(req, "{\"status\":\"error\"}", 18);
    }
    return ESP_OK;
}

static esp_err_t led_state_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    if (strncmp(content, "state=1", 7) == 0) led_mgr_set_state(1);
    else if (strncmp(content, "state=0", 7) == 0) led_mgr_set_state(0);
    
    httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
    return ESP_OK;
}

static esp_err_t led_color_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    uint8_t led_r = 0, led_g = 0, led_b = 0;
    led_mgr_get_color(&led_r, &led_g, &led_b);
    
    char *p = strtok(content, "&");
    while (p) {
        if (strncmp(p, "r=", 2) == 0) led_r = atoi(p + 2);
        else if (strncmp(p, "g=", 2) == 0) led_g = atoi(p + 2);
        else if (strncmp(p, "b=", 2) == 0) led_b = atoi(p + 2);
        p = strtok(NULL, "&");
    }
    led_mgr_set_color(led_r, led_g, led_b);
    
    httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
    return ESP_OK;
}

static esp_err_t led_blink_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    char *p = strstr(content, "period=");
    if (p) led_mgr_set_blink_period(atoi(p + 7));
    
    httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
    return ESP_OK;
}

static esp_err_t motor_speed_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    char *p = strstr(content, "duty=");
    if (p) motor_mgr_set_duty(atoi(p + 5));
    
    httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
    char status_json[256];
    
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    if (wifi_mgr_is_connected()) {
        snprintf(status_json, sizeof(status_json),
            "{\"wifi_connected\":true,\"mode\":\"STA\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
            wifi_mgr_get_saved_ssid(), wifi_mgr_get_ip());
    } else {
        if (strlen(wifi_mgr_get_saved_ssid()) > 0) {
            snprintf(status_json, sizeof(status_json),
                "{\"wifi_connected\":false,\"mode\":\"CONNECTING\",\"ssid\":\"%s\",\"ip\":\"\"}",
                wifi_mgr_get_saved_ssid());
        } else {
            snprintf(status_json, sizeof(status_json),
                "{\"wifi_connected\":false,\"mode\":\"AP\",\"ssid\":\"ESP32-Config\",\"ip\":\"192.168.4.1\"}");
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, status_json, strlen(status_json));
    return ESP_OK;
}

static const httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
static const httpd_uri_t uri_config = { .uri = "/config", .method = HTTP_GET, .handler = config_get_handler };
static const httpd_uri_t uri_save_wifi = { .uri = "/save_wifi", .method = HTTP_POST, .handler = save_wifi_handler };
static const httpd_uri_t uri_reset_wifi = { .uri = "/reset_wifi", .method = HTTP_POST, .handler = reset_wifi_handler };
static const httpd_uri_t uri_led_state = { .uri = "/led_state", .method = HTTP_POST, .handler = led_state_handler };
static const httpd_uri_t uri_led_color = { .uri = "/led_color", .method = HTTP_POST, .handler = led_color_handler };
static const httpd_uri_t uri_led_blink = { .uri = "/led_blink", .method = HTTP_POST, .handler = led_blink_handler };
static const httpd_uri_t uri_motor_speed = { .uri = "/motor_speed", .method = HTTP_POST, .handler = motor_speed_handler };
static const httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };

esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &uri_root);
        httpd_register_uri_handler(http_server, &uri_config);
        httpd_register_uri_handler(http_server, &uri_save_wifi);
        httpd_register_uri_handler(http_server, &uri_reset_wifi);
        httpd_register_uri_handler(http_server, &uri_led_state);
        httpd_register_uri_handler(http_server, &uri_led_color);
        httpd_register_uri_handler(http_server, &uri_led_blink);
        httpd_register_uri_handler(http_server, &uri_motor_speed);
        httpd_register_uri_handler(http_server, &uri_status);
        ESP_LOGI(TAG, "HTTP Server started");
        return ESP_OK;
    }
    return ESP_FAIL;
}

void stop_web_server(void)
{
    if (http_server) {
        httpd_stop(http_server);
        http_server = NULL;
    }
}
