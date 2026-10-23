# Hosts

A host applies commits to the platform's views and reports events. Each one is a thin
shim: the app, its components and its state are the same Eskiu code everywhere.

| Platform | Host | Layout |
|---|---|---|
| Web | `hosts/web/`: `renderer.mjs` over the DOM, the app compiled to wasm by emscripten | CSS flexbox with Yoga's defaults |
| iOS | `hosts/ios/host.mm`: UIKit, Objective-C++ | Yoga 3.2.1 |
| Android | `hosts/android/`: `UiActivity.java` over `android.view`, `host.cpp` the JNI shim | Yoga 3.2.1 |
| macOS | `hosts/macos/host.m`: the web host in a `WKWebView` window | the web's |
| Linux | `hosts/linux/host.c`: the web host in a GTK / WebKitGTK window | the web's |
| Windows | `hosts/windows/host.cpp`: the web host in a Win32 / WebView2 window | the web's |

## Elements per host

| Kind | Web | iOS | Android |
|---|---|---|---|
| column, row | `div` (flex) | `UIView` | a `ViewGroup` that places its children from Yoga |
| text | `span` | `UILabel` | `TextView` |
| button | `button` | `UIButton` (system) | `Button` |
| text input | `input type=text` | `UITextField` | `EditText` |
| scroll | `div` with `overflow: auto` | `UIScrollView` | `ScrollView` |
| image | `img` | `UIImageView` | `ImageView` |
| pressable | `div role=button` | `UIControl` with the button trait | a clickable view group |
| switch | `input type=checkbox role=switch` | `UISwitch` | `Switch` |
| slider | `input type=range` | `UISlider` | `SeekBar` (its progress counts from the minimum) |
| activity indicator | a CSS spinner, `role=progressbar` | `UIActivityIndicatorView` | `ProgressBar` |
| refresh control | a pull at the top of the scroll | `UIRefreshControl` | a pull at the top of the scroll and a `ProgressBar` (the framework has no refresh view) |
| modal | `div role=dialog` over the page | a view over the window | a view over the window |
| alert | `dialog` | `UIAlertController` | `AlertDialog` |
| status bar | nothing | the view controller's status bar | `WindowInsetsController` |
| screen | a `div`, `display: none` under the top | a `UIView`, hidden under the top; slides in and out | a view group, gone under the top; slides in and out |
| native view | a `div` holding the factory's element | a `UIView` holding the factory's view | a `FrameLayout` holding the factory's view |

