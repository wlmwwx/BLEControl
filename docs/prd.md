# ESP32-C3 Virtual Bluetooth Keyboard & Mouse

## API Controlled BLE HID Automation Device

**产品版本：V1.0 PRD**

**项目类型：硬件 + 固件 + API**

**目标平台：Android / iOS**

**核心芯片：ESP32-C3**

---

# 1. 产品概述

## 1.1 产品名称

暂定：

**BTControl**

或：

**BLE HID Controller**

产品定位：

> 一个基于 ESP32-C3 的 BLE HID 输入设备，通过网络 API 接收自动化指令，并将指令转换为 Bluetooth HID Keyboard / Mouse 输入发送给 Android 或 iOS 设备。

整体工作方式：

```text
          API Client
              │
              │ HTTP / WebSocket
              ▼
       ┌───────────────┐
       │   ESP32-C3    │
       │               │
       │ API Server    │
       │ Command Queue │
       │ HID Engine    │
       └───────┬───────┘
               │
               │ Bluetooth LE HID
               ▼
       ┌─────────────────┐
       │ Android / iOS   │
       │                 │
       │ Keyboard        │
       │ Mouse           │
       └─────────────────┘
```

设备本身不需要安装 Android/iOS App 即可完成标准 Keyboard / Mouse 功能。

---

# 2. 产品目标

## 2.1 核心目标

实现一个可以被程序控制的虚拟 Bluetooth HID 输入设备。

用户可以通过 HTTP API：

```text
POST /keyboard/type
POST /keyboard/key
POST /mouse/move
POST /mouse/click
POST /mouse/scroll
POST /mouse/drag
POST /input/tap
POST /input/swipe
POST /input/move_to
```

控制手机。

例如：

```http
POST /keyboard/type
Content-Type: application/json

{
  "text": "Hello World"
}
```

ESP32-C3 将字符串转换成 HID Keyboard Report。

---

# 3. 非目标

以下能力不作为 V1 的硬性承诺：

1. 绕过 Android/iOS 安全机制。
2. 绕过 App 权限。
3. 在 iOS 上向任意 App 注入原生 Touch Event。
4. 在 Android/iOS 上保证任意位置的绝对 Touch。
5. 在没有任何校准机制的情况下保证 `move_to(x,y)` 精确到像素。
6. 模拟手机内部的系统级 Input Injection API。

---

# 4. 最关键的技术可行性分析

这是整个项目最需要提前确认的部分。

## 4.1 Keyboard

### 可行性：★★★★★

ESP32-C3：

```text
BLE
 ↓
HID over GATT
 ↓
Keyboard
 ↓
Android / iOS
```

可以实现：

* A-Z
* a-z
* 0-9
* Enter
* Escape
* Backspace
* Tab
* Space
* Arrow
* Function Keys
* Ctrl
* Alt
* Shift
* Command
  -组合键

例如：

```json
{
  "keys": ["CTRL", "C"]
}
```

发送：

```text
Ctrl Down
C Down
C Up
Ctrl Up
```

---

# 5. Mouse

### 可行性：★★★★★

BLE HID Mouse 可以发送：

```text
X movement
Y movement
Button
Wheel
```

例如：

```json
{
  "dx": 100,
  "dy": -50
}
```

转换为 HID Mouse Report：

```text
X = +100
Y = -50
```

可以实现：

* Move
* Left Click
* Right Click
* Middle Click
* Double Click
* Mouse Down
* Mouse Up
* Drag
* Scroll

---

# 6. Touch 能力的技术边界

这是本产品最重要的限制。

标准 Bluetooth Mouse：

```text
Move(dx, dy)
Click()
Scroll()
```

并不是：

```text
Touch(x, y)
TouchDown(x, y)
TouchMove(x, y)
TouchUp(x, y)
```

也就是说：

```text
BLE Mouse
    ↓
Android/iOS
    ↓
Pointer / Mouse Input
```

和：

```text
Touchscreen
    ↓
Touch Event
    ↓
Application
```

并不是同一个输入通道。

Apple 的 HID 体系本身区分 keyboard、pointing device、digitizer 等不同 HID 类型。

因此 PRD 中应该把：

```text
Mouse
Touch
```

设计成两个不同能力层。

---

# 7. Touch 功能定义

产品提供统一 API：

```http
POST /touch/tap
POST /touch/swipe
POST /touch/down
POST /touch/move
POST /touch/up
```

