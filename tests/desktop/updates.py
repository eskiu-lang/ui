#!/usr/bin/env python3
"""Asset updates on a desktop host (docs/updates.md): tests/web/updates-app built for this
system's native host (macos, linux or windows), or with --webview for its webview host
(WKWebView, WebKitGTK, WebView2), a changed asset and a new one published with `ui publish`
and served on 127.0.0.1:8793 (the app's [updates] url).

- the app downloads the bundle after its first frame and keeps it for the next start;
- the next start uses it: data.txt comes from the downloaded file;
- a bundle whose file does not match its hash is refused and the app keeps the bundle it has.

The app prints its state (UI_UPDATES_REPORT=1), since a release build has no driver. A
webview build prints it on the page's console, which the host forwards to its output, with
the text the page got for data.txt from the host (the update directory first, then the app's
own file).

    python3 tests/desktop/updates.py macos
    python3 tests/desktop/updates.py macos --webview

Needs the host's toolchain and a display (and emcc with --webview). The signing key is made
for the test in build/.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(HERE / "tests/web"))
from serve import serve  # noqa: E402

ARGS = [arg for arg in sys.argv[1:] if arg != "--webview"]
WEBVIEW = "--webview" in sys.argv[1:]
HOST = ARGS[0] if ARGS else {"darwin": "macos", "win32": "windows"}.get(sys.platform, "linux")
APP = HERE / f"build/updates-app-{HOST}{'-webview' if WEBVIEW else ''}"
BUNDLE = APP / "build/ui/updates"
UPDATES = HERE / "build/updates-server"
ID = "org.eskiu.test.updates"

failures = []


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def ui(*args):
    env = {k: v for k, v in os.environ.items() if k not in ("UI_DEV", "UI_TEST")}
    env["UI_UPDATE_KEY_FILE"] = str(APP / "build/test.key")
    tool = HERE / ("build/ui.exe" if sys.platform == "win32" else "build/ui")
    result = subprocess.run([tool, *args], cwd=APP, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"ui {' '.join(args)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def publish(version):
    ui("publish", "--version", str(version))
    shutil.rmtree(UPDATES, ignore_errors=True)
    shutil.copytree(BUNDLE, UPDATES)


def data_dir():
    """Where the host keeps the app's data (and the updater's files and state)."""
    if HOST == "macos":
        return Path.home() / "Library/Application Support" / ID
    if HOST == "windows":
        return Path(os.environ["APPDATA"]) / "updatesapp"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / ID


def webview_data():
    """Where the webview keeps the page's storage (the core's state is in its localStorage)."""
    if not WEBVIEW:
        return []
    if HOST == "macos":
        return [Path.home() / "Library/WebKit" / ID, Path.home() / "Library/Caches" / ID]
    if HOST == "windows":
        return [Path(os.environ["LOCALAPPDATA"]) / "updatesapp"]
    return [Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / ID]


def clean():
    for path in [data_dir(), *webview_data()]:
        shutil.rmtree(path, ignore_errors=True)


def updated(state, text):
    """Whether data.txt is the downloaded file: its path (native) or the page's text (webview)."""
    data = state.get("data", "")
    if WEBVIEW:
        return data == text.strip()
    return data != "data.txt" and Path(data).is_file() and Path(data).read_text() == text


def program():
    out = APP / "build/ui" / HOST
    return {"macos": out / "updatesapp.app/Contents/MacOS/updatesapp", "linux": out / "updatesapp",
            "windows": out / "updatesapp.exe"}[HOST]


def run(until, seconds=40):
    """Starts the app and reads its state lines until one matches `until`; the last state."""
    env = {**os.environ, "UI_UPDATES_REPORT": "1", "UI_NO_OPEN": "1"}
    process = subprocess.Popen([program()], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    state, deadline = {}, time.monotonic() + seconds
    # An app that prints nothing more is stopped at the deadline too.
    timer = threading.Timer(seconds, process.kill)
    timer.start()
    try:
        while time.monotonic() < deadline:
            line = process.stdout.readline()
            if not line:
                break
            found = re.match(r"UPDATES version=(\S*) ready=(\S*) error=(.*) data=(.*)$", line.rstrip("\r\n"))
            if not found:
                continue
            state = dict(zip(("version", "ready", "error", "data"), found.groups()))
            if until(state):
                break
    finally:
        timer.cancel()
        process.kill()
        process.wait()
    return state


def main():
    shutil.rmtree(APP, ignore_errors=True)
    shutil.copytree(HERE / "tests/web/updates-app", APP, ignore=shutil.ignore_patterns("build"))
    # ui, from the copy (relative: the tool joins a path that has no drive letter to the app's).
    ui_path = Path(os.path.relpath(HERE, APP)).as_posix()
    for name in ("eskiu.lock", "eskiu.toml"):
        (APP / name).write_text((APP / name).read_text().replace("../../..", ui_path))
    clean()
    ui("build", HOST, *(["--webview"] if WEBVIEW else []))
    (APP / "assets/data.txt").write_text("second\n")
    (APP / "assets/new.txt").write_text("added\n")
    publish(100)
    server, _ = serve(UPDATES, cors=True, port=8793)

    state = run(lambda s: s["ready"] == "100" or s["error"])
    check(state.get("ready") == "100" and state.get("version") == "", f"bundle 100 downloads and waits for the next start ({state})")
    state = run(lambda s: s["version"] == "100" and updated(s, "second\n"))
    check(state.get("version") == "100", f"the next start uses bundle 100 ({state})")
    check(updated(state, "second\n"), f"data.txt is the downloaded file ({state.get('data')})")

    # A file that does not match its hash.
    (APP / "assets/data.txt").write_text("third\n")
    publish(200)
    changed = re.search(r"^file data.txt \d+ ([0-9a-f]{64})$", (UPDATES / "manifest.txt").read_text(), re.M).group(1)
    (UPDATES / "files" / changed).write_text("tampered\n")
    state = run(lambda s: bool(s["error"]))
    check("does not match its hash" in state.get("error", ""), f"a file that does not match its hash is refused ({state})")
    state = run(lambda s: s["version"] == "100" and updated(s, "second\n"))
    check(state.get("version") == "100" and updated(state, "second\n"), f"and the app keeps bundle 100 ({state})")
    server.shutdown()
    clean()
    if failures:
        sys.exit(1)
    print(f"PASS {HOST}{' webview' if WEBVIEW else ''} updates")


if __name__ == "__main__":
    main()
