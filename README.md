# BTControl — ESP32-C3 BLE HID 控制器

让 ESP32-C3 化身蓝牙键盘/鼠标，通过 WiFi 上的 HTTP API 控制已配对的手机。电脑/控制端不需要和手机建立任何连接。

```
PC / 控制端 (HTTP 客户端 / Python SDK)         Android / iOS 手机
        │                                            ▲
        └───── WiFi ───▶ ESP32-C3 ──── BLE HID ──────┘
```

设备同时持有两条连接：WiFi（来自控制端）和 BLE HID（到手机）。控制端发送键盘/鼠标/音量等请求，固件将其翻译成 HID 报文发给手机。

> 详细产品规格见 [`docs/prd.md`](docs/prd.md)。

## 功能特性

- **BLE HID**：键盘（含大写/标点自动 Shift）、鼠标（移动/点击/滚动/拖动/双击/按下/释放）、消费控制（音量/播放/静音等），单 HID service + report ID 1/2/3，兼容 Android / iOS
- **WiFi 配网**：无配置时进入 AP 模式（SSID `BTControl`，密码 `12345678`，`192.168.4.1`），内置配网页；已保存 WiFi 时连接家里网络（STA），失败自动回退 AP
- **mDNS**：设备通过 `<设备名小写>.local` 解析（如 `btcontrol-01.local`），AP/STA 模式都生效
- **设备名可配置**：配网时可自定义（≤18 字符），未配置时自动用 `BTControl-<MAC后4位>`，多块板子互不重名
- **HTTP API**：18 个端点，同时支持键盘/鼠标/消费控制/命令队列
- **Python SDK**：`sdk/python/btcontrol.py` 封装了全部端点

## 硬件要求

- ESP32-C3 开发板（本项目目标是 `esp32c3`，4MB flash）
- 用于烧录和供电的 USB 线

## 环境与编译

ESP-IDF **v6.1.0**，安装于 `/home/wlmwwx/.espressif/v6.1/esp-idf`（不在 PATH 里，需先 source）：

```bash
source /home/wlmwwx/.espressif/v6.1/esp-idf/export.sh
cd <本项目目录>

idf.py set-target esp32c3          # 首次需设置目标芯片
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # Ctrl-] 退出 monitor
```

串口默认 `/dev/ttyACM0`（ESP32-C3 原生 USB Serial/JTAG）。

## WiFi 配网

| 状态 | 行为 |
| --- | --- |
| 无已保存 WiFi | 进入 **AP 模式**：SSID `BTControl` / 密码 `12345678`，地址 `192.168.4.1`。手机连上热点后访问 `http://192.168.4.1/` 打开配网页 |
| 已保存 WiFi | 连接家里网络（**STA 模式**），通过 `http://<hostname>.local` 或 DHCP IP 访问 |
| STA 连不上 | 认证失败 / 多次重试失败 / 30s 超时 → **自动回退 AP 模式**，可重新配网 |

配网页收集：**WiFi 名称、密码、（可选）设备名称**。`POST /wifi/config` 保存后自动重启进 STA；`POST /wifi/forget` 清除配置（设备名保留）重启回 AP。

## HTTP API

所有端点无前缀（PRD 中的 `/api/v1` 未实现），无鉴权。成功返回 `{"ok":true}`，解析失败返回 500。

| Method | Path | Body |
| --- | --- | --- |
| GET | `/` | 配网页 |
| GET | `/wifi/status` | `{"mode":"ap\|sta","connected":bool,"ssid":str,"ip":str,"name":str}` |
| POST | `/wifi/config` | `{"ssid":"...","password":"..."[, "name":"..."]}`，保存并重启 |
| POST | `/wifi/forget` | 清除配置并重启 |
| GET | `/status` | `{"connected":bool,"device_name":str}` |
| GET | `/info` | `{"device":str,"free_heap":int}` |
| POST | `/keyboard/type` | `{"text":"Hello"}` |
| POST | `/keyboard/key` | `{"keys":["CTRL","C"]}` |
| POST | `/mouse/move` | `{"dx":100,"dy":50}` |
| POST | `/mouse/click` | `{"button":1}`（1=左, 2=右, 4=中） |
| POST | `/mouse/scroll` | `{"scroll":-5}`（正=向上, 负=向下） |
| POST | `/mouse/drag` | `{"dx":500,"dy":0}` |
| POST | `/mouse/double_click` | — |
| POST | `/mouse/press` | `{"button":1}` |
| POST | `/mouse/release` | — |
| POST | `/consumer` | `{"usage":0xE9}` |
| POST | `/queue/add` | `{"cmd":"...","data":"..."}` |
| POST | `/queue/exec` | — |

