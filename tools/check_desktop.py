#!/usr/bin/env python3
"""Build and execute a real WKWebView desktop host against the wasm app."""
import functools
import http.server
import json
from pathlib import Path
import subprocess
import threading

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build/desktop"
BUILD.mkdir(exist_ok=True)
shots = HERE / "build/screenshots"  # test captures; the README's come from tools/screenshots.py
shots.mkdir(parents=True, exist_ok=True)
binary = BUILD / "EskiuDesktop"
subprocess.run(["xcrun", "clang", "-fobjc-arc", HERE / "hosts/macos/host.m", "-framework", "Cocoa",
                "-framework", "WebKit", "-o", binary], check=True, timeout=60)
handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(HERE / "build/web"))
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    url = f"http://127.0.0.1:{server.server_port}/?example=counter&selftest=1"
    result = subprocess.run([binary, url, shots / "todo-macos.png"], capture_output=True, text=True, timeout=60)
    (HERE / "build/desktop-results.txt").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    report = json.loads(result.stdout)
    assert report["status"] == "PASS", report
    report["host"] = "macOS WKWebView (shared wasm/DOM renderer)"
    (HERE / "build/desktop-results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
finally:
    server.shutdown()
