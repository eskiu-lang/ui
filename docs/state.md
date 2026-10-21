# State

## Signals

```eskiu
*Signal<int> count = use_signal<int>(0);
Text(count.get() > 9 ? "many" : "few");
Button("+", void() { count.set(count.get() + 1); });
```

`use_signal<T>(initial)` gives the current scope a signal, created on the first render and
kept by position for as long as the scope is mounted. `get()` reads it and subscribes the
block that reads it; `set(v)` writes it, and every block that read it renders again in the
next frame. Setting the same value does nothing.

A signal holds a value with built-in equality: a number, a `bool`, a pointer. A pointer in
a signal is borrowed; the signal does not own what it points to. Text goes in `use_string`.

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
use_effect(void() {
    int id = start_timer(count.get());
    on_cleanup(void() { stop_timer(id); });
});
```

`use_effect(callback)` runs after each commit of its scope that changed something it read,
parents first. `on_cleanup` (once per run) is called before the next run and when the scope
unmounts.

## Timers

```eskiu
*Signal<int> seconds = use_signal<int>(0);
*Signal<bool> running = use_signal<bool>(true);
use_interval(running.get() ? 1000 : 0, void() { seconds.set(seconds.get() + 1); });
use_timeout(3000, void() { hint.set(false); });
```

`use_interval(ms, handler)` calls `handler` every `ms` milliseconds while the scope is
mounted; a period of 0 or less pauses it, and a new period restarts it. `use_timeout(ms,
handler)` calls it once, `ms` after the scope mounts or after `ms` last changed. The host keeps
the clock (a `setInterval` on the web, a run loop timer on iOS, a `Handler` on Android), and a
tick reaches the handler on the UI thread like any event, so it may set signals.

## The hooks rule

Hooks (`use_signal`, `use_memo`, `use_effect`, `use_string`, `use_back`,
`use_native_event`, `use_interval`, `use_timeout`) are matched by the order a block calls
them in. A block must call the
same hooks, of the same types, in the same order every time it renders. The core checks it:
a block that calls a different hook, or the same hook with another type, at a slot stops
the program with "hook kind/type changed at slot N", and one that calls more hooks after it
mounted with "hook count increased". Put a hook in a conditional branch inside its own
`Scope` instead.

## Scopes and identity

Each element's block is a scope: its hooks belong to it, and it renders again on its own
when a signal it read changes. `Scope(name, key, block)` makes a component's scope
explicit and names it in diagnostics:

```eskiu
void TodoRow(*Todo todo) {
    Scope("TodoRow", key(todo.id), void() {
        *Signal<bool> editing = use_signal<bool>(false);
        ...
    });
}
```

`key(id)` keeps a child's node, and so its state, when its siblings are added, removed or
reordered. Without a key, children of a block match by position among the unkeyed siblings
of the same kind, so `if (saving) { Text("Saving"); }` does not disturb a `Column` next to
it. Keys must be unique among siblings: a duplicate stops the program, or, with
`ui.lenient` set, warns and treats the later child as unkeyed. Key 0 is a key.

When the program stops, the message says where:

```
ui: hook kind/type changed at slot 0: expected use_signal, found use_memo
  in Column[0] > TodoRow[key 3]
```

## Model signals

State that belongs to the app rather than to a screen is a model signal:

```eskiu
*Signal<int> total;

void ui_app_start(int which) {
    total = signal_new<int>(0);
    ui_mount(void() { App(); });
}

void ui_app_stop() { total.free(); }
```

`signal_new<T>` makes a signal no scope owns. It outlives any mount, works before the
runtime exists, and its owner frees it with `free()` after the UI that reads it is gone
(the hosts call `ui_app_stop` after the shutdown).

## Strings

```eskiu
*StringState draft = use_string("");
TextInput(Input{value: draft.get(), placeholder: "New task"}, void(string text) { draft.set(text); });
```

`use_string(initial)` holds immutable, reference-counted text (`UiString`). `get()` borrows
the text until the next `set()`. A handler that outlives the render and needs the text
takes a `snapshot()`, which retains it, and releases it in its cleanup.

Text given to an element (`Text`, a button's title, an input's value) is copied, so a
`String` built for one render can be freed right after the call:

```eskiu
String label;
defer String_free(&label);
String_format(&label, "%d left", remaining.get());
Text(label.data);
```

## Closures a component keeps

A handler given to an element is owned by the element's node and freed after the commit
that replaced or removed it. A component that takes a closure and only calls it from a
handler of its own (so no element owns it directly) gives the destructor to `ui_own`
before its first element:

```eskiu
void Checkbox(string label, bool checked, escaping fn(bool)->void on_change) {
    ui_own(void() { ui_release_bool(on_change); });
    Pressable(Style{label: label}, void() { on_change(!checked); }, void() { ... });
}
```

The destructor goes with the next element's block and runs when that block is replaced or
unmounted. `src/core/closures.esk` has a `ui_release_*` for each closure type the elements
take.

## Errors in handlers

A throw out of a block, handler or effect is not caught: the program stops through Eskiu's
uncaught-exception reporter. A caller that catches it anyway finds the next `ui_frame` or
event stopping with "runtime left mid-phase".