但是具体实现根据设备模式决定。

## Mode A：BLE Mouse Mode

通过 Mouse HID 模拟：

```text
move
click
drag
scroll
```

这是 V1 必须支持的模式。

---

## Mode B：BLE Digitizer / Touchpad Mode

研究 HID Digitizer / Touchpad Report Descriptor。

目标：

```text
Touch Down
Touch Move
Touch Up
```

并携带：

```text
X
Y
Contact ID
Pressure
Contact State
```

但必须在 Android / iOS 真机上验证。

**不能在 PRD 中直接承诺所有 Android/iOS 设备都将其识别为 Touch Event。**

---

## Mode C：Phone Agent

如果未来必须实现：

```text
tap(x,y)
swipe(x1,y1,x2,y2)
long_press(x,y)
```

则可以增加手机端 Agent。

架构：

```text
API Client
     │
     ▼
ESP32-C3
     │
     ▼
BLE
     │
     ▼
Android Agent
     │
     ▼
Android Input / Accessibility
```

Android 可以作为后续专项方案研究。

iOS 则需要单独评估系统权限和 Apple 平台限制。

---

# 8. Move To 功能定义

用户要求：

```text
move_to(x, y)
```

这里必须明确：

**BLE Mouse 原生提供的是相对移动，不是绝对坐标。**

因此：

```text
move(dx, dy)
```

天然可实现。

而：

```text
move_to(x, y)
```

需要建立：

```text
当前鼠标位置
+
目标屏幕坐标
+
屏幕尺寸
+
鼠标移动灵敏度
```

之间的映射。

---

# 9. MoveTo 两种实现方式

## 9.1 Relative Move

API：

```json
{
  "dx": 100,
  "dy": 50
}
```

最可靠。

---

## 9.2 Calibrated MoveTo

API：

```json
{
  "x": 500,
  "y": 300
}
```

设备内部维护：

```text
current_x
current_y
```

然后：

```text
dx = target_x - current_x
dy = target_y - current_y
```

转换成多个 HID Mouse Report：

```text
Report 1: dx=127
Report 2: dx=127
Report 3: dx=127
...
```

但是这个功能必须经过：

```text
屏幕尺寸校准
鼠标速度校准
坐标系校准
```

因此定义为：

**V1.5 / Experimental**

而不是 V1 核心能力。

---

# 10. 产品功能结构

```text
BTControl
│
├── Bluetooth HID
│   ├── Keyboard
│   ├── Mouse
│   ├── Consumer Control
│   └── Experimental Digitizer
│
├── Command Engine
│   ├── Command Parser
│   ├── Command Queue
│   ├── Scheduler
│   └── Rate Limiter
│
├── API Server
│   ├── HTTP REST API
│   └── WebSocket
│
├── Device Manager
│   ├── BLE Pairing
│   ├── Connection
│   ├── Reconnect
│   └── Device Status
│
├── Configuration
│   ├── WiFi
│   ├── Device Name
│   ├── API Token
│   └── HID Settings
│
└── Diagnostics
    ├── Logs
    ├── Metrics
    └── Error Codes
```

---

# 11. 系统架构

## 11.1 硬件

核心：

```text
ESP32-C3
```

建议硬件：

```text
ESP32-C3
    │
    ├── BLE
    │
    ├── WiFi
    │
    ├── Status LED
    │
    ├── Reset Button
    │
    └── Boot Button
```

可选：

```text
USB-C
Li-ion Battery
Battery Charger
OLED
Buzzer
```

---

# 12. ESP32-C3 固件架构

推荐：

**ESP-IDF + NimBLE + FreeRTOS**

ESP32-C3 的 NimBLE 是轻量 BLE Host，适合资源受限设备。

软件结构：

```text
main/
│
├── main.c
│
├── ble/
│   ├── ble_manager.c
│   ├── hid_keyboard.c
│   ├── hid_mouse.c
│   └── hid_digitizer.c
│
├── api/
│   ├── http_server.c
│   ├── websocket.c
│   └── api_auth.c
│
├── command/
│   ├── command_parser.c
│   ├── command_queue.c
│   └── command_executor.c
│
├── device/
│   ├── device_config.c
│   ├── wifi_manager.c
│   └── device_manager.c
│
└── system/
    ├── logger.c
    ├── watchdog.c
    └── metrics.c
```

---

# 13. API 通信方式

