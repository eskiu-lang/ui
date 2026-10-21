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
ui run ios            # the simulator; boots the first iPhone when none is running
ui run android        # a device, or the first emulator it finds
```

`ui new NAME --ui PATH` depends on a local checkout instead of GitHub. `ui build TARGET`
builds without running.

`src/main.esk` is the app:

```eskiu
import <string>;
import <ui>;

void App() {
    Column(Style{grow: 1, padding: 24, gap: 16, justify: JustifyCenter, align: AlignCenter}, void() {
        *Signal<int> count = use_signal<int>(0);
        TextWith(Style{font_size: 28, font_weight: 700}, "myapp");
        String label;
        defer String_free(&label);
        String_format(&label, "Pressed %d times", count.get());
        Text(label.data);
        ButtonWith(Style{background: rgb(79, 70, 229), color: rgb(255, 255, 255), radius: 12, padding: 12},
            "Press me", void() { count.set(count.get() + 1); });
    });
}

// The hosts call these: mount the app, and free what it owns when it stops.
void ui_app_start(int which) { ui_mount(void() { App(); }); }
void ui_app_stop() {}
```

A component is a function; the elements it calls (`Column`, `Text`, `ButtonWith`) build the
view, and the block passed to a container is its content. See [State](state.md) for
signals and [Components](components.md) for what there is.

## A project's layout

```
myapp/
  eskiu.toml          the package, with ui as a dependency
  eskiu.lock          the resolved dependencies (commit it)
  ui.toml             optional: the app's name and id, frameworks, permissions, native dependencies
  native/             optional: code per platform (native/web, native/ios, native/android)
  src/main.esk        the app: ui_app_start and ui_app_stop
  assets/             files the app loads by name (Image("logo.png") ...), shipped on every platform
  build/ui/           what `ui build` makes: web/, macos/, ios/, android/ (ignored by git)
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

```bash
cd examples/tasks && ui run ios
```
