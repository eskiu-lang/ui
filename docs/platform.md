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
    ui_mount(void() {
        Insets insets = ui_insets();
        Column(Style{grow: 1, padding: insets.top, background: kit.page}, void() { ... });
    });
}
```

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

## Modals

```eskiu
Modal(open.get(), void() { open.set(false); }, void() {
    Column(Style{grow: 1, padding: 24, background: kit.surface}, void() { ... });
});
```

While `visible`, the block is drawn on a layer over the whole window. Back on Android and
Escape on the web call `on_request_close`; the app decides whether it closes.

## Lifecycle

A host calls `ui_start(which)` when the app starts: it creates the runtime, calls the app's
`ui_app_start(which)` and renders the first frame. `ui_stop()` unmounts the tree, shuts the
runtime down and calls `ui_app_stop()`, where the app frees its model. Events that arrive
after the stop are ignored.

## Native code

`use_native_event(channel, handler)` receives what native code posts on a channel;
`NativeView(style, type, on_event)` draws a view the app registered with a host. Eskiu calls
native modules through `extern` functions. See [native code](native.md).
