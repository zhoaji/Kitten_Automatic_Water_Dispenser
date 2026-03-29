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
#include "power_mgr.h"

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
    "  padding: 16px 20px 14px;"
    "  display: flex;"
    "  align-items: center;"
    "  justify-content: space-between;"
    "}"
    ".header-left {"
    "  display: flex;"
    "  align-items: center;"
    "  gap: 10px;"
    "}"
    ".header-icon { font-size: 30px; }"
    ".header-title {"
    "  color: #fff;"
    "  font-size: 18px;"
    "  font-weight: 700;"
    "  text-shadow: 1px 1px 3px rgba(0,0,0,0.15);"
    "  line-height: 1.2;"
    "}"
    ".header-sub {"
    "  color: rgba(255,255,255,0.85);"
    "  font-size: 11px;"
    "  margin-top: 2px;"
    "}"
    ".settings-btn {"
    "  background: rgba(255,255,255,0.25);"
    "  border: none;"
    "  border-radius: 50%;"
    "  width: 40px;"
    "  height: 40px;"
    "  font-size: 20px;"
    "  cursor: pointer;"
    "  display: flex;"
    "  align-items: center;"
    "  justify-content: center;"
    "  transition: background 0.2s, transform 0.3s;"
    "  color: white;"
    "  flex-shrink: 0;"
    "}"
    ".settings-btn:hover { background: rgba(255,255,255,0.4); transform: rotate(30deg); }"
    ".status-bar {"
    "  display: flex;"
    "  align-items: center;"
    "  gap: 8px;"
    "  background: #f0f4ff;"
    "  padding: 9px 18px;"
    "  border-bottom: 1px solid #e8eaf6;"
    "}"
    ".status-dot {"
    "  width: 8px; height: 8px;"
    "  border-radius: 50%;"
    "  background: #4CAF50;"
    "  flex-shrink: 0;"
    "  box-shadow: 0 0 0 3px rgba(76,175,80,0.2);"
    "}"
    ".status-dot.sleeping { background: #bbb; box-shadow: 0 0 0 3px rgba(187,187,187,0.2); }"
    ".status-bar-text {"
    "  font-size: 13px;"
    "  color: #555;"
    "  flex: 1;"
    "  white-space: nowrap;"
    "  overflow: hidden;"
    "  text-overflow: ellipsis;"
    "}"
    ".status-bar-icon { font-size: 13px; }"
    ".content { padding: 20px; }"
    ".control-card {"
    "  background: #fff;"
    "  border: 2px solid #e0e0e0;"
    "  border-radius: 16px;"
    "  padding: 22px;"
    "  margin-bottom: 16px;"
    "}"
    ".control-card h2 {"
    "  color: #333;"
    "  font-size: 17px;"
    "  margin-bottom: 18px;"
    "  display: flex;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"
    ".slider-container { margin: 16px 0; }"
    ".slider-label {"
    "  display: flex;"
    "  justify-content: space-between;"
    "  margin-bottom: 10px;"
    "  font-size: 14px;"
    "  color: #555;"
    "}"
    ".slider-value { font-weight: bold; color: #667eea; font-size: 18px; }"
    "input[type=\"range\"] {"
    "  width: 100%; height: 12px; border-radius: 6px;"
    "  background: linear-gradient(to right, #4CAF50, #ff9800, #f44336);"
    "  outline: none; -webkit-appearance: none;"
    "}"
    "input[type=\"range\"]::-webkit-slider-thumb {"
    "  -webkit-appearance: none; width: 26px; height: 26px; border-radius: 50%;"
    "  background: white; box-shadow: 0 2px 8px rgba(0,0,0,0.2);"
    "  cursor: pointer; border: 3px solid #667eea;"
    "}"
    ".flow-indicator {"
    "  display: flex; justify-content: space-between;"
    "  margin-top: 12px; padding: 12px; background: #f5f5f5; border-radius: 12px;"
    "}"
    ".flow-level { text-align: center; flex: 1; }"
    ".flow-level-icon { font-size: 22px; margin-bottom: 4px; }"
    ".flow-level-text { font-size: 11px; color: #666; }"
    ".preset-buttons {"
    "  display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 14px;"
    "}"
    ".preset-btn {"
    "  padding: 11px 6px; border: none; border-radius: 10px;"
    "  font-size: 13px; cursor: pointer; transition: all 0.3s;"
    "}"
    ".preset-btn.off { background: #ffebee; color: #c62828; }"
    ".preset-btn.low { background: #e3f2fd; color: #1565c0; }"
    ".preset-btn.medium { background: #e8f5e9; color: #2e7d32; }"
    ".preset-btn.high { background: #fff3e0; color: #ef6c00; }"
    ".preset-btn:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.15); }"
    ".footer { text-align: center; padding: 14px; color: #bbb; font-size: 11px; }"
    ".toast {"
    "  position: fixed; top: 20px; left: 50%; transform: translateX(-50%);"
    "  background: rgba(0,0,0,0.8); color: white; padding: 10px 22px;"
    "  border-radius: 30px; font-size: 14px; opacity: 0; transition: opacity 0.3s; z-index: 2000;"
    "}"
    ".toast.show { opacity: 1; }"
    ".toggle-switch { position: relative; width: 52px; height: 28px; flex-shrink: 0; }"
    ".toggle-switch input { opacity: 0; width: 0; height: 0; }"
    ".toggle-slider {"
    "  position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;"
    "  background-color: #ccc; transition: 0.4s; border-radius: 28px;"
    "}"
    ".toggle-slider:before {"
    "  position: absolute; content: \"\"; height: 22px; width: 22px;"
    "  left: 3px; bottom: 3px; background-color: white; transition: 0.4s; border-radius: 50%;"
    "}"
    "input:checked + .toggle-slider { background-color: #667eea; }"
    "input:checked + .toggle-slider:before { transform: translateX(24px); }"
    ".modal-overlay {"
    "  display: none; position: fixed; inset: 0;"
    "  background: rgba(0,0,0,0.45); z-index: 1500;"
    "  align-items: flex-end; justify-content: center;"
    "}"
    ".modal-overlay.open { display: flex; }"
    ".modal-sheet {"
    "  background: white; border-radius: 24px 24px 0 0;"
    "  width: 100%; max-width: 480px; padding: 8px 0 28px;"
    "  animation: slideUp 0.3s ease;"
    "}"
    "@keyframes slideUp { from { transform: translateY(100%); } to { transform: translateY(0); } }"
    ".modal-handle {"
    "  width: 40px; height: 4px; background: #ddd; border-radius: 2px;"
    "  margin: 10px auto 18px;"
    "}"
    ".modal-title {"
    "  font-size: 16px; font-weight: 700; color: #333;"
    "  padding: 0 22px 14px; border-bottom: 1px solid #f0f0f0; display: flex; align-items: center; gap: 8px;"
    "}"
    ".modal-body { padding: 0 22px; }"
    ".setting-row {"
    "  display: flex; align-items: center; justify-content: space-between;"
    "  padding: 16px 0; border-bottom: 1px solid #f5f5f5;"
    "}"
    ".setting-row:last-child { border-bottom: none; }"
    ".setting-label { display: flex; align-items: center; gap: 12px; }"
    ".setting-label-icon { font-size: 22px; }"
    ".setting-label-name { font-size: 15px; color: #333; font-weight: 500; }"
    ".setting-label-sub { font-size: 12px; color: #888; margin-top: 2px; }"
    ".wifi-btn {"
    "  padding: 10px 16px;"
    "  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
    "  color: white; border: none; border-radius: 10px;"
    "  font-size: 13px; cursor: pointer; white-space: nowrap; font-weight: 500;"
    "}"
    ".wifi-btn:hover { opacity: 0.9; }"
    ".power-btn {"
    "  padding: 10px 16px; border: none; border-radius: 10px;"
    "  font-size: 13px; font-weight: bold; cursor: pointer;"
    "  transition: all 0.3s; white-space: nowrap;"
    "}"
    ".power-btn.power-off {"
    "  background: linear-gradient(135deg, #ff5252, #c62828);"
    "  color: white; box-shadow: 0 3px 8px rgba(255,82,82,0.35);"
    "}"
    ".power-btn.power-on {"
    "  background: linear-gradient(135deg, #43e97b, #38f9d7);"
    "  color: #1a5c3a; box-shadow: 0 3px 8px rgba(67,233,123,0.35);"
    "}"
    ".power-btn:hover { transform: translateY(-1px); opacity: 0.92; }"
    ".func-row {"
    "  display: flex; align-items: center; justify-content: space-between;"
    "  padding: 14px 0;"
    "}"
    ".func-row-label {"
    "  display: flex; align-items: center; gap: 12px;"
    "}"
    ".func-row-icon { font-size: 22px; }"
    ".func-row-name { font-size: 15px; color: #333; font-weight: 500; }"
    ".func-row-sub { font-size: 12px; color: #888; margin-top: 2px; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"toast\" id=\"toast\"></div>"
    "<div class=\"modal-overlay\" id=\"settingsModal\" onclick=\"closeSettings(event)\">"
    "  <div class=\"modal-sheet\">"
    "    <div class=\"modal-handle\"></div>"
    "    <div class=\"modal-title\">⚙️ 设置</div>"
    "    <div class=\"modal-body\">"
    "      <div class=\"setting-row\">"
    "        <div class=\"setting-label\">"
    "          <span class=\"setting-label-icon\">📶</span>"
    "          <div>"
    "            <div class=\"setting-label-name\">WiFi 网络</div>"
    "            <div class=\"setting-label-sub\" id=\"wifiStatus\">正在检测...</div>"
    "          </div>"
    "        </div>"
    "        <button class=\"wifi-btn\" onclick=\"window.location.href='/config'\">🔄 重新配置</button>"
    "      </div>"
    "    </div>"
    "  </div>"
    "</div>"
    "<div class=\"container\">"
    "  <div class=\"header\">"
    "    <div class=\"header-left\">"
    "      <span class=\"header-icon\">🐱</span>"
    "      <div>"
    "        <div class=\"header-title\">猫咪自动出水机</div>"
    "        <div class=\"header-sub\">Cat Water Fountain</div>"
    "      </div>"
    "    </div>"
    "    <button class=\"settings-btn\" onclick=\"openSettings()\">⚙️</button>"
    "  </div>"
    "  <div class=\"status-bar\">"
    "    <div class=\"status-dot\" id=\"statusDot\"></div>"
    "    <span class=\"status-bar-icon\" id=\"statusIcon\">💧</span>"
    "    <span class=\"status-bar-text\" id=\"statusText\">正在检测...</span>"
    "  </div>"
    "  <div class=\"content\">"
    "    <div class=\"control-card\">"
    "      <h2>🚿 出水流量控制</h2>"
    "      <div class=\"slider-container\">"
    "        <div class=\"slider-label\">"
    "          <span>流量大小</span>"
    "          <span class=\"slider-value\" id=\"dutyValue\">50%</span>"
    "        </div>"
    "        <input type=\"range\" id=\"dutySlider\" min=\"0\" max=\"100\" value=\"50\" oninput=\"updateDuty(this.value)\">"
    "        <div class=\"flow-indicator\">"
    "          <div class=\"flow-level\"><div class=\"flow-level-icon\">💧</div><div class=\"flow-level-text\">关闭</div></div>"
    "          <div class=\"flow-level\"><div class=\"flow-level-icon\">💦</div><div class=\"flow-level-text\">小流</div></div>"
    "          <div class=\"flow-level\"><div class=\"flow-level-icon\">🌊</div><div class=\"flow-level-text\">中流</div></div>"
    "          <div class=\"flow-level\"><div class=\"flow-level-icon\">🌊🌊</div><div class=\"flow-level-text\">大流</div></div>"
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
    "    <div class=\"control-card\">"
    "      <div class=\"func-row\">"
    "        <div class=\"func-row-label\">"
    "          <span class=\"func-row-icon\">💡</span>"
    "          <span class=\"func-row-name\">LED 指示灯</span>"
    "        </div>"
    "        <label class=\"toggle-switch\">"
    "          <input type=\"checkbox\" id=\"ledToggle\" checked onchange=\"toggleLed(this.checked)\">"
    "          <span class=\"toggle-slider\"></span>"
    "        </label>"
    "      </div>"
    "      <div class=\"func-row\" style=\"border-top:1px solid #f0f0f0;margin-top:0;\">"
    "        <div class=\"func-row-label\">"
    "          <span class=\"func-row-icon\" id=\"powerIcon\">🟢</span>"
    "          <div>"
    "            <div class=\"func-row-name\">设备电源</div>"
    "            <div class=\"func-row-sub\" id=\"powerStatusText\">正常运行中</div>"
    "          </div>"
    "        </div>"
    "        <button class=\"power-btn power-off\" id=\"powerBtn\" onclick=\"powerControl()\">⏻ 关机</button>"
    "      </div>"
    "    </div>"
    "  </div>"
    "  <div class=\"footer\">ESP32-S3 猫咪出水机 v1.0</div>"
    "</div>"
    "<script>"
    "function showToast(msg) {"
    "  var t = document.getElementById('toast');"
    "  t.textContent = msg; t.classList.add('show');"
    "  setTimeout(function() { t.classList.remove('show'); }, 2000);"
    "}"
    "function openSettings() { document.getElementById('settingsModal').classList.add('open'); }"
    "function closeSettings(e) {"
    "  if (e.target === document.getElementById('settingsModal'))"
    "    document.getElementById('settingsModal').classList.remove('open');"
    "}"
    "var isSleeping = false;"
    "function powerControl() {"
    "  var action = isSleeping ? 'wakeup' : 'shutdown';"
    "  var msg = isSleeping ? '确定要开机吗？' : '确定要关机？设备将进入低功耗休眠状态。';"
    "  if (!confirm(msg)) return;"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/power', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) {"
    "      if (action === 'shutdown') { showToast('😴 设备已进入休眠'); updatePowerUI(true); }"
    "      else { showToast('✅ 设备已唤醒'); updatePowerUI(false); }"
    "    }"
    "  };"
    "  xhr.send('action=' + action);"
    "}"
    "function updatePowerUI(sleeping) {"
    "  isSleeping = sleeping;"
    "  var btn = document.getElementById('powerBtn');"
    "  var icon = document.getElementById('powerIcon');"
    "  var sub = document.getElementById('powerStatusText');"
    "  var dot = document.getElementById('statusDot');"
    "  var stIcon = document.getElementById('statusIcon');"
    "  var stText = document.getElementById('statusText');"
    "  if (sleeping) {"
    "    btn.textContent = '⏻ 开机'; btn.className = 'power-btn power-on';"
    "    icon.textContent = '🔴'; sub.textContent = '低功耗休眠中';"
    "    dot.className = 'status-dot sleeping';"
    "    stIcon.textContent = '😴'; stText.textContent = '设备已休眠 - WiFi保持连接';"
    "  } else {"
    "    btn.textContent = '⏻ 关机'; btn.className = 'power-btn power-off';"
    "    icon.textContent = '🟢'; sub.textContent = '正常运行中';"
    "    dot.className = 'status-dot';"
    "  }"
    "}"
    "function toggleLed(checked) {"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/led_control', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200)"
    "      showToast(checked ? 'LED 已开启' : 'LED 已关闭');"
    "  };"
    "  xhr.send('state=' + (checked ? '1' : '0'));"
    "}"
    "function updateDuty(value) {"
    "  document.getElementById('dutyValue').textContent = value + '%';"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('POST', '/motor_speed', true);"
    "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
    "  xhr.onreadystatechange = function() {"
    "    if (xhr.readyState == 4 && xhr.status == 200) showToast('流量已设置为 ' + value + '%');"
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
    "        var d = JSON.parse(xhr.responseText);"
    "        var sleeping = !!d.sleeping;"
    "        updatePowerUI(sleeping);"
    "        var stIcon = document.getElementById('statusIcon');"
    "        var stText = document.getElementById('statusText');"
    "        var wifiSt = document.getElementById('wifiStatus');"
    "        if (!sleeping) {"
    "          if (d.wifi_connected) {"
    "            stIcon.textContent = '💧';"
    "            stText.textContent = '正常运行 | ' + d.ssid + ' · ' + d.ip;"
    "            wifiSt.textContent = '✅ ' + d.ssid + ' | ' + d.ip;"
    "          } else if (d.mode === 'AP') {"
    "            stIcon.textContent = '📡'; stText.textContent = '热点模式 - 请配置WiFi';"
    "            wifiSt.textContent = '📡 热点模式';"
    "          } else {"
    "            stIcon.textContent = '⏳'; stText.textContent = '正在连接WiFi...';"
    "            wifiSt.textContent = '🔄 连接中...';"
    "          }"
    "        }"
    "        if (d.motor_duty !== undefined) {"
    "          document.getElementById('dutySlider').value = d.motor_duty;"
    "          document.getElementById('dutyValue').textContent = d.motor_duty + '%';"
    "        }"
    "        if (d.led_state !== undefined)"
    "          document.getElementById('ledToggle').checked = (d.led_state == 1);"
    "      } catch(e) {}"
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
        power_mgr_set_saved_duty(duty);  // 同步保存供唤醒恢复
        
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
    char status_json[450];
    int sleeping = power_mgr_is_sleeping() ? 1 : 0;
    
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    if (wifi_mgr_is_connected()) {
        snprintf(status_json, sizeof(status_json),
            "{\"wifi_connected\":true,\"mode\":\"STA\",\"ssid\":\"%s\",\"ip\":\"%s\",\"motor_duty\":%d,\"led_state\":%d,\"sleeping\":%d}",
            wifi_mgr_get_saved_ssid(), wifi_mgr_get_ip(), current_motor_duty, current_led_state, sleeping);
    } else {
        if (strlen(wifi_mgr_get_saved_ssid()) > 0) {
            snprintf(status_json, sizeof(status_json),
                "{\"wifi_connected\":false,\"mode\":\"CONNECTING\",\"ssid\":\"%s\",\"ip\":\"\",\"motor_duty\":%d,\"led_state\":%d,\"sleeping\":%d}",
                wifi_mgr_get_saved_ssid(), current_motor_duty, current_led_state, sleeping);
        } else {
            snprintf(status_json, sizeof(status_json),
                "{\"wifi_connected\":false,\"mode\":\"AP\",\"ssid\":\"ESP32-Cat\",\"ip\":\"192.168.4.1\",\"motor_duty\":%d,\"led_state\":%d,\"sleeping\":%d}",
                current_motor_duty, current_led_state, sleeping);
        }
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, status_json, strlen(status_json));
    return ESP_OK;
}