建议第一版：

**HTTP REST API**

原因：

* 简单
* Python / Go / Node.js 都容易调用
* Postman 可以直接测试
* 自动化框架容易集成
* 后期可以增加 WebSocket

通信：

```text
Controller
    │
    │ HTTP
    ▼
ESP32-C3
```

---

# 14. API 基础设计

Base URL：

```text
http://<device-ip>/api/v1
```

---

# 15. Device API

## 获取设备状态

```http
GET /api/v1/device
```

Response：

```json
{
  "device_id": "btcontrol-001",
  "name": "BTControl",
  "firmware": "1.0.0",
  "wifi": true,
  "bluetooth": true,
  "hid_connected": true,
  "hid_type": "keyboard_mouse",
  "battery": 87
}
```

---

# 16. Bluetooth API

## 获取 BLE 状态

```http
GET /api/v1/bluetooth
```

Response：

```json
{
  "enabled": true,
  "connected": true,
  "device_name": "BTControl",
  "peer_name": "iPhone",
  "hid_mode": "keyboard_mouse"
}
```

---

## 开始配对

```http
POST /api/v1/bluetooth/pair
```

---

## 断开

```http
POST /api/v1/bluetooth/disconnect
```

---

# 17. Keyboard API

## 按键

```http
POST /api/v1/keyboard/key
```

Request：

```json
{
  "key": "ENTER"
}
```

---

## 组合键

```http
POST /api/v1/keyboard/hotkey
```

Request：

```json
{
  "keys": [
    "CTRL",
    "C"
  ]
}
```

---

## 输入文本

```http
POST /api/v1/keyboard/type
```

Request：

```json
{
  "text": "Hello World"
}
```

---

## Key Down

```http
POST /api/v1/keyboard/down
```

```json
{
  "key": "SHIFT"
}
```

---

## Key Up

```http
POST /api/v1/keyboard/up
```

```json
{
  "key": "SHIFT"
}
```

---

# 18. Mouse API

## 移动

```http
POST /api/v1/mouse/move
```

Request：

```json
{
  "dx": 100,
  "dy": 50
}
```

---

## 点击

```http
POST /api/v1/mouse/click
```

```json
{
  "button": "left"
}
```

---

## 双击

```http
POST /api/v1/mouse/double_click
```

---

## Mouse Down

```http
POST /api/v1/mouse/down
```

```json
{
  "button": "left"
}
```

---

## Mouse Up

```http
POST /api/v1/mouse/up
```

---

## Scroll

```http
POST /api/v1/mouse/scroll
```

```json
{
  "dx": 0,
  "dy": -5
}
```

---

# 19. Drag API

```http
POST /api/v1/mouse/drag
```

Request：

```json
{
  "dx": 500,
  "dy": 0,
  "duration_ms": 500
}
```

内部执行：

```text
Mouse Down

Move 10px
Move 10px
Move 10px
...
Move 10px

Mouse Up
```

这样比一次性发送一个巨大位移更加可靠。

---

# 20. Touch API

为了让上层 API 不依赖具体 HID 实现，定义统一抽象。

## Tap

```http
POST /api/v1/touch/tap
```

```json
{
  "x": 500,
  "y": 800
}
```

---

## Swipe

```http
POST /api/v1/touch/swipe
```

```json
{
  "x1": 500,
  "y1": 1000,
  "x2": 500,
  "y2": 300,
  "duration_ms": 500
}
```

---

## Touch Down

```http
POST /api/v1/touch/down
```

```json
{
  "x": 500,
  "y": 500
}
```

---

## Touch Move

```http
POST /api/v1/touch/move
```

```json
{
  "x": 550,
  "y": 500
}
```

---

## Touch Up

```http
POST /api/v1/touch/up
```

---

# 21. MoveTo API

```http
POST /api/v1/mouse/move_to
```

```json
{
  "x": 500,
  "y": 300
}
```

Response：

```json
{
  "success": true,
  "mode": "calibrated_relative",
  "target": {
    "x": 500,
    "y": 300
  }
}
```

注意：

该 API 在 V1 中定义为：

**Experimental**

---

# 22. Command API

除了单个 API，还建议设计一个通用 Command API。

```http
POST /api/v1/commands
```

Request：

