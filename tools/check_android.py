#!/usr/bin/env python3
"""Native Android UI + cross-compiled runtime gates on a connected device/emulator."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

HERE = Path(__file__).resolve().parents[1]
from paths import compiler as find_compiler
BUILD = HERE / "build/android"
subprocess.run([sys.executable, HERE / "tools/build_android.py"], check=True, timeout=180)
adb = [os.environ.get("ADB", "adb")]
if os.environ.get("ANDROID_SERIAL"):
    adb += ["-s", os.environ["ANDROID_SERIAL"]]


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, timeout=60, **kwargs)


serial = subprocess.check_output([*adb, "get-serialno"], text=True).strip()
if serial in ("unknown", ""):
    raise SystemExit("No connected Android device; set ANDROID_SERIAL if several are connected.")
booted = subprocess.check_output([*adb, "shell", "getprop", "sys.boot_completed"], text=True).strip()
if booted != "1":
    raise SystemExit("Android is not fully booted yet")
run([*adb, "install", "-r", BUILD / "EskiuUI.apk"])
run([*adb, "shell", "am", "force-stop", "org.eskiu.ui.prototype"])
session = uuid.uuid4().hex
run([*adb, "shell", "am", "start", "-W", "-n", "org.eskiu.ui.prototype/.UiActivity",
     "--ez", "selftest", "true", "--es", "session", session])
report = None
deadline = time.monotonic() + 30
while time.monotonic() < deadline:
    result = subprocess.run([*adb, "shell", "run-as", "org.eskiu.ui.prototype", "cat", "files/selftest.json"],
                            capture_output=True, text=True, timeout=10)
    if result.returncode == 0:
        candidate = json.loads(result.stdout)
        if candidate.get("session") == session:
            report = candidate; break
    time.sleep(.2)
if not report or report.get("status") != "PASS":
    raise SystemExit("Android UI self-test failed or timed out. Inspect adb logcat -d -s AndroidRuntime.")
report["serial"] = serial
report["model"] = subprocess.check_output([*adb, "shell", "getprop", "ro.product.model"], text=True).strip()
report["api"] = subprocess.check_output([*adb, "shell", "getprop", "ro.build.version.sdk"], text=True).strip()
shots = HERE / "screenshots"
shots.mkdir(exist_ok=True)
# Exercise the same allocation-counted runtime on the Android target as well.
compiler = find_compiler()
ir = subprocess.check_output([compiler, HERE / "tests/model.esk", "--target", "aarch64-linux-android24",
                              "--test-codegen", "-O2"], text=True)
ir = ir[ir.index("; ModuleID"):].split("\n========================================================")[0]
for symbol in ("malloc", "calloc", "free"):
    ir = re.sub(r"@" + symbol + r"(?=\()", "@ui_test_" + symbol, ir)
path = BUILD / "model.ll"
path.write_text(ir)
sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/28.2.13676358"))
clang = ndk / "toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang"
run([os.environ.get("CLANG", "clang"), "--target=aarch64-linux-android24", "-O2", "-c", path,
     "-o", BUILD / "model.o"])
run([clang, BUILD / "model.o", HERE / "tests/heap.c", "-o", BUILD / "model"])
remote = "/data/local/tmp/eskiu-ui-model"
run([*adb, "push", BUILD / "model", remote], stdout=subprocess.DEVNULL)
report["runtime"] = subprocess.check_output([*adb, "shell", remote], text=True, timeout=60)
assert "PASS leak gates" in report["runtime"]
# Capture after the runtime gate, once the app's launch animation has settled.
with (shots / "counter-android.png").open("wb") as shot:
    run([*adb, "exec-out", "screencap", "-p"], stdout=shot)
(HERE / "build/android-results.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
