# Changelog

All notable changes to ui are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed

- `ui agent web` keeps the app's window on every route: a `Navigator` push rewrote the page's
  URL without its `#ui-agent:WxH`, so the agent lost the page (screenshots failed) and the app
  laid out at the browser's size. A modal and the splash, fixed to the window, now cover the
  app's window too, not the browser's.
- A failed runtime check on the web (a duplicate key, a render loop, a hook outside a render)
  reaches `ui agent` and `ui test` with its message and where, instead of only
  "Aborted(native code called abort())": the page prints through the console of the moment,
  which the driver keeps.
- `ui run android` and `ui agent android` replace an installed app signed with another key
  (another machine's debug key) by uninstalling it first, instead of failing.

## [0.6.2] - 2026-11-18

### Fixed

- `ui agent web` gives the app a 390 by 844 point window (or `UI_AGENT_WINDOW`) on any Chrome:
  the page is opened as `#ui-agent:WxH` and takes that size, and screenshots take that region.
  Headless Chrome on Linux kept a window at least 500 wide, so the app laid out at 500 by 701.
- `ui agent --json` reads `--json` (it printed text before), and when the app does not build
  its errors come first as JSON diagnostics, then `{"ok": false, "error": ...}`.
- A failed build of the current sources says "the build of these sources failed" instead of
  "refusing to drive a build older than the sources", with the line break it lacked.

### Added

- `ui test --json` writes a line per step that passed, `{"ok": true, "host", "line", "step"}`, so a
  test that passes still shows its steps.

## [0.6.1]

### Added

- `textf("Count: %d", n)`: printf-style text for the current frame, freed when the frame is
  done, so a label with a number needs no `String` and no `defer` (`Text(textf(...))`). An
  element copies its text, so this is safe wherever an element takes one. The `ui new`
  template uses it. Eskiu 0.16.1's `-Wformat` checks its format string.
- `ui agent perf` on the web: `memory_reserved_bytes`, the size of the wasm memory.
- `ListSignal<T>` (`use_list<T>()` in a scope, `list_signal_new<T>()` in a model): a list whose
  readers render again when it changes (`push`, `insert`, `remove`, `set`, `swap`, `clear`,
  and `changed()` after an edit in place), so a model needs no revision counter.
- `frame_string()`: an empty `String` for the frame, for an API that writes into one
  (`time_format`), freed with the frame as `textf` is.
- `ui_own_fn(callback)`: a component frees a closure it was given, of any type, in place of
  `ui_own` with the `ui_release_*` of that type. The kit uses it.
- `TextInput_bind(Input, *StringState)`, a field whose text is the state, and
  `StringState_empty`.
- `Box(Style)`, a box with no children, and `Spacer(grow)`.
- `NoColor` for an unset color, and the docs show a palette as `0xRRGGBBAA` constants.
- `route_int(pattern, route, name, fallback)`: one `:name` part of a route as a number.
- The examples use all of these.

### Changed

- The package moved to the eskiu-packages organization; the old URL redirects. `ui new`
  makes apps on ui 0.6.1.

### Fixed

- `ui agent perf` on the web reported the size of the wasm memory as `memory_bytes`
  (16 MB on every page, since it only grows); it is now what the program's allocator holds.
- An element found by its accessible name includes what is inside it, as a screen reader
  reads it: `tap Agree` on a kit `Checkbox("Agree", ...)` (a labeled pressable around its
  label's text) is one match, not two, in `ui agent` and in `.uitest` files.

## [0.6.0]

### Changed

- A render that reaches another hook at a slot (a branch that shows another component), or
  calls fewer or more hooks than the last one, no longer stops the program: the hooks from
  that slot on unmount and the new ones mount, and the ones before keep their state
  (`ui_discard_slots_from`; `docs/state.md`, "The hooks rule").
- Android: `UiActivity` is split by topic into `UiCommands`, `UiDriver`, `UiPanTracker`,
  `UiProps`, `UiSelfTest`, `UiTree` and `UiYogaGroup` (`hosts/android/`). The ui tool
  compiles every Java file there; a build of its own that compiled `UiActivity.java` alone
  has to compile them all.
- The larger hosts, the driver, the agent, `ui test` and `<ui/webapp>` are split into
  parts, and the larger modules keep theirs in private `src/_module/` directories. No API
  change.

## [0.5.0]

### Added

- `<ui/webapp>` (`src/webapp.esk`): an Eskiu program as the backend of a web front end, such
  as a React bundle, in the desktop's webview, as Tauri does for Rust. The page runs the
  program's commands with JSON arguments and gets JSON, strings, bytes or errors back, in the
  call or later from any thread (`WebappCall`, `WebappReply`); events go both ways
  (`webapp_emit`, `webapp_listen`); URL schemes of the program serve files and bytes with
  ranges and CORS (`webapp_protocol`, `webapp_protocol_dir`); windows have a title, a size,
  a smallest size, full screen and a close the program or the page can refuse; file,
  folder, save, message and confirm dialogs are the system's. See `docs/webapp.md`.
- The web app hosts: WKWebView on macOS (`hosts/macos/webapp.m`, with the standard menu
  bar), WebView2 on Windows (`hosts/windows/webapp.cpp`, schemes as `https://NAME.localhost/`)
  and WebKitGTK on Linux (`hosts/linux/webapp.c`). A window's page stays on the app's origin;
  a link elsewhere opens in the browser.
- `@eskiu/webapp` (`js/webapp`): the page's module, in the shape of `@tauri-apps/api`
  (`invoke`, `listen`, `once`, `emit`, `convertFileSrc`, the dialogs of
  `@tauri-apps/plugin-dialog`, `getCurrentWindow`), with TypeScript types. The program also
  serves it as `/@eskiu/webapp.js`.
- `ui build` and `ui run` for `macos`, `linux` and `windows` build a project with `[webapp]
  frontend` in `ui.toml` as a web app (`before_build` runs the bundler first);
  `ui run TARGET --dev` loads `[webapp] dev_url`. A Windows web app builds from macOS and
  Linux with llvm-mingw.
- `examples/webapp`: each feature behind a button, and a self-test (`WEBAPP_SELFTEST=1`) in a
  real window, run by `tools/ci_host.sh macos` and `linux`; `tests/webapp.esk` and
  `tests/webapp.mjs` in `tools/ci.sh`.
