# Style and layout

## `Style`

```eskiu
Column(Style{padding: 16, gap: 12, background: rgb(255, 255, 255), radius: 16}) { ... }
```

| Field | Default | Meaning |
|---|---|---|
| `key` | none | Identity among siblings: `key(id)`. See [state](state.md). |
| `gap` | 8 | Space between children, in points. |
| `padding` | 0 | Space inside the edges, in points. |
| `label` | none | The accessible name: ARIA label on the web, `accessibilityLabel` on iOS, `contentDescription` on Android, an image's alternative text. |
| `width`, `height` | 0 | Size in points; 0 lets the content and the parent decide. |
| `grow` | 0 | How much of the parent's free space along its direction this child takes (flex-grow). |
| `wrap` | false | A row's children flow onto more lines (flex-wrap). |
| `background`, `color` | -1 | Background and text color from `rgb`/`rgba`; -1 is the platform's default. |
| `font_size` | 0 | Points; 0 is the platform's default. |
| `font_weight` | 0 | 100 to 900; 0 is the default. |
| `border_width`, `border_color` | 0, -1 | The border, in points. |
| `radius` | 0 | Corner radius in points; it clips the content. |
| `opacity` | 100 | Percent. |
| `align` | `AlignDefault` | Children across the axis: `AlignStart`, `AlignCenter`, `AlignEnd`, `AlignStretch`. |
| `justify` | `JustifyDefault` | Children along the axis: `JustifyStart`, `JustifyCenter`, `JustifyEnd`, `JustifyBetween`, `JustifyAround`. |
| `absolute` | false | Out of the parent's flow, placed by `left`, `top`, `right`, `bottom`. |
| `left`, `top`, `right`, `bottom` | `kUnset` | Offsets from the parent's edges, in points, for an absolute element (0 is an offset). |
| `aspect_ratio` | 0 | Width over height: the side that is not set comes from the one that is. |
| `min_width`, `max_width`, `min_height`, `max_height` | 0 | Bounds on the size the layout gives, in points; 0 is no bound. |
| `translate_x`, `translate_y` | 0 | Moves the drawn element, in points, without changing the layout. |
| `scale` | 100 | Scales the drawn element about its center, in percent. |
| `rotate` | 0 | Turns the drawn element about its center, in degrees. |
| `transition_ms` | 0 | Animates changes of the opacity, the transforms and the colors, in milliseconds. |
| `shadow` | 0 | Raises the element with a soft shadow below it: its elevation in points, as React Native's `elevation` (a `CALayer` shadow on iOS, `View.setElevation` on Android, `box-shadow` on the web). A raised element does not clip its content to its radius on iOS. |
| `cover` | false | An `Image` fills its box, cropped, instead of fitting in it (React Native's `resizeMode: "cover"`). |
| `gradient` | -1 | Paints the background as a linear gradient from `background` (transparent when unset) to this color. |
| `gradient_angle` | 180 | The gradient's direction in degrees, as CSS: 180 top to bottom, 90 left to right, 135 corner to corner (Android turns it to the nearest 45). |
| `pixelated` | false | An `Image` scales without smoothing, each pixel a sharp square, for pixel art (CSS `image-rendering: pixelated`). |
| `font` | null | Draws the text in a font file from `assets/` (`"fonts/VT323.ttf"`, TrueType or OpenType), loaded once; one file per weight, so `font_weight` does not apply. A file that does not load leaves the system font. |
| `easing` | `EaseInOut` | The transition's curve: `EaseInOut`, `EaseOut`, `EaseIn`, `EaseLinear`, `EaseSpring`. |

A field left at its default is not sent to the host, and changing one sends only that
field. Styles are plain values, so shared styles are functions or globals:

```eskiu
Style card() { return Style{padding: 16, radius: 16, background: kit.surface}; }
```

## Colors

`rgb(r, g, b)` and `rgba(r, g, b, a)` take channels from 0 to 255 and give an `int64` of
the form `0xRRGGBBAA`, which can also be written directly (`0x4F46E5FF`). A color with
alpha 0 is transparent.

## Layout

Layout is flexbox with React Native's rules. On iOS and Android the hosts run Yoga 3.2.1
over the native views; on the web the renderer sets CSS flexbox with Yoga's defaults:

- a `Column`, `Scroll`, `Pressable` and `Modal` stack their children vertically, a `Row`
  horizontally;
- sizes include padding and border (`box-sizing: border-box`);
- children do not shrink (`flex-shrink: 0`) and have no minimum size, so a long text in a
  row needs `grow: 1` to take the remaining space and wrap within it;
- a `Scroll`, and an element with `grow`, also shrink to the space their parent has (as
  React Native's `ScrollView` and `flex: 1`), so a scroll under a growing column stays inside
  the window and scrolls instead of taking its content's height;
- children stretch across the axis unless `align` says otherwise.

An absolute element is placed against its parent's padding edges and the other children are
laid out as if it were not there, as in React Native; it draws over the siblings before it:

```eskiu
Row(Style{height: 160, radius: 16, background: kit.primary}) {
    Row(Style{absolute: true, top: 12, right: 12, padding: 6, radius: 10, background: rgb(255, 255, 255)}) { Text("New"); }
}
```

## Motion

A transition animates the changes the next renders make, on the host: `UIView` animations on
iOS, `ViewPropertyAnimator` on Android, CSS transitions on the web, so an animation does not
need the core on every frame.

```eskiu
*Signal<bool> open = use_signal<bool>(false);
Column(Style{opacity: open.get() ? 100 : 0, translate_y: open.get() ? 0 : 24, transition_ms: 250,
             easing: EaseOut}) { ... }
```

A drag moves an element through its transforms, with no transition while the finger is down
and one when it lets go:

```eskiu
*Signal<int> dx = use_signal<int>(0);
*Signal<bool> held = use_signal<bool>(false);
ui_on_pan(void(Pan p) {
    held.set(p.phase == PanBegan || p.phase == PanMoved);
    dx.set(held.get() ? p.dx : 0);
});
Row(Style{translate_x: dx.get(), transition_ms: held.get() ? 0 : 250, easing: EaseSpring}) { ... }
```

For a value the host cannot animate (a number drawn as text, a share of a row),
`use_animated(target, ms, easing)` is the value on its way to `target`: when the target
changes, it moves there over `ms` milliseconds along the curve, rendering its readers once per
display frame (`use_frame`) until it arrives. `use_animated_then(target, ms, easing, on_done)`
also runs `on_done` each time it arrives.

```eskiu
int shown = use_animated(score.get(), 600, EaseOut);
String text;
defer String_free(&text);
String_format(&text, "%d points", shown);
Text(text.data);
```

Two more run on their own once mounted. `use_repeat(from, to, ms, easing, count, reverse)`
goes from `from` to `to` again and again, `count` times (0: forever), back each other time
when `reverse`. `use_sequence(steps, n, count)` runs through `Keyframe{value, ms, easing}`
steps: it holds the first value for its `ms` (a delay), then moves to each next one over its
own. Both start over when their arguments change and stop on their last value.

```eskiu
int bob = use_repeat(0, -6, 400, EaseInOut, 0, true);     // a sprite bobbing, forever
Keyframe[3] shake = {Keyframe{0, 0, 0}, Keyframe{8, 60, EaseOut}, Keyframe{0, 120, EaseSpring}};
int dx = use_sequence(&shake[0], 3, 1);
Image(Style{width: 48, height: 48, translate_y: bob, translate_x: dx}, "egg.png");
```

The transforms are drawn, not laid out: the element keeps its place and its siblings do not
move. Layout changes (a size, a position from the flow) are not animated yet.

## Measuring

Text, buttons, inputs, images, switches and indicators are measured by the platform: their
size comes from their content unless `width` or `height` is given. A `Modal` is laid out on
its own over the whole window, and a `NativeView` is a leaf that needs a size.

The root fills the window inside the safe area and above the keyboard. An app that wants
the whole screen sets the edge-to-edge policy and pads itself from `ui_insets()` (see
[the platform](platform.md)).
