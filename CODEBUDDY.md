# CODEBUDDY.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

## What this project is

A **BLE HID controller** firmware: the ESP32-C3 exposes an HTTP API over WiFi, and translates incoming requests into BLE HID reports sent to a paired phone. The controlling PC never talks to the phone.

```
PC (HTTP client / Python SDK)          Android / iOS device
        │                                      ▲
        └── WiFi AP ──▶ ESP32-C3 ──BLE HID────┘
```

The ESP32-C3 holds two simultaneous connections: WiFi AP (from the controller PC) and BLE HID (to the phone). `docs/prd.md` is the canonical product spec (2322 lines); the firmware is an earlier-stage implementation of it.

This repo started life as the stock ESP-IDF `blink` example, and the vestiges are still here — see "Vestigial blink example" below.

## Environment

ESP-IDF **v6.1.0** is installed at `/home/wlmwwx/.espressif/v6.1/esp-idf` but is **not on `PATH` in a plain shell**. Source it first in every new shell:

```bash
source /home/wlmwwx/.espressif/v6.1/esp-idf/export.sh
```

Target is **esp32c3** (RISC-V). Default serial port per `.vscode/settings.json` is `/dev/ttyACM0` (not `/dev/ttyUSB0`).

## Commands

```bash
idf.py set-target esp32c3          # required once; regenerates sdkconfig for the target
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # Ctrl-] exits the monitor
idf.py menuconfig                  # "Example Configuration" menu
idf.py fullclean

# Hardware-in-the-loop test (needs a connected board)
pytest pytest_blink.py -k esp32c3 --target esp32c3

# Rebuild only the app, or just re-flash
idf.py app-flash
idf.py monitor
```

There is no lint step and no host-side unit tests. `pytest_blink.py` is inherited from the stock example — it flashes the board and only asserts the `.bin` was produced and logs its size; it does not exercise any HID or HTTP behavior.

To talk to a running device, use `sdk/python/btcontrol.py` (see "Python SDK" below) or plain `curl` against `192.168.4.1`.

## Architecture

### Firmware layout (`main/`)

`main/CMakeLists.txt` compiles exactly three files; anything else in `main/` is not in the build.

| File | Role |
| --- | --- |
| `ble_hid_poc.c` (900+ lines) | Everything else: HID report descriptors, HTTP server + all HID endpoint handlers, command queue, UART console, `app_main` |
| `esp_hid_gap.c` / `.h` | NimBLE GAP layer only — BT controller init, advertising payload, SM pairing config, GAP event handler |
| `wifi_prov.c` / `.h` | WiFi provisioning — NVS storage, STA/AP mode selection + fallback, mDNS, provisioning HTTP endpoints (`/`, `/wifi/*`) |
| `idf_component.yml` | Component manager manifest (pulls `espressif/led_strip`, `espressif/mdns`) |
| `Kconfig.projbuild` | Defines the `Example Configuration` menu (LED type/GPIO/period) |

The split matters: **all HTTP→HID translation lives in `ble_hid_poc.c`**, `esp_hid_gap.c` is the only place that touches NimBLE GAP APIs directly, and **all WiFi-mode logic lives in `wifi_prov.c`**.

### Startup sequence (`app_main`, ble_hid_poc.c:837)

1. `nvs_flash_init` (erases NVS and retries if no free pages / version mismatch)
2. `wifi_prov_init()` — reads NVS: saved SSID → **STA**, else **AP** (provisioning); starts mDNS
3. `http_server_start()` — HTTP server comes up **before** BLE; binds `INADDR_ANY`, so it serves on whichever interface is active
4. BT controller: `esp_bt_controller_init()` + `esp_bt_controller_enable(ESP_BT_MODE_BLE)` — **mandatory** before `esp_nimble_init()`, otherwise `esp_vhci_host_register_callback()` fails (`BLE_INIT: hci inits failed`)
5. `esp_nimble_init()`
6. `esp_hidd_dev_init()` with `ble_hid_config`, then battery level
7. `esp_hid_ble_gap_adv_init()` — builds advertising fields; **advertising is not started here**
8. `nimble_port_freertos_init(ble_hid_host_task)` — NimBLE host task runs `nimble_port_run()`
9. Spawns `interactive_test_task` (UART console) and `queue_task`

