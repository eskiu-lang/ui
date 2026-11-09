# Testing

The tools find the compiler through `ESKIUC` (`ESKIUC_SELF` for the self-hosted one), an
`eskiu` checkout next to this repository (`ESKIU_ROOT` to point elsewhere), or the `PATH`.

## The model tests

`tests/model/` runs the runtime against a recording renderer: one file per area
(`scheduler`, `memory`, `identity`, `elements`, `lists`, `system`, `apps`, `errors`), with
shared helpers in `support.esk` and the cases called from `main.esk`. Each case checks the
commits and events, and ends with no allocation left. The `errors` cases run through a host
mirror that counts any op on an id the host does not hold, so a recovered error must leave
only valid commits.

```bash
tools/ci.sh            # on a released eskiuc, as CI runs it
python3 tools/check.py # everything below, with an eskiu checkout
```

`tools/ci.sh` runs the model tests, `tests/ed25519.esk` (Ed25519 and SHA-512 against RFC 8032's
vectors), `tests/updates.esk` (asset updates against a host stand-in: [updates](updates.md)),
`tests/tags.esk` (hooks are matched by type),
`tests/constants.esk` (the app's constants as the ui tool compiles them in),
the compiled-out check (a release object has no symbol of the driver, a `UI_TEST` one has them),
`tests/rejections.esk` (each misuse stops with its message: a render loop, a changed hook,
a duplicate key, a memo cycle, the root's fallback throwing, a misplaced RefreshControl, the
scope path in a message), the headless counter, and builds the
`ui` tool and checks `ui new`, `ui serve` and `ui build --json` (with a stand-in compiler,
`tests/fake_eskiuc.sh`: the page's wait answers each build, a failed one with its output, and
its text diagnostics, a warning with an option, a hint and notes, become the JSON lines
`eskiuc --diagnostics=json` would write), and `ui publish` (a signed manifest, a key made
outside the project, the build's update macros, a key that is not `ui.toml`'s refused).

`tools/check.py` also:

- counts every allocation, closure environments included, by pointing the test IR's
  allocator calls at `tests/heap.c`; each case, and 10,000 mount and unmount cycles, must
  end at zero;
- runs the tests at `-O0` and `-O2`, under AddressSanitizer (with a negative control that
  must be reported) and built by the self-hosted compiler;
- runs `tests/benchmark.esk` (10,000 rows mounted, reversed, updated and unmounted).

`reload.esk` checks what a reload keeps: the snapshot's text, the routes and the signals
restored, a changed type and a removed component's state dropped, a keyed signal found where it
moved, and nothing restored after the first input. With a compiler that has `src_line()`,
`tools/ci.sh` and `tools/check.py` build the model tests again with `UI_SRCLOC`, where
`srcloc.esk` checks each element's call position (a kit component's included) and `tree`'s,
and a rejection names the element's line; with Eskiu 0.14.1 they say SKIP.

A new element or behavior gets a model test in the file for its area. The model tests are
built with `UI_TEST`, so they include the driver; `driver.esk` checks test ids (kept in the
tree, never sent to the host, found before the accessible name), the ops the core answers and
the directives it gives the host, and the virtual clock (timers fire only when it moves,
`clock settle` runs to idle, `use_frame` ticks on it).

## UI tests

`ui test` runs an app's UI tests on each host through the driver `ui agent` uses
([Agents](agents.md)). A test is a text file, `tests/ui/NAME.uitest` in the app, and the same
file runs unchanged on the web, macOS, the iOS simulator and Android:

```bash
ui test                    # every test on every host available here
ui test web ios            # these hosts
ui test android tests/ui/login.uitest
ui test web --update       # record the screenshots again
```

With no host it runs on each one this machine has: the web needs emcc and Chrome
(`UI_CHROME`), macOS a Mac (the window needs a display), iOS a simulator, Android a device or a running emulator (`adb devices`); the
others are reported as skipped. Each file starts the app afresh (a new process, a new page on
the web) through `ui serve HOST`, which `ui test` starts when it is not running and stops
after, and takes the driver's clock, so a test never sleeps: what an op causes has rendered
when it answers, and time moves only with `clock`. `--verbose` prints each step and its
answer; `--keep` leaves `ui serve` running; `--perf FILE` writes what `perf` says after each
test (cold start, frame times, memory) into FILE, by host and test.

