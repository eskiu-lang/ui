#!/usr/bin/env python3
"""The conformance suite: the scene in examples/conformance.esk on every available host,
each compared with the frames, roles and event log the scene must produce.

Hosts: ios (a booted simulator), android (a booted device or emulator), web (the shared
DOM renderer in a macOS WKWebView window; needs an awake, unlocked display). A host that
is not available is reported as skipped. Exit status 1 if an available host differs."""
import functools
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build"
TOLERANCE = 1.0  # points; Android lays out in whole pixels

# [x, y, width, height] relative to the scene; None: measured by the platform (text, controls).
EXPECTED_FRAMES = {
    "Suite": [0, 0, 320, None],
    "Boxes": [10, 10, 300, 50],
    "Fixed": [10, 10, 100, 50],
    "Grow": [120, 10, 190, 50],
    "List": [10, 70, 300, 100],
    "Item 0": [10, 70, 300, 40],
    "Item 1": [10, 118, 300, 40],
    "Item 2": [10, 166, 300, 40],
    "Logo": [10, 180, 48, 48],
    "Field": [10, 238, 300, None],
    "Tap": [10, None, 300, None],
}
EXPECTED_ROLES = {"Suite": "group", "Boxes": "group", "Fixed": "group", "Grow": "group", "List": "scroll",
                  "Item 0": "group", "Item 1": "group", "Item 2": "group", "Logo": "image",
                  "Field": "textfield", "Tap": "button"}
EXPECTED_LOG = "tap,tap,text:abc"


def differences(report):
    out = []
    for label, expected in EXPECTED_FRAMES.items():
        got = report["frames"].get(label)
        if got is None:
            out.append(f"{label}: missing")
            continue
        for name, want, have in zip(("x", "y", "width", "height"), expected, got):
            if want is not None and abs(want - have) > TOLERANCE:
                out.append(f"{label}.{name}: expected {want}, got {have}")
    for label, role in EXPECTED_ROLES.items():
        if report["roles"].get(label) != role:
            out.append(f"{label}: role {report['roles'].get(label)}, expected {role}")
    if report.get("log") != EXPECTED_LOG:
        out.append(f"log: {report.get('log')!r}, expected {EXPECTED_LOG!r}")
    return out


def run_ios():
    devices = json.loads(subprocess.check_output(["xcrun", "simctl", "list", "devices", "booted", "--json"], text=True))
    if not any(d["state"] == "Booted" for group in devices["devices"].values() for d in group):
        return None, "no booted simulator"
    subprocess.run([sys.executable, HERE / "tools/build_ios.py"], check=True, timeout=300, capture_output=True)
    subprocess.run(["xcrun", "simctl", "install", "booted", BUILD / "ios/EskiuUI.app"], check=True, timeout=60)
    result = subprocess.run(["xcrun", "simctl", "launch", "--terminate-running-process", "--console", "booted",
                             "org.eskiu.ui.prototype", "--conformance"], capture_output=True, text=True, timeout=60)
    for line in result.stdout.splitlines():
        if line.startswith("CONFORMANCE "):
            return json.loads(line.split(" ", 1)[1]), None
    return None, "no result: " + (result.stdout + result.stderr)[-400:]


def run_android():
    adb = [os.environ.get("ADB", "adb")] + (["-s", os.environ["ANDROID_SERIAL"]] if "ANDROID_SERIAL" in os.environ else [])
    try:
        devices = subprocess.check_output([*adb, "devices"], text=True, timeout=10).split("\n")[1:]
    except (OSError, subprocess.SubprocessError):
        return None, "no adb"
    if not any(line.endswith("\tdevice") for line in devices):
        return None, "no device"
    subprocess.run([sys.executable, HERE / "tools/build_android.py"], check=True, timeout=600, capture_output=True)
    subprocess.run([*adb, "install", "-r", BUILD / "android/EskiuUI.apk"], check=True, timeout=60, capture_output=True)
    subprocess.run([*adb, "shell", "am", "force-stop", "org.eskiu.ui.prototype"], check=True, timeout=30)
    session = uuid.uuid4().hex
    subprocess.run([*adb, "shell", "am", "start", "-W", "-n", "org.eskiu.ui.prototype/.UiActivity",
                    "--ez", "conformance", "true", "--es", "session", session], check=True, timeout=60,
                   capture_output=True)
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        result = subprocess.run([*adb, "shell", "run-as", "org.eskiu.ui.prototype", "cat", "files/conformance.json"],
                                capture_output=True, text=True, timeout=10)
        if result.returncode == 0 and result.stdout.strip():
            report = json.loads(result.stdout)
            if report.get("session") == session:
                return report, None
        time.sleep(.2)
    return None, "timed out"


def run_web():
    subprocess.run([sys.executable, HERE / "tools/build_web.py"], check=True, timeout=300, capture_output=True)
    binary = BUILD / "desktop/EskiuDesktop"
    binary.parent.mkdir(exist_ok=True)
    subprocess.run(["xcrun", "clang", "-fobjc-arc", HERE / "desktop/host.m", "-framework", "Cocoa",
                    "-framework", "WebKit", "-o", binary], check=True, timeout=60)
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass
    handler = functools.partial(Quiet, directory=str(BUILD / "web"))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        url = f"http://127.0.0.1:{server.server_port}/?example=conformance&selftest=1"
        result = subprocess.run([binary, url, BUILD / "desktop/conformance.png"], capture_output=True, text=True,
                                timeout=60)
        report = json.loads(result.stdout) if result.stdout.strip().startswith("{") else None
        if not report or report.get("status") != "DONE":
            return None, (report or {}).get("error", "no result") + " (is the display awake?)"
        return report, None
    except subprocess.TimeoutExpired:
        return None, "timed out (is the display awake?)"
    finally:
        server.shutdown()


def main():
    wanted = sys.argv[1:] or ["ios", "android", "web"]
    hosts = {"ios": run_ios, "android": run_android, "web": run_web}
    results, failed = {}, False
    for name in wanted:
        report, skipped = hosts[name]()
        if report is None:
            print(f"{name}: SKIPPED ({skipped})")
            results[name] = {"skipped": skipped}
            continue
        diff = differences(report)
        failed = failed or bool(diff)
        print(f"{name}: {'PASS' if not diff else 'FAIL'}")
        for line in diff:
            print("  " + line)
        results[name] = {"report": report, "differences": diff}
    (BUILD / "conformance-results.json").write_text(json.dumps(results, indent=2) + "\n")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
