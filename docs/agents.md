# Agents

`ui agent` lets a program (a coding agent, a test script) build, drive, see and test an app
on the web, the iOS simulator and Android with one small set of operations. It talks to the
app's dev build, the one `ui serve` makes, through a driver the core has in those builds
only.

```bash
ui agent web tree "tap add" "type title Milk" state "screenshot out.png"
ui agent ios --json tap add clock settle layout
ui agent android --stop
```

## Running the app

`ui agent HOST` attaches to `ui serve HOST` for the project it runs in (it looks for
`eskiu.toml` from the current directory up), and starts it in the background when it is not
running, with no browser or simulator window. Its output goes to
`build/ui/agent/serve-HOST.log`.

- **web.** `ui agent` opens the app in a headless Chrome of its own (`UI_CHROME` names one;
  otherwise Chrome for Testing, Chrome or Chromium in `/Applications`, or `google-chrome`,
  `chromium` or `chrome-headless-shell` on the `PATH`), 390 by 844 points (`UI_AGENT_WINDOW=WxH`
  for another size). That page takes the requests before any other page of the app. Without a
  Chrome it drives whatever page of the app is open, and `screenshot` is not available.
- **ios.** The simulator that is booted, or the first iPhone, booted for it.
- **android.** The device or emulator `adb` sees (`ANDROID_SERIAL` picks one).

`ui agent HOST --stop` stops what it started: the browser, `ui serve HOST` and the app.

Before the first op `ui agent` checks that the running build was made from the sources as
they are now. While `ui serve` is building it waits; when the last build of these sources
failed it stops with the compiler's errors:

```
ui agent: refusing to drive a build older than the sources: the last build failed:
error: src/main.esk:52:12: Expected declaration, got EOF
Fix it and save; ui serve web builds again (ui agent web ... retries once it built).
```

## Targets

`tap`, `type` and `scroll` take an element. The rule:

1. An element whose `test_id` is the name: `Style{test_id: "add"}`.
2. Else one whose accessible name is: its `label`, or for a `Text` or a `Button` without a
   label, its own text.
3. `#12` is the element with id 12, as `tree` and `layout` show them.

Two or more matches is an error naming them; give one a `test_id`. A `test_id` is only for
tests and the driver: it is not the accessibility label, a screen reader never hears it, and
a release build ignores it.

```eskiu
ButtonWith(Style{test_id: "add"}, "Add") { count.set(count.get() + 1); }
TextInput(Input{value: title.get(), style: Style{test_id: "title"}}, void(string text) { title.set(text); });
```

## Ops

Each argument is one op. An op with arguments is quoted (`"tap add"`), or its arguments
follow it (`tap add`); the text of `type` is the rest of its argument (`"type title Milk
bread"`). The ops run in order, and the first that fails stops `ui agent` with exit code 1.
`--json` prints each op's answer as one JSON object per line instead of text.

| Op | Answers |
|---|---|
| `tree` | the elements: kind, id, test_id, label, text, key, component name, props that are not their default |
| `layout` | each element's frame in points from the window's top left, and whether it shows |
| `screenshot FILE` | the window's pixels as a PNG |
| `tap ID` | a press: a button or pressable is clicked, a switch toggled, a field focused, a modal asked to close; a text inside a pressable presses the pressable |
| `type ID TEXT` | the field's text becomes TEXT, as the platform's edit (it replaces what was there) |
| `scroll ID DY` | the scroll at or around ID moves DY points down (negative: up) |
| `back` | the platform's back: the newest modal closes, else a `use_back` handler or the router goes back |
| `state` | the signals of each scope that has some, the router's routes, open modals, timers, display frames, fetches, the clock |
| `logs` | what the app printed and the errors and warnings the core reported |
| `clock +MS` | moves the driver's clock MS milliseconds, running each timer and frame on the way |
| `clock settle` | runs the clock until nothing is pending (at most 60 s of it) |
| `clock real` | gives the clock back to the platform |
| `perf` | frame times, commit sizes, live nodes and the app's memory |

### tree

```
$ ui agent web tree
Column #2 {padding: 16, grow: 1}
  Text #3 [count] "Count: 0"
  Button #4 [add] "Add"
  TextInput #5 [title] "" placeholder="Title"
```

`[name]` is the test_id. With `--json` the answer is `{"ok": true, "op": "tree", "root":
{...}}`, each element `{"id", "kind", "test_id", "label", "text", "key", "name", "props",
"children"}` (members it does not have are left out).

### layout

```
$ ui agent ios layout
window 402x874 points, scale 300%
Button #4 [add] "Add"  x=16 y=104 w=370 h=34
```

The frames come from the host: the views' frames on iOS and Android, the elements' boxes on
the web. `--json`: `{"frames": [{"id", "kind", ..., "x", "y", "width", "height",
"visible"}], "window": {"width", "height", "scale"}}`.

### screenshot

```
$ ui agent web "screenshot shot.png"
screenshot: shot.png (390x844 pixels)
```

The browser's page through its DevTools, `xcrun simctl io booted screenshot` on iOS,
`adb exec-out screencap -p` on Android.

### tap, type, scroll

```
$ ui agent android tap add "type title Milk bread" "scroll list 300"
tapped add (Button #4)
typed into title (TextInput #5)
scrolled list (Scroll #13) to 300
```

