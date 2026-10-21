#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parents[1]
from paths import compiler as find_compiler
BUILD = HERE / "build/web"
BUILD.mkdir(parents=True, exist_ok=True)
compiler = find_compiler()
subprocess.run([compiler, str(HERE / "tests/harness/app.esk"), "--target", "wasm32-unknown-emscripten",
                "-O2", "-o", str(BUILD / "app.mjs"),
                "--link-arg=--js-library", "--link-arg=" + str(HERE / "hosts/web/ui-library.js"),
                "--link-arg=--js-library", "--link-arg=" + str(HERE / "examples/native/native/web/clock.js"),
                "--link-arg=-sMODULARIZE=1", "--link-arg=-sEXPORT_ES6=1",
                "--link-arg=-sALLOW_MEMORY_GROWTH=1", "--link-arg=-sINVOKE_RUN=0",
                "--link-arg=--no-entry",
                "--link-arg=-sEXPORTED_FUNCTIONS=_ui_start,_ui_dispatch,_ui_dispatch_text,"
                "_ui_dispatch_focus,_ui_dispatch_toggle,_ui_dispatch_scroll,_ui_dispatch_alert,_ui_dispatch_refresh,"
                "_ui_report_back,"
                "_ui_report_link,_ui_report_window,_ui_tick,_ui_stop,"
                "_ui_conformance_log,_ui_dispatch_native,_ui_dispatch_view,_ui_dispatch_route,_ui_dispatch_layout,_ui_dispatch_timer,_ui_dispatch_hover",
                "--link-arg=-sEXPORTED_RUNTIME_METHODS=UTF8ToString,ccall"], check=True)
# The renderer (hosts/web) and the test pages (tests/harness/web) side by side.
for filename in ("renderer.mjs", "scheduler.mjs"):
    shutil.copyfile(HERE / "hosts/web" / filename, BUILD / filename)
for filename in ("index.html", "benchmark.mjs", "platform-test.mjs", "conformance.mjs"):
    shutil.copyfile(HERE / "tests/harness/web" / filename, BUILD / filename)
for asset in (HERE / "tests/harness/assets").iterdir():
    shutil.copyfile(asset, BUILD / asset.name)
# Every example is in this module; an app built by tools/ui carries only its own code.
if (BUILD / "app.wasm").stat().st_size > 160 * 1024:
    raise SystemExit("demo wasm budget exceeded (160 KiB)")
print("Built:", BUILD, "wasm bytes:", (BUILD / "app.wasm").stat().st_size)