```json
{
  "commands": [
    {
      "type": "keyboard.type",
      "text": "Hello"
    },
    {
      "type": "keyboard.key",
      "key": "ENTER"
    },
    {
      "type": "mouse.move",
      "dx": 100,
      "dy": 50
    },
    {
      "type": "mouse.click",
      "button": "left"
    }
  ]
}
```

这样可以一次提交完整自动化流程。

---

# 23. Command Queue

ESP32 内部建立：

```text
API
 ↓
Command Parser
 ↓
Command Queue
 ↓
Command Executor
 ↓
HID Report
```

例如：

```text
TYPE("hello")
      ↓
KEY(H)
KEY(E)
KEY(L)
KEY(L)
KEY(O)
      ↓
HID
```

---

# 24. Command ID

每个请求生成：

```text
command_id
```

例如：

```json
{
  "command_id": "cmd_01JABC123",
  "status": "queued"
}
```

查询：

```http
GET /api/v1/commands/{command_id}
```

返回：

```json
{
  "command_id": "cmd_01JABC123",
  "status": "completed"
}
```

状态：

```text
queued
running
completed
failed
cancelled
```

---

# 25. Batch 自动化

未来可以：

```http
POST /api/v1/workflows
```

例如：

```json
{
  "name": "login",
  "steps": [
    {
      "action": "keyboard.type",
      "text": "username"
    },
    {
      "action": "keyboard.key",
      "key": "TAB"
    },
    {
      "action": "keyboard.type",
      "text": "password"
    },
    {
      "action": "keyboard.key",
      "key": "ENTER"
    }
  ]
}
```

这会让产品从：

> 虚拟蓝牙键盘

升级为：

> **硬件自动化输入设备**

---

# 26. API Authentication

不能让局域网任何设备都可以控制手机。

至少支持：

```http
Authorization: Bearer <token>
```

例如：

```http
Authorization: Bearer bt_abcdef123456
```

设备启动时：

```text
Generate API Token
```

或者通过配置接口设置。

---

# 27. 配网

第一次启动：

```text
ESP32-C3
   │
   ├── BLE Provisioning
   │
   └── WiFi AP
```

推荐支持：

```text
BTControl-XXXX
```

用户连接 AP：

```text
192.168.4.1
```

配置：

```text
WiFi SSID
WiFi Password
Device Name
API Token
```

配置完成：

```text
ESP32
 ↓
连接家庭/局域网 WiFi
 ↓
启动 HTTP API
```

---

# 28. API Discovery

为了避免用户不知道 ESP32 IP，可以提供：

```text
mDNS
```

例如：

```text
http://btcontrol.local
```

这样 Controller 可以：

```text
GET http://btcontrol.local/api/v1/device
```

---

# 29. WebSocket

V1.5 增加：

```text
ws://btcontrol.local/api/v1/ws
```

用于：

```text
设备状态
BLE connection
Command status
Error
Log
```

例如：

```json
{
  "event": "hid.connected",
  "device": "iPhone"
}
```

---

# 30. Controller SDK

为了方便自动化测试，建议官方提供：

```text
Python SDK
```

例如：

```python
from btcontrol import Device

device = Device("http://btcontrol.local")

device.keyboard.type("hello")
device.keyboard.press("ENTER")

device.mouse.move(100, 50)
device.mouse.click()
```

进一步：

```python
device.touch.tap(500, 300)
device.touch.swipe(
    500, 1000,
    500, 300,
    duration=0.5
)
```

底层全部转换成 HTTP API。

---

# 31. CLI

同时提供：

```bash
btctl device status
```

```bash
btctl keyboard type "Hello"
```

```bash
btctl keyboard press ENTER
```

```bash
btctl mouse move 100 50
```

```bash
btctl mouse click
```

```bash
btctl mouse scroll -5
```

这对于测试工程师非常方便。

---

# 32. 状态机

BLE HID：

```text
DISCONNECTED
      │
      ▼
ADVERTISING
      │
      ▼
CONNECTING
      │
      ▼
CONNECTED
      │
      ├──── connection lost
      │
      ▼
RECONNECTING
```

API：

```text
STARTING
   ↓
READY
   ↓
BUSY
   ↓
READY
```

---

# 33. 双连接问题

设备实际上存在两条逻辑连接：

```text
                 ┌── BLE HID ──→ Phone
ESP32-C3
                 └── WiFi HTTP ─→ Controller
```

这是整个系统非常关键的架构。

Controller：

```text
PC
 │
 │ WiFi
 ▼
ESP32
 │
 │ BLE
 ▼
Phone
```

