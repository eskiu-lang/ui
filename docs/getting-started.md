# Getting started

## Install the tool

The `ui` tool is part of this package, written in Eskiu:

```bash
git clone https://github.com/eskiu-lang/ui
eskiuc ui/tools/ui/main.esk -o ~/.eskiu/bin/ui      # any directory on the PATH
```

What each platform needs:

| Target | Needs |
|---|---|
| `web` | emscripten (`emcc`) |
| `macos` | Xcode's command-line tools |
| `linux` | `gcc` or `clang`, `pkg-config`, `libwebkit2gtk-4.1-dev` (or `4.0`) |
| `windows` | MSVC (`cl`) or MinGW-w64 (`g++`, or `x86_64-w64-mingw32-g++` from macOS or Linux); the WebView2 SDK is fetched for you |
| `ios` | Xcode, with an iPhone simulator |
| `android` | the Android SDK with an NDK, build tools and a platform (`ANDROID_HOME`, `ANDROID_NDK_HOME`), and an emulator or a device |

The native hosts lay out with Yoga 3.2.1; the tool fetches it with git and compiles it
once per toolchain into `$ESKIU_HOME/cache` (`~/.eskiu/cache`).

## A first app

```bash
ui new myapp          # a project that depends on github.com/eskiu-lang/ui
cd myapp
ui run web            # serves build/ui/web and opens the browser (UI_PORT to change the port)
ui run macos          # a WKWebView window
ui run linux          # a WebKitGTK window
ui run windows        # a WebView2 window
ui run ios            # the simulator; boots the first iPhone when none is running
ui run android        # a device, or the first emulator it finds
```

`ui new NAME --ui PATH` depends on a local checkout instead of GitHub. `ui build TARGET`
builds without running.

## The development loop

```bash
ui serve              # the web: builds, serves, and reloads the page after each change
ui serve macos        # the same in the macOS window
ui serve linux        # the same in the Linux window
ui serve windows      # the same in the Windows window
ui serve ios          # each change installs and launches the app on the simulator again
ui serve android      # the same on the device or emulator
```

`ui serve` watches what a build reads: `src/`, `native/`, `assets/` and `ui.toml` of the
app and of every package it depends on, and `eskiu.toml` and `eskiu.lock`. After a change it
builds again, with the app's code at `-O0` and the host, the native code and Yoga reused when
they did not change. On the web and in the macOS window, the page reloads once the build is
done, keeping its route and its saved data. On the simulator and on Android the running app
starts again on the new code in the same process, without being installed and launched again,
so it starts from its first screen; a change to the host, the native code or the assets
installs it again. A change takes about 0.3 s to show on iOS, 0.4 s on Android and 0.6 s on the
web for the kit gallery. A failed build shows the compiler's errors over the app on the web
and in the terminal; the app keeps running the last build.

`src/main.esk` is the app:

```eskiu
import <string>;
import <ui>;

void App() {
    Column(Style{grow: 1, padding: 24, gap: 16, justify: JustifyCenter, align: AlignCenter}) {
        *Signal<int> count = use_signal<int>(0);
        TextWith(Style{font_size: 28, font_weight: 700}, "myapp");
        String label;
        defer String_free(&label);
        String_format(&label, "Pressed %d times", count.get());
        Text(label.data);
        ButtonWith(Style{background: rgb(79, 70, 229), color: rgb(255, 255, 255), radius: 12, padding: 12},
            "Press me") { count.set(count.get() + 1); }
    }
}

// The hosts call these: mount the app, and free what it owns when it stops.
void ui_app_start(int which) { ui_mount(void() { App(); }); }
void ui_app_stop() {}
```

A component is a function; the elements it calls (`Column`, `Text`, `ButtonWith`) build the
view, and the block passed to a container is its content. See [State](state.md) for
signals and [Components](components.md) for what there is.

## Xcode and Android Studio

```bash
ui open ios           # the app's Xcode project, opened in Xcode
ui open android       # its Gradle project, opened in Android Studio
```

Run, debug and profile there as with any app: the native debugger, breakpoints in native code,
Instruments, the Android profiler, a phone of your own. Each Run builds the Eskiu code first
(a build step that runs `ui object`), so an edit to a `.esk` file is in the next Run, with no
command in between; a Debug build compiles it at `-O0`. On iOS the project is signed for your
team (`[ios] team` in `ui.toml`, or the only team this Mac has a certificate for), so it runs
on a device; without one it runs on the simulator.

The projects live in `build/ui/ios/xcode/` and `build/ui/android/gradle/` and are made again
by `ui open`: run it again after changing `ui.toml` or adding native code. Changes made in the
IDE's project settings do not last; put them in `ui.toml`. `ui serve` stays the fastest loop
for the views; the IDEs are for the native side and for devices.

## A project's layout

```
myapp/
  eskiu.toml          the package, with ui as a dependency
  eskiu.lock          the resolved dependencies (commit it)
  ui.toml             optional: the app's name, id, icon and version, frameworks, permissions, native dependencies
  native/             optional: code per platform (native/web, native/ios, native/android)
  src/main.esk        the app: ui_app_start and ui_app_stop
  assets/             files the app loads by name (Image("logo.png") ...), shipped on every platform
  build/ui/           what `ui build` makes: web/, macos/, linux/, windows/, ios/, android/ (ignored by git)
```

`ui_app_start(which)` runs when a host starts the app (which is 0 unless a host was told
otherwise); it mounts the root and can seed the app's model. `ui_app_stop()` runs when the
host stops it, after the tree is gone, to free what the model owns.

## The examples

Each directory in `examples/` is a project like the one above:

| Example | Shows |
|---|---|
| `counter` | one signal, one text, one button |
| `todo` | keyed rows with their own state, a model-owned signal, a text field, a scroll |
| `tasks` | a whole app: header, progress card, filters, a styled field, a virtual list of cards |
| `kit-gallery` | every `<ui/kit>` component |
| `native` | a native module and a native view on every platform ([native code](native.md)) |
| `navigation` | screens and pages: a list, a page per item, links ([navigation](navigation.md)) |

```bash
cd examples/tasks && ui run ios
```
