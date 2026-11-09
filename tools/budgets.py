#!/usr/bin/env python3
"""Speed budgets (docs/testing.md): measures a host and checks the numbers against
tools/budgets.toml, where each budget has the baseline measured on CI and a tolerance.

    python3 tools/budgets.py web        # cold start, edit to change, frame time, module sizes
    python3 tools/budgets.py macos      # the AppKit host in a window on this Mac
    python3 tools/budgets.py ios        # a booted simulator (or the first iPhone, booted)
    python3 tools/budgets.py android    # a device or a running emulator
    python3 tools/budgets.py model      # the recording renderer's benchmark (no host)
    python3 tools/budgets.py web --update   # write what was measured as the new baselines

It prints a table (and adds it to the GitHub job summary), writes build/budgets-HOST.json and
exits 1 when a number is worse than its baseline by more than the tolerance.

- cold_start_ms: `perf`'s startup_ms after `ui test` starts examples/counter afresh: process
  start (a new page on the web) to the first commit applied, a dev build.
- edit_ms: under `ui serve`, a source file of examples/counter changed, until `ui agent`
  sees the new text in the tree (the median of three edits).
- frame_p95_ms: `perf`'s p95 frame time after tests/bench's 100 frames of 300 rows.
- wasm_bytes.*: the release web module of examples/counter and examples/kit-gallery.
- model.*: tests/benchmark.esk, the recording renderer at -O2: a frame over 1000 rows, and
  mounting and reversing 10,000.
"""
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
import tomllib

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build"
BUDGETS = HERE / "tools/budgets.toml"
UI = BUILD / "ui"


def run(args, cwd=None, timeout=1800, **kwargs):
    return subprocess.run([str(a) for a in args], cwd=cwd, check=True, timeout=timeout, **kwargs)


def ui_tool():
    run(["eskiuc", HERE / "tools/ui/main.esk", "-o", UI])


def perf_of(host, project, test):
    """`ui test HOST` in `project` with --perf; the numbers of `test`."""
    out = BUILD / f"perf-{host}.json"
    out.unlink(missing_ok=True)
    run([UI, "test", host, "--perf", out], cwd=HERE / project)
    return json.loads(out.read_text())[host][test]


def edit_ms(host):
    """Edit to change on screen under `ui serve`: the median of three edits."""
    project = HERE / "examples/counter"
    source = project / "src/counter.esk"
    original = source.read_text()
    times = []
    try:
        run([UI, "agent", host, "--stop"], cwd=project, capture_output=True)
        run([UI, "agent", host, "--json", "tree"], cwd=project, capture_output=True)
        for n in range(3):
            marker = f"Eskiu counter {int(time.time() * 1000) % 100000}-{n}"
            source.write_text(original.replace('Text("Eskiu counter");', f'Text("{marker}");'))
            start = time.monotonic()
            result = run([UI, "agent", host, "--json", "tree"], cwd=project, capture_output=True, text=True)
            elapsed = (time.monotonic() - start) * 1000
            if marker not in result.stdout:
                raise SystemExit(f"edit_ms: the tree does not show the edit:\n{result.stdout[-400:]}")
            times.append(elapsed)
    finally:
        source.write_text(original)
        subprocess.run([str(UI), "agent", host, "--stop"], cwd=project, capture_output=True)
    print(f"edit to change ({host}): " + ", ".join(f"{t:.0f}" for t in times) + " ms")
    return statistics.median(times)


def wasm_bytes(project):
    env = {k: v for k, v in os.environ.items() if k not in ("UI_DEV", "UI_TEST")}
    run([UI, "build", "web"], cwd=HERE / project, env=env, capture_output=True)
    return (HERE / project / "build/ui/web/app.wasm").stat().st_size


