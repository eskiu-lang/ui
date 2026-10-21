<p align="center">
  <img src="https://raw.githubusercontent.com/eskiu-lang/eskiu/main/assets/logo.png" alt="Eskiu, a kitten with a ball of yarn" width="140">
</p>

<h2 align="center">ui</h2>
<p align="center">One UI in Eskiu for iOS, Android, the web and the desktop.</p>
<p align="center"><sub>Part of the <a href="https://github.com/eskiu-lang">Eskiu</a> project</sub></p>
<p align="center">
  <a href="https://eskiu-lang.org">eskiu-lang.org</a> &nbsp;&middot;&nbsp;
  <a href="#modules">Modules</a> &nbsp;&middot;&nbsp;
  <a href="examples/components.esk">Example</a> &nbsp;&middot;&nbsp;
  <a href="https://github.com/eskiu-lang/eskiu">The language</a>
</p>

<p align="center">
  <a href="https://github.com/eskiu-lang/ui/actions/workflows/ci.yml"><img src="https://github.com/eskiu-lang/ui/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/version-0.0.0-6448d4" alt="Version 0.0.0">
  <img src="https://img.shields.io/badge/status-phase%200%20prototype-orange" alt="Status: phase 0 prototype">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT"></a>
</p>

---

`ui` is a reactive UI framework in Eskiu. A component is an ordinary function, the view is
built by calls (`Column`, `Row`, `Text`, `Button`), and state lives in signals. The same
component code draws UIKit views on iOS and `android.view` views on Android, both laid out
by Yoga, and DOM elements on the web through WebAssembly; the desktop runs the web renderer
in a webview.

