# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a BLE HID Controller project based on ESP32-C3. The device acts as a virtual Bluetooth keyboard/mouse that receives commands via HTTP API and sends HID reports to Android/iOS devices.

**Architecture:**
```
PC (HTTP Client)
    │
    │ WiFi
    ▼
ESP32-C3
    │ BLE HID
    ▼
Android / iOS Device
```

The ESP32-C3 has two simultaneous connections: WiFi (from controller PC) and BLE HID (to phone). The PRD in `docs/prd.md` defines the full product specification.

## Build Commands

```bash
# Set target chip (required once per project, or edit sdkconfig.defaults.<chip>)
idf.py set-target esp32c3

# Build the project
idf.py build

# Flash and monitor serial output
idf.py -p /dev/ttyUSB0 flash monitor

# Open configuration menu (LED type, GPIO, blink period under Example Configuration)
idf.py menuconfig

# Clean build
idf.py fullclean

# Run tests (requires hardware)
pytest pytest_blink.py -k esp32c3 --target esp32c3
```

## Project Structure

```
main/
├── blink_example_main.c   # Current blink example (placeholder)
├── idf_component.yml      # Component dependencies (led_strip)
└── Kconfig.projbuild      # Menuconfig definitions

docs/
└── prd.md                 # Product Requirements Document

build/                     # Build output (generated)
sdkconfig                  # ESP-IDF configuration (generated)
```

## Key Technical Notes

- **ESP32-C3 BLE Only**: This chip does not support Bluetooth Classic — only BLE HID is available
- **Recommended BLE Stack**: NimBLE (lightweight, suitable for resource-constrained devices)
- **Current State**: `main/blink_example_main.c` is the ESP-IDF stock blink example — the BLE HID controller firmware (HTTP API server, NimBLE stack, HID reports) has not yet been built. The PRD in `docs/prd.md` is the source of truth for what needs to be implemented
- **HID Report Types**: Keyboard, Mouse, Consumer Control, and experimental Digitizer
- **Important**: iOS BLE HID compatibility may involve MFi certification considerations (see PRD section 38)
