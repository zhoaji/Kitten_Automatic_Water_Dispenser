# 🐱 猫咪自动出水机 (Cat Water Fountain)

基于 ESP32-S3 的智能猫咪出水机系统，支持静音 PWM 流量控制、远程终端管理、现代化 Web 控制面板以及智能停机保护。

## 🌟 核心特性

-   **🤫 超静音流量控制**：使用 30kHz 高频 PWM 驱动电机，消除人耳可感知的开关噪声，支持 0-100% 流量精准调节。
-   **📱 现代化 Web 面板**：内置极简、响应式的控制网页，提供实时状态监控、流量滑块、LED 开关及电源管理。
-   **🐚 SSH 远程管理**：支持通过 SSH 协议直接登录设备，无需物理连接即可执行系统命令和调试。
-   **⏳ 智能自动停机**：可配置 0-300 秒无操作自动关泵，有效保护电机并延长水泵寿命。
-   **📱 Blynk IoT 云控制**：支持通过 Blynk 手机 App 跨局域网控制，实现流量调节、远程开关机及状态实时同步。
-   **🌙 低功耗模式**：支持设备休眠与远程唤醒，在满足需求的同时最大程度降低功耗。
-   **🌈 视觉反馈系统**：集成 RGB LED 指示灯，根据当前流量实时变换颜色（绿色 = 小流，红色 = 大流），并同步反馈至 Blynk App。

## 🛠️ 硬件要求

-   **核心板**：ESP32-S3 开发板（推荐 N16R2 版本）
-   **电机引脚**：GPIO 4（支持 PWM 调速，高电平停止，低电平运行）
-   **LED 引脚**：适配常用 RGB LED 引脚
-   **连接线**：USB-C 数据线

## 🚀 快速开始

### 1. 编译与烧录

确保已安装 [ESP-IDF v5.0+](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html) 环境。

```bash
# 1. 编译项目
idf.py build

# 2. 烧录到设备 (替换 COMX 为你的实际串口号)
idf.py -p COMX flash

# 3. 查看系统日志
idf.py -p COMX monitor
```

### 2. 初始配网 (SoftAP 模式)

1.  设备启动后若未连接 WiFi，将自动开启热点。
2.  连接 WiFi：`ESP32-Config`，密码：`12345678`。
3.  手机访问 `http://192.168.4.1`。
4.  在配置页面输入你的家用 WiFi 信息并保存。
5.  设备重启后将尝试连接目标网络。

## 🎮 控制方式

### 1. Web 浏览器控制

连接 WiFi 后，在浏览器输入设备 IP 地址即可进入控制中心：
-   **流量调节**：拖动滑块或使用预设按钮（小流/中流/大流）。
-   **自动停机**：在设置菜单中调整无操作停机时间（20s - 300s）。
-   **电源管理**：点击“关机”进入休眠，点击“开机”远程唤醒。

### 2. SSH 远程终端

支持在局域网内通过终端连接，进行系统调试与控制。

**登录命令：**
```bash
# 将 <ip_address> 替换为设备的实际 IP 地址
ssh <ip_address>

# 或者指定任意用户名（如 root）进行连接
ssh root@<ip_address>
```

**登录凭据：**
- **用户名**：随意输入（如 `admin` 或 `esp32`），代码不校验用户名。
- **密码**：**无需密码**。如果终端提示输入密码，直接按回车（Enter）即可登录。
- **确认连接**：由于是自签主机密钥，首次连接时若提示 `Are you sure you want to continue connecting?`，请输入 `yes`。

**可用命令：**
-   `status`：查看 WiFi、IP、电源及 LED 详细状态。
-   `flow <0-100>`：通过命令行设置出水流量。
-   `led on/off`：开关指示灯。
-   `sleep` / `wakeup`：手动控制电源状态。
-   `help`：显示更多命令帮助。

### 3. 串口命令

波特率：`115200`。支持与 SSH 终端相同的命令集。

### 4. Blynk IoT 远程云控制 (New)

支持连接 Blynk IoT 平台，实现跨地域的远程监控与控制：

-   **配置步骤**：在 `main/blynk_mqtt.h` 中填写您的 `BLYNK_TEMPLATE_ID` 和 `BLYNK_AUTH_TOKEN` 并重新编译烧录。
-   **虚拟引脚映射 (Virtual Pins)**：
    -   **V1 (`swich`)**：LED 指示灯总开关（0 = 关，1 = 开）。
    -   **V3 (`flow`)**：出水流量百分比滑块 (0-100)。
    -   **V4 (`power`)**：设备电源指令（0 = 进入休眠，1 = 唤醒设备）。
    -   **V5 (`autostop`)**：自动停机时长设定（单位：秒）。
-   **实时反馈**：支持全双工同步。无论通过 Web、SSH 还是设备自动停机导致的更改，Blynk App 上的滑块和开关均会实时同步跳变。

## 📂 文件结构

