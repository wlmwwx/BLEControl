#!/usr/bin/env python3
"""MCP server exposing the BTControl BLE HID controller as tools.

An AI agent (Claude, CodeBuddy, etc.) connects to this MCP server and can
then type text, move the mouse, click, scroll, adjust volume, and more on
the phone paired with the ESP32-C3 device, through its HTTP API.

Setup (once):
    cd sdk/python
    python3 -m venv .venv
    .venv/bin/pip install mcp requests

Run (stdio transport - the default for local agents):
    .venv/bin/python btcontrol_mcp.py --host 192.168.10.168

Run (SSE transport, for remote agents):
    .venv/bin/python btcontrol_mcp.py --host 192.168.10.168 --transport sse --port 8000

Exposed tools: get_status, get_wifi_status, type_text, press_keys,
mouse_move, mouse_click, mouse_double_click, mouse_scroll, mouse_drag,
mouse_press, mouse_release, consumer, volume_up, volume_down, mute,
play, pause, stop, next_track, prev_track.
"""

import argparse

import requests
from mcp.server.mcpserver import MCPServer

server = MCPServer("BTControl")

BASE = "http://192.168.10.168"
TIMEOUT = 5.0


def _post(path: str, payload: dict) -> dict:
    r = requests.post(f"{BASE}{path}", json=payload, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


def _get(path: str) -> dict:
    r = requests.get(f"{BASE}{path}", timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()


@server.tool()
def get_status() -> dict:
    """BLE HID connection state and device name."""
    return _get("/status")


@server.tool()
def get_wifi_status() -> dict:
    """WiFi mode (ap/sta), connection state, SSID, IP and device name."""
    return _get("/wifi/status")


@server.tool()
def type_text(text: str) -> dict:
    """Type the given text on the phone. ASCII only - no Chinese/Unicode
    (HID keyboards send keycodes, so Chinese needs the phone's IME)."""
    return _post("/keyboard/type", {"text": text})


@server.tool()
def press_keys(keys: list[str]) -> dict:
    """Press a key combination, e.g. ["CTRL","C"] or ["A"] (first key only
    is currently sent by the firmware)."""
    return _post("/keyboard/key", {"keys": keys})


@server.tool()
def mouse_move(dx: int, dy: int) -> dict:
    """Move the mouse cursor relative by (dx, dy) pixels."""
    return _post("/mouse/move", {"dx": dx, "dy": dy})


@server.tool()
def mouse_click(button: int = 1) -> dict:
    """Click a mouse button: 1=left, 2=right, 4=middle."""
    return _post("/mouse/click", {"button": button})


@server.tool()
def mouse_double_click() -> dict:
    """Double click the left button."""
    return _post("/mouse/double_click", {})


@server.tool()
def mouse_scroll(amount: int) -> dict:
    """Scroll the mouse wheel; positive scrolls up, negative scrolls down."""
    return _post("/mouse/scroll", {"scroll": amount})


@server.tool()
def mouse_drag(dx: int, dy: int) -> dict:
    """Drag while holding the left button, moving relative by (dx, dy)."""
    return _post("/mouse/drag", {"dx": dx, "dy": dy})


@server.tool()
def mouse_press(button: int = 1) -> dict:
    """Press and hold a mouse button (1=left, 2=right, 4=middle)."""
    return _post("/mouse/press", {"button": button})


@server.tool()
def mouse_release() -> dict:
    """Release all mouse buttons."""
    return _post("/mouse/release", {})


@server.tool()
def consumer(usage: int) -> dict:
    """Send a consumer-control usage code, e.g. 0xE9=volume up, 0xEA=volume
    down, 0xE2=mute, 0xB0=play, 0xB1=pause, 0xB5=next, 0xB6=prev."""
    return _post("/consumer", {"usage": usage})


@server.tool()
def volume_up() -> dict:
    """Increase the phone volume."""
    return consumer(0xE9)


@server.tool()
def volume_down() -> dict:
    """Decrease the phone volume."""
    return consumer(0xEA)


@server.tool()
def mute() -> dict:
    """Toggle mute."""
    return consumer(0xE2)


@server.tool()
def play() -> dict:
    """Play media."""
    return consumer(0xB0)


@server.tool()
def pause() -> dict:
    """Pause media."""
    return consumer(0xB1)


@server.tool()
def stop() -> dict:
    """Stop media playback."""
    return consumer(0xB7)


@server.tool()
def next_track() -> dict:
    """Skip to the next track."""
    return consumer(0xB5)


@server.tool()
def prev_track() -> dict:
    """Go to the previous track."""
    return consumer(0xB6)


def main() -> None:
    global BASE
    parser = argparse.ArgumentParser(description="MCP server for BTControl")
    parser.add_argument("--host", default="192.168.10.168",
                        help="device IP or mDNS name (default 192.168.10.168)")
    parser.add_argument("--transport", default="stdio",
                        choices=["stdio", "sse"],
                        help="MCP transport (default stdio)")
    parser.add_argument("--port", type=int, default=8000,
                        help="port for the SSE transport")
    args = parser.parse_args()

    BASE = f"http://{args.host}"
    server.run(transport=args.transport, host="0.0.0.0", port=args.port)


if __name__ == "__main__":
    main()