// 电源控制（开机/关机）
static esp_err_t power_control_handler(httpd_req_t *req) {
    char content[64];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) return ESP_FAIL;
    content[ret] = '\0';
    
    char *p = strstr(content, "action=");
    if (p) {
        p += 7; // 跳过 "action="
        if (strncmp(p, "shutdown", 8) == 0) {
            ESP_LOGI(TAG, "收到关机命令");
            power_mgr_shutdown();
            current_motor_duty = 0;    // 同步UI显示状态
            current_led_state = 0;
            httpd_resp_send(req, "{\"status\":\"ok\",\"action\":\"shutdown\"}", -1);
        } else if (strncmp(p, "wakeup", 6) == 0) {
            ESP_LOGI(TAG, "收到开机命令");
            power_mgr_wakeup();
            current_motor_duty = power_mgr_get_saved_duty(); // 恢复UI显示状态
            current_led_state = 1;
            httpd_resp_send(req, "{\"status\":\"ok\",\"action\":\"wakeup\"}", -1);
        } else {
            httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"unknown action\"}", -1);
        }
    } else {
        httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"missing action\"}", -1);
    }
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
static const httpd_uri_t uri_power = { .uri = "/power", .method = HTTP_POST, .handler = power_control_handler };

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
        httpd_register_uri_handler(http_server, &uri_power);
        
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