快速示例：

```bash
# 配网后（AP 模式下是 192.168.4.1，STA 模式下用 mDNS 名或 DHCP IP）
curl -X POST http://btcontrol-01.local/keyboard/type -d '{"text":"hello"}'
curl -X POST http://btcontrol-01.local/mouse/move   -d '{"dx":50,"dy":0}'
curl -X POST http://btcontrol-01.local/mouse/scroll -d '{"scroll":3}'
```

## Python SDK

```bash
pip install requests
```

```python
from btcontrol import BTControl

bt = BTControl(host="192.168.10.168")   # 或 "btcontrol-01.local"
bt.type("Hello from BTControl!")
bt.key("CTRL", "C")
bt.move(dx=50, dy=0)
bt.scroll(amount=3)
bt.volume_up()
```

- `sdk/python/btcontrol.py`：全部端点的封装（`type`/`key`/`move`/`click`/`scroll`/`drag`/`double_click`/`press_mouse`/`release_mouse`/`queue_add`/`queue_exec`/消费控制/组合键）
- `sdk/python/scroll_loop.py`：周期滚屏测试脚本，如每 10 秒向上滚动一次：
  ```bash
  python3 sdk/python/scroll_loop.py --host 192.168.10.168
  ```

## UART 控制台

无需 WiFi 即可直接测试 HID（串口 monitor 里输入）：

| 命令 | 说明 |
| --- | --- |
| `k <char>` / `ks <char>` | 发送单键 / 带 Shift |
| `kc <text>` | 发送文本 |
| `m <dx> <dy>` | 鼠标移动 |
| `mc <btn>` | 鼠标点击 |
| `mu` | 释放鼠标 |
| `sc <v\|V\|m\|p\|P\|s\|n\|b>` | 消费控制键 |
| `status` | 显示 BLE / WiFi 状态 |

## 已知限制

- **中文输入**：HID 键盘只能发送键码，无法直接输入 Unicode；中文需手机输入法（可发拼音，如 `{"text":"zhong"}` 由输入法出候选字）
- **手机端 .local 解析**：iOS/macOS 原生支持；Android 大部分浏览器/应用不解析 `.local`，需直接用 IP；Windows 需装 Bonjour
- `/keyboard/key` 目前只发送第一个键；JSON 解析为手写 `strstr`/`sscanf`，字段顺序/空白需严格匹配
- 命令队列 `/queue/add` + `/queue/exec` 仅支持 `wait` 延迟，其余为占位
- 无鉴权（PRD 中的 Bearer Token 未实现）
- PRD 中的触摸/手势、绝对 `move_to`、WebSocket、OTA 等尚未实现

## 目录结构

```
main/
├── ble_hid_poc.c      # HID 报告、HTTP 服务器 + 全部端点、命令队列、UART 控制台、app_main
├── esp_hid_gap.c/.h   # NimBLE GAP：BT controller 初始化、广播、配对
├── wifi_prov.c/.h     # WiFi：STA/AP 选择与回退、mDNS、配网端点
├── CMakeLists.txt
└── idf_component.yml  # 组件依赖（led_strip、mdns）
sdk/python/            # Python SDK（btcontrol.py、scroll_loop.py）
docs/prd.md            # 产品需求文档
```

更多架构与开发细节见 [CODEBUDDY.md](CODEBUDDY.md)。
