# Testing

The tools find the compiler through `ESKIUC` (`ESKIUC_SELF` for the self-hosted one), an
`eskiu` checkout next to this repository (`ESKIU_ROOT` to point elsewhere), or the `PATH`.

## The model tests

`tests/model/` runs the runtime against a recording renderer: one file per area
(`scheduler`, `memory`, `identity`, `elements`, `lists`, `system`, `apps`), with shared
helpers in `support.esk` and the cases called from `main.esk`. Each case checks the
commits and events, and ends with no allocation left.

```bash
tools/ci.sh            # on a released eskiuc, as CI runs it
python3 tools/check.py # everything below, with an eskiu checkout
```

`tools/ci.sh` runs the model tests, `tests/tags.esk` (hooks are matched by type),
`tests/rejections.esk` (each misuse stops with its message: a render loop, a changed hook,
a duplicate key, a memo cycle, an uncaught exception, the runtime left mid-phase, a
misplaced RefreshControl, the scope path in a message), the headless counter, and builds the
`ui` tool and checks `ui new`.

`tools/check.py` also:

- counts every allocation, closure environments included, by pointing the test IR's
  allocator calls at `tests/heap.c`; each case, and 10,000 mount and unmount cycles, must
  end at zero;
- runs the tests at `-O0` and `-O2`, under AddressSanitizer (with a negative control that
  must be reported) and built by the self-hosted compiler;
- runs `tests/benchmark.esk` (10,000 rows mounted, reversed, updated and unmounted).

A new element or behavior gets a model test in the file for its area.

## The hosts

| Command | Checks |
|---|---|
| `python3 tools/build_web.py` then `node tests/web.mjs` | the wasm module against a recording DOM; the build fails when the demo module passes 160 KiB |
| `python3 tools/check_ios.py` | a booted simulator (`DEVICE` picks one) |
| `python3 tools/check_android.py` | a booted arm64 device or emulator (`ANDROID_SERIAL`), SDK 35, NDK 28.2 |
| `python3 tools/check_desktop.py` | a WKWebView window running the web build (needs an awake, unlocked display) |

They drive `tests/harness/app.esk`, which holds every scene a host can be asked for: the
counter (0), the TODO list (1), a probe (2), the conformance scene (3), the tasks app (4)
and the kit gallery (5). A host is started on one with its own launch argument (a query
string on the web, an intent extra on Android, a launch argument on iOS).

## Conformance

`tests/harness/conformance.esk` is one scene every renderer draws. Each host reports the
frame and accessibility role of every labeled element and the handler log after a fixed
script: two taps, typing "abc", a press, a toggle, an alert answered, a modal opened and
closed, a pull to refresh, a feed of 1000 rows scrolled. It also reports the colors,
radius, border, opacity and font a styled box and a title were drawn with.

```bash
python3 tools/conformance.py           # every available host
python3 tools/conformance.py ios web   # only these
```

It compares what each host reports with the frames the scene must have, within one point.
iOS, Android and the web pass.

## Screenshots

`python3 tools/screenshots.py` takes the README's screenshots of `examples/tasks` on the
web, iOS, Android and macOS into `screenshots/`, with the PNG metadata removed.

## CI

`.github/workflows/ci.yml` runs `tools/ci.sh` on Linux and macOS with the released `eskiuc`.
The host checks need simulators, emulators and a display, so they run locally before a
release.

## Measurements

Arm64 Mac (Mac16,8), Eskiu 0.12.0, LLVM 22.1.8:

| Experiment | Result |
|---|---|
| 10,000 TODO mounts and unmounts, native `-O2` | about 40 ms |
| 10,000 renders replacing every handler, native `-O2` | about 12 ms |
| Recording renderer, 10 / 100 / 1,000 rows | p95 0.010 / 0.058 / 0.61 ms per frame |
| Chromium, counter / TODO, 120 frames | render and forced layout p95 1.1 / 8.3 ms |
| iPhone simulator, 100 taps | batched into one frame of 0.13 ms |
| Android emulator, 100 taps | batched into one frame of 1.4 ms |

Whole-list operations on 10,000 rows (`tests/benchmark.esk`, `-O2`), before and after
phase 1's indexing:

| Operation | Before | Now |
|---|---|---|
| Mount | 124 ms | 8.1 ms |
| Reverse (every row moves) | 256 ms | 5.9 ms |
| One event and its frame | 230 ms | 5.4 ms |
| Unmount | 4,133 ms | 6.8 ms |
| A signal read by every row changes | 92 ms | 1.2 ms |

These are baselines, not presentation-time or physical-device numbers;
`build/*-results.json` holds the latest machine-readable results.
