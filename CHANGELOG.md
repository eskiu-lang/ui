# Changelog

All notable changes to ui are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Accessibility in `Style`: `role` (a `Role`: button, link, header, image, checkbox, radio, radio
  group, switch, tab, tab list, slider, progress bar, menu, menu item, list, list item, alert,
  search), `checked` (a `CheckState`), `selected`, `expanded` (a `Disclosure`), `value` and
  `hint` as words, and a range (`value_min`, `value_max`, `value_now`). All six hosts map them
  to their platform's API; docs/hosts.md lists what a platform cannot say.
- `Style.disabled`: the element and everything inside it are shown and announced as
  unavailable, its controls take no input, and its handlers are not called.
- Text input options in `Input`: `secure`, `keyboard` (a `Keyboard`), `return_key` (a
  `ReturnKey`), `multiline`, `capitalize` (a `Capitalize`), `autocorrect` and `autofill` (an
  `Autofill`), and `ui_on_submit` for the text when the user presses return.
- Layout in `Style`: `padding_top`, `padding_right`, `padding_bottom`, `padding_left`,
  `margin` and one per side, `text_align` (a `TextAlign`), `max_lines` (cut with an ellipsis)
  and `shrink` (flex-shrink).
- `ui_on_image`: an `Image`'s load or failure. An image that cannot be loaded shows nothing
  but its background (the web showed the browser's broken icon and its alt text).
- Text follows the system's text size: Dynamic Type on iOS, sp on Android, GNOME's text
  scaling factor on Linux and the text size setting on Windows, laid out again when it changes.
- The kit's words in `kit_text` (`KitText`): English by default, `kit_text_spanish()`,
  `kit_text_for(ui_locale())`, or fields of your own: month and weekday names, the first day
  of the week, how a date is written, and what the components say to a screen reader.
- `ui agent tree` shows the accessibility, input and layout props, the value and the hint.
- docs/stability.md: what is public, versions and deprecations, and what Preview means.

### Changed

- Internal names start with `_` (161 of them, documented as internal before: the runtime's
  helpers, the driver's, reload's, updates', ed25519's internals). An app or package that
  called one calls the public API instead; the runtime's types (`Ui`, `Node`, `Source`,
  `Edge`, `Pending`) and the host ABI keep their names, outside the stability promise.
- The kit says what it is: a `Checkbox` is a checkbox with its state, a `RadioGroup` radios in
  a group, `SegmentedControl` and `TabBar` tabs with the selected one, `Collapsible` whether it
  is open, `ProgressBar` its percent, `NavBar`'s title a header, `Calendar` and `DatePicker`
  their days as whole dates and their arrows by name, `ColorPicker` its colors by name.
  Targets are at least 44 points.
- `kit_toast` queues: a toast shown while another is up waits its turn, and each is announced
  to a screen reader (before, only the last of several showed).
- `SwipeToDelete` has a delete button with a screen reader on, and on a long press.
- The themes meet WCAG contrast: `muted` text is AA on the page and on surfaces, and `control`
  (the border of what takes input, now used by `TextField` and `DatePicker`) 3:1.
- `kit_light()` and `kit_dark()` are `must_use`: calling one without assigning it was a
  silent no-op.
- `kit_month_name(month, &out)` writes the name in the kit's words (it returned English).
- An open `Modal` takes the keyboard on the web: what is under it is inert, focus moves into it
  and back when it closes.

### Fixed

- An app that fails as it starts (a `RefreshControl` before no `Scroll`, a duplicate key on
  the first frame) tells `ui agent` and `ui test` why at once on the web; before they waited
  80 seconds with no message.
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
