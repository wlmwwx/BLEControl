"""
BTControl Python SDK
ESP32-C3 BLE HID Controller via HTTP API
"""

import requests
import time


class BTControl:
    """BTControl device client"""

    def __init__(self, host="192.168.4.1", timeout=5.0):
        self.host = host
        self.timeout = timeout
        self.base_url = f"http://{host}"

    def status(self):
        """Get connection status"""
        r = requests.get(f"{self.base_url}/status", timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def is_connected(self):
        """Check if BLE is connected to host"""
        try:
            return self.status().get("connected", False)
        except:
            return False

    # Keyboard methods
    def type(self, text):
        """Type a string"""
        r = requests.post(f"{self.base_url}/keyboard/type",
                        json={"text": text}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def key(self, *keys):
        """Send key combination, e.g., bt.key("CTRL", "C")"""
        r = requests.post(f"{self.base_url}/keyboard/key",
                        json={"keys": list(keys)}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def press(self, *keys):
        """Press and hold keys"""
        return self.key(*keys)

    # Mouse methods
    def move(self, dx=0, dy=0):
        """Move mouse relative"""
        r = requests.post(f"{self.base_url}/mouse/move",
                        json={"dx": dx, "dy": dy}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def click(self, button=1):
        """Mouse click (1=left, 2=right, 4=middle)"""
        r = requests.post(f"{self.base_url}/mouse/click",
                        json={"button": button}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def double_click(self):
        """Double click"""
        r = requests.post(f"{self.base_url}/mouse/double_click",
                        timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def scroll(self, amount=1):
        """Scroll wheel"""
        r = requests.post(f"{self.base_url}/mouse/scroll",
                        json={"scroll": amount}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def drag(self, dx=0, dy=0):
        """Drag (move while holding left button)"""
        r = requests.post(f"{self.base_url}/mouse/drag",
                        json={"dx": dx, "dy": dy}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def press_mouse(self, button=1):
        """Press and hold mouse button"""
        r = requests.post(f"{self.base_url}/mouse/press",
                        json={"button": button}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def release_mouse(self):
        """Release mouse button"""
        r = requests.post(f"{self.base_url}/mouse/release",
                        timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    # Command queue methods
    def queue_add(self, cmd, data=""):
        """Add command to queue"""
        r = requests.post(f"{self.base_url}/queue/add",
                        json={"cmd": cmd, "data": data}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def queue_exec(self):
        """Execute all queued commands"""
        r = requests.post(f"{self.base_url}/queue/exec", timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def queue_command(self, cmd, data=""):
        """Add and immediately execute a command"""
        self.queue_add(cmd, data)
        return self.queue_exec()

    # Utility methods
    def wait_for_connection(self, timeout=30):
        """Wait for BLE connection"""
        start = time.time()
        while time.time() - start < timeout:
            if self.is_connected():
                return True
            time.sleep(0.5)
        return False

    # Shortcuts
    def left_click(self):
        return self.click(1)

    def right_click(self):
        return self.click(2)

    def middle_click(self):
        return self.click(4)

    # Consumer control (media keys)
    def volume_up(self):
        """Volume up"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xE9}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def volume_down(self):
        """Volume down"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xEA}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def mute(self):
        """Mute"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xE2}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def play(self):
        """Play"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xB0}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def pause(self):
        """Pause"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xB1}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def stop(self):
        """Stop"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xB7}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def next_track(self):
        """Next track"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xB5}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def prev_track(self):
        """Previous track"""
        r = requests.post(f"{self.base_url}/consumer", json={"usage": 0xB6}, timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    # Automation helpers
    def tap(self, x=0, y=0):
        """Tap at current position (for accessibility navigation)"""
        self.left_click()
        return True

    def swipe(self, dx, dy, steps=5):
        """Swipe by dragging"""
        self.press_mouse(1)
        time.sleep(0.05)
        for i in range(steps):
            self.move(dx // steps, dy // steps)
            time.sleep(0.05)
        self.release_mouse()
        return True

    def move_to(self, dx, dy):
        """Move mouse to relative position"""
        return self.move(dx, dy)

    # Sequence helpers
    def type_line(self, text):
        """Type text followed by Enter"""
        self.type(text)
        time.sleep(0.1)
        self.key("ENTER")
        return True

    def select_all(self):
        """Ctrl+A"""
        return self.key("CTRL", "A")

    def copy(self):
        """Ctrl+C"""
        return self.key("CTRL", "C")

    def paste(self):
        """Ctrl+V"""
        return self.key("CTRL", "V")

    def cut(self):
        """Ctrl+X"""
        return self.key("CTRL", "X")

    def undo(self):
        """Ctrl+Z"""
        return self.key("CTRL", "Z")

    def save(self):
        """Ctrl+S"""
        return self.key("CTRL", "S")


if __name__ == "__main__":
    # Demo
    bt = BTControl()
    print(f"Connected: {bt.is_connected()}")
