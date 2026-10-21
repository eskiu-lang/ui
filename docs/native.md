# Native code

An app, or a package it depends on, can ship code per platform: modules that Eskiu calls
and that send events back, and views the layout places like any element. `ui build` finds
it in `native/` and builds it in, and `ui.toml` says what else the platform needs:
frameworks, permissions, plist entries, Maven libraries, Swift packages.

`examples/native` has a clock module (ticks from native code) and a slider view, on every
platform:

```
examples/native/
  src/clock.esk               the Eskiu side: externs, use_native_event, NativeView
  native/web/clock.js         the clock as an emscripten library
  native/web/slider.mjs       the slider: <input type=range>
  native/ios/clock.m          the clock over an NSTimer
  native/ios/slider.m         the slider: UISlider
  native/android/clock.c      the clock over a thread
  native/android/Slider.java  the slider: SeekBar
```

## The Eskiu side

A module is a set of `extern` functions, defined by each platform's code:

```eskiu
extern void clock_start(int ms);
extern void clock_stop();
```

Native code answers through events on a channel. `use_native_event` handles them while
its scope is mounted; the payload is a string, borrowed for the call:

```eskiu
*Signal<int> ticks = use_signal<int>(0);
use_native_event("clock", void(string payload) { ticks.set(ticks.get() + 1); });
use_effect(void() {
    clock_start(1000);
    on_cleanup(void() { clock_stop(); });
});
```

A view is a `NativeView` with the type the platforms registered. It is a leaf, so give it a
size. Its props are named strings, given with `ui_native_prop` right before it; only the
ones that changed are sent, and one no longer given is sent as `""`. Its events reach
`on_event` as a name and a payload:

```eskiu
void NativeSlider(string label, int value, escaping fn(int)->void on_change) {
    ui_own(void() { ui_release_int(on_change); });
    String text;
    defer String_free(&text);
    String_format(&text, "%d", value);
    ui_native_prop("value", text.data);
    NativeView(Style{height: 44, label: label}, "slider", void(string name, string payload) {
        if (strcmp(name, "change") == 0) { on_change(atoi(payload)); }
    });
}
```

Wrapping the view in a component like this gives the rest of the app an ordinary
component with typed arguments.

## The web

| File | What it is |
|---|---|
| `native/web/*.js` | Emscripten libraries (`--js-library`): they define the externs. `Module.uiEmit(channel, payload)` posts an event. |
| `native/web/*.mjs` | ES modules; each one's `setup(ui)` runs before the app starts. `ui.registerView(type, factory)` registers a view, `ui.emit(channel, payload)` posts an event. |

```js
mergeInto(LibraryManager.library, {
  clock_start: function(ms) {
    Module.clockTimer = setInterval(() => Module.uiEmit('clock', 'tick'), ms);
  },
  clock_stop: function() { clearInterval(Module.clockTimer); },
});
```

A view factory's `create(emit)` returns `{element, set(name, value), dispose()}` (`set` and
`dispose` are optional). The element fills the box the layout gives the `NativeView`, and
`emit(name, payload)` sends an event to `on_event`:

```js
export function setup(ui) {
  ui.registerView('slider', {
    create(emit) {
      const input = document.createElement('input');
      input.type = 'range';
      input.addEventListener('input', () => emit('change', input.value));
      return {element: input, set(name, value) { if (name === 'value') input.value = value; }};
    },
  });
}
```

macOS runs the web host, so it uses the web's native code.

## iOS

| File | What it is |
|---|---|
| `native/ios/*.m`, `*.mm` | Objective-C and Objective-C++, compiled with ARC and `ui_native.h` on the include path. |
| `native/ios/*.swift` | Swift; it sees `ui_native.h` through the bridging header. The app is then built through Xcode (below). |

`ui_native.h` (in `hosts/ios/`) declares:

```objc
void ui_native_emit(const char *channel, const char *payload);   // any thread
void ui_register_view(NSString *type, UIView *(^create)(UiViewEmit emit),
                      void (^set)(UIView *view, NSString *name, NSString *value));
#define UI_NATIVE_SETUP(name) ...   // a function that runs before the app starts
```

```objc
UI_NATIVE_SETUP(slider) {
    ui_register_view(@"slider", ^UIView *(UiViewEmit emit) {
        UISlider *slider = [UISlider new];
        // ... call emit(@"change", value) when it moves
        return slider;
    }, ^(UIView *view, NSString *name, NSString *value) {
        if ([name isEqualToString:@"value"]) ((UISlider *)view).value = value.floatValue;
    });
}
```

Swift has no setup functions. A class named with `@objc(Name)` that has an
`@objc static func setup()` is called at launch instead: the tool lists each Swift file's
class in the app's Info.plist.

```swift
@objc(SliderSetup) class SliderSetup: NSObject {
    @objc static func setup() {
        ui_register_view("slider", { emit in ... }, { view, name, value in ... })
    }
}
```

The view fills the `NativeView`'s box (its autoresizing mask follows the box).

## Android

