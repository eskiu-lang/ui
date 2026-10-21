# Architecture

## Layers

```
 app            components: functions that call elements and hooks
 kit            <ui/kit>: themed components built only from elements
 elements       <ui>: Column, Text, TextInput, FlatList, Modal, NativeView...
 core           the tree, reactivity, frames and commits, the wire format
 ── commit (bytes) ──▶                     ◀── events (calls) ──
 hosts          web (DOM), iOS (UIKit + Yoga), Android (android.view + Yoga), macOS (WKWebView)
 tool           tools/ui: new, build, run, serve; compiles the app and links it into each host
```

Everything above the commit line is Eskiu and the same on every platform. A host only
applies commits and reports events, so the platforms differ in how they draw, not in what
the app does.

## A frame

1. **Something changes.** A handler sets a signal, or the host reports an event or a new
   environment (insets, window, foreground). Each signal knows the blocks that read it;
   a change schedules those blocks for the next frame.
2. **Render.** `ui_frame` runs the scheduled blocks, parents before children. A block
   calls elements; each element call is matched against the children the block emitted
   last time (by key, else by kind and position among unkeyed siblings of that kind).
   Matched nodes keep their identity and state; new ones are created and the ones not
   emitted again are unmounted. Only props that changed are written.
3. **Commit.** The mutations of the frame go into one byte buffer (the
   [protocol](protocol.md)). Commands asked for since the last frame (alerts, URLs to
   open) follow them. The host's `apply` gets the buffer.
4. **Apply.** The host decodes the buffer, updates its views and lays them out.
5. **Complete.** The host calls `ui_complete(sequence)`, during `apply` or later. Only then
   does the core free what the commit retired (old handlers, removed nodes, their strings),
   free the removed ids for reuse, run the effects of the scopes that committed, and deliver
   the events that arrived while the commit was in flight.

Writes made during a render are applied after the render phase, and a block that keeps
scheduling itself for 100 frames stops the program ("render loop"). Effects run after the
commit completes, parents first.

## Threading

There is one UI thread. Every host calls into the core on its main thread: the web from
the page's event loop and `requestAnimationFrame`, iOS from the main run loop and a display
link, Android from the UI thread's `Choreographer`, macOS from the webview. A native module
that works on another thread posts its result back to the UI thread before it calls
`ui_event_native` (see [the platform](platform.md)).

## Ownership

Closures given to elements belong to the node that took them. When a render gives a node
a new handler, the old one is freed once the commit that replaced it completes, so a
renderer never calls a freed handler. A component that keeps closures it was given
(a `Checkbox`'s `on_change`) hands their destructor to `ui_own`, which frees them with the
next element's block. Strings passed to elements are copied. See [state](state.md).

## The repository

| Path | What it is |
|---|---|
| `src/ui.esk` | `<ui>`, importing its parts in order |
| `src/style.esk` | `Style`, `key`, colors, `Align`, `Justify` |
| `src/core/` | `state` (the runtime global), `diagnostics`, `keys` (the key table), `wire` (commits and the decoder), `reactive` (signals, memos, effects, hooks), `closures` (freeing closures by type), `tree` (nodes, matching, moves, unmounting, `ui_element`), `runtime` (frames, commits, events) |
| `src/elements/` | `layout` (containers, `ui_on_layout`, `ui_on_hover`), `text`, `input` (fields, switches, sliders), `media`, `lists`, `overlay` (`Modal`, `Overlay`, `Alert`), `system`, `native`, `navigation`, `timers`, `gestures`, `animated` |
| `src/apis/` | `environment` (platform, appearance, accessibility, language), `actions` (keyboard, share, clipboard, haptics, announcements), `storage`, `network` (`ui_fetch`) |
| `src/state/strings.esk` | `use_string` |
| `src/kit.esk`, `src/kit/` | `<ui/kit>`: `theme` (light and dark), `display`, `buttons`, `forms`, `navigation`, `disclosure`, `swipe`, `overlays` (popovers, menus, tooltips, toasts), `pickers` (calendar, date, color) |
| `hosts/bridge.esk` | What every host calls (`ui_start`, `ui_dispatch_*`, `ui_report_*`, `ui_tick`, `ui_stop`) and the app's two entry points |
| `hosts/commit.h` | A C decoder of commits, shared by the native hosts |
| `hosts/core.h` | The core's entry points for the native hosts: linked, or loaded from the app's library in a reload build |
| `hosts/ios/ui_native.h`, `hosts/android/ui_native.h` | The C API native code includes ([native code](native.md)) |
| `hosts/web/`, `hosts/ios/`, `hosts/android/`, `hosts/macos/` | The hosts |
| `tools/ui/` | The `ui` command: `project` (the lock, the packages), `config` (`ui.toml`), `web`, `macos`, `yoga`, `ios`, `xcode`, `android`, `gradle`, `new`, `serve` (watch, build in a child, reload) |
| `examples/` | Projects: `counter`, `todo`, `tasks`, `kit-gallery`, `native`, `navigation` |
| `tests/` | The model tests, the host harness and the conformance scene ([testing](testing.md)) |

The parts of `<ui>` are not modules of their own: `src/ui.esk` imports them in an order
where each part sees the ones before it, and the runtime's globals live in
`src/core/state.esk`, imported first, since a global must be declared before it is used.