```
# The counter starts at zero and counts each press.
expect count text "Count: 0"
tap Increment
repeat 2 tap Increment
expect count text "Count: 3"
expect state Column[0].0 3
expect count frame 16 _ _ _ ±2
expect screenshot counted 0.5%
```

One step per line; a line starting with `#` is a comment, and blank lines are skipped. A
word with blanks is written in double quotes (`"Open sheet"`, with `\"` and `\\` inside).

| Step | Does |
|---|---|
| an op | one of `ui agent`'s ops as it takes them: `tap ID`, `type ID TEXT`, `scroll ID DY`, `back`, `clock +MS`, `clock settle`, `clock real`, `screenshot FILE`, `tree`, `layout`, `state`, `logs`, `perf`. The last argument is the rest of the line (`type title Milk bread`) |
| `repeat N STEP` | the step N times |
| `expect ID exists` | an element is in the tree |
| `expect ID missing` | no element is (an unmounted row, a closed sheet) |
| `expect ID text "T"` | the element's text is T |
| `expect ID prop NAME VALUE` | a prop the app set (`tree` leaves out defaults: `background "#ff0000ff"`, `radius 8`, `on 1`), or a member of the element (`label`, `placeholder`, `key`) |
| `expect ID frame X Y W H [±TOL] [in OTHER]` | the frame the host laid out, in points from the window's top left or OTHER's; `_` takes any value; each within TOL, 1 by default (`+-2` works too) |
| `expect state PATH VALUE` | a member of `state` (`modals`, `timers`, `fetches`, `frames`, `clock`, `now`; `routes` as `/ > /places/2`), or `SCOPE.N`, the Nth signal of a scope: SCOPE is its test_id, its component name or its path as `ui agent state` prints it |
| `expect screenshot NAME [T%]` | the window against `screenshots/NAME.HOST.png` next to the test; at most T% of the pixels (0.1% by default) may differ by more than 24 of 255 in a channel. A missing reference is recorded and passes (commit it); `--update` records them all again. Opt in: a test has none unless it says so |

IDs follow `ui agent`'s rule: a test_id, else an accessible name (a label, a text's or a
button's own text), or `#12`. VALUE is compared by its kind: a number by value, `true` and
`false`, a string as written, an array or object as JSON.

A failure names the file, the line and what was found, and the file stops there:

```
ui test: android
  FAIL counter (android)
    tests/ui/counter.uitest:5: expect count text "Count: 2"
      expected the text "Count: 2", found "Count: 1"
ui test: 2 passed, 1 failed
```