Advertising starts from the HID event callback, not from `app_main`: `ESP_HIDD_START_EVENT` → `esp_hid_ble_gap_adv_start()` (ble_hid_poc.c:236). Calling `ble_gap_adv_start()` before the host is synced returns `BLE_HS_ENOTSYNC`; the START event only fires once the host runs.

### HID reports

Three report descriptors are defined in `ble_hid_poc.c`, addressed by report ID via `esp_hidd_dev_input_set(s_hid_dev, 0, <report_id>, buf, len)`:

| ID | Type | Descriptor | Buffer |
| --- | --- | --- | --- |
| 1 | Keyboard | `keyboardReportMap` (line 116) | 8 bytes: modifiers, reserved, key1–3, pad |
| 2 | Mouse | `mouseReportMap` (line 153) | 4 bytes: buttons, dx, dy, wheel (signed, ±127) |
| 3 | Consumer Control | `consumerReportMap` (line 184) | 2 bytes: 16-bit usage bitmap; `send_consumer_report()` maps usage codes (0xE9…) to bit positions via `consumer_key_map` |

Device identity: name `BTControl-POC`, VID `0x16C0`, PID `0x05DF`, appearance `ESP_HID_APPEARANCE_KEYBOARD`.

ASCII→HID keycode conversion is a 128-entry lookup table (`usb_keycode_map`, line 304) plus `ascii_to_keycode()`; uppercase letters are handled by OR-ing in `LEFT_SHIFT` at the call site rather than in the table.

Pairing config (`esp_hid_gap.c:65`): IO capability display-only, bonding on, **MITM on and Secure Connections on** (`sm_mitm=1`, `sm_sc=1`). `BLE_GAP_EVENT_PASSKEY_ACTION` injects the hardcoded passkey `123456` for `BLE_SM_IOACT_DISP` and auto-accepts `NUMCMP`. Deliberately weak — do not treat this as a security boundary. **Not yet validated against a real phone.**

### WiFi and HTTP

WiFi is managed by `wifi_prov.c`, not hardcoded in the app:

- **STA mode** — if NVS (`wifi` namespace, `ssid`/`pass` keys) holds a saved network, the device connects to it and gets a DHCP IP.
- **AP mode** (provisioning) — no saved config → AP with SSID `BTControl`, password `12345678`, WPA/WPA2-PSK, max 3 clients, at `192.168.4.1`.
- **STA → AP fallback** — auth failure, 3 failed connect retries, or a 30 s connect watchdog switches to AP at runtime. The switch runs in a small `wifi_ctrl_task` (deferred via a FreeRTOS queue) — never call `esp_wifi_stop()` from inside a wifi event handler. AP → STA happens only via reboot.
- **Provisioning** — in AP mode, `GET /` serves a self-contained HTML form; `POST /wifi/config` saves creds + `nvs_commit()` + reboots into STA; `POST /wifi/forget` erases + reboots back to AP.
- **mDNS** — `espressif/mdns` (registry component), hostname `btcontrol`, so the device resolves as `btcontrol.local` on both AP and STA.
- Both netifs (AP + STA) are created before `esp_wifi_start()`; the httpd binds `INADDR_ANY`, so the HTTP API works in both modes.

`http_server_start()` (line 661) registers 14 HID URIs plus the 4 `wifi_prov` URIs on the default port (80). **`config.max_uri_handlers` is set to 24** — the esp_http_server default is 8, which would silently drop endpoints with `httpd_register_uri_handler: no slots left`. Endpoints are **unprefixed** — the `/api/v1` prefix in the PRD is not implemented:

| Method | Path | Body |
| --- | --- | --- |
| GET | `/` | provisioning HTML page |
| GET | `/wifi/status` | — → `{"mode":"ap\|sta","connected":bool,"ssid":str,"ip":str}` |
| POST | `/wifi/config` | `{"ssid":"...","password":"..."}` (or form-encoded) → save + reboot |
| POST | `/wifi/forget` | — → erase config + reboot |
| GET | `/status` | — → `{"connected":bool,"device_name":str}` |
| GET | `/info` | — → `{"device":str,"free_heap":int}` |
| POST | `/keyboard/type` | `{"text":"Hello"}` |
| POST | `/keyboard/key` | `{"keys":["CTRL","C"]}` |
| POST | `/mouse/move` | `{"dx":100,"dy":50}` |
| POST | `/mouse/click` | `{"button":1}` (1=left, 2=right, 4=middle) |
| POST | `/mouse/scroll` | `{"scroll":-5}` |
| POST | `/mouse/drag` | `{"dx":500,"dy":0}` |
| POST | `/mouse/double_click` | — |
| POST | `/mouse/press` | `{"button":1}` |
| POST | `/mouse/release` | — |
| POST | `/consumer` | `{"usage":0xE9}` |
| POST | `/queue/add` | `{"cmd":"...","data":"..."}` |
| POST | `/queue/exec` | — |

