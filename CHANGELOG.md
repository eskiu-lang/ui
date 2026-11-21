# Changelog

All notable changes to ui are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.7.0] - 2026-11-21

Needs Eskiu 0.16.2 (it compiles with 0.16.1, but 0.16.2 passes `Style` by pointer, which
keeps a web app's module about a sixth smaller).

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
- `ui_text_color`: the color of a Text or a TextInput whose Style sets none (-1, the default,
  is the platform's). `kit_follow_system` sets it to the theme's ink, so plain text stays
  readable in the kit's dark theme; after `kit = kit_dark();` set it yourself.
- `ui agent --json` passes on the compiler's warnings for the build it drives, as JSON lines
  before the answers (`ui serve` keeps them in its status). Eskiu 0.16.2 reports warnings in
  the app's own files, which the build imports (a `textf` whose arguments do not match its
  format, for one).
- `ui agent` and `ui test`: `submit ID` presses return in a TextInput, and an op's word in
  double quotes keeps its spaces (`tap "I agree"`).
- `KitText.weekday_names`: a Calendar's weekday initials carry the day's name for screen
  readers.
- `Style` fields: `line_height`, `italic`, `z_index` (stacking among siblings), `clip` (content
  outside the box, radius included, is hidden) and `layout_ms` (a frame the layout changes
  slides there over that time, with `easing`; skipped under reduced motion).
- `ModalWith(ModalOptions, visible, on_request_close, block)`: a sheet from the bottom edge
  (`PresentSheet`), a centered dialog (`PresentCenter`) or the whole screen (`PresentFull`),
  with a Style for the panel and an animated or immediate appearance. The core lays the panel
  out, so it looks the same on every host.
- `KeepAlive(Key, active, block)`: content kept mounted and hidden while inactive, so a tab
  keeps its state.
- `kit_set(theme)` changes the kit's theme at any time; every kit component follows it.
- `ui_style_next(Style)`: a test_id, key, label, margins, size, grow, shrink or z_index for the
  next element made, so a kit component (which takes no Style) gets them too.
- `ui_accent_color`: the accent of a Switch or a Slider with no color (the kit sets it to its
  primary color); a Switch's or Slider's `color` is its accent on every host.
- `ui_right_click_long_press` (true): whether a right click on the web is a long press.
- `ui_set_words(UiWords)` hands the hosts the words a screen reader says that the platform
  does not translate (a mixed checkbox, expanded and collapsed, Android's names for a menu, an
  alert, a link); `KitText.access` holds them in English or Spanish and `kit_use_text` sets the
  kit's words and these at once.
- `ui agent` and `ui test`: `drag ID DX DY [MS]`, `long_press ID` and `hover ID in|out`,
  carried out by the core so they behave the same on every host; `state` lists every
  Navigator's stack (`navigators`).
- Right-to-left languages lay out right to left on the web (rows, start and end mirror).
- Right-to-left languages lay out right to left on iOS and Android too: Yoga runs right to
  left, so rows, start and end, grids, the screens' slides and iOS's back swipe mirror, and
  the views take the language's direction. `left`, `right` and the sides' padding and margins
  stay physical, as in CSS.
- iOS and Android: `line_height`, `italic`, `z_index`, `clip`, `layout_ms` (frames the layout
  changes move there, with the transition's easing; at once under reduce motion), a
  `ModalWith`'s appearing (a sheet slides up, a dialog or a full screen modal fades in), a
  Switch's or Slider's accent, and the layout props that were only on the web: the sides'
  padding (a `Text`'s too), margins, `text_align`, `max_lines`, `shrink` and `ui_on_image`.
- A runtime error in a build with source positions ends with the app's line (`at
  src/main.esk:12:5`), and an element a package made points at the app's call that led to it
  in the tree (`file`, `line`), with the package's own line as `made_at`.
- The macOS, Linux and Windows hosts take the layout and text props (padding per side,
  margins, `text_align`, `max_lines`, `shrink`, padding on a Text), `line_height`, `italic`,
  `z_index`, `clip`, `layout_ms`, a ModalWith's presentation and appearance, `ui_on_image`,
  and a Switch's or Slider's accent.
- Right to left on macOS, Linux and Windows: with a right-to-left system language the layout
  mirrors (rows, start and end, grids) and the text starts from the right.
- Pull to refresh on the desktops: the scroll pulled past its top (a trackpad's or the wheel's
  elastic scroll on macOS, GTK's overshoot on Linux, the wheel turned up at the top on
  Windows), or View > Refresh, with a spinner at the top of the scroll.
- Linux: a real backdrop blur (what is behind the box drawn again and blurred), and a share
  dialog that copies or sends an email through the desktop portal.
- Windows: shadows, scale and rotation (a transformed picture of the window, which still
  takes clicks), a backdrop blur, announcements through UI Automation's notification event, and
  UI Automation's heading level, switch type and search landmark.
- The desktop hosts' own conformance runs (`python3 tools/conformance.py --host-only HOST`) and
  asset updates on the native desktop hosts (`tests/desktop/updates.py`), in CI on macOS, Linux
  and Windows; CI also compiles the Win32 host's source with MSVC.
- Asset updates in the webview builds (`--webview` on macOS, Linux and Windows): the host keeps
  the bundle's files in the app's data directory (`ui-updates/`), checks each one's SHA-256,
  and serves a file of the bundle in use in place of the bundled one, from the next page load
  or `ui_update_apply`, as the service worker does on the web. `tests/desktop/updates.py HOST
  --webview` runs it in CI on the three desktops.
- Windows: `ui_share` opens the system's share sheet (`IDataTransferManagerInterop`, Windows 10
  and later) with the text and the link; the clipboard is the fallback when the sheet cannot
  open. The WinRT pieces are declared in the host, so MinGW-w64 and MSVC build it.
- The webview hosts print the page's console on standard output in a build that is not a
  release.

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
  Code that used its result passes a `String` now: `kit_month_name(m, &name)`.
- `kit_toast_owner(count)` lost its message parameter: the queue (`kit_toast_queue`) holds the
  messages. Only a custom toast host calls it.
- An open `Modal` takes the keyboard on the web: what is under it is inert, focus moves into it
  and back when it closes.
- Internal plumbing that had public names is behind `_` now: the driver's helpers
  (`ui_driver_error`, `ui_driver_advance`, `ui_driver_settle`, ...), the ed25519 internals
  (`ui_fe_*`, `ui_ed_*`), `ui_rollback`, `ui_render_failed`, `ui_unwind_evaluating`,
  `ui_untrack`, `ui_answer_cache`, `Reader` and `RenderSave`. The names a component, a
  package or a test uses stay public and are listed in docs/stability.md (`ui_own`,
  `ui_release_*`, `ui_slot`, `ui_decode`), as are the host ABI (`ui_driver_request`,
  `ui_driver_finish`, ...).
- A `Button`, and a one-line `TextInput`, given no size is at least 44 points tall (a Button
  also 44 wide).
- `ui agent ... layout`: `visible` is false for an element outside the window (a row scrolled
  away), not only for a hidden one, on the web.
- `ui agent ... state` leaves out the signals a hook keeps for itself (`use_animated`'s,
  `use_repeat`'s).
- `.uitest`: `expect ID text` on a container compares the texts inside it, in order.
- A `Tooltip` also shows for a few seconds after a long press (touch screens have no hover).
- An error in a timer's handler names the scope that made the timer.
- The WebView2 host serves the app's files itself (`WebResourceRequested` on
  `https://app.assets/`, no folder mapping), so a route without an extension gets the page as
  on the other webview hosts. The WebKitGTK host keeps the page's storage in
  `$XDG_DATA_HOME/ID/webkit` and its cache in `$XDG_CACHE_HOME/ID`.
- On the web, a downloaded bundle is ready (`ui_update_ready`) once the page has kept its list,
  not before.

### Fixed

- Linux and Windows: `disabled` above a Modal reaches what is in it.
- Windows: a click on a disabled control inside a pressable no longer presses the pressable.
- `ui build windows` on Windows compiles packages' native code (an object's name kept the
  drive's colon and the backslashes).
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
- A new `ui serve web` session could start with an earlier session's signal values: a page
  opened during the first build ran the app left in `build/ui/web`, and the reload after the
  build handed its signals to the new app. The page waits for the first build now.
- `ui agent ... logs` failed (`ok: false`) when the first line of the app's output started
  with `error: `, such as a `console.error` on the web: a log is never read as the host
  failing.
- Text with no color on the web follows the system's dark appearance, as on iOS and macOS.
- Kit: `Title` and `Heading` are headings to screen readers; `DropdownMenu` and `Select` say
  whether they are open, their panel is a menu, and each row is a menu item (a `Select`'s
  rows also say which one is chosen); `Checkbox`, `RadioGroup`'s rows, `TabBar`,
  `SegmentedControl` and menu rows are at least 44 points tall.
- `tap` (and `type`, `submit`) on a disabled element fails with a message instead of
  answering ok for a press that did nothing.
- Web: a multiline TextInput takes the page's font and room for three lines; announcements
  are read while a Modal is open (the live region is no longer made inert); an Image that
  fails to load keeps its background (its words move to `aria-label`).
- Kit: a toast is announced once (it is no longer also an alert); the same message twice in a
  row is queued once, and at most `KIT_TOASTS_WAITING` (5) wait; `SegmentedControl`'s
  unselected options are in ink (muted on the track was under 4.5:1); on the web,
  `SwipeToDelete` always has its delete button, since a page cannot tell that a screen reader
  runs.
- Web: padding on a `Text` was ignored.
- iOS: a checkbox is a toggle button, its state said by VoiceOver in the user's language;
  Android: a link, a tab and a menu item are widget classes TalkBack names in the user's
  language. Words a platform has no localized form of (a mixed check, a disclosure before iOS
  18, an Android menu, menu item, alert, search or link) are the app's, from `ui_set_words`
  (the kit's Spanish or English through `kit_use_text`), English until it sends some
  (docs/hosts.md).
- Android: text with no color follows dark mode, also when it changes while the app runs, and
  so do the window behind the app and the splash's theme (before, it stayed dark on a dark
  background).
- `ui agent`'s `layout` on iOS and Android says `visible` false for a view outside the window
  or scrolled out of its scroll view, not only for a hidden one.
- iOS: the text size (Dynamic Type) scales a line height as it does the font.
- macOS: a view with both `clip` and a shadow keeps its shadow: the children are clipped in an
  inner view with the same radius, and the view's own layer draws the shadow outside.
- docs/state.md says that a duplicate key stops a release build too, as it does (a comment in the runtime said release builds only warned).

## [0.6.2]

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
