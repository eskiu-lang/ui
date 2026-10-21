# The platform

The host reports the environment to the core; reading it through these functions
subscribes the block, so it renders again when the value changes.

## Insets and the keyboard

```eskiu
Insets insets = ui_insets();   // top, right, bottom, left, keyboard, in points
```

`Insets` holds the system bars and display cutouts on each side and the height of the
software keyboard over the bottom. By default the host puts the root inside the safe area
and above the keyboard, and the insets are only information. An app that draws under the
bars sets the edge-to-edge policy in `ui_app_start` and pads itself:

```eskiu
void ui_app_start(int which) {
    ui_edge_policy = 1;
    ui_mount() {
        Insets insets = ui_insets();
        Column(Style{grow: 1, padding: insets.top, background: kit.page}) { ... }
    }
}
```

## Light and dark

```eskiu
bool dark = ui_color_scheme() == SchemeDark;
```

`ui_color_scheme()` is the system's appearance, `SchemeLight` or `SchemeDark`, and it changes
while the app runs (the activity is not recreated on Android). `<ui/kit>` has a light and a
dark theme: `kit_follow_system()` at the top of the root block sets `kit` to the one the
system asks for, and the root renders again when it changes.

```eskiu
void App() {
    kit_follow_system();
    StatusBar(kit.dark ? BarLight : BarDark, false);
    Column(Style{grow: 1, background: kit.page}) { ... }
}
```

## Accessibility

`ui_screen_reader()` is true while VoiceOver or TalkBack runs, `ui_reduce_motion()` when the
user asked for less motion (on Android, animations turned off), and `ui_font_scale()` is the
text size the user chose, in percent (100 by default). The web reports reduced motion from
its media query and no screen reader.

## The platform and the language

`ui_platform()` is `PlatformIos`, `PlatformAndroid` or `PlatformWeb` (macOS runs the web
host), known when the app is compiled; `ui_platform_name()` spells it. `ui_locale()` is the
user's language as a BCP 47 tag (`"es-MX"`), and `ui_is_rtl()` says whether it is written
right to left. Layout does not mirror for a right-to-left language yet.

## The window

`ui_window()` gives the window's `width` and `height` in points and its `scale` (pixels per
point, times 100: 300 on a 3x phone). A layout that changes with the size reads it:

```eskiu
bool wide = ui_window().width >= 700;
```

## Foreground

`ui_is_active()` is false while the app is in the background on iOS and Android. A web page
is always active.

## The status bar

```eskiu
StatusBar(BarLight, false);
```

The content style (`BarDefault`, `BarLight` for light content on a dark background,
`BarDark`) and whether the bar shows. The last `StatusBar` rendered wins; it has no view and
does nothing on the web.

## Back

```eskiu
use_back(bool() {
    if (!editing.get()) { return false; }
    editing.set(false);
    return true;
});
```

Android's back button (and gesture) first asks the newest open `Modal` to close, then calls
the back handlers from the newest registered; a handler returns true when it handled the
press. When none did, the platform goes back (the activity finishes). On the web, Escape
closes the newest modal.

## Links

A `Navigator` turns links into routes ([navigation](navigation.md)).

`ui_open_url(url)` opens a URL with the platform: the browser, or the app that handles it.
`ui_link()` is the link the app was opened with or was last sent, or null: a deep link on
iOS and Android, the page's URL on the web (reported again when its hash changes).

## Alerts

```eskiu
Alert("Delete task?", "This cannot be undone.", "Cancel\nDelete", void(int choice) {
    if (choice == 1) { remove_task(id); }
});
```

A native alert (`UIAlertController`, `AlertDialog`, a `<dialog>` on the web). The handler
gets the index of the button pressed, or -1 when the alert was dismissed without one. Alerts
are commands: they reach the host with the next commit.

## System actions

| Call | What it does |
|---|---|
| `ui_keyboard_dismiss()` | Hides the software keyboard; the focused field loses focus. |
| `ui_share(text, url)` | The system's share sheet with the text and an optional URL (`null`: none). The web uses `navigator.share`, or copies the text where there is none. |
| `ui_clipboard_set(text)` | Puts text in the clipboard. |
| `ui_clipboard_get(fn(string)->void)` | What the clipboard holds (`""` for nothing), as an event. |
| `ui_haptic(Haptic)` | A touch of feedback: `HapticSelection`, `HapticLight`, `HapticMedium`, `HapticHeavy`, `HapticSuccess`, `HapticWarning`, `HapticError`. A short vibration on the web, where the device has one. |
| `ui_announce(text)` | Reads the text aloud with the screen reader, if one runs (a live region on the web). |

Like alerts, these are commands: they reach the host with the next commit.

```eskiu
Button("Copy link") {
    ui_clipboard_set(link);
    ui_haptic(HapticSuccess);
    ui_announce("Link copied");
}
```

## The network

```eskiu
*Signal<int> status = use_signal<int>(0);
Button("Refresh") {
    ui_fetch(FetchRequest{url: "https://api.example.com/today", headers: "Accept: application/json"},
        void(FetchResult r) {
            if (r.status == 0) { println(r.error); return; }
            status.set(r.status);
            JsonValue* doc = json_parse(r.body);   // <json>
            ...
        });
}
```

`ui_fetch(request, on_done)` sends a request through the platform: `NSURLSession` on iOS,
`HttpURLConnection` on Android, the browser's `fetch` on the web. TLS, proxies and
certificates are the system's, and nothing else has to be linked. A `FetchRequest` has
`url`, `method` (`"GET"` when null), `headers` (one `"Name: value"` per line), `body` (sent
unless null) and `timeout_ms` (0 for the platform's). The handler runs on the UI thread with
a `FetchResult`: `status` (0 when the request failed, with `error` saying why), `body` as text
and the response `headers`, borrowed for the call.

Android apps get the `INTERNET` permission, and builds allow plain HTTP for development
(`usesCleartextTraffic`); iOS allows plain HTTP to the local network. On the web, a request
to another origin needs that server's CORS headers.

## Modals

```eskiu
Modal(open.get(), void() { open.set(false); }) {
    Column(Style{grow: 1, padding: 24, background: kit.surface}) { ... }
}
```

While `visible`, the block is drawn on a layer over the whole window. Back on Android and
Escape on the web and a tap on the backdrop call `on_request_close`; the app decides whether
it closes. `Overlay` is the same layer with no dim, for menus and popovers.

## Lifecycle

A host calls `ui_start(which)` when the app starts: it creates the runtime, calls the app's
`ui_app_start(which)` and renders the first frame. `ui_stop()` unmounts the tree, shuts the
runtime down and calls `ui_app_stop()`, where the app frees its model. Events that arrive
after the stop are ignored.

## Native code

`use_native_event(channel, handler)` receives what native code posts on a channel;
`NativeView(style, type, on_event)` draws a view the app registered with a host. Eskiu calls
native modules through `extern` functions. See [native code](native.md).