因此：

**PC 不需要直接连接手机。**

---

# 34. 推荐的数据流

例如：

```text
Python
  │
  │ POST /mouse/move
  ▼
ESP32 HTTP Server
  │
  ▼
Command Queue
  │
  ▼
Mouse HID Engine
  │
  ▼
BLE HID Report
  │
  ▼
iPhone
```

---

# 35. HID Report 层

建议将 HID Report 单独抽象。

```text
HID Device
│
├── Keyboard Report
│
├── Mouse Report
│
├── Consumer Report
│
└── Digitizer Report
```

Keyboard：

```text
Modifier
Reserved
Key1
Key2
Key3
Key4
Key5
Key6
```

Mouse：

```text
Buttons
X
Y
Wheel
```

Digitizer：

```text
Contact ID
Tip Switch
X
Y
Pressure
```

这样未来更换 HID Descriptor 不会影响 API。

---

# 36. HID Profile

建议 V1：

```text
Keyboard
Mouse
Consumer Control
```

Consumer Control 可以支持：

```text
Volume Up
Volume Down
Mute
Play
Pause
Next
Previous
Power
```

---

# 37. Android 兼容性

目标：

```text
Android 10+
```

重点测试：

* Pixel
* Samsung
* Xiaomi
* OnePlus

测试：

```text
Keyboard
Mouse
Scroll
Click
Modifier
Consumer Control
```

---

# 38. iOS 兼容性

目标：

```text
iOS / iPadOS
```

但这里必须建立单独的 Compatibility Matrix。

例如：

| 功能       |      Android |       iPhone |         iPad |
| -------- | -----------: | -----------: | -----------: |
| Keyboard |           P0 |           P0 |           P0 |
| Mouse    |           P0 |           P1 |           P0 |
| Click    |           P0 |           P1 |           P0 |
| Scroll   |           P0 |           P1 |           P0 |
| Hotkey   |           P0 |           P1 |           P0 |
| Touch    | Experimental | Experimental | Experimental |
| MoveTo   | Experimental | Experimental | Experimental |

尤其是 iOS，不能简单按照 Android 的 HID 行为推断。Espressif 的 ESP-AT 文档甚至特别提示其 BLE HID 与 iOS 产品交互涉及 MFi 认证要求，因此量产前必须做实际兼容性和认证评估。

---

# 39. 性能指标

## API 延迟

目标：

```text
HTTP API → HID Report

P50 < 20ms
P95 < 50ms
```

---

## Keyboard

目标：

```text
≥ 100 key events/sec
```

---

## Mouse

目标：

```text
≥ 100 reports/sec
```

实际值根据 BLE connection interval 和 HID report 配置测试确定。

---

# 40. 稳定性

目标：

```text
连续运行 24h
```

要求：

* 不 crash
* 不 memory leak
* BLE 不异常断连
* WiFi 不异常断连
* command queue 不死锁
* watchdog 不频繁触发

---

# 41. BLE 自动重连

手机断开：

```text
Phone
  ↓
BLE disconnected
  ↓
ESP32
  ↓
Advertising
  ↓
Phone reconnect
```

不需要重新配置设备。

---

# 42. 安全要求

API 必须至少支持：

```text
Bearer Token
```

并建议：

```text
Rate Limit
```

例如：

```text
1000 requests/sec
```

根据设备实际能力调整。

同时提供：

```http
POST /api/v1/security/token/regenerate
```

重新生成 Token。

---

# 43. 日志

日志等级：

```text
ERROR
WARN
INFO
DEBUG
```

例如：

```text
INFO BLE connected: iPhone
INFO HID keyboard ready
INFO API request: keyboard.type
INFO Command completed
WARN BLE disconnected
```

禁止默认输出敏感数据，例如完整 API Token。

---

# 44. 错误码

例如：

```text
1001 DEVICE_NOT_READY
1002 BLE_NOT_CONNECTED
1003 WIFI_NOT_CONNECTED
1004 INVALID_KEY
1005 INVALID_MOUSE_BUTTON
1006 COMMAND_QUEUE_FULL
1007 INVALID_COORDINATE
1008 UNSUPPORTED_HID_OPERATION
1009 AUTH_FAILED
1010 COMMAND_TIMEOUT
```

---

# 45. API 错误格式

统一：

