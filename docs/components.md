# Components

A container takes its content as a block, `escaping fn()->void`. Every element takes a
[`Style`](style-and-layout.md) except `Text`, `Button` and the platform elements; the
`...With` forms add one.

## `<ui>`

### Layout

| Element | What it is |
|---|---|
| `Column(Style, block)` | Children top to bottom. |
| `Row(Style, block)` | Children left to right. |
| `Scroll(Style, block)` | A column whose content scrolls when it is taller than the space it gets. |
| `Pressable(Style, on_press, block)` | A column that reacts to a press anywhere in it. |
| `Scope(name, Key, block)` | A component's own scope: state, identity, and `name` in diagnostics. No view. |
| `ui_on_pan(fn(Pan)->void)` | For the element right after it: a drag on it, from `PanBegan` (past the platform's slop) through `PanMoved` to `PanEnded` or `PanCancelled`, with `dx`, `dy` (points since it began) and `vx`, `vy` (points per second). A press that does not move is still a press, and a drag takes over from a pressable inside. |
| `ui_on_hover(fn(bool)->void)` | For the element right after it: true when a pointer (a mouse, a trackpad, an iPad's pointer, a stylus) moves over it, false when it leaves. Touch screens have no hover. |
| `ui_on_layout(fn(Frame)->void)` | For the element right after it: the handler gets its `Frame{x, y, width, height, window_x, window_y}` (points; x and y relative to the parent, the window position from the window's top left, also after a scroll) after each change, as React Native's `onLayout`. |

### Text and buttons

| Element | What it is |
|---|---|
| `Text(string)`, `TextWith(Style, string)` | Text. It is copied; format it with `<string>`'s `String_format`, which `-Wformat` checks. |
| `Button(string, on_press)`, `ButtonWith(Style, string, on_press)` | A button with a title. |
| `Button_owned(string, on_press, cleanup)` | A button whose `cleanup` runs when the handler is replaced or unmounted. |

### Input

| Element | What it is |
|---|---|
| `TextInput(Input, fn(string)->void)` | A controlled text field. `Input{key, value, placeholder, label, focused, style}`: `value` is what it shows, the handler gets each edit (borrowed for the call). `focused` is 1 to focus it, 0 to blur it, -1 (the default) to leave focus to the user. |
| `TextInput_focus(Input, on_change, fn(bool)->void on_focus)` | The same, told when the field gains or loses focus. |
| `Switch(Style, bool value, fn(bool)->void)` | A controlled on/off switch. |
| `Slider(Style, int value, int min, int max, fn(int)->void)` | A controlled slider in whole steps from `min` to `max`: `UISlider`, `SeekBar`, `<input type=range>`. |

Inputs, switches and sliders are controlled: what they show is the value the app rendered.
When the app ignores an edit, a toggle or a move, the next frame sends its value back and the
view returns to it.

### Media

| Element | What it is |
|---|---|
| `Image(Style, string source)` | An image from an `http(s)` URL, a file in `assets/` (by name) or a file by its absolute path (a photo the app saved; a `blob:` or `data:` URL on the web). Give it a width and height; `label` is its text alternative. |
| `ActivityIndicator(Style, bool animating)` | The platform's busy indicator. |

### Lists

| Element | What it is |
|---|---|
| `FlatList(Style, int count, int item_height, fn(int)->void render)` | A virtual list: only the rows near the viewport are mounted, five more on each side, so a million rows cost what a screenful does. Rows have a fixed height. `render(i)` emits row `i`, in a cell keyed by `i`. |
| `SectionList(Style, *List<int> sizes, int header_height, int item_height, fn(int, int)->void render)` | The same with sections: `sizes` holds each section's row count (the app keeps it alive); `render(s, -1)` emits a header, `render(s, i)` a row. |
| `RefreshControl(bool refreshing, on_refresh)` | Pull to refresh for the `Scroll` or list right after it; the platform's indicator shows while `refreshing`. Anything else after it stops the program. |

### Overlays

| Element | What it is |
|---|---|
| `Modal(bool visible, on_request_close, block)` | Content on a layer over the whole screen while `visible`, over a dim backdrop. Back on Android, Escape on the web and a tap on the backdrop (outside the content) call `on_request_close`. |
| `Overlay(bool visible, on_dismiss, block)` | The same layer without the dim, for content placed next to something: the block positions itself (absolute, from a `Frame`'s `window_x` and `window_y`). |
| `Alert(title, message, buttons, fn(int)->void)` | A native alert. `buttons` are separated by newlines (null: "OK"); the handler gets the index pressed, -1 when it was dismissed. |

### The platform

| Name | What it is |
|---|---|
| `StatusBar(int BarStyle, bool hidden)` | The system status bar: `BarDefault`, `BarLight` or `BarDark` content, and whether it shows. The last one rendered wins. |
| `use_back(fn()->bool)` | A back handler while the scope is mounted; it returns true when it handled the press. |
| `ui_open_url(string)` | Open a URL with the platform. |
| `ui_link()` | The link the app was opened with or was last sent, or null. |
| `ui_insets()`, `ui_window()`, `ui_is_active()` | The system bars and keyboard, the window, whether the app is in the foreground. |

These are described in [the platform](platform.md).

### Native

| Name | What it is |
|---|---|
| `NativeView(Style, string type, fn(string, string)->void on_event)` | A view the app registered with each host under `type`; `on_event` gets the view's events (name, payload). |
| `ui_native_prop(name, value)` | A named string prop for the `NativeView` right after it. |
| `use_native_event(channel, fn(string)->void)` | A handler for a native module's events on `channel` while the scope is mounted. |

See [native code](native.md) for the platform side.

### Navigation

| Name | What it is |
|---|---|
| `router_new(root)`, `Router_free` | A stack of routes, owned by the app's model. |
| `Navigator(*Router, fn(string)->void screen)` | The stack as screens; the ones under the top stay mounted. |
| `router.push`, `back`, `replace`, `reset`, `path`, `depth`, `at` | Change and read the stack. |
| `route_match(pattern, route, *List<String> params)` | `"/places/:id"` against a route; the caller frees the params. |
| `route_param(pattern, route, index)` | One parameter of a route the navigator handed a screen, kept as long as the route is on the stack (null when it does not match). |

See [navigation](navigation.md).

### State

`use_signal`, `signal_new`, `use_memo`, `use_effect`, `on_cleanup`, `use_mount`, `use_string` and
`ui_own` are described in [state](state.md).

## `<ui/kit>`

`import <ui/kit>;` adds themed components built only from the elements above. Their colors
and radius come from the global `kit` (a `Theme`); change its fields before mounting:

```eskiu
void ui_app_start(int which) {
    kit.primary = rgb(13, 148, 136);
    kit.radius = 8;
    ui_mount() { App(); }
}
```

| `Theme` field | Default | Used for |
|---|---|---|
| `primary`, `on_primary` | indigo, white | primary buttons, selection, progress |
| `primary_soft` | light indigo | avatars |
| `ink`, `muted` | near black, gray | text, secondary text |
| `page`, `surface` | light gray, white | the page, cards and fields |
| `line`, `control`, `track` | grays | borders, unchecked controls, tracks |
| `danger` | red | danger buttons |
| `radius` | 12 | corners |

Options (`RadioGroup`, `SegmentedControl`, `TabBar`) are one string with a line per option.
Tones are `ToneNeutral`, `TonePrimary`, `ToneSuccess`, `ToneWarning`, `ToneDanger`;
variants `VariantPrimary`, `VariantSecondary`, `VariantGhost`, `VariantDanger`.

| Component | What it is |
|---|---|
| `Card(block)` | A surface with the theme's border and radius. |
| `Badge(text, tone)` | A small label in a tone's colors. |
| `Avatar(initials, size)`, `AvatarImage(source, size, label)` | Initials, or an image, in a circle. |
| `Separator()`, `VSeparator()` | A hairline across a column, or down a row. |
| `ProgressBar(value, max)` | `value` of `max` as a filled track, which moves to a new value. |
| `KitButton(text, variant, on_press)` | A button in a variant's style. |
| `TextField(value, placeholder, on_change)` | A text field in the theme's style; the placeholder is its accessible name. |
| `Checkbox(label, checked, fn(bool)->void)` | A checkbox and its label. |
| `RadioGroup(options, selected, fn(int)->void)` | One choice among options. |
| `SegmentedControl(options, selected, fn(int)->void)` | Options side by side, the selected one raised. |
| `TabBar(tabs, selected, fn(int)->void)` | Tabs over content the caller shows, the selected one underlined. |
| `Toolbar(block)` | A bar of controls. |
| `NavBar(title, actions)` | A title bar with controls at its end. |
| `ListItem(title, subtitle, on_press)` | A pressable row with a title and an optional subtitle; it lights up under a pointer. |
| `Collapsible(title, content)` | A header that shows or hides its content; its plus turns into a cross. |
| `Popover(open, on_dismiss, trigger, content)` | `content` in a panel next to `trigger` while `open`: below it, or above when there is no room, at least as wide as the trigger and kept in the window. |
| `DropdownMenu(label, options, fn(int)->void)` | A button that opens a menu of options (a line each). |
| `Select(options, selected, fn(int)->void)` | A field showing the chosen option, with a menu to change it. |
| `MenuItem(text, checked, on_press)` | A row of a menu, lit under a pointer. |
| `Tooltip(text, content)` | `text` in a bubble above `content` while a pointer is over it. |
| `kit_toast(text)`, `ToastHost()` | A short message at the bottom for a few seconds; the app puts `ToastHost()` last in its root. |
| `Calendar(Date, fn(Date)->void)` | A month's days with arrows to the months around it. |
| `DatePicker(Date, fn(Date)->void)` | A field showing a date that opens a `Calendar`. |
| `ColorPicker(int64 color, fn(int64)->void)` | A swatch that opens a grid of colors. |
| `SwipeToDelete(label, on_delete, content)` | A row the user drags left: past a third of its width, or flicked, it slides out and `on_delete` runs; short of that it springs back. Key each row. |
| `Accordion(sections)` | Collapsibles in one card. |

`kit_light()` and `kit_dark()` are the two themes; `kit_follow_system()` picks the one for the
system's appearance ([the platform](platform.md)). `examples/kit-gallery` shows every
component, in both.

## Writing a component

A component is a function. Give it a `Scope` when it has state of its own or appears in a
list, take what it shows as arguments, and report changes through closures:

```eskiu
void Stepper(string label, int value, escaping fn(int)->void on_change) {
    ui_own(void() { ui_release_int(on_change); });
    Row(Style{gap: 12, align: AlignCenter, label: label}) {
        String text;
        defer String_free(&text);
        String_format(&text, "%s: %d", label, value);
        Button("-") { on_change(value - 1); }
        Text(text.data);
        Button("+") { on_change(value + 1); }
    }
}
```