def model():
    """The recording renderer at -O2: p95 of a frame over 1000 rows (tests/benchmark.esk)."""
    BUILD.mkdir(exist_ok=True)
    run(["eskiuc", HERE / "tests/benchmark.esk", "-O2", "-o", BUILD / "benchmark.o"])
    eh = "-lc++" if sys.platform == "darwin" else "-lstdc++"
    run([os.environ.get("CC", "cc"), BUILD / "benchmark.o", HERE / "tests/heap.c", eh, "-o", BUILD / "benchmark"])
    text = run([BUILD / "benchmark"], capture_output=True, text=True).stdout
    found = {}
    for rows, p95 in re.findall(r"rows=(\d+) frames=\d+ p50_ms=[\d.]+ p95_ms=([\d.]+)", text):
        if rows == "1000":
            found[f"model.frame_p95_ms.rows{rows}"] = float(p95)
    scale = re.search(r"scale rows=10000 mount_ms=([\d.]+) reverse_ms=([\d.]+)", text)
    if scale:
        found["model.mount_ms.rows10000"] = float(scale.group(1))
        found["model.reverse_ms.rows10000"] = float(scale.group(2))
    return found


def measure(host):
    if host == "model":
        return model()
    ui_tool()
    found = {}
    counter = perf_of(host, "examples/counter", "counter")
    found[f"{host}.cold_start_ms"] = counter["startup_ms"]
    found[f"{host}.frame_p95_ms"] = perf_of(host, "tests/bench", "bench")["frame_p95_ms"]
    found[f"{host}.edit_ms"] = edit_ms(host)
    if host == "web":
        found["web.wasm_bytes.counter"] = wasm_bytes("examples/counter")
        found["web.wasm_bytes.kit_gallery"] = wasm_bytes("examples/kit-gallery")
    return found


def number(value):
    """A measurement as text: whole bytes, timings to a tenth (three places under 10 ms)."""
    if not isinstance(value, float):
        return str(value)
    return f"{value:.3f}" if value < 10 else f"{value:.1f}"


def load():
    return tomllib.loads(BUDGETS.read_text())


def write_baselines(found):
    """Each measured key's baseline in budgets.toml becomes what was measured."""
    text = BUDGETS.read_text()
    for key, value in found.items():
        shown = number(value)
        pattern = re.compile(r'(\["' + re.escape(key) + r'"\]\nbaseline = )[^\n]*')
        if pattern.search(text):
            text = pattern.sub(lambda m: m.group(1) + shown, text)
        else:
            text += f'\n["{key}"]\nbaseline = {shown}\ntolerance = 0.5\n'
    BUDGETS.write_text(text)


def check(found):
    budgets = load()
    rows, failed = [], False
    for key in sorted(found):
        value = found[key]
        budget = budgets.get(key)
        if budget is None or budget.get("baseline", 0) == 0:
            rows.append((key, value, None, None, "new: no baseline"))
            continue
        base, tolerance = budget["baseline"], budget["tolerance"]
        limit = base * (1 + tolerance)
        change = (value - base) / base * 100
        ok = value <= limit
        failed = failed or not ok
        rows.append((key, value, base, limit, f"{change:+.0f}%" + ("" if ok else f" FAIL: over {limit:.6g}")))
    shown = lambda v: "" if v is None else number(v)
    lines = ["| Budget | Measured | Baseline | Limit | Change |", "|---|---|---|---|---|"]
    lines += [f"| {k} | {shown(v)} | {shown(b)} | {shown(l)} | {s} |" for k, v, b, l, s in rows]
    table = "\n".join(lines)
    print(table)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as out:
            out.write("### Speed budgets\n\n" + table + "\n\n")
    return failed


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1 or args[0] not in ("web", "macos", "ios", "android", "model"):
        raise SystemExit(__doc__)
    host = args[0]
    found = measure(host)
    BUILD.mkdir(exist_ok=True)
    (BUILD / f"budgets-{host}.json").write_text(json.dumps(found, indent=2) + "\n")
    if "--update" in sys.argv:
        write_baselines(found)
        print(f"wrote the baselines of {host} to {BUDGETS.relative_to(HERE)}")
    sys.exit(1 if check(found) else 0)


if __name__ == "__main__":
    main()