This is the **phase 0 prototype** of the UI plan (`docs/dev/ui-plan.md` in the
[compiler repository](https://github.com/eskiu-lang/eskiu)): it settles the runtime model
(scheduler, ownership, identity, renderer protocol) against executable tests before the core
is written. Views use explicit lambdas, the form the planned trailing blocks will produce;
the compiler is unchanged.

- **Fine-grained updates.** Each element's block records the signals it reads; when one
  changes, only that block runs again and its children are diffed.
- **Keyed identity.** `key(id)` keeps a row's node and state across reorders. Key zero is a
  key; an omitted key is not.
- **Commits.** A frame's mutations reach the renderer as one commit. Old handlers, nodes and
  strings are freed only after the renderer completes it, synchronously or later, and events
  that arrive meanwhile are queued.
- **Checked rules.** Hooks are checked by count, kind and concrete type. Render loops,
  duplicate keys and memo cycles stop the program with a message.

```eskiu
import <string>;
import <ui>;

void Counter() {
    Column(Style{padding: 16}, void() {
        *Signal<int> count = use_signal<int>(0);
        Text("Eskiu counter");
        String label;
        defer String_free(&label);
        String_format(&label, "Count: %d", count.get());
        Text(label.data);
        Button("Increment", void() { count.set(count.get() + 1); });
    });
}
```

With trailing blocks, planned for Eskiu 0.13, the same view reads:

```eskiu
void Counter() {
    Column(Style{padding: 16}) {
        *Signal<int> count = use_signal<int>(0);
        Text("Eskiu counter");
        String label;
        defer String_free(&label);
        String_format(&label, "Count: %d", count.get());
        Text(label.data);
        Button("Increment", void() { count.set(count.get() + 1); });
    }
}
```

`examples/components.esk` has the counter and a TODO list with keyed rows, per-row state and
a model-owned signal. `examples/app.esk` is the entry point the web, iOS and Android hosts
share.

## Modules

### `<ui>`

| Name | What it does |
|---|---|
| `Column(Style, fn()->void)`, `Row(...)` | A container; its block emits the children. |
| `Scope(Key, fn()->void)` | A container that gives its block a scope (state and identity). |
| `Text(string)` | A text node. The text is copied. Format it with `<string>`'s `String_format`, which `-Wformat` checks. |
| `Button(string, fn()->void)` | A button and its handler. `Button_owned` adds a cleanup, run when the handler is replaced or unmounted. |
| `Style{key, gap, padding}`, `key(int64)` | Layout props; `key` sets identity among siblings. |
| `use_signal<T>(T)` | State owned by the current scope, by call order. |
| `signal_new<T>(T)`, `Signal_free` | State owned by the app's model: it outlives mounts and works with no runtime. |
| `Signal_get`, `Signal_set` | Read (and subscribe), write. A write during render applies after the render phase. |
| `use_memo<T>(fn()->T)` | A derived value, computed lazily; readers wake only when its value changes. |
| `use_effect(fn()->void)`, `on_cleanup(fn()->void)` | Work after each commit of its scope, parents first; the cleanup runs before the next run and at unmount. |
| `ui_init`, `ui_mount`, `ui_frame`, `ui_complete`, `ui_event`, `ui_needs_frame`, `ui_shutdown` | The host's side: create, render a frame, report a commit complete, deliver events. |

Values in `use_signal<T>` need built-in equality (scalars and pointers); pointers stored in
it are borrowed. A throw out of a block, handler or effect is not caught: the program stops
through Eskiu's uncaught-exception reporter. A caller that catches it anyway finds the next
`ui_frame` or `ui_event` stopping with "runtime left mid-phase".

### `<ui/owned_string>`

`use_string(string)` holds an immutable, reference-counted `UiString`. `get()` borrows until
the next `set()`; `snapshot()` retains the value for a handler that outlives the render, and
the handler's cleanup releases it.

## Platforms

| Platform | Renderer | Checked |
|---|---|---|
| Web | `web/renderer.mjs` over the DOM, wasm through emscripten | Node with a recording DOM, Chromium |
| iOS | UIKit + Yoga, Objective-C++ host (`ios/host.mm`) | iPhone simulator, iOS 27 |
| Android | `android.view` + Yoga, Java activity and JNI shim (`android/`) | arm64 emulator, API 35 |
| macOS | the web renderer in WKWebView (`desktop/host.m`) | macOS 27 |
| Windows, Linux | not yet | |
| Physical phones | not yet | |

The native hosts are thin platform shims with a C API toward the Eskiu core; components and
state are Eskiu. Yoga 3.2.1 is downloaded for the iOS build, not vendored. The Android host
draws behind the status bar for now; safe insets come with the renderer phase.

## Measurements

Arm64 Mac (Mac16,8), Eskiu 0.12.0, LLVM 22.1.8:

| Experiment | Result |
|---|---|
| 10,000 TODO mounts and unmounts, native `-O2` | about 40 ms |
| 10,000 renders replacing every handler, native `-O2` | about 12 ms |
| Recording renderer, 10 / 100 / 1,000 rows | p95 0.009 / 0.069 / 2.1 ms per frame |
| wasm module (counter and TODO) | 48,769 bytes; the build fails above 128 KiB |
| Chromium, counter / TODO, 120 frames | render and forced layout p95 1.1 / 8.3 ms |
| iPhone simulator, 100 taps | batched into one frame of 0.13 ms |
| Android emulator, 100 taps | batched into one frame of 1.4 ms |

These are first baselines, not presentation-time or physical-device numbers;
`build/*-results.json` holds the latest machine-readable results.

![TODO on the web after reordering, with editing state kept](screenshots/todo-web.jpg)

| iOS | Android | macOS |
|---|---|---|
| ![Counter on iOS with UIKit and Yoga](screenshots/counter-ios.png) | ![Counter on Android with android.view and Yoga](screenshots/counter-android.png) | ![TODO in the macOS WKWebView host](screenshots/todo-macos.png) |

## Contributing

The tools find the compiler through `ESKIUC` (`ESKIUC_SELF` for the self-hosted one), an
`eskiu` checkout next to this repository (`ESKIU_ROOT` to point elsewhere), or the `PATH`.

```bash
tools/ci.sh                        # model, rejection and type-tag tests on a released eskiuc
python3 tools/check.py             # + allocation counting, ASan, -O0/-O2 and the self-host
python3 tools/build_web.py         # wasm and DOM renderer into build/web (emscripten)
node tests/web.mjs                 # the wasm module against a recording DOM
python3 -m http.server 8765 --directory build/web   # ?example=counter or ?example=todo

git clone --depth 1 --branch v3.2.1 https://github.com/react/yoga.git build/yoga
python3 tools/check_ios.py         # a booted simulator (DEVICE to pick one)
python3 tools/check_android.py     # SDK 35, NDK 28.2, a booted arm64 device (ANDROID_SERIAL)
python3 tools/check_desktop.py     # a WKWebView window running the web build
```

`tools/check.py` counts every allocation, closure environments included, by pointing the
test IR's allocator calls at `tests/heap.c`; each case and 10,000 mount and unmount cycles
must end at zero. The same tests run under AddressSanitizer, with a negative control that
must be reported. `tools/ci.sh` checks the behavior without the counting, which needs a clang
that reads the compiler's IR.

## License

MIT. See [LICENSE](LICENSE).
