#!/usr/bin/env python3
"""Run the UIKit/Yoga counter on a booted simulator; fail unless UI assertions pass."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, HERE / "tools/build_ios.py"], check=True, timeout=180)
devices = json.loads(subprocess.check_output(["xcrun", "simctl", "list", "devices", "booted", "--json"], text=True))
booted = [device for group in devices["devices"].values() for device in group if device["state"] == "Booted"]
device = os.environ.get("DEVICE", booted[0]["udid"] if booted else "")
if not device:
    raise SystemExit("No booted simulator. Boot one with xcrun simctl boot DEVICE.")
subprocess.run(["xcrun", "simctl", "install", device, HERE / "build/ios/EskiuUI.app"], check=True, timeout=60)
result = subprocess.run(["xcrun", "simctl", "launch", "--terminate-running-process", "--console", device,
                         "org.eskiu.ui.prototype", "--self-test"], capture_output=True, text=True, timeout=60)
output = result.stdout + result.stderr
(HERE / "build/ios-results.txt").write_text(output)
print(output)
if result.returncode or "iOS counter: PASS" not in output or "Count: 100" not in output:
    raise SystemExit("iOS counter self-test failed")
match = re.search(r"frame_p95_ms=([\d.]+)", output)
report = {"device": device, "simulator": True, "frameP95Ms": float(match[1]), "output": output}
(HERE / "build/ios-results.json").write_text(json.dumps(report, indent=2) + "\n")