| File | What it is |
|---|---|
| `native/android/*.c`, `*.cpp` | C and C++, compiled into the app's library with `ui_native.h` on the include path: they define the externs. |
| `native/android/*.java`, `*.kt` | Java and Kotlin. Each class's `public static void setup()` (in Kotlin, an object's `@JvmStatic fun setup()`) runs before the app starts. Kotlin builds through Gradle (below). |

From Java and Kotlin, `org.eskiu.ui.UiActivity` has:

```java
UiActivity.registerView(String type, UiActivity.ViewFactory factory);
UiActivity.emit(String channel, String payload);   // any thread
UiActivity.current();                              // the activity, for a Context
UiActivity.requestPermissions(String[] permissions, Consumer<Boolean> done);   // runtime permissions
```

```java
public final class Slider {
    public static void setup() {
        UiActivity.registerView("slider", new UiActivity.ViewFactory() {
            @Override public View create(Context context, UiActivity.ViewEvents events) {
                SeekBar bar = new SeekBar(context);
                // ... events.emit("change", value) when it moves
                return bar;
            }
            @Override public void set(View view, String name, String value) { ... }
        });
    }
}
```

From C, `ui_native.h` (in `hosts/android/`) has `ui_native_emit(channel, payload)` (any
thread), `ui_jni_env()` (this thread's `JNIEnv`, attached on first use), `ui_find_class(name)`
(an app class through the app's class loader, which `FindClass` does not use on a thread
native code started) and `ui_activity()`.

## Threads

The core runs on the UI thread. `ui_native_emit` and `UiActivity.emit` may be called from
any thread: they post the event to the UI thread. A view's `emit` is meant for the UI
thread, where views live; called from another thread, it is posted there too. On the web
there is one thread.

## `ui.toml`

Next to `eskiu.toml`, in the app and in any package. The tool merges every package's file;
`[app]` is read only from the app's.

```toml
[app]
name = "Tasks"                   # the name people see (default: the package's name)
id = "com.example.tasks"         # bundle id and Android package (default: org.eskiu.app.NAME)
scheme = "tasks"                 # links tasks://route open the app (see navigation.md)

[web]
base = "/tasks/"                 # where the page is served from (default "/")

[ios]
frameworks = ["CoreLocation"]    # system frameworks to link

[ios.plist]
NSLocationWhenInUseUsageDescription = "Shows tasks near you"

[ios.spm]
Collections = { url = "https://github.com/apple/swift-collections", from = "1.1.0" }

[android]
permissions = ["android.permission.ACCESS_FINE_LOCATION"]
dependencies = ["com.google.android.material:material:1.12.0"]
```

Values are strings and arrays of strings, and a table's key may take an inline table of
them. A Swift package takes `url` and one of `from` (up to the next major version), `exact`
or `branch`; its products are `products = [...]`, or the key's name.

## Dependencies: Gradle and Xcode

Without third-party libraries the tool builds directly with the platform's compilers, with
no downloads. Third-party libraries need the platform's build system:

- **Android.** `[android] dependencies` (Maven coordinates), or Kotlin code, builds through
  Gradle: the tool writes a project in `build/ui/android/gradle/` around the library it
  compiled and runs `gradle assembleDebug`. An AAR's resources and manifest are merged as
  in any Android app. It needs Gradle 9 and a JDK 17; `[android] agp` picks the Android
  Gradle plugin's version (9.4.1 by default). `UI_GRADLE=1` takes this path always.
- **iOS.** `[ios.spm]` packages, or Swift code, build through Xcode: the tool writes
  `build/ui/ios/xcode/NAME.xcodeproj` with the packages, the host and the native sources,
  links the app's object and Yoga's, and runs `xcodebuild` for the simulator. `UI_XCODE=1`
  takes this path always.

Both projects open in Android Studio and Xcode, to debug the native side.

## Packages

A package can wrap a native library for every app: its `native/` and its `ui.toml` travel
with it. An app that depends on it (`eskiuc add maps --git ...`) gets its code, its
frameworks, permissions and Maven or Swift dependencies, and imports its components:

```
maps/
  eskiu.toml
  ui.toml                  [android] dependencies, [ios.spm] packages
  src/maps.esk             MapView(...), a component over NativeView
  native/ios/MapView.swift
  native/android/MapView.kt
  native/web/map.mjs
```

[eskiu-lang/ui-location](https://github.com/eskiu-lang/ui-location) is one: `use_location`
over CoreLocation, `LocationManager` (with a runtime permission and C externs that call Java
through JNI) and `navigator.geolocation`, a `ui.toml` with the framework, a usage text the app
can replace and the Android permissions, tests that run on the computer with stand-ins for the
native side, and an example app. A package like it starts and stops its service with
`use_mount` ([state](state.md)), so the service runs while a component that uses it is
mounted.

## Testing

`tests/web.mjs` runs the native example's web code against the recording DOM;
`tools/check_native.py` runs it on the iOS simulator and an Android device: ticks from the
module, the slider's initial value, a moved slider, and a value sent back.