All handlers respond `{"ok":true}` on success and HTTP 500 on any parse failure. There is no auth, no token, no rate limiting — the PRD specifies Bearer-token auth but it is unimplemented.

**JSON is not parsed with a parser.** Handlers use `strstr` + hand-rolled scanning or `sscanf` against an exact format string (e.g. `sscanf(buf, "{\"dx\":%d,\"dy\":%d}", &dx, &dy)` at line 492). Bodies with different key order, whitespace, or nesting silently fail and return 500. `/keyboard/key` matches modifier names by `strstr` anywhere in the body, and matches the base key by scanning an ordered list of 35 names and breaking on the first hit — so `{"keys":["A","B"]}` sends only `A`. `/wifi/config` accepts both JSON and form-urlencoded bodies.

### Command queue

A 32-entry ring buffer (`s_cmd_queue`, head/tail/count) with `queue_push`/`queue_pop`, drained by `queue_task` (line 97) every 10 ticks. `process_command` (line 79) only implements `wait`; `keyboard_type`, `keyboard_key`, `mouse_move`, and `mouse_click` are **empty stubs** with comments saying they are "already handled in HTTP handler". In practice `/queue/add` + `/queue/exec` can only enqueue delays. Treat the batch/queue feature as scaffolding.

### UART console

`interactive_test_task` (line 716) reads `stdin` in a loop and offers a text REPL: `k <char>`, `ks <char>` (shift), `kc <text>`, `m <dx> <dy>`, `mc <btn>`, `mu`, `sc <v|V|m|p|P|s|n|b>` (consumer keys), `status`. This is the fastest way to test HID without WiFi. It depends on `stdin` being wired to the USB serial/JTAG console, which is the default on ESP32-C3.

## Gotchas that are easy to get wrong