```json
{
  "success": false,
  "error": {
    "code": 1002,
    "message": "BLE HID device is not connected"
  }
}
```

---

# 46. 配置 API

```http
GET /api/v1/config
```

```json
{
  "device_name": "BTControl",
  "hid_mode": "keyboard_mouse",
  "api_enabled": true,
  "mouse_speed": 1.0,
  "auto_reconnect": true
}
```

---

# 47. Firmware OTA

建议 V1.5 加入：

```text
OTA Update
```

接口：

```http
POST /api/v1/firmware/update
```

或者 Web UI：

```text
Firmware
Current Version: 1.0.0

[ Upload Firmware ]
```

---

# 48. Web 管理页面

建议增加一个非常简单的 Web UI：

```text
BTControl
────────────────────

Bluetooth
● Connected
Device: iPhone

WiFi
● Connected
IP: 192.168.1.100

────────────────────

Keyboard
[ Test Keyboard ]

Mouse
[ ← ] [ ↑ ] [ ↓ ] [ → ]
[ Left Click ]
[ Right Click ]

────────────────────

Logs
12:31 BLE Connected
12:32 Keyboard Event
12:32 Mouse Event
```

它本质上也是 API Client。

---

# 49. 测试架构

这个产品非常适合做自动化测试。

建议：

```text
pytest
   │
   ▼
Python SDK
   │
   ▼
HTTP API
   │
   ▼
ESP32-C3
   │
   ▼
BLE
   │
   ▼
Android / iOS
```

测试分为：

```text
Unit Test
Integration Test
BLE Test
API Test
Compatibility Test
Stress Test
```

---

# 50. 自动化测试示例

```python
def test_keyboard():
    device.keyboard.type("hello")
    device.keyboard.press("ENTER")
```

Mouse：

```python
def test_mouse():
    device.mouse.move(100, 100)
    device.mouse.click("left")
```

Stress：

```python
for i in range(10000):
    device.keyboard.press("A")
```

---

# 51. 测试设备矩阵

建议至少：

```text
Android
├── Pixel
├── Samsung
├── Xiaomi
└── OnePlus

Apple
├── iPhone
└── iPad
```

每种：

```text
Keyboard
Mouse
Scroll
Click
Hotkey
Reconnect
Long Running
```

---

# 52. MVP 范围

第一版不要做得太大。

## P0

### Hardware

* ESP32-C3
* USB
* LED
* Button

### BLE

* BLE HID
* Keyboard
* Mouse
* Consumer Control

### API

```text
/device
/bluetooth
/keyboard/key
/keyboard/type
/keyboard/down
/keyboard/up
/keyboard/hotkey
/mouse/move
/mouse/click
/mouse/down
/mouse/up
/mouse/scroll
```

### Network

* WiFi
* HTTP REST
* Token authentication
* mDNS

---

# 53. V1.5

增加：

```text
Web UI
WebSocket
Python SDK
CLI
OTA
Command Queue
Batch Command
Workflow
```

---

# 54. V2

研究：

```text
HID Digitizer
Touch
Swipe
Absolute Coordinate
MoveTo
```

同时建立：

```text
Android Compatibility Layer
iOS Compatibility Layer
```

---

# 55. 最终产品能力分层

建议最终产品不是简单定义为：

> 蓝牙鼠标键盘

而是：

```text
                    BTControl
                       │
          ┌────────────┴────────────┐
          │                         │
      Standard HID             Advanced Input
          │                         │
    ┌─────┼─────┐             ┌─────┼─────┐
    │     │     │             │     │     │
 Keyboard Mouse Consumer    Touch Swipe MoveTo
    │     │     │             │     │     │
 Android iOS Android        Experimental
```

---

# 56. 核心产品原则

### 原则一

**API 与 HID 解耦。**

API 不应该直接操作 BLE：

```text
API
 ↓
Command
 ↓
Input abstraction
 ↓
HID
```

---

### 原则二

**Mouse 和 Touch 分开。**

不要把：

```text
mouse.click()
```

假设成：

```text
touch.tap()
```

二者底层语义不同。

---

### 原则三

**Relative 和 Absolute 分开。**

```text
move(dx, dy)
```

是标准 Mouse 能力。

```text
move_to(x, y)
```

是高级能力，需要校准或特殊 HID。

---

### 原则四

**Android 和 iOS 分别验证。**

不能认为：

```text
Android 支持
=
iOS 支持
```

