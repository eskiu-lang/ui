# Protocol

The core and a host speak through two things: a commit, one byte buffer per frame from the
core to the host, and calls from the host to the core for events and the environment. This
page is for writing or changing a host; apps never see it.

## Commits

A commit is a sequence of records, little-endian. Each record starts with an op byte and a
node id (`u32`):

| Op | Name | Then |
|---|---|---|
| 1 | create | generation `u32`, kind `u8` |
| 2 | prop | prop `u8`, value `i32` |
| 3 | text | length `u32`, the bytes, a NUL |
| 4 | insert | parent `u32`, before `u32` (0: at the end) |
| 5 | move | parent `u32`, before `u32` |
| 6 | remove | nothing |
| 7 | listen | nothing: the node now has a handler |
| 8 | unlisten | nothing |
| 9 | string prop | prop `u8` (1 label, 2 placeholder, 3 image source, 4 native view type), length `u32`, the bytes, a NUL |
| 10 | command | the id is a request id, not a node: command `u8` (1 alert: title, message, then the buttons; 2 open a URL; 3 the web's history: `push` route, `replace` route, `back`, `reset` routes...; 4 hide the keyboard; 5 share: text, URL; 6 copy: text; 7 paste, answered by `ui_dispatch_answer`; 8 haptic: its kind; 9 announce: text), count `u8`, then that many strings, each a length `u32`, the bytes and a NUL |
| 11 | named prop | a native view's prop: the name (length `u32`, bytes, NUL), then the value (the same) |

A native view gets its type as string prop 4 when it is created (again if it changes), then
its named props as op 11. The host makes the view the type's factory registered and puts it
inside the node's box ([native code](native.md)).

A node's removal comes before its descendants', so a host can keep a leaving screen's
content while it slides out.

The buffer belongs to the core: the host reads it during `apply` and copies what it keeps.
`hosts/commit.h` is a C decoder for native hosts; `ui_decode` in `src/core/wire.esk` decodes
it into `Mutation` records for tests and tools.

### Kinds

| Kind | Element | Kind | Element |
|---|---|---|---|
| 0 | scope (no view) | 7 | image |
| 1 | column | 8 | pressable (a column that takes presses) |
| 2 | row | 9 | switch |
| 3 | text | 10 | activity indicator |
| 4 | button | 11 | status bar (no view) |
| 5 | text input | 12 | modal (a column on a layer over the window, laid out on its own) |
| 6 | scroll | 13 | native view (registered with the host by type) |
| | | 14 | screen (a column filling its navigator; only the active one shows) |
| | | 15 | slider |

### Int props

| Prop | Meaning | Prop | Meaning |
|---|---|---|---|
| 1 | gap | 12 | border width |
| 2 | padding | 13 | border color |
| 3 | focused (1 focus, 0 blur) | 14 | radius |
| 4 | width | 15 | opacity (percent) |
| 5 | height | 16 | align (`Align`) |
| 6 | grow | 17 | justify (`Justify`) |
| 7 | wrap (1 or 0) | 18 | switch on (1 or 0) |
| 8 | background (RGBA, 0: default) | 19 | spinning (1 or 0) |
| 9 | text color (RGBA, 0: default) | 20 | status bar style (`BarStyle`) |
| 10 | font size | 21 | status bar hidden (1 or 0) |
| 11 | font weight | 22 | refreshing (a scroll with a RefreshControl) |
| | | 23 | active (1 or 0: the screen on top of its navigator) |
| | | 24 | absolute (1 or 0) |
| | | 25 to 28 | left, top, right, bottom (points; -1000000: not set) |
| | | 29 | aspect ratio (width over height, in thousandths; 0: none) |
| | | 30 | layout listener (1: report the node's frame when it changes) |
| | | 31 | hover listener (1: report a pointer entering and leaving) |
| | | 32, 33, 34 | slider value, minimum, maximum (the range comes first) |

A text input's value is its text (op 3). Sizes are points.

## Completion

The host tells the core it applied a commit with `ui_complete(sequence)`, during `apply` or
later. Until then the core does not render another frame, and it keeps everything the
commit retired (old handlers, removed nodes and their strings) alive, so the host can still
deliver an event for a node the commit removed. Events posted while a commit is in flight
are queued, their text copied, and delivered in order after completion.

The bridge (`hosts/bridge.esk`) completes each commit as soon as `ui_host_commit` returns,
since every host applies synchronously.

## Ids and generations

Node ids are small integers, reused once the commit that removed a node completes. Each
reuse takes a new generation, sent with `create`, and every event carries the id and the
generation it was sent for. An event whose generation is not the node's current one is
dropped, so an event for a removed node never reaches the node that took its id.

## Events

The host calls the bridge, on the UI thread:

| Call | Event |
|---|---|
| `ui_dispatch(id, gen)` | a press (button, pressable; on a modal: a request to close) |
| `ui_dispatch_text(id, gen, text)` | an input's text changed (read during the call) |
| `ui_dispatch_focus(id, gen, focused)` | an input gained (1) or lost (0) focus |
| `ui_dispatch_toggle(id, gen, on)` | a switch was toggled |
| `ui_dispatch_scroll(id, gen, offset, viewport)` | a scroll moved, in points |
| `ui_dispatch_refresh(id, gen)` | the user pulled a scroll's refresh control |
| `ui_dispatch_alert(request, choice)` | an alert was answered (-1 dismissed) |
| `ui_dispatch_native(channel, payload)` | a native module posted an event (`use_native_event`) |
| `ui_dispatch_view(id, gen, name, payload)` | a native view sent an event (`NativeView`'s `on_event`) |
| `ui_dispatch_layout(id, gen, x, y, width, height)` | a listened node's frame changed (points, relative to its parent) |
| `ui_dispatch_answer(request, text)` | the text a request asked for (a paste) |
| `ui_dispatch_value(id, gen, value)` | the user moved a slider |
| `ui_dispatch_hover(id, gen, inside)` | a pointer entered (1) or left (0) a node with a hover handler |
| `ui_dispatch_timer(token)` | a timer the core armed fired |
| `ui_dispatch_route(route, index)` | the browser went back or forward to `route`, at `index` in the app's history entries |
| `ui_report_back()` | Android's back: returns 1 when the app handled it |
| `ui_report_insets(top, right, bottom, left, keyboard)`, `ui_report_window(w, h, scale)`, `ui_report_active(active)`, `ui_report_link(url)` | the environment |
| `ui_report_appearance(dark)`, `ui_report_accessibility(reader, reduce_motion, font_scale)`, `ui_report_locale(tag, rtl)` | the system's settings |
| `ui_tick()` | render the frame the host scheduled |

After a call that leaves work, the bridge calls the host's `ui_host_schedule()`, and the host
calls `ui_tick()` on its next frame (`requestAnimationFrame`, a `CADisplayLink`, the
`Choreographer`). `ui_host_cancel()` drops a scheduled frame at stop.

The core also asks the host for timers (`use_interval`, `use_timeout`):
`ui_host_timer_start(token, ms, repeat)` arms one and `ui_host_timer_stop(token)` disarms it;
each tick comes back as `ui_dispatch_timer(token)`.

A refused edit or toggle is answered: the core renders the input's scope again and, if the
app kept the old value, sends it back, so what the view shows stays what the app rendered.
