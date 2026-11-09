# Hosts

A host applies commits to the platform's views and reports events. Each one is a thin
shim: the app, its components and its state are the same Eskiu code everywhere.

| Platform | Host | Layout |
|---|---|---|
| Web | `hosts/web/`: `renderer.mjs` over the DOM, the app compiled to wasm by emscripten | CSS flexbox with Yoga's defaults |
| iOS | `hosts/ios/host.mm`: UIKit, Objective-C++ | Yoga 3.2.1 |
| Android | `hosts/android/`: `UiActivity.java` over `android.view`, `host.cpp` the JNI shim | Yoga 3.2.1 |
| macOS | `hosts/macos/host.mm`: AppKit, Objective-C++ (`hosts/macos/webview.m`: the web host in a `WKWebView` window, with `--webview`) | Yoga 3.2.1 |
| Linux | `hosts/linux/host.cpp`: GTK 4, C++ (`hosts/linux/webview.c`: the web host in a WebKitGTK window, with `--webview`) | Yoga 3.2.1 |
| Windows | `hosts/windows/host.cpp`: Win32 windows and common controls, C++ (`hosts/windows/webview.cpp`: the web host in a WebView2 window, with `--webview`) | Yoga 3.2.1 |

## Elements per host

| Kind | Web | iOS | Android | macOS | Linux |
|---|---|---|---|---|---|
| column, row | `div` (flex) | `UIView` | a `ViewGroup` that places its children from Yoga | a flipped, layer-backed `NSView` | a `UiBox`: a `GtkWidget` that places its children at Yoga's frames and paints the app's background, border, radius and shadow |
| text | `span` | `UILabel` | `TextView` | `NSTextField` (a wrapping label) | `GtkLabel` (wrapping) |
| button | `button` | `UIButton` (system) | `Button` | `NSButton` (a push button; borderless, painted by its layer, when the app gives it a background or a border) | `GtkButton` (the theme's; with no frame of its own, the CSS class `ui-styled`, and painted by its box, when the app gives it a background or a border) |
| text input | `input type=text` | `UITextField` | `EditText` | `NSTextField` (bezeled; borderless with the app's border, background and padding) | `GtkEntry` (the theme's; frameless, inside the app's padding, when styled) |
| scroll | `div` with `overflow: auto` | `UIScrollView` | `ScrollView` | `NSScrollView` (overlay scrollers) around a flipped document view | `GtkScrolledWindow` (overlay scrollbars) around a box sized to the content |
| image | `img` | `UIImageView` | `ImageView` | `NSImageView` (fit or cover) | the box draws the `GdkTexture` (fit or cover, smoothed or pixelated) |
| pressable | `div role=button` | `UIControl` with the button trait | a clickable view group | an `NSView` with the button role (`accessibilityPerformPress`) | a box with the button role and a `GtkGestureClick` |
| switch | `input type=checkbox role=switch` | `UISwitch` | `Switch` | `NSSwitch` | `GtkSwitch` (at its own size) |
| slider | `input type=range` | `UISlider` | `SeekBar` (its progress counts from the minimum) | `NSSlider` | `GtkScale` |
| activity indicator | a CSS spinner, `role=progressbar` | `UIActivityIndicatorView` | `ProgressBar` | `NSProgressIndicator` (spinning) | `GtkSpinner` |
| refresh control | a pull at the top of the scroll | `UIRefreshControl` | a pull at the top of the scroll and a `ProgressBar` (the framework has no refresh view) | nothing (no pull on a desktop; `refreshing` is kept) | nothing, as on macOS |
| modal | `div role=dialog` over the page | a view over the window | a view over the window | a sheet (`beginSheet`), as big as its content; an `Overlay` is a view over the window | a box over the window's content, dimmed, with the dialog role (as on iOS and Android) |
| alert | `dialog` | `UIAlertController` | `AlertDialog` | `NSAlert` as a sheet | `GtkAlertDialog` |
| status bar | nothing | the view controller's status bar | `WindowInsetsController` | nothing | nothing |
| screen | a `div`, `display: none` under the top | a `UIView`, hidden under the top; slides in and out | a view group, gone under the top; slides in and out | an `NSView`, hidden under the top; slides in and out | a box, hidden under the top; slides in and out |
| native view | a `div` holding the factory's element | a `UIView` holding the factory's view | a `FrameLayout` holding the factory's view | an `NSView` holding the factory's view | a box holding the factory's widget |

Labels become `aria-label` (an image's `alt`), `accessibilityLabel` (UIKit and AppKit),
`contentDescription` and GTK's accessible label (AT-SPI).

## Services per host

| Service | Web | iOS | Android | macOS | Linux |
|---|---|---|---|---|---|
| frames | `requestAnimationFrame` | `CADisplayLink` | `Choreographer` | `dispatch_async` to the main queue (one frame per turn of the run loop) | an idle on GLib's main loop (one frame per turn); `use_frame` from the window's frame clock |
| timers | `setTimeout`, `setInterval` | `NSTimer` on the main run loop | `Handler.postDelayed` | `NSTimer` on the main run loop | `g_timeout_add` |
| transitions | CSS transitions | `UIView` animations (springs too) | `ViewPropertyAnimator`, `ValueAnimator` for colors | Core Animation on the view's layer (`CABasicAnimation`, `CASpringAnimation`) | the host's own, the web's curves, on its clock (opacity, colors, transforms, slides) |
| drags | pointer events, captured once past 6 px | `UIPanGestureRecognizer` | a touch listener, intercepted from a pressable inside past the slop | `NSPanGestureRecognizer` | `GtkGestureDrag`, claimed once past 6 points |
| hover | `mouseenter`, `mouseleave` | `UIHoverGestureRecognizer` | `setOnHoverListener` | an `NSTrackingArea` | `GtkEventControllerMotion` |
| network | `fetch` | `NSURLSession` | `HttpURLConnection` on a thread | `NSURLSession` | libcurl on a thread, loaded when first used (`dlopen`, no link-time dependency) |
| storage | `localStorage` | Application Support | `getFilesDir()` | Application Support/BUNDLE_ID | `$XDG_DATA_HOME/ID` (`~/.local/share/ID`) |
| clipboard | `navigator.clipboard` (or the page's copy) | `UIPasteboard` | `ClipboardManager` | `NSPasteboard` | `GdkClipboard` |
| share | `navigator.share` (or a copy) | `UIActivityViewController` | `ACTION_SEND` chooser | `NSSharingServicePicker` | a copy (a Linux desktop has no share sheet) |
| haptics | `navigator.vibrate` | feedback generators | `performHapticFeedback` | `NSHapticFeedbackManager` (a trackpad's) | nothing |
| announcements | a live region | `UIAccessibilityAnnouncementNotification` | `announceForAccessibility` | `NSAccessibilityAnnouncementRequestedNotification` | `gtk_accessible_announce` (GTK 4.14) |
| appearance | `prefers-color-scheme`, `prefers-reduced-motion`, `navigator.language` | trait collection, `UIAccessibility`, `NSLocale` | configuration, `AccessibilityManager`, animator scale | `NSApp.effectiveAppearance` (dark mode), `NSWorkspace` accessibility options, `NSLocale` | `GtkSettings` (a dark theme or color scheme, `gtk-enable-animations`), the locale's language |
| asset updates ([updates](updates.md)) | Cache Storage and a service worker | Application Support | `getFilesDir()` | Application Support/BUNDLE_ID | `$XDG_DATA_HOME/ID` |

Each host also has a registry of native view types and a way for native code to post
events from any thread: [native code](native.md).

## The web

`ui build web` compiles the app with `--target wasm32-unknown-emscripten` and links it with
emscripten and `hosts/web/ui-library.js`, which hands each commit to the renderer as a view
of wasm memory, in one call. `scheduler.mjs` runs `ui_tick` once per animation frame when
work is scheduled. `app.html` starts the app, reports the window and the URL, and holds the
page's styles. The output is `build/ui/web/`: `index.html`, the JavaScript, the wasm module
and the assets.

### The first frame

`ui build web` (dev and release) renders the app's first frame at build time and writes it
into `index.html`, so the page shows the app before the module and the wasm load. The tool runs
`hosts/web/prerender.mjs` with node (`EMSDK_NODE`, else on the `PATH`): it starts the app on a
small DOM of its own, as the page does at its root route, and writes the markup the renderer
made, with its inline styles, the renderer's style sheet and the fonts' `@font-face` rules.
`#app` carries the first commit's fingerprint (`data-ui-prerender`, FNV-1a over its mutations)
and the route it was made for (`data-ui-route`).

When the module starts, the renderer computes its own first commit's fingerprint. When it is
the same, the commit adopts the page's elements: each node takes the element with its
`data-ui-id` instead of a new one, props and text are written again with the values they have
(nothing moves, nothing is made again), and handlers work on them as on any element. When it is
not (the first render depended on something the build could not know: saved data, a reload's
state, an update in use), the renderer drops the markup and builds the tree afresh, in the
same task, so the page never shows both. A page opened on another route than the root, or one
a reload under `ui serve` restores, drops the markup before its first paint (an inline script
right after `#app`) and starts empty, as before. The first render runs before the window, dark
mode and language are reported, so it does not depend on them; an app that adapts to them
shows the prerendered frame until the module reports them, a frame later.

`[web] prerender = false` leaves it out. Measured on `examples/counter`'s release page in
headless Chrome (Chromium 128, an arm64 Mac, the medians of five loads), the first contentful
paint:

| Network | Without | With the prerendered frame |
|---|---|---|
| local server | 35 ms | 30 ms |
| DevTools' "Slow 4G" (150 ms latency, 1.6 Mbit/s) | 1,710 ms | 220 ms |

`web.first_paint_ms` in `tools/budgets.toml` keeps the second ([testing](testing.md#speed-budgets)).

### Under `ui serve`

Under `ui serve`, the tool's server adds a script to `index.html` as it serves it (the built
file is unchanged). The script asks `/__ui/wait?v=N`, which answers when the build after N
is done (or after 25 seconds) with `{"version", "error"}`: the page reloads after a build
that worked and shows a failed one's output over the app. The server answers from eight
threads, so a page that waits does not hold up the others.

## iOS

`ui build ios` cross-compiles the app for the simulator (`arm64-apple-ios15.0-simulator`),
compiles Yoga, the host and `native/ios` with `xcrun clang`, and makes `build/ui/ios/NAME.app` with
`Info.plist` and the assets. A `CADisplayLink` drives `ui_tick`; the scene delegate reports
the safe area, the keyboard, the window, foreground changes and opened URLs. The host, the
native code and Yoga are compiled again only when they changed (a `.stamp` next to each
output records its inputs).

## Android

`ui build android` cross-compiles the app for `aarch64-linux-android24`, compiles Yoga and
`host.cpp` with the NDK's clang into `libeskiu_ui.so`, compiles `UiActivity.java` with `javac`
and `d8`, and packages, aligns and signs the APK with the build tools (`aapt`, `zipalign`,
`apksigner`, a debug key), with no Gradle. Native C, C++ and Java code goes into the same
library and dex. With Maven dependencies or Kotlin code it builds through Gradle instead, and
iOS with Swift packages or Swift code through Xcode ([native code](native.md)). The `Choreographer` drives `ui_tick`; the
activity reports insets, the window, `onResume`/`onPause`, back presses and intents. The
host, the native code and the classes are built again only when they changed.

## Reload builds

Under `ui serve`, builds set `UI_DEV=1`: the app's code is compiled at `-O0`, and on iOS and
Android it goes into a library of its own instead of the host's binary. The host is compiled
with `UI_RELOAD`, which makes the core's entry points (`hosts/core.h`) pointers that it fills
with `dlopen` and `dlsym`. After a build the tool hands the running app the new library and
the app starts again on it, in the same process: the host asks the old core for what it keeps
(`ui_reload_save`: its routes and signals, [State](state.md#a-reload-under-ui-serve)), stops
it, drops the views, timers and per-node state it left, loads the new core, hands it the saved
text (`ui_reload_restore`) and starts it. The app is installed and launched again only when the
host, the native code, the plist or manifest, or the assets changed. On the web the serve
script saves the text in `sessionStorage` before it reloads the page, and the page hands it to
the new module before `ui_start`.

- **iOS.** The app's code is a bundle linked against the executable (`-bundle_loader`), which
  exports the host's and the native code's functions (`-export_dynamic`). The simulator's apps
  read this Mac's files, so the host polls `build/ui/ios/reload/current` (its path comes in
  `UI_RELOAD_FILE`) every 50 ms for the newest bundle's path, and writes its pid next to it,
  which tells the tool the app is running.
- **macOS.** As on iOS: the app's code is a bundle linked against the executable, which
  exports the host's and the native code's functions; the host polls
  `build/ui/macos/reload/current` (`UI_RELOAD_FILE`) every 50 ms and writes its pid next to
  it. `ui serve macos` starts the app in the background, its output in
  `build/ui/macos/app.log`, and starts it again only when the host, the native code or the
  Info.plist changed.
- **Linux.** As on macOS: the app's code is a shared library the executable (linked with
  `-rdynamic`, so the library finds the host's and the native code's functions) loads with
  `dlopen`; the host polls `build/ui/linux/reload/current` (`UI_RELOAD_FILE`) every 50 ms and
  writes its pid next to it. `ui serve linux` starts the app in the background, its output in
  `build/ui/linux/app.log`, and starts it again only when the host or the native code changed.
- **Windows.** The app's code is a DLL that imports the host's and the native code's functions
  from the executable (linked with `--export-all-symbols`, whose import library the DLL links
  against). A loaded DLL stays locked, so each build names a new one; the host polls
  `build/ui/windows/reload/current` every 50 ms and writes its pid next to it, and the tool ends
  the app before it links the executable again.
- **Android.** The app's code is a library linked against `libeskiu_ui.so`. The tool pushes it
  and, in one `adb shell`, checks the app is installed, copies it into the app's files with
  `run-as` (the debug APK allows it), names it in `files/ui-reload` and sends the activity an
  intent with `ui_reload`; the activity loads it at start too. `ui serve` asks the device's ABI
  once, not for each build.

`ui serve` looks at the sources every 50 ms and builds once they held still for 50 ms; the
assets are copied again only when they changed.

## macOS

`ui build macos` compiles the app for this Mac (`arm64-apple-macos11.0` or x86_64), compiles
Yoga, `hosts/macos/host.mm` and `native/macos` with `xcrun clang++`, and makes
`build/ui/macos/NAME.app` with `Info.plist`, the icon and the assets, signed ad hoc. A
release has both architectures (`lipo`). The host is AppKit, as the iOS host is UIKit: each
element is an AppKit view (the table above), laid out by Yoga in flipped coordinates, so the
frames are the other hosts' to the point. Its parts:

- **The window.** 900 by 700 points (ui.toml `[macos] window = "430x880"`, or `UI_WINDOW=WxH`),
  resizable down to 320 by 240; its size is the app's window and the insets are zero. Its
  title is the app's name.
- **The menu bar.** The app's menu (About, Hide, Quit), File (Close Window), Edit (Undo,
  Redo, Cut, Copy, Paste, Select All with their shortcuts, which the fields take through the
  responder chain), View (Back, Cmd+[; Enter Full Screen) and Window (Minimize, Zoom).
- **Back.** Escape, Cmd+[ and a swipe right are the platform's back (`ui_report_back`): the
  newest modal closes, else a `use_back` handler or the router goes back. Escape in a sheet
  asks its Modal to close.
- **Dark mode.** The system's appearance (`NSApp.effectiveAppearance`) goes to the app with
  each change; a text with no color of its own takes `labelColor`, which follows it.
- **Accessibility.** AppKit's own roles: a label is static text, a button a button, a
  pressable a button whose press is `accessibilityPerformPress`, a field a text field, a
  switch, a slider, an image, a spinner a progress indicator, a modal a sheet, a labeled
  column or row a group with its label.
- **Styled controls.** A button or a field the app gives a background, a border or a radius
  loses the system's bezel, and its layer paints what the app asked (the kit's buttons and
  fields look as on the other hosts); a plain one is the system's push button or field.
- **What is not native yet.** A RefreshControl has no pull (a desktop has none), and a
  screenshot (`ui agent`) is the window's content as AppKit draws it, without the sheets.

ui.toml's `[macos]` table: `renderer` (`"web"` for the WKWebView host), `window`,
`frameworks` (system frameworks to link) and `[macos.plist]` (Info.plist entries). The
app's URL scheme (`[app] scheme`) opens it (`ui_link`).

### The WKWebView host

`ui build macos --webview` (also `run`, `serve` and `release`), or ui.toml `[macos] renderer =
"web"` for an app that relies on the web host (its DOM, `native/web` code), builds the web
host and a Cocoa app whose window is a `WKWebView` loading it (`hosts/macos/webview.m`), for
both architectures. The window takes the page's title and has the same menus (with View's
Reload). `ui serve macos --webview` reloads the page after each build, as on the web.

## Linux

`ui build linux` compiles the app for this machine, compiles Yoga, `hosts/linux/host.cpp` and
`native/linux` with the C++ compiler (`CXX`, else `c++`) and GTK 4's flags from `pkg-config`,
and makes `build/ui/linux/NAME`, with the assets in `assets/` and `icon.png` beside it. It needs
GTK 4.14 or later (Ubuntu 24.04, Debian 13, Fedora 40). The host is GTK 4, as the macOS host is
AppKit: each element is a GTK widget (the table above), laid out by Yoga, so the frames are the
other hosts' to the point. Its parts:

- **The window.** 900 by 700 points of app under a menu bar (ui.toml `[linux] window =
  "430x880"`, or `UI_WINDOW=WxH`), resizable down to 320 by 240; its size is the app's window and
  the insets are zero. Its title is the app's name, its icon `icon.png`.
- **The menu bar.** File (Close Window, Quit), Edit (Cut, Copy, Paste, Select All, for the field
  with the focus) and View (Back, Full Screen), a `GMenu` on the window's actions, with Ctrl+W,
  Ctrl+Q and F11.
- **Back.** Escape, Alt+Left and a mouse's back button are the platform's back
  (`ui_report_back`): the newest modal closes, else a `use_back` handler or the router goes back.
- **Dark mode.** GTK's settings (a dark theme, `gtk-application-prefer-dark-theme`, and on GTK
  4.20 the interface's color scheme) go to the app with each change; a text with no color of
  its own takes the theme's, which follows it. No libadwaita.
- **Accessibility.** GTK's accessible roles over AT-SPI: a label, a button, a pressable a box
  with the button role, an entry, a switch, a slider, an image, a spinner, a modal a dialog,
  a labeled column or row a group with its label.
- **Styled controls.** A button or a field the app gives a background, a border or a radius
  loses the theme's frame (the CSS class `ui-styled`) and its box paints what the app asked
  (the kit's buttons and fields look as on the other hosts); a plain one is the theme's.
- **Text.** 16 points unless the app says otherwise, in the system's sans-serif font (the
  frames of text and controls depend on the font, as on every host; the conformance tests
  check those that do not). A font file from `assets/` is added to fontconfig.
- **What is not native yet.** A RefreshControl has no pull (a desktop has none), a backdrop
  blur paints the window's color at 65% (GTK cannot blur what is under a widget) and a share
  copies the text. A screenshot (`ui agent`) is the window's content without an alert.

ui.toml's `[linux]` table: `renderer` (`"web"` for the WebKitGTK host) and `window`. A
release (`ui release linux`) is a `.tar.gz` of the program, its assets and its icon.

### The WebKitGTK host

`ui build linux --webview` (also `run`, `serve` and `release`), or ui.toml `[linux] renderer =
"web"` for an app that relies on the web host, builds the web host and a GTK 3 program whose
window is a WebKitGTK view (`hosts/linux/webview.c`, `webkit2gtk-4.1` or `4.0` through
`pkg-config`). The app in `web/` beside the executable is served as `app://localhost/` (a path
with no extension is a route and gets the page), so the page's base, its module imports and its
routes work as on a server. The window takes the document's title; a release build has no
inspector. `ui serve linux --webview` reloads the page after each build, as on the web.

## Windows

`ui build windows` compiles the app for `x86_64-w64-windows-gnu`, compiles Yoga,
`hosts/windows/host.cpp` and `native/windows` with MinGW-w64's C++ compiler (llvm-mingw's
`clang++` or `g++` on Windows, `x86_64-w64-mingw32-clang++` or `-g++` from macOS or Linux; `CXX`
picks one), and links `build/ui/windows/NAME.exe` with its manifest (common controls 6,
per-monitor DPI awareness v2) and the C++ runtime inside, `assets/` and `icon.ico` beside it.
The host is Win32, as the macOS host is AppKit: each element is a child window, laid out by
Yoga in points (DIPs; pixels are points times the window's DPI over 96), so the frames are the
other hosts' to the point.

| Kind | Windows |
|---|---|
| column, row, pressable, screen | a child window the host paints (background, gradient, border, radius with GDI+) |
| text | a `STATIC`, in Segoe UI at the app's size (16 points by default), measured with `DrawText` |
| button | a `BUTTON` (a push button in the system's font; owner drawn in the app's font and colors when it styles it) |
| text input | an `EDIT` in a frame the host paints (the system's look, or the app's border, background and padding) |
| scroll | a window with a vertical scroll bar around a document window; the wheel and the bar scroll it |
| image | a window drawing the picture with GDI+ (fit or cover, smooth or pixelated); files from `assets/`, http(s) on a thread |
| switch | an owner-drawn `BUTTON`: a Windows 11 toggle in the accent color, its MSAA role a check button |
| slider | a trackbar (`msctls_trackbar32`) |
| activity indicator | a turning arc the host draws, its role a progress bar |
| modal | a window over the content, with a picture of what is under it dimmed (an `Overlay` too) |
| alert | a task dialog (`TaskDialogIndirect`) with the app's buttons |
| native view | a window holding the factory's child window |

- **The window.** 900 by 700 points of client area (ui.toml `[windows] window = "430x880"`, or
  `UI_WINDOW=WxH`), resizable down to 320 by 240; its size is the app's window, the insets are
  zero, and a move to a monitor of another DPI lays it out again. Its title is the app's name.
- **Menus.** File (About, Exit), Edit (Undo, Cut, Copy, Paste, Select All with their
  shortcuts, for the field with the focus) and View (Back, Alt+Left).
- **Back.** Escape, Alt+Left and a mouse's back button are the platform's back
  (`ui_report_back`): the newest modal closes, else a `use_back` handler or the router goes back.
- **Dark mode.** The system's app mode (`AppsUseLightTheme`) goes to the app with each change; the
  title bar (`DWMWA_USE_IMMERSIVE_DARK_MODE`), the menus, the buttons, fields and scroll bars
  follow it, and a text with no color of its own takes the window's text color.
- **Accessibility.** The standard controls carry their own MSAA and UI Automation roles; the
  host gives the others theirs with dynamic annotation (`IAccPropServices`): a pressable is a
  push button, a switch a check button with its state, an image a graphic, a spinner a
  progress bar, a modal a dialog, a labeled column or row a grouping, each with its label.
- **Services.** Frames and timers are window messages and `SetTimer`; transitions (opacity,
  translation, background color) and the screens' slides are the host's, on its clock; drags,
  long presses and hover come from the mouse (captured past 6 px); the network is WinHTTP on a
  thread; storage is `%APPDATA%\NAME`; the clipboard is the system's, and a share copies to it.
- **What is not native yet.** A transform's scale and rotation, a shadow and a backdrop blur
  are kept but not drawn; a vertical scroll bar takes its width from the scroll's content when
  it shows; an announcement is a name change event, which screen readers may not speak; a
  RefreshControl has no pull. MSVC does not build this host (the app's object is MinGW's).

ui.toml's `[windows]` table: `renderer` (`"web"` for the WebView2 host) and `window`.

### The WebView2 host

`ui build windows --webview` (also `run`, `serve` and `release`), or ui.toml `[windows]
renderer = "web"`, builds the web host and a Win32 application using Microsoft Edge WebView2
(`hosts/windows/webview.cpp`), with MSVC (`cl.exe`, from a Developer prompt) or MinGW-w64; `CXX`
picks another. The tool fetches the WebView2 SDK's NuGet package once (checked by its SHA-256)
for its header, and puts its `WebView2Loader.dll` next to the executable. The app in `web/` is
served as `https://app.assets/`; WebView2 keeps its data in `%LOCALAPPDATA%\NAME`. The window
has no console, takes the document's title, reloads on F5 and Ctrl+R and closes on Ctrl+W,
and a failure to start WebView2 says so in a message box. A release build turns the
developer tools off. Running it needs the WebView2 Runtime, which Windows 10 and 11 carry.

## The driver

A dev build (`ui serve`) has the driver `ui agent` talks to ([agents](agents.md)). The core
answers what it can; each host carries the rest: the platform's input (a click, a field's
edit, a scroll), each view's frame, its output and memory, and its animations on the
driver's clock.

| | Web | iOS | Android | macOS | Linux |
|---|---|---|---|---|---|
| channel | the dev server: `/__ui/agent`, `hosts/web/driver.mjs` | TCP on 127.0.0.1:8766 in the app, `hosts/driver.h` | the abstract socket `eskiu-ui-agent`, `adb forward`, `hosts/driver.h` | TCP on 127.0.0.1:8768 in the app, `hosts/driver.h` | TCP on 127.0.0.1:8769 in the app, `hosts/driver.h` |
| tap, type, scroll | `click()`, `input` event, `scrollTop` | `sendActionsForControlEvents`, `setContentOffset` | `performClick`, `setText`, `scrollBy` | a control's action, `accessibilityPerformPress`, the field editor's text and `NSControlTextDidChangeNotification`, the clip view's `scrollToPoint:` | a control's signal (`clicked`, a switch's `active`), `gtk_editable_set_text`, the scroll's `GtkAdjustment` |
| animations on the clock | `document.getAnimations()` paused and seeked; a field's caret hidden | the window's layer at `speed` 0, `timeOffset` the driver's time | the animator duration scale at 0; a field's cursor hidden | the window content's layer at `speed` 0, `timeOffset` the driver's time | the host's transitions on the driver's time; GTK's animations and the caret's blinking off |
| screenshot (by `ui agent`) | the headless browser's DevTools | `simctl io screenshot` | `adb exec-out screencap` | the host draws its content view into a bitmap (`cacheDisplayInRect:`), in sRGB | the host snapshots its content (`gtk_widget_snapshot_child`) and renders it with the window's GSK renderer |
| memory (`perf`) | the wasm heap | the process's footprint | the native and Java heaps | the process's footprint | the process's resident set |
| start (`perf`'s cold start) | `performance.timeOrigin` | the process's start time (`sysctl`) | the process's start time (`/proc/self/stat`) | the process's start time (`sysctl`) | the process's start time (`/proc/self/stat`) |

On Windows the channel is TCP on 127.0.0.1:8770 in the app (`hosts/driver.h` over Winsock);
tap is a button's `BM_CLICK` (a switch's too), a pressable's or a modal's press, a field's focus;
type sets the `EDIT`'s text, which sends its `EN_CHANGE`; scroll moves the scroll window's
document and bar. On the driver's clock the host's transitions, slides and spinner take the
driver's time. A screenshot is the window's client area as the compositor has it
(`PrintWindow` with `PW_RENDERFULLCONTENT`), the memory `perf` reports the process's private
bytes, and the cold start counts from the process's creation time (`GetProcessTimes`).

The host compiles its side only with `UI_TEST` (the ui tool passes it for a dev build), and
the core's only with the `UI_TEST` the ui tool defines in the entry file: a release has
neither. The Android activity keeps its small `driver*` methods in every build; nothing calls
them without the native side.

## Writing a host

A host needs to:

1. Define `ui_host_commit(data, len)` to apply a commit (`hosts/commit.h` decodes one in C),
   `ui_host_schedule()` to ask for a frame and `ui_host_cancel()` to drop one, and
   `ui_host_timer_start(token, ms, repeat)` and `ui_host_timer_stop(token)` for timers,
   `ui_host_frames(on)` for the display clock (`use_frame`), and `ui_host_asset_path(name)`
   for `ui_asset_path` (a path from `malloc`, or null).
2. Call `ui_start(0)` at launch, `ui_tick()` for each scheduled frame and `ui_stop()` at
   the end.
3. Report events and the environment with the `ui_dispatch_*` and `ui_report_*` calls
   ([protocol](protocol.md)), on the UI thread.
4. Lay the tree out with Yoga (or the platform's flexbox) using the props.
5. Pass the conformance scene ([testing](testing.md#conformance)).
6. In a dev build, carry the driver's directives ([agents](agents.md#the-protocol)) and pass
   `tests/agent/check.sh` and `ui test` in `tests/conformance`.