尤其是 Touch / Digitizer。

---

# 57. 第一阶段建议的技术栈

| 模块             | 技术                |
| -------------- | ----------------- |
| MCU            | ESP32-C3          |
| Firmware       | ESP-IDF           |
| RTOS           | FreeRTOS          |
| BLE            | NimBLE            |
| HID            | BLE HID over GATT |
| Network        | WiFi              |
| API            | HTTP REST         |
| Realtime       | WebSocket         |
| Discovery      | mDNS              |
| Authentication | Bearer Token      |
| SDK            | Python            |
| CLI            | Python            |
| Test           | pytest            |
| Documentation  | OpenAPI           |

ESP32-C3 的官方文档明确支持 BLE，并同时提供 Bluedroid/NimBLE 两套 BLE Host；C3 不支持 Bluetooth Classic，因此整个项目应该从一开始按 BLE HID 设计，而不是依赖经典蓝牙 HID。

---

# 58. MVP 最终架构

```text
                     ┌─────────────────┐
                     │ Python / Go /   │
                     │ Node.js Client  │
                     └────────┬────────┘
                              │
                           HTTP
                              │
                              ▼
                   ┌────────────────────┐
                   │     ESP32-C3       │
                   │                    │
                   │  HTTP Server       │
                   │       │            │
                   │       ▼            │
                   │ Command Queue      │
                   │       │            │
                   │       ▼            │
                   │ Input Engine       │
                   │       │            │
                   │       ▼            │
                   │ BLE HID             │
                   └────────┬───────────┘
                            │
                         BLE HID
                            │
              ┌─────────────┴─────────────┐
              │                           │
          Android                      iOS/iPadOS
              │                           │
        Keyboard/Mouse              Keyboard/Mouse
```

---

# 59. MVP 验收标准

第一版完成后，需要能够做到：

### Case 1

PC：

```bash
curl -X POST \
http://btcontrol.local/api/v1/keyboard/type \
-d '{"text":"Hello"}'
```

手机收到：

```text
Hello
```

---

### Case 2

```bash
curl -X POST \
http://btcontrol.local/api/v1/keyboard/hotkey \
-d '{"keys":["CTRL","A"]}'
```

手机执行：

```text
Ctrl + A
```

---

### Case 3

```bash
curl -X POST \
http://btcontrol.local/api/v1/mouse/move \
-d '{"dx":100,"dy":50}'
```

手机指针移动。

---

### Case 4

```bash
curl -X POST \
http://btcontrol.local/api/v1/mouse/click \
-d '{"button":"left"}'
```

手机完成点击。

---

### Case 5

手机断开：

```text
BLE disconnect
```

重新连接后：

```text
自动恢复 HID
```

---

# 60. 项目最重要的技术 Spike

在正式写大量代码之前，我建议首先做一个 **3～5 天的技术验证 PoC**：

```text
ESP32-C3
    │
    │ BLE HID
    ▼
Android
```

验证：

```text
Keyboard
Mouse
Click
Scroll
Reconnect
```

然后：

```text
ESP32-C3
    │
    │ BLE HID
    ▼
iPhone
```

同样验证。

最后重点验证：

```text
Touch
Swipe
MoveTo
```

如果：

```text
BLE Digitizer
```

在目标手机上不能产生预期 Touch Event，就不要继续在 ESP32 HID 层投入大量开发，而是转向：

```text
ESP32 + Phone Agent
```

或者重新评估产品架构。

---

# 61. 项目优先级

最终建议按照这个顺序开发：

```text
Phase 0
技术可行性验证
        ↓
Phase 1
ESP32-C3 BLE Keyboard
        ↓
Phase 2
ESP32-C3 BLE Mouse
        ↓
Phase 3
WiFi + REST API
        ↓
Phase 4
Command Queue + Python SDK
        ↓
Phase 5
Android/iOS Compatibility
        ↓
Phase 6
Touch / Swipe / MoveTo 技术验证
        ↓
Phase 7
Digitizer / Advanced Input
        ↓
Phase 8
OTA + Web UI + Production
```

**尤其不要把 Touch/Swipe/MoveTo 放在 Keyboard/Mouse 前面。**

因为 Keyboard + Mouse + API 这一部分是非常明确、可落地的产品；而 Touch/绝对坐标输入涉及 Android/iOS 对 HID 类型的解释和系统权限，是这个项目真正的技术风险点。