-   `main/motor_mgr.c`：高频 PWM 电机驱动逻辑。
-   `main/web_server.c`：内置网页与 HTTP 处理 API。
-   `main/ssh_server.c`：支持 libssh 的远程终端服务。
-   `main/power_mgr.c`：电源状态、休眠及唤醒管理。
-   `main/wifi_mgr.c`：双模式 WiFi 管理及 NVS 存储。
-   `main/blynk_mqtt.c`：Blynk IoT 云平台连接与状态双向同步逻辑。

## ⚙️ 技术参数

-   **PWM 频率**：30kHz（静音驱动）
-   **SSH 端口**：22
-   **Web 服务器**：HTTP (Port 80)
-   **配网模式**：SoftAP (192.168.4.1)
-   **休眠协议**：Light Sleep (WiFi 保持连接)

## 🐛 常见问题与底层修复说明 (Troubleshooting & Fixes)

在使用 `libssh 0.11.0` (ESP-IDF v5.2.6 环境下) 构建 SSH 服务时，我们进行过以下重要的底层问题修复：

### 1. 修复 `Failed to import private ECDSA host key` 报错
- **原因**：由于 `libssh 0.11.0` 在上游重构了 `ssh_bind_struct` 的内存结构，导致原来例程里采用的“暴力扫描内存指针偏移”方法 (`import_embedded_host_key`) 彻底失效，使私钥被写入了错误的内存地址导致绑定失败。
- **修复动作**：删除了脆弱的内存修改 Hack 函数。现已全面换用 `libssh 0.11.0` 原生提供的 `SSH_BIND_OPTIONS_IMPORT_KEY_STR` 宏，直接调用官方函数将硬编码的 Ed25519 密钥文本正确注入内存。

### 2. 修复客户端连接时立刻产生 `Connection reset` 并造成芯片 `InstrFetchProhibited` 重启
- **原因**：当 SSH 客户端 (`ssh root@ip`) 刚刚接通时，老版本 libssh 内置的防攻击机制会自动调用一遍 `ssh_reseed()` 去刷新随机数种子以防备 Linux 系统中 `fork()` 导致的随机数重复。但在没有 `fork` 的 FreeRTOS 下，强行调用 `mbedtls_ctr_drbg_reseed` 会与 ESP32 正在由 WiFi 子系统高频使用的硬件底层 RNG (随机数生成器) 产生激烈冲突并引发空指针指令调用异常 (`PC : 0x00000000`)。
- **修复动作**：深入修改了 `managed_components/david-cermak__libssh/libssh-0.11.0/src/libmbedcrypto.c` 的核心代码。通过给 `ssh_reseed()` 函数中的调用加上 `#ifndef ESP_PLATFORM` 的预编译宏指令，在 ESP32 平台上彻底物理屏蔽了这一多余且危险的行为。

### 3. 修复 `Key exchange error: PRNG error` 问题
- **原因**：`libssh` 依赖 MbedTLS 内部的 `ctr_drbg` 子系统生成会话 Cookie 随机数。但在某些使用原生硬件随机发生器的 ESP-IDF 配置中，`mbedtls_ctr_drbg_seed` 在初始化时会因状态不合要求而沉默返回失败事件。这样当客户端建立验证会话需要获取随机数时，程序就会由于 DRBG 未启动而爆出 `PRNG error` 并阻断连接。
- **修复动作**：直接修改了 `managed_components/.../getrandom_mbedcrypto.c` 里的随机数接口底层 `ssh_mbedtls_random`。使用宏指令劫持了该函数，使其在 `ESP_PLATFORM` 环境下直接绕过老旧低效的 MbedTLS 伪随机数算法引擎，直达调用 ESP32 原生的硬件加密真随机数函数 `esp_fill_random()`。这不仅大幅提升了握手计算速度，更一劳永逸地斩断了随机数种子崩溃相关的各类 Bug。

### 4. 修复握手时爆出 `Out of memory` 并引发 `IntegerDivideByZero`内核崩溃
- **原因**：由于 ESP32 考虑到内存空间与硬件加速，其 MbedTLS 默认并未编译耗费巨大运算的软加密算法 `ChaCha20`，导致其在密码库结构体中留作了空壳（`keysize = 0` 及 `blocksize = 0`）。然而，`libssh` 源代码中的能力宣告宏（`kex.c`）竟然写死了对老客户端广播自身支持 `chacha20-poly1305`。电脑端的 OpenSSH 连接时优先选了该高级加密算法，随后 libssh 执行密钥申请分配时触发了 `malloc(0)` 产生假性 `Out of memory`，并且在准备底层数据包阶段由于尝试以 `0` 作为取模运算的除数，当场引发底层数学异常，造成极其严重的 `IntegerDivideByZero` 系统级死机。
- **修复动作**：直捣黄龙修改了 `managed_components/.../kex.c` 中的宏定义区。为 `ESP_PLATFORM` 新增了判断过滤，直接注销注空了 `CHACHA20` 的宏声明。这一举两得：彻底排除了 MbedTLS 编译精简引起的死机怪圈，也强制引导电脑端 OpenSSH 退而求其次，换用 ESP32 全面自带**硬件级别加速**通道的 `AES` 算法系列，极大幅度地加强了 SSH 数据层面的传输性能和系统整体续航！

---
*Powered by ESP-IDF & Love for Cats.* 🐱💦
