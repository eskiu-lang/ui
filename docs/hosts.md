# Hosts

A host applies commits to the platform's views and reports events. Each one is a thin
shim: the app, its components and its state are the same Eskiu code everywhere.

| Platform | Host | Layout |
|---|---|---|
| Web | `hosts/web/`: `renderer.mjs` over the DOM, the app compiled to wasm by emscripten | CSS flexbox with Yoga's defaults |
| iOS | `hosts/ios/host.mm`: UIKit, Objective-C++ | Yoga 3.2.1 |
| Android | `hosts/android/`: `UiActivity.java` over `android.view`, `host.cpp` the JNI shim | Yoga 3.2.1 |
| macOS | `hosts/macos/host.m`: the web host in a `WKWebView` window | the web's |
| Windows, Linux | not yet | |

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
| activity indicator | a CSS spinner, `role=progressbar` | `UIActivityIndicatorView` | `ProgressBar` |
| refresh control | a pull at the top of the scroll | `UIRefreshControl` | a pull at the top of the scroll and a `ProgressBar` (the framework has no refresh view) |
| modal | `div role=dialog` over the page | a view over the window | a view over the window |
| alert | `dialog` | `UIAlertController` | `AlertDialog` |
| status bar | nothing | the view controller's status bar | `WindowInsetsController` |

Labels become `aria-label` (an image's `alt`), `accessibilityLabel` and
`contentDescription`.

## The web

`ui build web` compiles the app with `--target wasm32-unknown-emscripten` and links it with
emscripten and `hosts/web/ui-library.js`, which hands each commit to the renderer as a view
of wasm memory, in one call. `scheduler.mjs` runs `ui_tick` once per animation frame when
work is scheduled. `app.html` starts the app, reports the window and the URL, and holds the
page's styles. The output is `build/ui/web/`: `index.html`, the JavaScript, the wasm module
and the assets.

## iOS

`ui build ios` cross-compiles the app for the simulator (`arm64-apple-ios15.0-simulator`),
compiles Yoga and the host with `xcrun clang`, and makes `build/ui/ios/NAME.app` with
`Info.plist` and the assets. A `CADisplayLink` drives `ui_tick`; the scene delegate reports
the safe area, the keyboard, the window, foreground changes and opened URLs.

## Android

`ui build android` cross-compiles the app for `aarch64-linux-android24`, compiles Yoga and
`host.cpp` with the NDK's clang into `libeskiu_ui.so`, compiles `UiActivity.java` with `javac`
and `d8`, and packages, aligns and signs the APK with the build tools (`aapt`, `zipalign`,
`apksigner`, a debug key), with no Gradle. The `Choreographer` drives `ui_tick`; the
activity reports insets, the window, `onResume`/`onPause`, back presses and intents.

## macOS

`ui build macos` builds the web host and a small Cocoa app whose window is a `WKWebView`
loading it. The title comes from the page.

## Writing a host

A host needs to:

1. Define `ui_host_commit(data, len)` to apply a commit (`hosts/commit.h` decodes one in C),
   `ui_host_schedule()` to ask for a frame and `ui_host_cancel()` to drop one.
2. Call `ui_start(0)` at launch, `ui_tick()` for each scheduled frame and `ui_stop()` at
   the end.
3. Report events and the environment with the `ui_dispatch_*` and `ui_report_*` calls
   ([protocol](protocol.md)), on the UI thread.
4. Lay the tree out with Yoga (or the platform's flexbox) using the props.
5. Pass the conformance scene ([testing](testing.md)).
