#!/usr/bin/env python3
"""The conformance suite: the scene in tests/harness/conformance.esk on every available host.

Two parts. First `ui test HOST` on tests/conformance, the scene as an app: its frames, its
handlers driven by the platform's input, the feed and the modal (tests/conformance/tests/ui/,
the same files on every host). Then what the driver's ops cannot reach, from each host's own
run of the scene: accessibility roles, what the host drew (colors, radius, font, transform),
the slider, the alert, the clipboard, the fetch, pull to refresh and the modal's cover.

Hosts: ios (a booted simulator), android (a booted device or emulator), web (`ui test` in a
headless Chrome, then the shared DOM renderer in a macOS WKWebView window, which needs an
awake, unlocked display). A host that is not available is reported as skipped. Exit status
1 if an available host differs."""
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import uuid

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build"
TOLERANCE = 1.0  # points; Android lays out in whole pixels

# The frames are checked by tests/conformance/tests/ui/layout.uitest (`ui test`).
EXPECTED_ROLES = {"Suite": "group", "Boxes": "group", "Fixed": "group", "Grow": "group", "List": "scroll",
                  "Item 0": "group", "Item 1": "group", "Item 2": "group", "Logo": "image",
                  "Field": "textfield", "Tap": "button", "Press": "button", "Toggle": "switch", "Volume": "slider",
                  "Busy": "progressbar", "Painted": "group", "Title": "text", "Feed": "scroll", "Ask": "button"}
EXPECTED_LOG = "layout:100x50,timer,store:saved,tap,tap,text:abc,press,switch:1,slider:70,clip:eskiu,alert:1,close"
# What the host drew: colors as rrggbbaa, sizes in points, opacity 0 to 1.
EXPECTED_STYLES = {
    "Painted": {"background": "ff0000ff", "radius": 8, "border": 2, "borderColor": "000000ff", "opacity": 0.5},
    "Title": {"color": "0000ffff", "fontSize": 24, "fontWeight": 700},
    "Moved": {"translateX": 20, "translateY": 10, "scale": 1.5, "rotate": 45},
}


# Each platform's own way to ask a modal to close, tried after the shared script.
EXTRA_LOG = {"android": ",close", "web": ",close"}


def differences(report, host=""):
    out = []
    for label, role in EXPECTED_ROLES.items():
        if report["roles"].get(label) != role:
            out.append(f"{label}: role {report['roles'].get(label)}, expected {role}")
    for label, expected in EXPECTED_STYLES.items():
        got = report.get("styles", {}).get(label, {})
        for name, want in expected.items():
            have = got.get(name)
            same = abs(want - have) <= 0.02 if isinstance(want, (int, float)) and isinstance(have, (int, float)) else want == have
            if not same:
                out.append(f"{label}.{name}: expected {want!r}, got {have!r}")
    # After the feed scrolls to 2000 points only a window of rows is alive (where row 50 is,
    # and that row 0 is gone, is in tests/conformance/tests/ui/feed.uitest).
    scrolled = report.get("scrolled", {})
    rows = [label for label in scrolled if label.startswith("Row ")]
    # The modal: a layer over the whole window, holding the 200 by 100 sheet.
    modal = report.get("modal")
    if not modal:
        out.append("modal: not reported")
    else:
        cover, window = modal["cover"], modal["window"]
        if any(abs(a - b) > TOLERANCE for a, b in zip(cover, [0, 0, window[0], window[1]])):
            out.append(f"modal: covers {cover}, expected the window {window}")
        if any(abs(a - b) > TOLERANCE for a, b in zip(modal["sheet"][2:], [200, 100])):
            out.append(f"modal: sheet {modal['sheet']}, expected 200 by 100")
    if len(rows) > 30:
        out.append(f"feed: {len(rows)} rows mounted, expected a window")
    expected_log = EXPECTED_LOG + EXTRA_LOG.get(host, "") + ",refresh"
    # The fetch answers when the network does: present once, anywhere in the log.
    entries = (report.get("log") or "").split(",")
    fetches = [entry for entry in entries if entry.startswith("fetch:")]
    if fetches != ["fetch:200:pong"]:
        out.append(f"fetch: {fetches}, expected ['fetch:200:pong']")
    report = {**report, "log": ",".join(entry for entry in entries if not entry.startswith("fetch:"))}
    # Pulling the feed shows the platform's indicator; "Loaded" hides it.
    if report.get("refresh") != [True, False]:
        out.append(f"refresh indicator after pull and after loading: {report.get('refresh')}, expected [True, False]")
    if report.get("log") != expected_log:
        out.append(f"log: {report.get('log')!r}, expected {expected_log!r}")
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
    subprocess.run([*adb, "shell", "am", "start", "-W", "-n", "org.eskiu.ui.prototype/org.eskiu.ui.UiActivity",
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
    subprocess.run(["xcrun", "clang", "-fobjc-arc", HERE / "hosts/macos/webview.m", "-framework", "Cocoa",
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


class Ping(http.server.BaseHTTPRequestHandler):
    """The scene's fetch: "pong", readable from any page."""
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(b"pong")

    def log_message(self, *args):
        pass


def ui_tests(name):
    """`ui test NAME` on tests/conformance; None when the host is not available here."""
    ui = BUILD / "ui"
    subprocess.run(["eskiuc", HERE / "tools/ui/main.esk", "-o", ui], check=True, timeout=300)
    result = subprocess.run([ui, "test", name], cwd=HERE / "tests/conformance", capture_output=True, text=True,
                            timeout=1800)
    print(result.stdout, end="")
    return result.returncode == 0


def available(name):
    if name == "web":
        return shutil.which("emcc") is not None
    if name == "macos":
        return sys.platform == "darwin"
    if name == "ios":
        devices = json.loads(subprocess.check_output(["xcrun", "simctl", "list", "devices", "booted", "--json"], text=True))
        return any(d["state"] == "Booted" for group in devices["devices"].values() for d in group)
    adb = [os.environ.get("ADB", "adb")]
    try:
        return any(line.endswith("\tdevice") for line in subprocess.check_output([*adb, "devices"], text=True, timeout=10).split("\n")[1:])
    except (OSError, subprocess.SubprocessError):
        return False


def main():
    ping = http.server.ThreadingHTTPServer(("127.0.0.1", 8899), Ping)
    threading.Thread(target=ping.serve_forever, daemon=True).start()
    wanted = sys.argv[1:] or ["ios", "android", "web", "macos"]
    # The AppKit host has its ui tests and no run of its own yet.
    hosts = {"ios": run_ios, "android": run_android, "web": run_web, "macos": lambda: (None, "the ui tests only")}
    results, failed = {}, False
    for name in wanted:
        if available(name) and not ui_tests(name):
            failed = True
        report, skipped = hosts[name]()
        if report is None:
            print(f"{name}: SKIPPED ({skipped})")
            results[name] = {"skipped": skipped}
            continue
        diff = differences(report, name)
        failed = failed or bool(diff)
        print(f"{name}: {'PASS' if not diff else 'FAIL'}")
        for line in diff:
            print("  " + line)
        results[name] = {"report": report, "differences": diff}
    (BUILD / "conformance-results.json").write_text(json.dumps(results, indent=2) + "\n")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
