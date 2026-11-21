# State

## Signals

```eskiu
*Signal<int> count = use_signal<int>(0);
Text(count.get() > 9 ? "many" : "few");
Button("+") { count.set(count.get() + 1); }
```

`use_signal<T>(initial)` gives the current scope a signal, created on the first render and
kept by position for as long as the scope is mounted. `get()` reads it and subscribes the
block that reads it; `set(v)` writes it, and every block that read it renders again in the
next frame. Setting the same value does nothing.

A signal holds a value with built-in equality: a number, a `bool`, a pointer. A pointer in
a signal is borrowed; the signal does not own what it points to. Text goes in `use_string`.

## A reload under `ui serve`

A change under `ui serve` reloads the app on every host (a new page on the web, a new core in
the running app on iOS and Android), and the app comes back where it was:

- the routes of the router the platform follows (the first `Navigator`'s), root first;
- each `use_signal` and `use_string` whose identity and type did not change. Its identity is
  its scope's path, as `ui agent state` prints it (`Column[0] > TodoRow[key 3]`), and its
  place among that scope's signals (the first is 0). A scope that moved, an element added
  before it among its siblings, or a signal added before it in its scope gives it another
  identity, and it starts from its initial value; so does one whose type changed (an `int` that
  became a `bool`), and the state of a component the edit removed is dropped.

```eskiu
*Signal<int> filter = use_signal_keyed<int>("tasks.filter", 0);
```

`use_signal_keyed(key, initial)` is a `use_signal` whose identity is `key` alone, so its value
survives moving the code to another scope or reordering the hooks. A release build has no
reloads and ignores the key.

Only values that mean the same in the new code are kept: integers (`int`, `int64`, `int16`,
`uint8`, `char`, `uint`, `uint64`), `bool`, `float`, `double`, and `use_string` text. A pointer,
a borrowed `string`, a struct, a closure or a native handle is not: what it points to belonged
to the old code, so its signal starts from its initial value, as a memo, an effect and a timer
do (they run again). Model signals (`signal_new`) are made by `ui_app_start`, which runs again.

What is kept is restored as each scope mounts, until the first input (a press, an edit, a
toggle, back): a scope that mounts after it is new. The kept state is text, one line per route
and signal (`src/core/reload.esk` has the format); it exists only in a dev build.

## Memos

```eskiu
*Signal<int> remaining = use_memo<int>(int() { return count_open(tasks); });
```

`use_memo<T>(compute)` is a derived value. It is computed lazily, when something reads it,
and its readers render again only when its value changes, not every time an input changes.
A memo must be pure: setting a signal from one stops the program ("memo must be pure"), and
two memos that read each other stop it as a cycle.

## Effects

```eskiu
use_effect() {
    int id = start_timer(count.get());
    on_cleanup() { stop_timer(id); }
}
```

`use_effect(callback)` runs after each commit of its scope that changed something it read,
parents first. `on_cleanup` (once per run) is called before the next run and when the scope
unmounts.

## Saved data

```eskiu
string theme = ui_storage_get("theme");          // null until it is saved
ui_storage_set("theme", "dark");
ui_storage_remove("draft");
```

`ui_storage_get`, `ui_storage_set` and `ui_storage_remove` keep small text values that outlive
the app: settings, a draft, the last tab. The store is loaded on first use and written whole
on each change, in a file in the app's data directory (on the web, the page's
`localStorage`), so it is for small data. `ui_data_dir()` is that directory (empty on the
web), reported before the app starts, so `ui_app_start` can read from it; `<fs>` works in it
for anything larger.

`ui_asset_path("sprites/atlas.webp")` is a file of the app's `assets/` as a path to open with
`<fs>` or hand to a package that reads files (`<canvas>` images, `<audio>` sounds): the
bundle's file on iOS, and on Android a copy taken out of the APK into the app's cache (once,
again after an update). The caller frees it. It is null on the web and the desktop, which
serve the assets by URL, and for a file that is not there.

## Mounting

```eskiu
use_mount(void() { sensor_start(); }, void() { sensor_stop(); });
```

`use_mount(on_mount, on_unmount)` calls `on_mount` once, when the scope first renders, and
`on_unmount` when the scope unmounts. It is for what lives exactly as long as a component, such
as a subscription to a sensor or a native module, which `use_effect` would stop and start again
after every render that changed something it read. The closures later renders pass are
dropped.

## Timers

```eskiu
*Signal<int> seconds = use_signal<int>(0);
*Signal<bool> running = use_signal<bool>(true);
use_interval(running.get() ? 1000 : 0) { seconds.set(seconds.get() + 1); }
use_timeout(3000) { hint.set(false); }
```

