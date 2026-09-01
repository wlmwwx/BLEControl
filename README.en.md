# BTControl — ESP32-C3 BLE HID Controller

[English](README.en.md) | [中文](README.md)

Turns an ESP32-C3 into a Bluetooth keyboard/mouse that controls a paired phone through an HTTP API over WiFi. The controlling PC never talks to the phone directly.

```
PC / controller (HTTP client / Python SDK)         Android / iOS phone
        │                                                    ▲
        └───── WiFi ────▶ ESP32-C3 ──── BLE HID ─────────────┘
```

The device holds two simultaneous connections: WiFi (from the controller) and BLE HID (to the phone). The controller sends keyboard/mouse/volume requests, and the firmware translates them into HID reports for the phone.

> Full product spec: [`docs/prd.md`](docs/prd.md).

## Features

- **BLE HID**: keyboard (automatic Shift for uppercase/punctuation), mouse (move/click/scroll/drag/double-click/press/release), consumer control (volume/play/mute…), single HID service with report IDs 1/2/3, compatible with Android / iOS
- **WiFi provisioning**: with no saved network it enters AP mode (SSID `BTControl`, password `12345678`, `192.168.4.1`) with a built-in provisioning page; with a saved network it joins your home WiFi (STA), falling back to AP automatically on failure
- **mDNS**: the device resolves as `<lowercase-name>.local` (e.g. `btcontrol-01.local`) in both AP and STA modes
- **Configurable device name**: set during provisioning (≤18 chars); defaults to `BTControl-<last4-of-MAC>` so multiple boards never share a name
- **HTTP API**: 18 endpoints covering keyboard/mouse/consumer control and a command queue
- **Python SDK**: `sdk/python/btcontrol.py` wraps every endpoint

## Hardware

- An ESP32-C3 dev board (this project targets `esp32c3`, 4 MB flash)
- A USB cable for power and flashing

## Setup & Build

ESP-IDF **v6.1.0**, installed at `/home/wlmwwx/.espressif/v6.1/esp-idf` (not on `PATH` — source it first):

```bash
source /home/wlmwwx/.espressif/v6.1/esp-idf/export.sh
cd <this repo>

idf.py set-target esp32c3          # required once
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # Ctrl-] exits the monitor
```

Default serial port is `/dev/ttyACM0` (ESP32-C3 native USB Serial/JTAG).

## WiFi Provisioning

| State | Behavior |
| --- | --- |
| No saved WiFi | **AP mode**: SSID `BTControl` / password `12345678`, at `192.168.4.1`. Connect your phone to the hotspot and open `http://192.168.4.1/` for the provisioning page |
| Saved WiFi | Connects to your home network (**STA mode**), reachable via `http://<hostname>.local` or the DHCP IP |
| STA cannot connect | Auth failure / repeated failures / 30 s timeout → **falls back to AP mode** for re-provisioning |

The provisioning page collects: **WiFi SSID, password, (optional) device name**. `POST /wifi/config` saves and reboots into STA; `POST /wifi/forget` clears the config (the device name is kept) and reboots back to AP.

## HTTP API