- **The old consumer descriptor aborted HID init for the whole device.** `consumerReportMap` used to declare `Report Size 4 / Count 1` (4 bits — not a whole byte); `hid_parser` rejects it with `INPUT report does not amount to full bytes!`, which makes `esp_hidd_dev_init()` fail and nothing (keyboard/mouse included) works. It is now a 16-bit bitmap (2 bytes). If you touch `consumerReportMap`, the report must always total a whole number of bytes.
- **NimBLE caps the number of HID service instances at 2 by default.** `CONFIG_BT_NIMBLE_SVC_HID_MAX_INSTANCES` defaults to 2, and this firmware registers **one HID service per report map** (3 maps: keyboard, mouse, consumer). The 3rd service makes `esp_hidd_dev_init()` fail with `BLE_HS_ENOMEM` (error 6). It is set to 3 in `sdkconfig.defaults.esp32c3`. Also `CONFIG_BT_NIMBLE_SVC_HID_MAX_RPTS` (default 3) caps reports per service — the keyboard map's input+output reports count toward this.
- **Editing `sdkconfig.defaults*` does not apply to an existing generated `sdkconfig`.** The build does not re-apply changed defaults on its own. Run `idf.py reconfigure` after editing defaults (or delete `sdkconfig`). Symptom: you set a new option, build, and the symbol is still the old value in `sdkconfig`.
- **Advertisement UUIDs need their `type` field set.** `ble_gap_adv_set_fields()` fails with `ble_uuid_flat rc=3` if a malloc'd `ble_uuid16_t` only had `.value` assigned (its `u.type` is garbage). Initialize with `BLE_UUID16_INIT(...)`. Symptom in boot log: `E (698) NimBLE: ble_uuid_flat rc=3` right after `START - Starting BLE advertising...`.
- **Advertising expires after 3 minutes.** `esp_hid_ble_gap_adv_start()` passes `adv_duration_ms = 180000` (esp_hid_gap.c:153). The `BLE_GAP_EVENT_ADV_COMPLETE` handler only logs and does not restart advertising, so an unconnected board stops being discoverable. The phone must pair within 3 minutes of boot.
- **BT controller must reach ENABLED before `esp_nimble_init()`.** `esp_nimble_init()` does not init/enable the controller; `esp_vhci_host_register_callback()` returns `ESP_FAIL` unless `esp_bt_controller_enable(ESP_BT_MODE_BLE)` ran first. Symptom: `BLE_INIT: hci inits failed`. `app_main` handles this correctly now.
- **Vestigial blink example.** `main/blink_example_main.c` is the untouched stock example and is **not** in `main/CMakeLists.txt` `SRCS`, so it never compiles. `Kconfig.projbuild`'s `Example Configuration` menu (BLINK_LED_*, BLINK_GPIO, BLINK_PERIOD) and the `espressif/led_strip` managed component exist only for it; no built source references `BLINK_*` or `led_strip`. Do not "fix" build errors by adding `blink_example_main.c` to `SRCS`.
- **Dead sdkconfig options.** In `sdkconfig.defaults.esp32c3`, `CONFIG_HID_ENABLED` and `CONFIG_ESP_HTTP_SERVER_ENABLED` are silently ignored — neither symbol exists in IDF v6.1 (neither `esp_http_server` nor `esp_hid` exposes one). They are not what enables those components; `main/CMakeLists.txt` pulls them in via `REQUIRES ... esp_hid ... esp_http_server`. Don't debug a missing-component problem by toggling these.
- **Per-target default files.** There are `sdkconfig.defaults.<chip>` files for esp32, c3, c5, c6, c61, h2, p4, s2, s3, s31 — mostly stock pin assignments. Only the esp32c3 one carries project config (NimBLE, custom partition table, 4MB flash). Only esp32c3 is a working target for the HID firmware.
- `sdkconfig` is generated and huge (~99 KB); `sdkconfig.old` is a backup. Edit the `sdkconfig.defaults*` files or use `menuconfig`, and expect `sdkconfig` to be regenerated.
- Custom partition table in `partitions.csv`: 3 MB `factory` app at `0x10000`, NVS at `0x310000`. OTA slots are not defined despite an `otadata` partition being present.

## Python SDK

`sdk/python/btcontrol.py` is a thin `requests` wrapper, default host `192.168.4.1` (the AP-mode address), matching the unprefixed endpoints above: `type`, `key`, `move`, `click`, `double_click`, `scroll`, `drag`, `press_mouse`, `release_mouse`, `queue_add`, `queue_exec`, plus consumer helpers (`volume_up`, `mute`, `play`, `next_track`, …) and composite helpers (`select_all`, `copy`, `paste`, `undo`, `save`). `tap`, `swipe`, and `move_to` exist in the SDK but have **no firmware endpoints** — they are PRD-forward and will 404. Keep SDK and firmware endpoint lists in sync when adding routes. In STA mode the device is at a DHCP IP or `btcontrol.local` — pass it as the `host` argument.

## Where the project is headed

`docs/prd.md` defines phases 0–8. Git history tracks the climb: phase 0 (BLE HID PoC) → 1 (keyboard) → 2 (mouse) → 3/Phase 1 (WiFi AP + HTTP API) → 4 (command queue + Python SDK) → 5 (consumer control) → 8 (info endpoint), tagged `v0.1-phase0-proof-of-concept` … `v1.0-production`. HEAD was broken as committed (BLE did not initialize at all); that was fixed in a subsequent commit, and web-based WiFi provisioning (STA + AP fallback + mDNS) was added on top.

Deferred by the PRD and **not** in the firmware: `/api/v1` URL prefix, Bearer-token auth, touch/digitizer/swipe, absolute `move_to`, WebSocket transport, OTA, workflows. (WiFi provisioning with mDNS is now implemented, but note it is **web-based** — the PRD's BLE-provisioning variant and API Token collection are not.)

Two constraints worth keeping in mind: the ESP32-C3 has **no Bluetooth Classic**, so BLE HID is the only option; and PRD section 38 flags that Espressif's own docs note MFi certification requirements for BLE HID interacting with iOS, so iOS compatibility needs real-device validation before anything ships.

## Related files

`CLAUDE.md` also exists and carries the same build commands, but its project-structure section predates `ble_hid_poc.c` and still claims the BLE HID firmware has not been built. Trust the code over that description; if you make structural changes, update `CLAUDE.md` too.