The input is the platform's: `click()` and an `input` event on the web,
`sendActionsForControlEvents` and `setContentOffset` on iOS, `performClick`, `setText` and
`scrollBy` on Android. Each op answers once what it caused has rendered.

### back

```
$ ui agent ios back
back: handled
```

### state

```
$ ui agent web state
Column[0] (#2): [2,"Milk bread",false]
routes: / > /places/2
modals 0, timers 1, display frames off, fetches 0, clock virtual at 1000 ms
```

The values are the scope's `use_signal` and `use_string` values in the order the scope
declares them (ints, floats, bools and strings; another type shows as `"(a value of another
type)"`).

### logs

```
$ ui agent web logs
log: loaded 3 places
ui error: error in a handler: int 5
```

### clock

```
$ ui agent web "tap later" "clock +500" "clock settle"
tapped later (Button #7)
clock virtual at 500 ms (+500, 0 fired, busy: a timeout)
clock virtual at 1000 ms (+500, 1 fired, idle)
```

See [the clock](#the-clock).

### perf

```
$ ui agent ios perf
frames 40 (last 40 measured): p50 0.20 ms, p95 2.20 ms, max 4.90 ms
commits: last 40 B, max 4283 B, mean 153 B; 74 nodes; memory 16.1 MB
```

A frame's time is the core's render and the host's apply of its commit. The memory is the
wasm heap on the web, the process's footprint on iOS, the native and Java heaps on Android.

## The clock

The first `ui agent` op takes the app's clock (`--real-clock` leaves it to the platform).
From then on nothing moves between two ops:

- The core's timers (`use_interval`, `use_timeout`), `use_frame` ticks and what is built on
  them (`use_animated`, `use_repeat`, `use_sequence`) run on the driver's clock, which only
  `clock +MS` and `clock settle` move. A `clock +MS` runs each timer and display frame due on
  the way at its own time (frames every 1/60 s), with the renders they cause, in order.
- The platform's animations follow it: on the web each CSS transition and animation is
  paused when it starts and moved to the driver's time (`Element.getAnimations()`); on iOS the
  window's layer has `speed` 0 and its `timeOffset` is the driver's time; on Android the
  animators' duration scale is 0, so the host's transitions and slides end in the frame that
  starts them. A text field's blinking caret is hidden on the web and Android (iOS stops it
  with the layer).
- `clock settle` runs until no timeout, display frame, render or transition is pending.
  Intervals repeat forever and do not count. After 60 s of the clock it stops and says what is
  still busy (`busy: use_frame` for an animation that never ends).

The clock stays taken across `ui agent` calls until the app reloads or `clock real` gives it
back. A reload (a change under `ui serve`) starts the app on the platform's clock again; the
next op takes it.

## Testing an app

A test is a shell script of ops and checks on their `--json` answers. `tests/agent/check.sh`
in this repository is one, run on every host:

```bash
ui agent web --stop                       # a fresh start
ui agent web --json "tap add" | grep -q '"ok":true'
ui agent web --json tree | grep -q '"text":"Count: 1"'
ui agent web --json "tap later" "clock +1000" tree | grep -q '"text":"Done"'
ui agent web --stop
```

No sleeps: what a tap causes has rendered when it answers, and time moves only with
`clock`.

## The protocol

The driver is part of the core in a dev or test build (the ui tool defines `UI_TEST` in the
entry file of `ui serve`'s builds, or with `UI_TEST=1`; a release build has none of it).
A request is one JSON object, `{"op": "tap", "target": "add"}`, `{"op": "type", "target":
"title", "text": "Milk"}`, `{"op": "scroll", "target": "list", "dy": 300}`, `{"op": "clock",
"ms": 500}` (or `"settle": true`, `"real": true`), `{"op": "hello", "clock": "virtual"}`; the
answer is one JSON object with `"ok"` and `"op"`, and `"error"` when it failed.

The core answers `tree`, `state`, `back` and `hello` itself. For the others it answers with a
directive for the host, `{"host": "tap", "node": 4, "kind": 4}`; the host does its part and
passes its result to `ui_driver_done`, which answers the request. A host carries `tap`,
`type`, `scroll` (its result the new offset), `layout` (a line per view: `ID X Y WIDTH HEIGHT
VISIBLE`), `logs` (its lines), `perf` (the memory in bytes) and `hello`/`clock` (`now`,
`virtual`, `settle`: it moves its animations to the driver's time); a result starting with
`error: ` fails the request.

| Host | Transport |
|---|---|
| web | `ui agent` posts the request to the dev server (`POST /__ui/agent`); the page asks `/__ui/agent/next` for it and posts the answer to `/__ui/agent/reply` (`hosts/web/driver.mjs`) |
| iOS simulator | TCP on 127.0.0.1:8766 (`UI_AGENT_PORT`) in the app, one request per line (`hosts/driver.h`) |
| Android | the abstract socket `eskiu-ui-agent` in the app, reached by `adb forward tcp:8767 localabstract:eskiu-ui-agent` (`UI_AGENT_PORT` for another local port) |

Compiled out: `tools/ci.sh` checks that a release object has no symbol of the driver. The
`tests/agent` app's release wasm is 117,639 bytes with no trace of it; built with `UI_TEST`
at the same `-O2` it is 183,185 (the driver and the JSON reader it uses).

`hello` also answers the hash of the sources the build was made from (`UI_SOURCE`), which
`ui agent` compares with the sources as they are.