All endpoints are unprefixed (the PRD's `/api/v1` is not implemented) and unauthenticated. Success returns `{"ok":true}`, parse failures return 500.

| Method | Path | Body |
| --- | --- | --- |
| GET | `/` | provisioning page |
| GET | `/wifi/status` | `{"mode":"ap\|sta","connected":bool,"ssid":str,"ip":str,"name":str}` |
| POST | `/wifi/config` | `{"ssid":"...","password":"..."[, "name":"..."]}` — save + reboot |
| POST | `/wifi/forget` | clear config + reboot |
| GET | `/status` | `{"connected":bool,"device_name":str}` |
| GET | `/info` | `{"device":str,"free_heap":int}` |
| POST | `/keyboard/type` | `{"text":"Hello"}` |
| POST | `/keyboard/key` | `{"keys":["CTRL","C"]}` |
| POST | `/mouse/move` | `{"dx":100,"dy":50}` |
| POST | `/mouse/click` | `{"button":1}` (1=left, 2=right, 4=middle) |
| POST | `/mouse/scroll` | `{"scroll":-5}` (positive up, negative down) |
| POST | `/mouse/drag` | `{"dx":500,"dy":0}` |
| POST | `/mouse/double_click` | — |
| POST | `/mouse/press` | `{"button":1}` |
| POST | `/mouse/release` | — |
| POST | `/consumer` | `{"usage":0xE9}` |
| POST | `/queue/add` | `{"cmd":"...","data":"..."}` |
| POST | `/queue/exec` | — |

Quick examples:

```bash
# After provisioning (192.168.4.1 in AP mode; mDNS name or DHCP IP in STA mode)
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

bt = BTControl(host="192.168.10.168")   # or "btcontrol-01.local"
bt.type("Hello from BTControl!")
bt.key("CTRL", "C")
bt.move(dx=50, dy=0)
bt.scroll(amount=3)
bt.volume_up()
```

- `sdk/python/btcontrol.py`: wrappers for every endpoint (`type`/`key`/`move`/`click`/`scroll`/`drag`/`double_click`/`press_mouse`/`release_mouse`/`queue_add`/`queue_exec`/consumer helpers/composite keys)
- `sdk/python/scroll_loop.py`: periodic-scroll test helper, e.g. scroll up every 10 s:
  ```bash
  python3 sdk/python/scroll_loop.py --host 192.168.10.168
  ```

## MCP Support (AI Agent Control)

`sdk/python/btcontrol_mcp.py` is an [MCP](https://modelcontextprotocol.io) server that exposes the device's HTTP API as 20 tools (`type_text`, `mouse_move`, `mouse_click`, `mouse_scroll`, `volume_up`, `get_status`, …). AI agents such as Claude / CodeBuddy can drive the phone once connected.

```bash
cd sdk/python
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt

# stdio transport (default for local agents)
.venv/bin/python btcontrol_mcp.py --host 192.168.10.168

# SSE transport (for remote agents)
.venv/bin/python btcontrol_mcp.py --host 192.168.10.168 --transport sse --port 8000
```

Register this command as an MCP server in any MCP-capable agent (e.g. Claude Desktop / CodeBuddy).

## UART Console

Test HID without WiFi from the serial monitor:

| Command | Description |
| --- | --- |
| `k <char>` / `ks <char>` | send a single key / with Shift |
| `kc <text>` | send text |
| `m <dx> <dy>` | mouse move |
| `mc <btn>` | mouse click |
| `mu` | release mouse |
| `sc <v\|V\|m\|p\|P\|s\|n\|b>` | consumer key |
| `status` | show BLE / WiFi state |

## Known Limitations

- **Chinese input**: HID keyboards only send keycodes, not Unicode; Chinese needs the phone's IME (send pinyin, e.g. `{"text":"zhong"}`, and pick a candidate)
- **`.local` resolution**: native on iOS/macOS; most Android browsers/apps don't resolve `.local` — use the IP; Windows needs Bonjour installed
- `/keyboard/key` only sends the first key; JSON parsing is hand-rolled (`strstr`/`sscanf`) — strict field order/whitespace required
- The command queue (`/queue/add` + `/queue/exec`) only supports `wait` delays; the rest are stubs
- No authentication (the PRD's Bearer Token is not implemented)
- Touch/gesture, absolute `move_to`, WebSocket, OTA from the PRD are not implemented

## Layout

```
main/
├── ble_hid_poc.c      # HID reports, HTTP server + all endpoints, command queue, UART console, app_main
├── esp_hid_gap.c/.h   # NimBLE GAP: BT controller init, advertising, pairing
├── wifi_prov.c/.h     # WiFi: STA/AP selection + fallback, mDNS, provisioning endpoints
├── CMakeLists.txt
└── idf_component.yml  # dependencies (led_strip, mdns)
sdk/python/            # Python SDK (btcontrol.py, btcontrol_mcp.py, scroll_loop.py)
docs/prd.md            # product requirements
```

For architecture and development details see [CODEBUDDY.md](CODEBUDDY.md).
