#!/usr/bin/env python3
"""The README's screenshots: the Tasks showcase (examples/tasks) on the web, the iOS
simulator, the Android emulator and macOS (the AppKit host). A host that is not available is
skipped; `python3 tools/screenshots.py macos` takes only the hosts named.
Metadata chunks (eXIf, tEXt, iTXt, zTXt, tIME) are dropped from every PNG."""
import functools
import http.server
import os
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build"
SHOTS = HERE / "screenshots"
DROP = {b"eXIf", b"tEXt", b"iTXt", b"zTXt", b"tIME"}


def clean_png(path):
    data = path.read_bytes()
    out, at = [data[:8]], 8
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at:at + 12 + length]
        if kind not in DROP:
            out.append(chunk)
        at += 12 + length
    path.write_bytes(b"".join(out))


def serve():
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(BUILD / "web")))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def chrome():
    for path in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                 "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
                 "/Applications/Chromium.app/Contents/MacOS/Chromium"):
        if os.path.exists(path):
            return path
    return None


def shoot_web(server):
    browser = chrome()
    if not browser:
        return "no Chromium-based browser"
    url = f"http://127.0.0.1:{server.server_port}/?example=tasks"
    subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=2",
                    "--window-size=520,960", "--virtual-time-budget=5000", f"--screenshot={SHOTS / 'tasks-web.png'}", url],
                   check=True, timeout=60, capture_output=True)
    return None


def shoot_macos(server):
    """examples/tasks in the AppKit host, 430 by 880 points, through `ui agent macos`."""
    if sys.platform != "darwin":
        return "not a Mac"
    ui = BUILD / "ui"
    subprocess.run(["eskiuc", HERE / "tools/ui/main.esk", "-o", ui], check=True, timeout=300)
    project = HERE / "examples/tasks"
    env = {**os.environ, "UI_AGENT_WINDOW": "430x880"}
    subprocess.run([ui, "agent", "macos", "--stop"], cwd=project, capture_output=True)
    try:
        result = subprocess.run([ui, "agent", "macos", "clock settle", f"screenshot {SHOTS / 'tasks-macos.png'}"],
                                cwd=project, env=env, capture_output=True, text=True, timeout=600)
    finally:
        subprocess.run([ui, "agent", "macos", "--stop"], cwd=project, capture_output=True)
    return None if result.returncode == 0 else "no screenshot: " + result.stderr[-300:]


def shoot_ios():
    devices = subprocess.run(["xcrun", "simctl", "list", "devices", "booted"], capture_output=True, text=True).stdout
    if "Booted" not in devices:
        return "no booted simulator"
    subprocess.run([sys.executable, HERE / "tools/build_ios.py"], check=True, timeout=300, capture_output=True)
    subprocess.run(["xcrun", "simctl", "install", "booted", BUILD / "ios/EskiuUI.app"], check=True, timeout=60)
    subprocess.run(["xcrun", "simctl", "launch", "--terminate-running-process", "booted", "org.eskiu.ui.prototype",
                    "--example", "4"], check=True, timeout=60, capture_output=True)
    time.sleep(3)
    subprocess.run(["xcrun", "simctl", "io", "booted", "screenshot", SHOTS / "tasks-ios.png"], check=True, timeout=60,
                   capture_output=True)
    return None


def shoot_android():
    adb = [os.environ.get("ADB", "adb")]
    devices = subprocess.run([*adb, "devices"], capture_output=True, text=True).stdout.split("\n")[1:]
    if not any(line.endswith("\tdevice") for line in devices):
        return "no device"
    subprocess.run([sys.executable, HERE / "tools/build_android.py"], check=True, timeout=600, capture_output=True)
    subprocess.run([*adb, "install", "-r", BUILD / "android/EskiuUI.apk"], check=True, timeout=60, capture_output=True)
    subprocess.run([*adb, "shell", "am", "force-stop", "org.eskiu.ui.prototype"], check=True, timeout=30)
    subprocess.run([*adb, "shell", "am", "start", "-W", "-n", "org.eskiu.ui.prototype/org.eskiu.ui.UiActivity", "--ei", "example", "4"],
                   check=True, timeout=60, capture_output=True)
    time.sleep(3)
    png = subprocess.run([*adb, "exec-out", "screencap", "-p"], check=True, timeout=60, capture_output=True).stdout
    (SHOTS / "tasks-android.png").write_bytes(png)
    return None


def main():
    SHOTS.mkdir(exist_ok=True)
    chosen = sys.argv[1:]
    if not chosen or "web" in chosen:
        subprocess.run([sys.executable, HERE / "tools/build_web.py"], check=True, timeout=300, capture_output=True)
    server = serve()
    try:
        hosts = (("web", lambda: shoot_web(server)), ("macos", lambda: shoot_macos(server)),
                 ("ios", shoot_ios), ("android", shoot_android))
        for name, shoot in hosts:
            if chosen and name not in chosen:
                continue
            skipped = shoot()
            path = SHOTS / f"tasks-{name}.png"
            if skipped is None and path.exists():
                clean_png(path)
                print(f"{name}: {path.relative_to(HERE)}")
            else:
                print(f"{name}: SKIPPED ({skipped})")
    finally:
        server.shutdown()


if __name__ == "__main__":
    main()