`use_interval(ms, handler)` calls `handler` every `ms` milliseconds while the scope is
mounted; a period of 0 or less pauses it, and a new period restarts it. `use_timeout(ms,
handler)` calls it once, `ms` after the scope mounts or after `ms` last changed. The host keeps
the clock (a `setInterval` on the web, a run loop timer on iOS, a `Handler` on Android), and a
tick reaches the handler on the UI thread like any event, so it may set signals.

## Display frames

```eskiu
*Signal<bool> playing = use_signal<bool>(true);
use_frame(playing.get(), void(Tick tick) {
    world_step(&world, tick.delta);
    world_draw(&world);
});
```

`use_frame(running, handler)` calls `handler` once per frame the display shows (a
`CADisplayLink` on iOS, the `Choreographer` on Android, `requestAnimationFrame` on the web)
while the scope is mounted and `running` is true. `Tick` has the frame's `time` and the
`delta` since the last frame run, in seconds (0 for the first frame after frames start). What
the handler draws itself (a `GpuView`, a canvas) shows in that frame; a signal it sets renders
with the next one. The display clock runs only while some running handler is mounted, and a
host without one gets frames from a 16 ms timer.

## The hooks rule

Hooks (`use_signal`, `use_memo`, `use_effect`, `use_string`, `use_back`,
`use_native_event`, `use_interval`, `use_timeout`, `use_frame`, `use_app_state`, `use_animated`, `use_animated_then`, `use_repeat`, `use_sequence`, `use_mount`) are matched by the order a block calls
them in: the first hook a block calls is slot 0, the next slot 1, and so on, and a hook finds
its state again at its slot on the next render.

A branch may change the hooks: `if (ar) ArView(); else View();` with two components that use
different hooks, or a hook only one branch calls. When a render reaches another hook at a
slot (another kind, or the same kind from another place in the code), or calls fewer or more
hooks than the last render, the hooks from that slot on start afresh: the old ones unmount
(their cleanups run, their state is freed) and the new ones mount, as if one component left
and another came. The hooks before that slot keep their state. A branch that should keep its
own state while the other shows, or keep a hook's state when hooks before it change, goes in
its own `Scope` (or keyed element), which has its slots to itself.

## Blocks run again

A block runs again whenever a signal it read changes, long after the call that wrote it:
everything it captured must still be there then. Keep what a block reads in state that lives
as long as the block (a signal, a model, a global or a cache) rather than in a list or string
freed after the first render; text, labels and sources passed to elements are copied, but a
block that indexes a freed list on its next run reads freed memory.

## Scopes and identity

Each element's block is a scope: its hooks belong to it, and it renders again on its own
when a signal it read changes. `Scope(name, key, block)` makes a component's scope
explicit and names it in diagnostics:

```eskiu
void TodoRow(*Todo todo) {
    Scope("TodoRow", key(todo.id)) {
        *Signal<bool> editing = use_signal<bool>(false);
        ...
    }
}
```

`key(id)` keeps a child's node, and so its state, when its siblings are added, removed or
reordered. Without a key, children of a block match by position among the unkeyed siblings
of the same kind, so `if (saving) { Text("Saving"); }` does not disturb a `Column` next to
it. Keys must be unique among siblings: a duplicate stops the program, in a dev build and in a
release build alike, so a test or `ui agent` finds it before users do. An app that would
rather keep running sets `ui.lenient = true;` in `ui_app_start`: a duplicate then prints a
warning and the later child is treated as unkeyed. Key 0 is a key.

When the program stops (a duplicate key, a hook outside a render), the message says where:

```
ui: duplicate key 3 at child 1
  in Column[0] > TodoRow[key 3]
```

## Model signals

State that belongs to the app rather than to a screen is a model signal:

```eskiu
*Signal<int> total;

void ui_app_start(int which) {
    total = signal_new<int>(0);
    ui_mount() { App(); }
}

void ui_app_stop() { total.free(); }
```

`signal_new<T>` makes a signal no scope owns. It outlives any mount, works before the
runtime exists, and its owner frees it with `free()` after the UI that reads it is gone
(the hosts call `ui_app_stop` after the shutdown).

## Lists

A list of things the app shows is a `ListSignal`: a `List` whose readers render again when
it changes, so a model needs no counter of its own to say that it did.

