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
subprocess.run([compiler, str(HERE / "examples/app.esk"), "--target", "wasm32-unknown-emscripten",
                "-O2", "-o", str(BUILD / "app.mjs"),
                "--link-arg=--js-library", "--link-arg=" + str(HERE / "web/ui-library.js"),
                "--link-arg=-sMODULARIZE=1", "--link-arg=-sEXPORT_ES6=1",
                "--link-arg=-sALLOW_MEMORY_GROWTH=1", "--link-arg=-sINVOKE_RUN=0",
                "--link-arg=--no-entry",
                "--link-arg=-sEXPORTED_FUNCTIONS=_ui_start,_ui_dispatch,_ui_dispatch_text,"
                "_ui_dispatch_focus,_ui_dispatch_toggle,_ui_dispatch_scroll,_ui_report_window,_ui_tick,_ui_stop,"
                "_ui_conformance_log",
                "--link-arg=-sEXPORTED_RUNTIME_METHODS=UTF8ToString,ccall"], check=True)
for filename in ("index.html", "renderer.mjs", "scheduler.mjs", "benchmark.mjs", "platform-test.mjs",
                 "conformance.mjs"):
    shutil.copyfile(HERE / "web" / filename, BUILD / filename)
for asset in (HERE / "assets").iterdir():
    shutil.copyfile(asset, BUILD / asset.name)
if (BUILD / "app.wasm").stat().st_size > 128 * 1024:
    raise SystemExit("phase-0 wasm budget exceeded (128 KiB)")
print("Built:", BUILD, "wasm bytes:", (BUILD / "app.wasm").stat().st_size)