Labels become `aria-label` (an image's `alt`), `accessibilityLabel` and
`contentDescription`.

## Services per host

| Service | Web | iOS | Android |
|---|---|---|---|
| frames | `requestAnimationFrame` | `CADisplayLink` | `Choreographer` |
| timers | `setTimeout`, `setInterval` | `NSTimer` on the main run loop | `Handler.postDelayed` |
| transitions | CSS transitions | `UIView` animations (springs too) | `ViewPropertyAnimator`, `ValueAnimator` for colors |
| drags | pointer events, captured once past 6 px | `UIPanGestureRecognizer` | a touch listener, intercepted from a pressable inside past the slop |
| hover | `mouseenter`, `mouseleave` | `UIHoverGestureRecognizer` | `setOnHoverListener` |
| network | `fetch` | `NSURLSession` | `HttpURLConnection` on a thread |
| storage | `localStorage` | Application Support | `getFilesDir()` |
| clipboard | `navigator.clipboard` (or the page's copy) | `UIPasteboard` | `ClipboardManager` |
| share | `navigator.share` (or a copy) | `UIActivityViewController` | `ACTION_SEND` chooser |
| haptics | `navigator.vibrate` | feedback generators | `performHapticFeedback` |
| announcements | a live region | `UIAccessibilityAnnouncementNotification` | `announceForAccessibility` |
| appearance | `prefers-color-scheme`, `prefers-reduced-motion`, `navigator.language` | trait collection, `UIAccessibility`, `NSLocale` | configuration, `AccessibilityManager`, animator scale |

Each host also has a registry of native view types and a way for native code to post
events from any thread: [native code](native.md).

## The web

`ui build web` compiles the app with `--target wasm32-unknown-emscripten` and links it with
emscripten and `hosts/web/ui-library.js`, which hands each commit to the renderer as a view
of wasm memory, in one call. `scheduler.mjs` runs `ui_tick` once per animation frame when
work is scheduled. `app.html` starts the app, reports the window and the URL, and holds the
page's styles. The output is `build/ui/web/`: `index.html`, the JavaScript, the wasm module
and the assets.

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
the app starts again on it, in the same process: the old core stops, the host drops the views,
timers and per-node state it left, and the new core starts as at launch. The app is installed
and launched again only when the host, the native code, the plist or manifest, or the assets
changed.

- **iOS.** The app's code is a bundle linked against the executable (`-bundle_loader`), which
  exports the host's and the native code's functions (`-export_dynamic`). The simulator's apps
  read this Mac's files, so the host polls `build/ui/ios/reload/current` (its path comes in
  `UI_RELOAD_FILE`) for the newest bundle's path, and writes its pid next to it, which tells
  the tool the app is running.
- **Android.** The app's code is a library linked against `libeskiu_ui.so`. The tool copies it
  into the app's files with `run-as` (the debug APK allows it), names it in `files/ui-reload`,
  and sends the activity an intent with `ui_reload`; the activity loads it at start too.

## macOS

`ui build macos` builds the web host and a Cocoa app (`.app` bundle and binary) whose
window is a `WKWebView` loading it. The window takes the page's title and configures
native menu items (Quit, Edit with standard shortcuts Cmd+C, Cmd+V, Cmd+Z).

## Linux

`ui build linux` builds the web host and a native GTK 3 application using WebKitGTK
(`hosts/linux/host.c`). It connects to `webkit2gtk-4.1` (or `webkit2gtk-4.0`) via
`pkg-config`. The app in `web/` beside the executable is served as `app://localhost/` (a path
with no extension is a route and gets the page), so the page's base, its module imports and
its routes work as on a server. The window takes the document's title and supports snapshot
testing; a release build has no inspector.

## Windows

`ui build windows` builds the web host and a native Win32 application using Microsoft
Edge WebView2 (`hosts/windows/host.cpp`), with MSVC (`cl.exe`, from a Developer prompt) or
MinGW-w64 (`g++` on Windows, `x86_64-w64-mingw32-g++` from macOS or Linux); `CXX` picks
another. The tool fetches the WebView2 SDK's NuGet package once (checked by its SHA-256) for
its header, and puts its `WebView2Loader.dll` next to the executable. The app in `web/` is
served as `https://app.assets/`; WebView2 keeps its data in `%LOCALAPPDATA%\NAME`. The window
has no console, takes the document's title, reloads on F5 and Ctrl+R and closes on Ctrl+W,
and a failure to start WebView2 says so in a message box. A release build turns the
developer tools off. Running it needs the WebView2 Runtime, which Windows 10 and 11 carry.

## Writing a host

A host needs to:

1. Define `ui_host_commit(data, len)` to apply a commit (`hosts/commit.h` decodes one in C),
   `ui_host_schedule()` to ask for a frame and `ui_host_cancel()` to drop one, and
   `ui_host_timer_start(token, ms, repeat)` and `ui_host_timer_stop(token)` for timers.
2. Call `ui_start(0)` at launch, `ui_tick()` for each scheduled frame and `ui_stop()` at
   the end.
3. Report events and the environment with the `ui_dispatch_*` and `ui_report_*` calls
   ([protocol](protocol.md)), on the UI thread.
4. Lay the tree out with Yoga (or the platform's flexbox) using the props.
5. Pass the conformance scene ([testing](testing.md)).