A screenshot that differs leaves the new one and a diff (the differing pixels red) in
`build/ui/test/`. A screenshot depends on the machine (fonts, the simulator's model, the
browser's version): record the references on the machine that checks them, as CI uploads
them.

`ui new` makes `tests/ui/app.uitest`. In this repository `tests/conformance`,
`examples/counter` and `tests/bench` have UI tests.

## The hosts

| Command | Checks |
|---|---|
| `python3 tools/build_web.py` then `node tests/web.mjs` | the wasm module against a recording DOM; the build fails when the test module (every example and the whole kit) passes 240 KiB |
| `python3 tools/check_ios.py` | a booted simulator (`DEVICE` picks one) |
| `python3 tools/check_android.py` | a booted arm64 device or emulator (`ANDROID_SERIAL`), SDK 35, NDK 28.2 |
| `python3 tools/check_native.py` | the native example on the simulator and the Android device: module events, a view's props and events |
| `python3 tools/check_desktop.py` | the WKWebView host's window running the web build (needs an awake, unlocked display) |
| `tests/agent/check.sh web\|macos\|ios\|android` | every `ui agent` op on `tests/agent`, through `ui serve` and the host's driver channel: targets, input, layout, state, logs, perf, screenshots, the clock (two screenshots a second apart are the same, `clock settle` finishes the animations), and a reload after an edit that keeps the count, the field and the timeout's result (with source positions, `tree`'s file and line too); web needs emcc and Chrome (`UI_CHROME`) |
| `ui test HOST` in `tests/conformance` | the conformance scene's UI tests (below) |
| `python3 tests/web/prerender.py [--measure N]` | the prerendered first frame in headless Chrome: content before the module runs (the module blocked), the elements adopted (none made again, a press counts), a different first commit replacing the markup, another route starting empty; with `--measure`, the first contentful paint with and without it |
| `python3 tests/web/updates.py` | asset updates in headless Chrome against a local update server (`tests/web/updates-app`): a bundle downloaded and used at the next load, a changed file and an added one served by the service worker, a file that does not match its hash and a changed signature refused |
| `tools/ci_host.sh web\|macos\|ios\|android` | what CI runs on a host: the agent check, the conformance tests and the speed budgets (on macOS also a universal, signed release; on the web the two Chrome tests above) |

They drive `tests/harness/app.esk`, which holds every scene a host can be asked for: the
counter (0), the TODO list (1), a probe (2), the conformance scene (3), the tasks app (4)
the kit gallery (5) and the native example (6), whose native code each host build compiles
in. A host is started on one with its own launch argument (a query
string on the web, an intent extra on Android, a launch argument on iOS).

## Conformance

`tests/harness/conformance.esk` is one scene every renderer draws. `tests/conformance` is the
scene as an app, and its UI tests (`tests/conformance/tests/ui/`) check it the same way on
every host: `layout.uitest` the frame of each labeled element within one point (an absolute
element and an aspect ratio too) and the props of the styled ones, `input.uitest` the handler
log after a timer, two taps, typing "abc", a press, a toggle and a modal opened and closed,
`feed.uitest` a feed of 1000 rows scrolled to row 50 with the first rows unmounted.

What the driver's ops cannot reach comes from each host's own run of the scene: the
accessibility role of every labeled element, what the host drew (the colors, radius, border,
opacity, transform and font of a styled box and a title), a slider moved, an alert answered,
the clipboard, a fetch, a pull to refresh, the modal covering the window, and only a window
of the feed's rows alive.

```bash
python3 tools/conformance.py           # every available host: ui test, then the host's run
python3 tools/conformance.py ios web   # only these
```

iOS, Android, the web and the macOS AppKit host pass (macOS has the UI tests; the host's own
run of the scene is not written yet, so `conformance.py macos` runs the UI tests only).

## Agents

`ui agent` drives a running dev build with the same ops on every host, and a test made of
them needs no sleeps: what an op causes has rendered when it answers, and time moves only
with `clock`. See [Agents](agents.md). `tests/agent/check.sh` passes on the web (headless
Chrome), macOS (the AppKit host, macOS 27), the iPhone simulator (iOS 27) and the Android
emulator (API 35). `ui test` runs
files of the same ops with expectations ([UI tests](#ui-tests)).

## Screenshots

`python3 tools/screenshots.py` takes the README's screenshots of `examples/tasks` on the
web, iOS, Android and macOS into `screenshots/`, with the PNG metadata removed;
`python3 tools/screenshots.py macos` takes only the hosts named. The macOS one is the AppKit
host's window content, 430 by 880 points, through `ui agent macos`.

## CI

`.github/workflows/ci.yml` runs `tools/ci.sh` on Linux and macOS with the released `eskiuc`
(and `tools/gen_api.py --check` on Linux), builds and packages the Windows host, and runs the
host checks, `tools/ci_host.sh HOST`, in four jobs:

| Job | Runner | Host | Takes |
|---|---|---|---|
| `host-web` | ubuntu-24.04 | headless Chrome (the runner's), emscripten from `setup-emsdk` (cached) | about 1.5 min |
| `host-ios` | macos-15 | the first available iPhone simulator, booted by the job; Yoga's objects cached | about 10 min (the first build, then cached) |
| `host-macos` | macos-15 | the AppKit host's window on the runner's display; Yoga's objects cached | about 1.5 min, a universal release of the conformance app included |
| `host-android` | ubuntu-24.04 with KVM | the emulator, API 35 x86_64 `google_apis` (`android-emulator-runner`, its snapshot cached), NDK 28.2 | about 3.5 min |

Each runs `tests/agent/check.sh`, `ui test` on `tests/conformance`, and the speed budgets of
its host (below), which run the UI tests of `examples/counter` and `tests/bench`. On Android
the tool builds for the device's ABI, x86_64 on the emulator (`UI_ANDROID_ABI` picks one); a
build on Linux takes the NDK's Linux toolchain. A failed job uploads the logs of `ui serve`
and `ui agent` (and the macOS app's `app.log`), and the screenshots of the failed tests. The local-only checks
(`check_ios.py`, `check_android.py`, `check_native.py`, `check_desktop.py`, the host runs of
`conformance.py`) stay for what CI does not cover: the native example's module and view, a
physical device, the WKWebView window.

## Speed budgets

Each host job measures its host and fails when a number is worse than its budget, printing
the number: `python3 tools/budgets.py HOST` (`model` for the recording renderer, in the web
job). The budgets are in `tools/budgets.toml`, each a baseline a CI runner measured and a
tolerance: generous for timings, since shared runners are noisy (+50%, +100% on the
emulator and the few-millisecond ones), narrow for sizes (+2%). The job summary has the table, and
`build/budgets-HOST.json` the numbers.

| Budget | Measured as |
|---|---|
| `HOST.cold_start_ms` | `perf`'s cold start after `ui test` starts `examples/counter` afresh: the process's start (the page's navigation on the web) to the first commit applied, the core's clock against the start the host reports |
| `HOST.edit_ms` | under `ui serve`, a text in `examples/counter/src/counter.esk` changed until `ui agent HOST tree` shows it: the rebuild and the reload; the median of three |
| `HOST.frame_p95_ms` | `perf`'s p95 frame time (the core's render and the host's apply) after `tests/bench`'s 100 frames of 300 rows |
| `web.wasm_bytes.counter`, `web.wasm_bytes.kit_gallery` | the release `app.wasm` of `examples/counter` and `examples/kit-gallery` |
| `web.first_paint_ms` | the first contentful paint of `examples/counter`'s release page in headless Chrome on DevTools' "Slow 4G", the median of five loads: the prerendered first frame ([hosts](hosts.md#the-first-frame)) |
| `model.*` | `tests/benchmark.esk` at `-O2`: a frame over 1000 rows, mounting and reversing 10,000 |

The timings are of `ui serve`'s dev builds (`-O0`), as an app is developed. After a change
that moves a number on purpose, take the new number from the job summary into
`tools/budgets.toml` (`python3 tools/budgets.py HOST --update` writes what a run measured).

The baselines, measured on the runners on 2026-11-09 (macOS on the macos-15 runner, cold start the
AppKit host's):

| Budget | Web (Chrome, Linux) | iOS simulator | Android emulator | macOS (AppKit) |
|---|---|---|---|---|
| cold start | 33.5 ms | 714 ms | 615 ms | 199 ms |
| edit to change | 1.53 s | 1.61 s | 1.04 s | 1.01 s |
| frame p95 (300 rows) | 8.0 ms | 20.6 ms | 55.3 ms | 18.0 ms |
| `app.wasm`, counter / kit gallery | 107,015 / 172,356 bytes | | | |

The recording renderer on the Linux runner: a frame over 1000 rows 1.9 ms, mounting 10,000
rows 38 ms, reversing them 27.6 ms.

Edit to change on the web, `examples/counter` on an arm64 Mac (the median of three), before
and after `ui serve` looked at the sources every 50 ms instead of 300 ms with a 50 ms settle
instead of 150 ms, answered the page's wait within 10 ms, `ui agent` polled every 50 ms, and
the iOS host every 50 ms:

| Compiler | Build (`ui build web`, dev) | Before | After |
|---|---|---|---|
| Eskiu 0.14.1 | 0.98 s | 1.45 s | 1.12 s |
| Eskiu develop (2026-11-09, faster front end) | 0.73 s | 1.19 s | 0.90 s |

What is left is the compiler (0.55 s and 0.30 s to an object) and emscripten's link (about
0.45 s), then the page's reload.

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
