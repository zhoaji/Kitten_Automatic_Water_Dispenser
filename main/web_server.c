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
static int current_motor_duty = 50; // 默认50%
static int current_led_state = 1;   // 默认LED开启

// 根据流量计算LED颜色 (流量0-100% -> 绿色到红色)
static void update_led_by_flow(int duty)
{
    uint8_t r, g, b;
    
    if (duty == 0) {
        // 关闭时LED也关闭
        led_mgr_set_state(0);
        return;
    }
    
    // 流量越大，红色越多，绿色越少
    // duty=0 -> 绿色(0,255,0)
    // duty=100 -> 红色(255,0,0)
    r = (duty * 255) / 100;
    g = 255 - r;
    b = 0;
    
    if (current_led_state) {
        led_mgr_set_state(1);
        led_mgr_set_color(r, g, b);
    }
}

// 主页 HTML - 猫咪出水机控制界面
static const char *index_html = 
    "<!DOCTYPE html><html>"
    "<head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>猫咪自动出水机</title>"
    "<style>"
    "* { margin: 0; padding: 0; box-sizing: border-box; }"
    "body {"
    "  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;"
    "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
    "  min-height: 100vh;"
    "  padding: 20px;"
    "}"
    ".container {"
    "  max-width: 480px;"
    "  margin: 0 auto;"
    "  background: white;"
    "  border-radius: 24px;"
    "  box-shadow: 0 20px 60px rgba(0,0,0,0.3);"
    "  overflow: hidden;"
    "}"
    ".header {"
    "  background: linear-gradient(135deg, #ff9a9e 0%, #fecfef 99%, #fecfef 100%);"
    "  padding: 30px 20px;"
    "  text-align: center;"
    "}"
    ".header h1 {"
    "  color: #fff;"
    "  font-size: 28px;"
    "  text-shadow: 2px 2px 4px rgba(0,0,0,0.2);"
    "  margin-bottom: 5px;"
    "}"
    ".header .subtitle {"
    "  color: rgba(255,255,255,0.9);"
    "  font-size: 14px;"
    "}"
    ".cat-icon {"
    "  font-size: 60px;"
    "  margin-bottom: 10px;"
    "}"
    ".content {"
    "  padding: 25px;"
    "}"
    ".status-card {"
    "  background: #f8f9fa;"
    "  border-radius: 16px;"
    "  padding: 20px;"
    "  margin-bottom: 20px;"
    "  text-align: center;"
    "}"
    ".status-icon {"
    "  font-size: 40px;"
    "  margin-bottom: 10px;"
    "}"
    ".status-text {"
    "  font-size: 16px;"
    "  color: #333;"
    "  font-weight: 500;"
    "}"
    ".status-detail {"
    "  font-size: 13px;"
    "  color: #666;"
    "  margin-top: 5px;"
    "}"
    ".control-card {"
    "  background: #fff;"
    "  border: 2px solid #e0e0e0;"
    "  border-radius: 16px;"
    "  padding: 25px;"
    "  margin-bottom: 20px;"
    "}"
    ".control-card h2 {"
    "  color: #333;"
    "  font-size: 18px;"
    "  margin-bottom: 20px;"
    "  display: flex;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"
    ".slider-container {"
    "  margin: 20px 0;"
    "}"
    ".slider-label {"
    "  display: flex;"
    "  justify-content: space-between;"
    "  margin-bottom: 10px;"
    "  font-size: 14px;"
    "  color: #555;"
    "}"
    ".slider-value {"
    "  font-weight: bold;"
    "  color: #667eea;"
    "  font-size: 18px;"
    "}"
    "input[type=\"range\"] {"
    "  width: 100%;"
    "  height: 12px;"
    "  border-radius: 6px;"
    "  background: linear-gradient(to right, #4CAF50, #ff9800, #f44336);"
    "  outline: none;"
    "  -webkit-appearance: none;"
    "}"
    "input[type=\"range\"]::-webkit-slider-thumb {"
    "  -webkit-appearance: none;"
    "  width: 28px;"
    "  height: 28px;"
    "  border-radius: 50%;"
    "  background: white;"
    "  box-shadow: 0 2px 8px rgba(0,0,0,0.2);"
    "  cursor: pointer;"
    "  border: 3px solid #667eea;"
    "}"
    ".flow-indicator {"
    "  display: flex;"
    "  justify-content: space-between;"
    "  margin-top: 15px;"
    "  padding: 15px;"
    "  background: #f5f5f5;"
    "  border-radius: 12px;"
    "}"
    ".flow-level {"
    "  text-align: center;"
    "  flex: 1;"
    "}"
    ".flow-level-icon {"
    "  font-size: 24px;"
    "  margin-bottom: 5px;"
    "}"
    ".flow-level-text {"
    "  font-size: 11px;"
    "  color: #666;"
    "}"
    ".preset-buttons {"
    "  display: grid;"
    "  grid-template-columns: repeat(3, 1fr);"
    "  gap: 10px;"
    "  margin-top: 15px;"
    "}"
    ".preset-btn {"
    "  padding: 12px 8px;"
    "  border: none;"
    "  border-radius: 10px;"
    "  font-size: 13px;"
    "  cursor: pointer;"
    "  transition: all 0.3s;"
    "}"
    ".preset-btn.off {"
    "  background: #ffebee;"
    "  color: #c62828;"
    "}"
    ".preset-btn.low {"
    "  background: #e3f2fd;"
    "  color: #1565c0;"
    "}"
    ".preset-btn.medium {"
    "  background: #e8f5e9;"
    "  color: #2e7d32;"
    "}"
    ".preset-btn.high {"
    "  background: #fff3e0;"
    "  color: #ef6c00;"
    "}"
    ".preset-btn:hover {"
    "  transform: translateY(-2px);"
    "  box-shadow: 0 4px 12px rgba(0,0,0,0.15);"
    "}"
    ".wifi-section {"
    "  background: #f8f9fa;"
    "  border-radius: 16px;"
    "  padding: 20px;"
    "  margin-top: 20px;"
    "}"
    ".wifi-section h3 {"
    "  color: #333;"
    "  font-size: 16px;"
    "  margin-bottom: 15px;"
    "  display: flex;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"
    ".wifi-btn {"
    "  width: 100%;"
    "  padding: 14px;"
    "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
    "  color: white;"
    "  border: none;"
    "  border-radius: 12px;"
    "  font-size: 15px;"
    "  cursor: pointer;"
    "  margin-top: 10px;"
    "}"
    ".wifi-btn:hover {"
    "  opacity: 0.9;"
    "}"
    ".footer {"
    "  text-align: center;"
    "  padding: 20px;"
    "  color: #999;"
    "  font-size: 12px;"
    "}"
    ".toast {"
    "  position: fixed;"
    "  top: 20px;"
    "  left: 50%;"
    "  transform: translateX(-50%);"
    "  background: rgba(0,0,0,0.8);"
    "  color: white;"
    "  padding: 12px 24px;"
    "  border-radius: 30px;"
    "  font-size: 14px;"
    "  opacity: 0;"
    "  transition: opacity 0.3s;"
    "  z-index: 1000;"
    "}"
    ".toast.show {"
    "  opacity: 1;"
    "}"
    ".led-control {"
    "  display: flex;"
    "  align-items: center;"
    "  justify-content: space-between;"
    "  background: #f8f9fa;"
    "  padding: 15px 20px;"
    "  border-radius: 12px;"
    "  margin-bottom: 20px;"
    "}"
    ".led-control-label {"
    "  display: flex;"
    "  align-items: center;"
    "  gap: 10px;"
    "  font-size: 15px;"
    "  color: #333;"
    "}"
    ".led-icon {"
    "  font-size: 24px;"
    "}"
    ".toggle-switch {"
    "  position: relative;"
    "  width: 56px;"
    "  height: 30px;"
    "}"
    ".toggle-switch input {"
    "  opacity: 0;"
    "  width: 0;"
    "  height: 0;"
    "}"
    ".toggle-slider {"
    "  position: absolute;"
    "  cursor: pointer;"
    "  top: 0;"
    "  left: 0;"
    "  right: 0;"
    "  bottom: 0;"
    "  background-color: #ccc;"
    "  transition: 0.4s;"
    "  border-radius: 30px;"
    "}"
    ".toggle-slider:before {"
    "  position: absolute;"
    "  content: \"\";"
    "  height: 24px;"
    "  width: 24px;"
    "  left: 3px;"
    "  bottom: 3px;"
    "  background-color: white;"
    "  transition: 0.4s;"
    "  border-radius: 50%;"
    "}"
    "input:checked + .toggle-slider {"
    "  background-color: #667eea;"
    "}"
    "input:checked + .toggle-slider:before {"
    "  transform: translateX(26px);"
    "}"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"toast\" id=\"toast\"></div>"
    "<div class=\"container\">"
    "  <div class=\"header\">"
    "    <div class=\"cat-icon\">🐱</div>"
    "    <h1>猫咪自动出水机</h1>"
    "    <p class=\"subtitle\">Cat Water Fountain</p>"
    "  </div>"
    "  <div class=\"content\">"
    "    <div class=\"status-card\">"
    "      <div class=\"status-icon\" id=\"statusIcon\">💧</div>"
    "      <div class=\"status-text\" id=\"statusText\">系统正常</div>"
    "      <div class=\"status-detail\" id=\"statusDetail\">正在检测...</div>"
    "    </div>"
    "    <div class=\"led-control\">"
    "      <div class=\"led-control-label\">"
    "        <span class=\"led-icon\" id=\"ledIcon\">💡</span>"
    "        <span>LED 指示灯</span>"
    "      </div>"
    "      <label class=\"toggle-switch\">"
    "        <input type=\"checkbox\" id=\"ledToggle\" checked onchange=\"toggleLed(this.checked)\">"
    "        <span class=\"toggle-slider\"></span>"
    "      </label>"
    "    </div>"
    "    <div class=\"control-card\">"
    "      <h2>🚿 出水流量控制</h2>"
    "      <div class=\"slider-container\">"
    "        <div class=\"slider-label\">"
    "          <span>流量大小</span>"
    "          <span class=\"slider-value\" id=\"dutyValue\">50%</span>"
    "        </div>"
    "        <input type=\"range\" id=\"dutySlider\" min=\"0\" max=\"100\" value=\"50\" oninput=\"updateDuty(this.value)\">"
    "        <div class=\"flow-indicator\">"
    "          <div class=\"flow-level\">"
    "            <div class=\"flow-level-icon\">💧</div>"
    "            <div class=\"flow-level-text\">关闭</div>"
    "          </div>"
    "          <div class=\"flow-level\">"
    "            <div class=\"flow-level-icon\">💦</div>"
    "            <div class=\"flow-level-text\">小流</div>"
    "          </div>"
    "          <div class=\"flow-level\">"
    "            <div class=\"flow-level-icon\">🌊</div>"
    "            <div class=\"flow-level-text\">中流</div>"
    "          </div>"
    "          <div class=\"flow-level\">"
    "            <div class=\"flow-level-icon\">🌊🌊</div>"
    "            <div class=\"flow-level-text\">大流</div>"
    "          </div>"
    "        </div>"
    "      </div>"
    "      <div class=\"preset-buttons\">"
    "        <button class=\"preset-btn off\" onclick=\"setDuty(0)\">🚫 关闭</button>"
    "        <button class=\"preset-btn low\" onclick=\"setDuty(25)\">💧 小流</button>"
    "        <button class=\"preset-btn medium\" onclick=\"setDuty(50)\">💦 中流</button>"
    "        <button class=\"preset-btn high\" onclick=\"setDuty(75)\">🌊 大流</button>"
    "        <button class=\"preset-btn medium\" onclick=\"setDuty(60)\">⛽ 75%</button>"
    "        <button class=\"preset-btn high\" onclick=\"setDuty(100)\">💯 全开</button>"
    "      </div>"
    "    </div>"
    "    <div class=\"wifi-section\">"
    "      <h3>📶 WiFi 设置</h3>"
    "      <div class=\"status-text\" id=\"wifiStatus\">正在连接...</div>"
    "      <button class=\"wifi-btn\" onclick=\"window.location.href='/config'\">📱 重新配置WiFi</button>"
    "    </div>"
    "  </div>"
    "  <div class=\"footer\">"
    "    ESP32-S3 猫咪出水机 v1.0"
    "  </div>"
    "</div>"
    "<script>"
    "function showToast(msg) {"
    "  var toast = document.getElementById('toast');"
    "  toast.textContent = msg;"
    "  toast.classList.add('show');"
    "  setTimeout(function() { toast.classList.remove('show'); }, 2000);"
    "}"
    "function toggleLed(checked) {"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/led_control', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) {"
    "      if (checked) {"
    "        showToast('LED 已开启');"
    "      } else {"
    "        showToast('LED 已关闭');"
    "      }"
    "    }"
    "  };"
    "  xhr.send('state=' + (checked ? '1' : '0'));"
    "}"
    "function updateDuty(value) {"
    "  document.getElementById('dutyValue').textContent = value + '%';"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/motor_speed', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) {"
    "      showToast('流量已设置为 ' + value + '%');"
    "    }"
    "  };"
    "  xhr.send('duty=' + value);"
    "}"
    "function setDuty(value) {"
    "  document.getElementById('dutySlider').value = value;"
    "  document.getElementById('dutyValue').textContent = value + '%';"
    "  updateDuty(value);"
    "}"
    "function updateStatus() {"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) {"
    "      try {"
    "        var data = JSON.parse(xhr.responseText);"
    "        var wifiStatus = document.getElementById('wifiStatus');"
    "        var statusIcon = document.getElementById('statusIcon');"
    "        var statusText = document.getElementById('statusText');"
    "        var statusDetail = document.getElementById('statusDetail');"
    "        if (data.wifi_connected) {"
    "          wifiStatus.innerHTML = '✅ 已连接: ' + data.ssid + '<br>📍 IP: ' + data.ip;"
    "          statusIcon.textContent = '💧';"
    "          statusText.textContent = '系统正常运行中';"
    "          statusDetail.textContent = 'WiFi: ' + data.ssid + ' | IP: ' + data.ip;"
    "        } else if (data.mode === 'AP') {"
    "          wifiStatus.innerHTML = '📡 热点模式<br><a href=\"http://192.168.4.1/config\" style=\"color:#667eea;\">点击配置WiFi</a>';"
    "          statusIcon.textContent = '📡';"
    "          statusText.textContent = '待配置网络';"
    "          statusDetail.textContent = '请连接WiFi进行配置';"
    "        } else {"
    "          wifiStatus.innerHTML = '🔄 连接中...';"
    "          statusIcon.textContent = '⏳';"
    "          statusText.textContent = '正在连接...';"
    "          statusDetail.textContent = '请稍候...';"
    "        }"
        "        if (data.motor_duty !== undefined) {"
    "          document.getElementById('dutySlider').value = data.motor_duty;"
    "          document.getElementById('dutyValue').textContent = data.motor_duty + '%';"
    "        }"
    "        if (data.led_state !== undefined) {"
    "          document.getElementById('ledToggle').checked = (data.led_state == 1);"
    "        }"
    "      } catch(e) { console.log(e); }"
    "    }"
    "  };"
    "  xhr.open('GET', '/status?t=' + new Date().getTime(), true);"
    "  xhr.send();"
    "}"
    "setInterval(updateStatus, 3000);"
    "updateStatus();"
    "</script>"
    "</body>"
    "</html>";

// WiFi配置页面 HTML
static const char *config_html = 
    "<!DOCTYPE html><html>"
    "<head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>WiFi 配置 - 猫咪出水机</title>"
    "<style>"
    "* { margin: 0; padding: 0; box-sizing: border-box; }"
    "body {"
    "  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;"
    "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
    "  min-height: 100vh;"
    "  padding: 20px;"
    "}"
    ".container {"
    "  max-width: 420px;"
    "  margin: 0 auto;"
    "  background: white;"
    "  border-radius: 24px;"
    "  box-shadow: 0 20px 60px rgba(0,0,0,0.3);"
    "  overflow: hidden;"
    "}"
    ".header {"
    "  background: linear-gradient(135deg, #a8edea 0%, #fed6e3 100%);"
    "  padding: 30px 20px;"
    "  text-align: center;"
    "}"
    ".header h1 {"
    "  color: #333;"
    "  font-size: 24px;"
    "  margin-bottom: 5px;"
    "}"
    ".header .subtitle {"
    "  color: #666;"
    "  font-size: 14px;"
    "}"
    ".cat-icon { font-size: 50px; }"
    ".content { padding: 25px; }"
    ".form-group { margin-bottom: 20px; }"
    ".form-group label {"
    "  display: block;"
    "  margin-bottom: 8px;"
    "  color: #333;"
    "  font-size: 14px;"
    "  font-weight: 500;"
    "}"
    ".form-group input {"
    "  width: 100%;"
    "  padding: 14px;"
    "  border: 2px solid #e0e0e0;"
    "  border-radius: 12px;"
    "  font-size: 16px;"
    "  transition: border-color 0.3s;"
    "}"
    ".form-group input:focus {"
    "  outline: none;"
    "  border-color: #667eea;"
    "}"
    ".submit-btn {"
    "  width: 100%;"
    "  padding: 16px;"
    "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
    "  color: white;"
    "  border: none;"
    "  border-radius: 12px;"
    "  font-size: 16px;"
    "  font-weight: bold;"
    "  cursor: pointer;"
    "  transition: opacity 0.3s;"
    "}"
    ".submit-btn:hover { opacity: 0.9; }"
    ".reset-btn {"
    "  width: 100%;"
    "  padding: 14px;"
    "  background: #ff5252;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 12px;"
    "  font-size: 14px;"
    "  cursor: pointer;"
    "  margin-top: 10px;"
    "}"
    ".back-link {"
    "  display: block;"
    "  text-align: center;"
    "  margin-top: 20px;"
    "  color: #667eea;"
    "  text-decoration: none;"
    "  font-size: 14px;"
    "}"
    ".toast {"
    "  position: fixed;"
    "  top: 20px;"
    "  left: 50%;"
    "  transform: translateX(-50%);"
    "  background: rgba(0,0,0,0.8);"
    "  color: white;"
    "  padding: 12px 24px;"
    "  border-radius: 30px;"
    "  font-size: 14px;"
    "  opacity: 0;"
    "  transition: opacity 0.3s;"
    "  z-index: 1000;"
    "}"
    ".toast.show { opacity: 1; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"toast\" id=\"toast\"></div>"
    "<div class=\"container\">"
    "  <div class=\"header\">"
    "    <div class=\"cat-icon\">🐱</div>"
    "    <h1>WiFi 配置</h1>"
    "    <p class=\"subtitle\">连接网络后才能远程控制</p>"
    "  </div>"
    "  <div class=\"content\">"
    "    <form onsubmit=\"submitConfig(event)\">"
    "      <div class=\"form-group\">"
    "        <label>📶 WiFi 名称 (SSID)</label>"
    "        <input type=\"text\" id=\"ssid\" placeholder=\"请输入WiFi名称\" required>"
    "      </div>"
    "      <div class=\"form-group\">"
    "        <label>🔑 WiFi 密码</label>"
    "        <input type=\"password\" id=\"password\" placeholder=\"请输入WiFi密码\">"
    "      </div>"
    "      <button type=\"submit\" class=\"submit-btn\">✅ 保存并连接</button>"
    "    </form>"
    "    <button class=\"reset-btn\" onclick=\"resetConfig()\">🔄 重置WiFi配置</button>"
    "    <a href=\"/\" class=\"back-link\">← 返回控制页面</a>"
    "  </div>"
    "</div>"
    "<script>"
    "function showToast(msg) {"
    "  var toast = document.getElementById('toast');"
    "  toast.textContent = msg;"
    "  toast.classList.add('show');"
    "  setTimeout(function() { toast.classList.remove('show'); }, 3000);"
    "}"
    "function submitConfig(e) {"
    "  e.preventDefault();"
    "  var ssid = document.getElementById('ssid').value;"
    "  var password = document.getElementById('password').value;"
    "  if (!ssid) { showToast('请输入WiFi名称'); return; }"
    "  showToast('正在保存配置...');"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/save_wifi', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4) {"
    "      if (xhr.status == 200) {"
    "        showToast('✅ 配置成功！设备正在重启...');"
    "      } else {"
    "        showToast('❌ 配置失败，请重试');"
    "      }"
    "    }"
    "  };"
    "  xhr.send('ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password));"
    "}"
    "function resetConfig() {"
    "  if (!confirm('确定要重置WiFi配置吗？')) return;"
    "  showToast('正在重置...');"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/reset_wifi', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) {"
    "      showToast('✅ 已重置，设备即将重启');"
    "    }"
    "  };"
    "  xhr.send('reset=1');"
    "}"
    "</script>"
    "</body>"
    "</html>";

// 主页处理器
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

// 配置页面处理器
static esp_err_t config_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, config_html, strlen(config_html));
    return ESP_OK;
}

