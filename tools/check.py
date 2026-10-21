#!/usr/bin/env python3
"""Phase-0 host gates. No compiler edits or third-party Python dependencies."""
import json
import os
from pathlib import Path
import re
import subprocess
import time

HERE = Path(__file__).resolve().parents[1]
from paths import compiler as find_compiler, compiler_env
BUILD = HERE / "build"
CC = os.environ.get("CLANG", "clang")
ESKIUC = Path(find_compiler())
ENV = {**compiler_env(), "ASAN_OPTIONS": "detect_leaks=0:exitcode=86"}


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, env=ENV, timeout=60, **kwargs)


def build_model(compiler, name, opt, asan=False):
    # Rename allocator references in generated test IR only. The C shim calls libc,
    # counting ALL Eskiu allocations, including compiler-created closure envs.
    args = [compiler, HERE / "tests/model.esk", "--test-codegen", opt]
    if asan:
        args += ["--asan"]
    raw = run(args, capture_output=True, text=True).stdout
    if "; ModuleID" in raw:
        raw = raw[raw.index("; ModuleID"):].split("\n========================================================")[0]
    for symbol in ("malloc", "calloc", "free"):
        raw = re.sub(r"@" + symbol + r"(?=\()", "@ui_test_" + symbol, raw)
    if asan:
        # --test-codegen returns IR before the driver's sanitizer pipeline. Tell
        # clang to instrument every generated Eskiu function, not just heap.c.
        raw = re.sub(r"(?m)^(define [^\n]*)\{$", r"\1 sanitize_address {", raw)
        assert "sanitize_address" in raw
    ir = BUILD / f"{name}.ll"
    ir.write_text(raw)
    binary = BUILD / name
    run([CC, opt, "-Wno-override-module", *(["-fsanitize=address"] if asan else []),
         ir, HERE / "tests/heap.c", "-o", binary])
    result = run([binary], capture_output=True, text=True)
    print(name + ":\n" + result.stdout, end="")
    return result.stdout


def main():
    BUILD.mkdir(exist_ok=True)
    measurements = {"recorded_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "host": os.uname().machine}
    # Front-end and backend validation are separate, as required by AGENTS.md.
    run([ESKIUC, HERE / "tests/model.esk", "--test-typechecker"])
    for name, opt, asan in [("model-O0", "-O0", False), ("model-O2", "-O2", False), ("model-asan", "-O1", True)]:
        measurements[name] = build_model(ESKIUC, name, opt, asan)
    selfhost = Path(find_compiler("ESKIUC_SELF", "eskiuc-esk"))
    measurements["model-selfhost"] = build_model(selfhost, "model-selfhost", "-O0")
    for name, compiler in [("tags-cpp", ESKIUC), ("tags-self", selfhost)]:
        run([compiler, HERE / "tests/tags.esk", "-o", BUILD / name])
        run([BUILD / name])
    run([ESKIUC, HERE / "tests/rejections.esk", "-o", BUILD / "rejections"])
    for mode, message in [("1", "render loop"), ("2", "hook kind/type changed"),
                          ("3", "duplicate key"), ("4", "memo cycle"),
                          ("5", "uncaught exception"), ("6", "runtime left mid-phase"),
                          ("7", "duplicate key"),
                          ("8", "slot 0: expected use_signal, found use_memo\n  in Column[0] > TodoRow[key 3]")]:
        result = subprocess.run([BUILD / "rejections", mode], env=ENV, capture_output=True, text=True, timeout=10)
        if result.returncode == 0 or message not in result.stdout + result.stderr:
            raise AssertionError((mode, result.returncode, result.stdout, result.stderr))
        print("PASS rejection:", message)
    run([ESKIUC, HERE / "tests/payload_lifetime.esk", "--asan", "-O0", "-o", BUILD / "payload-asan"])
    result = subprocess.run([BUILD / "payload-asan"], env=ENV, capture_output=True, text=True, timeout=10)
    if result.returncode == 0 or "heap-use-after-free" not in result.stderr:
        raise AssertionError("ASan negative control did not detect the expired payload: " + result.stderr)
    print("PASS ASan negative control")
    run([ESKIUC, HERE / "tests/benchmark.esk", "-O2", "-o", BUILD / "benchmark.o"])
    run([CC, BUILD / "benchmark.o", HERE / "tests/heap.c", "-o", BUILD / "benchmark"])
    measurements["recording-renderer-benchmark"] = run([BUILD / "benchmark"], capture_output=True, text=True).stdout
    print(measurements["recording-renderer-benchmark"], end="")
    (BUILD / "host-results.json").write_text(json.dumps(measurements, indent=2) + "\n")


if __name__ == "__main__":
    main()