```eskiu
struct Todo { int id; bool done; string title; }
*ListSignal<Todo> todos;

void ui_app_start(int which) {
    todos = list_signal_new<Todo>();
    ui_mount() { App(); }
}

void App() {
    Column(Style{gap: 8}) {
        *List<Todo> items = todos.items();
        for (i in 0..items.size) { Text(items.data[i].title); }
        Button("Add") { todos.push(Todo{todos.size(), false, ui_copy("New")}); }
    }
}
```

Reading it (`size()`, `get(i)`, `items()`) subscribes the reader. `push`, `insert`,
`remove`, `set`, `swap` and `clear` change it and render its readers again; after editing
an element in place (`todos.items().data[i].done = true`), `changed()` tells them. As with a
signal, a change during a render waits until the render ends. `use_list<T>()` is the same
for a list a scope owns, freed when it unmounts; `list_signal_new<T>()` is for the model, and
its owner frees it with `free()`. The list owns its elements' storage, not what they point
at: free an element's strings before removing it or freeing the list.

## Strings

```eskiu
*StringState draft = use_string("");
TextInput_bind(Input{placeholder: "New task"}, draft);
Button("Add") { if (!draft.empty()) { add(draft.get()); draft.set(""); } }
```

`TextInput_bind` is a field whose text is the state: typing sets it, and setting it changes
the field. `empty()` says whether the text is empty.

`use_string(initial)` holds immutable, reference-counted text (`UiString`). `get()` borrows
the text until the next `set()`. A handler that outlives the render and needs the text
takes a `snapshot()`, which retains it, and releases it in its cleanup.

Text given to an element (`Text`, a button's title, an input's value) is copied, so text
built for one frame needs nothing freed: `textf` formats it, and `frame_string()` gives an
empty `String` for an API that writes into one. Both live until the end of the frame.

```eskiu
Text(textf("%d left", remaining.get()));
*String date = frame_string();
time_format(time_now_s(), "%A, %B %d", TIME_LOCAL, date);
Text(date.data);
```

## Closures a component keeps

A handler given to an element is owned by the element's node and freed after the commit
that replaced or removed it. A component that takes a closure and only calls it from a
handler of its own (so no element owns it directly) hands it to `ui_own_fn` before its
first element:

```eskiu
void Checkbox(string label, bool checked, escaping fn(bool)->void on_change) {
    ui_own_fn(on_change);
    Pressable(Style{label: label}, void() { on_change(!checked); }) { ... }
}
```

The closure is freed with the next element's block, when that block is replaced or
unmounted; `ui_own_fn` takes a closure of any type. For other cleanup, `ui_own` takes a
destructor of its own.

## Errors

A throw out of the app's code does not close the app.

**In a render**, the nearest `ErrorBoundary` above the code that threw shows its fallback.
Nothing of the failed render reaches the host: its mutations are cut from the commit, the
nodes it made are freed (their ids are free again at once), and the boundary's previous
content is removed. The content's state (signals, memos, effects) starts over when it renders
again, and its cleanups and `use_mount` unmount callbacks run as at an unmount.

```eskiu
ErrorBoundary(void(UiError e) {
    Text("The chart could not be drawn.");
    Button("Try again") { ui_boundary_reset(e.boundary); }
}) {
    Chart(points);
}
```

A boundary shows its fallback until `ui_boundary_reset(e.boundary)`, or until it is made
again (a change of an enclosing `Scope`'s key). The root is a boundary too: an error outside
any other shows the root's fallback, a short message unless the app sets its own with
`ui_set_fallback`. A fallback that throws hands the error to the boundary above it, and the
root's fallback throwing stops the program.

**In a handler** (a press, a text change, a timer, a fetch's answer, a native event), the
error is reported and the event is done. What the handler wrote before the throw stays
written, as in JavaScript.

**In an effect or a cleanup**, the error is reported. An effect that throws keeps no cleanup,
and the other effects run.

`UiError` has `where` (1 a render, 2 a handler, 3 an effect or a cleanup), `path` (the scope
path, `Column[0] > TodoRow[key 3]`), `text` (the exception as `exception_text()` gives it:
`int 7`, `string boom`) and `boundary`. Its strings are borrowed for the call, and in a
fallback for its render. Every error goes to the handler given to `ui_on_error`, or to stderr
(the console on the web) without one. Under `ui serve` on the web it also shows over the app
until a click.

Some things cannot be undone: a `use_mount` callback or an effect cleanup that ran during the
failed render has run. The runtime's own checks (a changed hook, a render loop, a duplicate
key) still stop the program with their message.

On the web, catching makes the module about 27 KB larger (the C++ exception runtime and the
rollback). Rendering speed does not change, natively or in wasm.