// 保存WiFi配置
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

// 重置WiFi配置
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

// 电机速度控制
static esp_err_t motor_speed_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    char *p = strstr(content, "duty=");
    if (p) {
        int duty = atoi(p + 5);
        if (duty < 0) duty = 0;
        if (duty > 100) duty = 100;
        motor_mgr_set_duty(duty);
        current_motor_duty = duty;
        
        // 根据流量更新LED颜色
        update_led_by_flow(duty);
    }
    
    httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
    return ESP_OK;
}

// LED控制
static esp_err_t led_control_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    char *p = strstr(content, "state=");
    if (p) {
        int state = atoi(p + 6);
        current_led_state = state;
        
        if (state == 0) {
            led_mgr_set_state(0);
        } else {
            // 开启时根据当前流量设置颜色
            update_led_by_flow(current_motor_duty);
        }
    }
    
    httpd_resp_send(req, "{\"status\":\"ok\"}", 17);
    return ESP_OK;
}

// 状态查询
static esp_err_t status_handler(httpd_req_t *req) {
    char status_json[400];
    
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    if (wifi_mgr_is_connected()) {
        snprintf(status_json, sizeof(status_json),
            "{\"wifi_connected\":true,\"mode\":\"STA\",\"ssid\":\"%s\",\"ip\":\"%s\",\"motor_duty\":%d,\"led_state\":%d}",
            wifi_mgr_get_saved_ssid(), wifi_mgr_get_ip(), current_motor_duty, current_led_state);
    } else {
        if (strlen(wifi_mgr_get_saved_ssid()) > 0) {
            snprintf(status_json, sizeof(status_json),
                "{\"wifi_connected\":false,\"mode\":\"CONNECTING\",\"ssid\":\"%s\",\"ip\":\"\",\"motor_duty\":%d,\"led_state\":%d}",
                wifi_mgr_get_saved_ssid(), current_motor_duty, current_led_state);
        } else {
            snprintf(status_json, sizeof(status_json),
                "{\"wifi_connected\":false,\"mode\":\"AP\",\"ssid\":\"ESP32-Cat\",\"ip\":\"192.168.4.1\",\"motor_duty\":%d,\"led_state\":%d}",
                current_motor_duty, current_led_state);
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, status_json, strlen(status_json));
    return ESP_OK;
}

// URI 路由定义
static const httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
static const httpd_uri_t uri_config = { .uri = "/config", .method = HTTP_GET, .handler = config_get_handler };
static const httpd_uri_t uri_save_wifi = { .uri = "/save_wifi", .method = HTTP_POST, .handler = save_wifi_handler };
static const httpd_uri_t uri_reset_wifi = { .uri = "/reset_wifi", .method = HTTP_POST, .handler = reset_wifi_handler };
static const httpd_uri_t uri_motor_speed = { .uri = "/motor_speed", .method = HTTP_POST, .handler = motor_speed_handler };
static const httpd_uri_t uri_led_control = { .uri = "/led_control", .method = HTTP_POST, .handler = led_control_handler };
static const httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };

// 启动Web服务器
esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_register_uri_handler(http_server, &uri_root);
        httpd_register_uri_handler(http_server, &uri_config);
        httpd_register_uri_handler(http_server, &uri_save_wifi);
        httpd_register_uri_handler(http_server, &uri_reset_wifi);
        httpd_register_uri_handler(http_server, &uri_motor_speed);
        httpd_register_uri_handler(http_server, &uri_led_control);
        httpd_register_uri_handler(http_server, &uri_status);
        
        // 初始化LED颜色
        update_led_by_flow(current_motor_duty);
        
        ESP_LOGI(TAG, "HTTP Server started");
        return ESP_OK;
    }
    return ESP_FAIL;
}

// 停止Web服务器
void stop_web_server(void)
{
    if (http_server) {
        httpd_stop(http_server);
        http_server = NULL;
    }
}
