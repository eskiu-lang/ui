"""A headless Chrome over the DevTools protocol, for the web host tests and budgets that look
at a page from the outside (first paint, the prerendered frame, updates). Standard library
only: the WebSocket is spoken by hand.

    with Chrome() as chrome:
        page = chrome.page()
        page.goto("http://127.0.0.1:8000/")
        page.wait("document.readyState === 'complete'")
        print(page.eval("document.title"))

The browser is UI_CHROME, else Chrome for Testing, Chrome or Chromium where `ui agent` looks
for them; UI_CHROME_ARGS adds flags (CI passes --no-sandbox).
"""
import base64
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import urllib.request


def find_chrome():
    chosen = os.environ.get("UI_CHROME")
    if chosen:
        return chosen
    for path in ("/Applications/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing",
                 "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                 "/Applications/Chromium.app/Contents/MacOS/Chromium"):
        if Path(path).exists():
            return path
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome-headless-shell"):
        found = shutil.which(name)
        if found:
            return found
    return None


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Page:
    """One tab: commands by name, events kept in order."""

    def __init__(self, url):
        rest = url.split("://", 1)[1]
        hostport, path = rest.split("/", 1)
        host, port = hostport.split(":")
        self.sock = socket.create_connection((host, int(port)), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET /{path} HTTP/1.1\r\nHost: {hostport}\r\nUpgrade: websocket\r\n"
                           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        reply = b""
        while b"\r\n\r\n" not in reply:
            reply += self.sock.recv(4096)
        if b" 101 " not in reply.split(b"\r\n", 1)[0]:
            raise RuntimeError(f"cdp: no WebSocket: {reply[:200]!r}")
        self.buffer = reply.split(b"\r\n\r\n", 1)[1]
        self.next_id = 0
        self.events = []

    def _read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("cdp: the browser closed the connection")
            self.buffer += chunk
        out, self.buffer = self.buffer[:n], self.buffer[n:]
        return out

    def _message(self):
        data = b""
        while True:
            head = self._read(2)
            fin, opcode, length = head[0] & 0x80, head[0] & 0x0F, head[1] & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._read(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._read(8))[0]
            payload = self._read(length)
            if opcode == 8:
                raise RuntimeError("cdp: closed")
            if opcode in (0, 1, 2):
                data += payload
                if fin:
                    return json.loads(data)

    def send(self, method, params=None, timeout=30):
        self.next_id += 1
        frame = json.dumps({"id": self.next_id, "method": method, "params": params or {}}).encode()
        mask = os.urandom(4)
        n = len(frame)
        head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else
                                bytes([0x80 | 126]) + struct.pack(">H", n) if n < 65536 else
                                bytes([0x80 | 127]) + struct.pack(">Q", n))
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(frame)))
        self.sock.settimeout(timeout)
        while True:
            message = self._message()
            if message.get("id") == self.next_id:
                if "error" in message:
                    raise RuntimeError(f"cdp: {method}: {message['error']}")
                return message.get("result", {})
            self.events.append(message)

    def eval(self, expression, timeout=30):
        """The value of a JavaScript expression (awaited when it is a promise)."""
        result = self.send("Runtime.evaluate", {"expression": expression, "returnByValue": True,
                                                "awaitPromise": True}, timeout=timeout)
        if "exceptionDetails" in result:
            raise RuntimeError(f"cdp: {expression[:80]}: {result['exceptionDetails']}")
        return result.get("result", {}).get("value")

    def wait(self, expression, timeout=20):
        """Until the expression is true; its value, or an error after `timeout` seconds."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                value = self.eval(expression)
            except RuntimeError:
                value = None
            if value:
                return value
            if time.monotonic() > deadline:
                raise RuntimeError(f"cdp: timed out waiting for {expression}")
            time.sleep(0.05)

    def goto(self, url):
        self.send("Page.enable")
        self.send("Page.navigate", {"url": url})


class Chrome:
    """A headless Chrome with a fresh profile, stopped on exit."""

    def __init__(self):
        self.binary = find_chrome()
        if not self.binary:
            raise SystemExit("no Chrome found (set UI_CHROME)")
        self.profile = tempfile.mkdtemp(prefix="ui-cdp-")
        self.port = free_port()
        args = [self.binary, f"--remote-debugging-port={self.port}", f"--user-data-dir={self.profile}",
                "--no-first-run", "--no-default-browser-check", "--window-size=400,800", "about:blank"]
        if "headless-shell" not in self.binary:
            args.insert(1, "--headless=new")
        args[1:1] = os.environ.get("UI_CHROME_ARGS", "").split()
        self.process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def __enter__(self):
        deadline = time.monotonic() + 30
        while True:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{self.port}/json/version", timeout=2).read()
                return self
            except OSError:
                if time.monotonic() > deadline:
                    raise SystemExit("Chrome did not start")
                time.sleep(0.1)

    def page(self):
        """A new tab."""
        request = urllib.request.Request(f"http://127.0.0.1:{self.port}/json/new?about:blank", method="PUT")
        target = json.loads(urllib.request.urlopen(request, timeout=10).read())
        return Page(target["webSocketDebuggerUrl"])

    def __exit__(self, *_):
        self.process.terminate()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
        shutil.rmtree(self.profile, ignore_errors=True)


# DevTools' "Slow 4G" for Network.emulateNetworkConditions (throughputs in bytes per second).
SLOW_4G = {"offline": False, "latency": 150, "downloadThroughput": 1.6e6 / 8, "uploadThroughput": 0.75e6 / 8}


def first_paint(page, url):
    """The first contentful paint of a fresh load of an app's page, in ms from the start of
    the navigation, once the app has rendered."""
    page.goto("about:blank")
    page.goto(url)
    page.wait("document.readyState === 'complete' && !!document.querySelector('#app [data-ui-id]')", timeout=60)
    return page.wait("performance.getEntriesByName('first-contentful-paint')[0]?.startTime")
