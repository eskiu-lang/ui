#!/usr/bin/env python3
"""Native code on the native hosts: scene 6 of the test app, the native example's clock module
and slider view (examples/native/native), on a booted iOS simulator and Android device.

  python3 tools/check_native.py            # both
  python3 tools/check_native.py android    # one

Each host: ticks from the module reach use_native_event, the slider shows the value prop,
moving it reaches NativeView's handler, and Mute sends the new value back to the view.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parents[1]
APP = "org.eskiu.ui.prototype"


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, timeout=120, **kwargs)


def ios():
    run([sys.executable, HERE / "tools/build_ios.py"])
    devices = subprocess.check_output(["xcrun", "simctl", "list", "devices", "booted"], text=True)
    if "Booted" not in devices:
        raise SystemExit("ios: no booted simulator")
    run(["xcrun", "simctl", "install", "booted", HERE / "build/ios/EskiuUI.app"])
    result = subprocess.run(["xcrun", "simctl", "launch", "--terminate-running-process", "--console", "booted", APP,
                             "--native-test"], capture_output=True, text=True, timeout=60)
    line = next((l for l in result.stdout.splitlines() if l.startswith("iOS native:")), "iOS native: no result")
    print(line)
    return "PASS" in line


def android():
    run([sys.executable, HERE / "tools/build_android.py"])
    adb = [os.environ.get("ADB", "adb")]
    if os.environ.get("ANDROID_SERIAL"):
        adb += ["-s", os.environ["ANDROID_SERIAL"]]
    run([*adb, "install", "-r", HERE / "build/android/EskiuUI.apk"])
    run([*adb, "shell", "am", "force-stop", APP])
    run([*adb, "shell", "am", "start", "-W", "-n", APP + "/org.eskiu.ui.UiActivity", "--ei", "example", "6"])

    def screen():
        return subprocess.check_output([*adb, "exec-out", "uiautomator", "dump", "/dev/tty"], text=True, timeout=30)

    def texts(dump):
        return re.findall(r'text="([^"]+)"', dump)

    time.sleep(2.5)
    dump = screen()
    ticks = next((int(t[7:]) for t in texts(dump) if t.startswith("Ticks: ")), 0)
    initial = "Volume: 30" in texts(dump)
    bar = re.search(r'class="android.widget.SeekBar"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', dump)
    changed = muted = False
    if bar:
        left, top, right, bottom = map(int, bar.groups())
        run([*adb, "shell", "input", "tap", left + (right - left) * 4 // 5, (top + bottom) // 2])
        time.sleep(1)
        volume = next((t for t in texts(screen()) if t.startswith("Volume: ")), "")
        changed = volume not in ("", "Volume: 30")
        mute = re.search(r'text="Mute"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', screen())
        if mute:
            x1, y1, x2, y2 = map(int, mute.groups())
            run([*adb, "shell", "input", "tap", (x1 + x2) // 2, (y1 + y2) // 2])
            time.sleep(1)
            dump = screen()
            progress = re.search(r'class="android.widget.SeekBar"', dump) is not None
            muted = "Volume: 0" in texts(dump) and progress
    run([*adb, "shell", "am", "force-stop", APP])
    ok = ticks >= 1 and initial and changed and muted
    print(f"Android native: {'PASS' if ok else 'FAIL'} ticks={ticks} initial={initial} changed={changed} muted={muted}")
    return ok


wanted = sys.argv[1:] or ["ios", "android"]
results = [{"ios": ios, "android": android}[name]() for name in wanted]
sys.exit(0 if all(results) else 1)
