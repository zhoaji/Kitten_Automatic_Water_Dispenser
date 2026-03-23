# ESP32-S3 LED 控制与 Wi-Fi 配网示例

基于 ESP-IDF 的 LED 控制示例，支持 Wi-Fi 配网和网页控制。

## 功能特性

- **Wi-Fi 配网**：通过 SoftAP 模式配置 Wi-Fi 账号密码
- **网页控制**：通过浏览器控制 LED 颜色和闪烁频率
- **自动重连**：Wi-Fi 断开后自动尝试重连
- **自动回退**：连接失败自动切换回热点模式
- **串口命令**：通过串口输入命令控制设备
- **LED 控制**：支持 RGB 颜色和闪烁频率调节

## 硬件性能要求

- ESP32-S3 开发板（N16R2版本）
- USB 数据线
- WS2812 可寻址 LED（可选，普通 GPIO LED 也支持）

## 快速开始

### 1. 配置项目

```bash
idf.py menuconfig
```

在 `Example Configuration` 中配置：
- `Blink LED type`: 选择 LED 类型（GPIO 或 RMT）
- `Blink GPIO number`: 设置 LED 引脚
- `Blink period in ms`: 设置默认闪烁周期

### 2. 编译烧录

```bash
# 编译
idf.py build

# 烧录
idf.py -p COMX flash

# 查看日志
idf.py -p COMX monitor
```

## 使用方法

### 首次配网

1. 设备启动后会自动创建 Wi-Fi 热点
2. 手机连接热点：**SSID**: `ESP32-Config`，**密码**: `12345678`
3. 浏览器访问 `http://192.168.4.1` 或自动跳转
4. 输入要连接的 Wi-Fi 名称和密码，提交
5. 设备会自动尝试连接，连接成功后会显示控制页面

### 网页控制

连接成功后，访问设备 IP 地址即可控制：
- LED 开/关
- LED 颜色（RGB）
- 闪烁频率（100-5000ms）
- 重新配网

### 串口命令

设备启动后，在串口终端（115200波特率）输入命令：

| 命令 | 说明 |
|------|------|
| `restart` | 重启 ESP32 |
| `reset` | 清除 Wi-Fi 配置并重启 |
| `ap` | 切换到 AP 模式 |
| `status` | 显示 Wi-Fi 状态 |

### 自动重连机制

- 设备启动时尝试连接保存的 Wi-Fi
- 最多重试 5 次，每次间隔约 1 秒
- 连接失败 5 次后自动清除保存的配置
- 60 秒超时后自动切换回热点模式

## 程序设计

### 架构设计

```
┌─────────────────────────────────────────────────┐
│                    app_main                      │
├─────────────────────────────────────────────────┤
│  1. NVS 初始化                                   │
│  2. 加载保存的 Wi-Fi 配置                        │
│  3. 创建 LED 控制任务                            │
│  4. 创建串口命令任务                            │
│  5. 判断是否有保存的配置                          │
│     ├─ 有配置 → 尝试 STA 连接                     │
│     │    ├─ 成功 → 启动 HTTP 服务器 (STA 模式)   │
│     │    └─ 失败 → 启动 AP 模式                   │
│     └─ 无配置 → 直接启动 AP 模式                  │
└─────────────────────────────────────────────────┘
```

### 关键模块

1. **Wi-Fi 管理**
   - `wifi_init_softap()`: 初始化热点模式
   - `wifi_init_sta()`: 初始化 Station 模式
   - `load_wifi_config()`: 从 NVS 加载配置
   - `clear_wifi_config()`: 清除 NVS 配置

2. **Web 服务器**
   - `/`: LED 控制页面
   - `/config`: Wi-Fi 配网页面
   - `/save_wifi`: 保存 Wi-Fi 配置
   - `/led_state`: LED 开关控制
   - `/led_color`: LED 颜色控制
   - `/led_blink`: 闪烁频率控制
   - `/status`: 获取状态

3. **LED 控制**
   - 使用 RMT 外设驱动 WS2812
   - 或使用普通 GPIO 驱动

### NVS 存储

Wi-Fi 配置保存在 NVS 分区：
- Namespace: `wifi_config`
- Key: `ssid` - Wi-Fi 名称
- Key: `password` - Wi-Fi 密码

### 热点配置

- SSID: `ESP32-Config`
- 密码: `12345678`
- IP: `192.168.4.1`
- 信道: 1

## 常见问题

### 忘记 Wi-Fi 密码怎么办？

1. 串口输入 `reset` 清除配置
2. 或者在配网页面点击"重置 Wi-Fi 配置"

### 串口命令没有反应？

确保：
1. 波特率设置为 115200
2. 没有其他程序占用串口
3. 使用正确的终端软件

### 如何完全清除所有配置？

```bash
idf.py erase_flash
```

这会清除 NVS 分区中的所有数据。

## 文件结构

```
blink2/
├── main/
│   ├── blink_example_main.c    # 主程序代码
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── CMakeLists.txt
├── README.md
└── pytest_blink.py
```

## 技术栈

- ESP-IDF v5.0
- FreeRTOS
- Wi-Fi (STA + AP)
- HTTP Server
- NVS Flash
- LED Strip (RMT)
