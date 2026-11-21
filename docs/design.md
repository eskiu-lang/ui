# Design guide

How a ui app should look and behave so that it reads as a finished product on every
platform, and works for everyone who uses it. It is written for people and for coding agents
building screens; [app structure](app-structure.md) covers where the code goes.

The short version: build from the kit (`<ui/kit>`), take every color and size from its theme,
keep to the spacing scale, give every screen its loading, empty and error states, and check
it in dark mode, at a large text size, and in a wide window.

## Start from the kit

The kit's components already follow this guide: sizes for touch, contrast that meets WCAG AA,
roles and states for screen readers, light and dark themes. Use `Title`, `Heading`, `Body`,
`Secondary` and `Caption` for text, `KitButton` for actions, `TextField`, `Checkbox`,
`RadioGroup`, `Select` and the pickers for forms, `Card` and `ListItem` for content, `NavBar`,
`TabBar` and `SegmentedControl` for moving around, `kit_toast` for feedback. Reach for the
raw elements (`Column`, `Row`, `Text`, `Pressable`) to arrange those, or for something the kit
does not have, styled with the same theme.

## Color

Take colors from the theme's roles, never from hex values in a view:

| Role | For |
|---|---|
| `kit.page` | the screen's background |
| `kit.surface` | cards, sheets, bars and fields on top of the page |
| `kit.ink` | text and icons |
| `kit.muted` | secondary text: captions, details, placeholders |
| `kit.primary`, `kit.on_primary` | the main action and its label; selection |
| `kit.primary_soft` | a tinted background for a selected or highlighted item |
| `kit.line` | separators (decorative) |
| `kit.control` | the border of what takes input |
| `kit.track` | the unfilled part of a progress bar, a segmented control's track |
| `kit.danger` | destructive actions and errors |

- A brand color goes into the theme (`Theme{primary: ...}` built from `kit_light()` and
  `kit_dark()`, set with `kit_set`), so it changes in one place and has a dark variant.
- Use one primary action per screen. Everything else is a secondary or ghost button.
- Never carry meaning with color alone: an error has words, a selected tab is also bolder or
  marked.
- Text on a color must stay at 4.5:1 contrast or more (3:1 for text 24 points or larger, and
  for borders of controls). The kit's pairs already do; check any pair you make.

## Type

Use the kit's scale and nothing in between: `Title` (34) for a screen's title, `Heading` (18)
for sections, `Body` (16) for text, `Secondary` (15) and `Caption` (13) for details. Two
weights are enough: 700 for headings and labels that must stand out, the regular weight for
the rest. The sizes grow with the user's text size: never fix a height that text must fit in
(a row of `height: 60` overlaps at large sizes; use `min_height` and padding).

## Spacing and layout

- Spacing comes from one scale: 4, 8, 12, 16, 24, 32 points. `gap` 8 inside a group, 12 to
  16 between groups, 24 to 32 between sections.
- A screen has 16 points of padding at its sides on a phone (20 to 24 in a wide window), and
  the safe area from `ui_insets()` around it.
- Cards have 16 points of padding and the theme's radius (`kit.radius`); a list row at least
  12 points above and below its text.
- Align things on a shared edge: a screen reads as one column of content with a clear start
  edge, not as centered boxes.
- In a wide window (`ui_wide()`), call `kit_dense()` (or `kit_follow_system`), move tabs into a
  sidebar, and cap the content's width (`max_width` around 720 points) instead of stretching
  lines across the screen.
- Right-to-left languages mirror rows, start and end on their own; do not hard-code "left"
  for what means "start".

## Touch and controls

- Everything that takes a tap is at least 44 by 44 points (the kit's controls and unstyled
  buttons are). Small icons get padding around them, not a smaller target.
- Leave at least 8 points between neighboring targets.
- A destructive action asks first (`Alert`) or can be undone (a toast with the undo).
- Disabled controls (`Style.disabled`) stay visible and say why nearby, rather than
  disappearing.
- Gestures always have a visible alternative: a swipe to delete also has a delete button
  (`SwipeToDelete` does this), a long press also has a menu or a button.

## States

Every screen that loads or lists something has four states, and each one is designed:

- **Loading**: `ActivityIndicator` centered, or a skeleton of the content's shape for lists;
  never a blank screen.
- **Empty**: a short title, one line on what goes here, and the action that fills it
  ("No tasks yet. Add one to start." with an Add button).
- **Error**: what failed in plain words, and a retry. Keep what was already shown.
- **Content**: the list or the detail itself.

Pull to refresh (`RefreshControl`) on lists the user expects to update. Confirm what the user
did with a toast (`kit_toast("Saved")`), short and in the past tense.

## Forms

- Each field has a visible label above it, not only a placeholder.
- Use the right input: `secure` for passwords, the keyboard for the content (`KeyboardEmail`,
  `KeyboardNumber`...), `autofill` for logins and one-time codes, `return_key` that says what
  happens (`ReturnNext` between fields, `ReturnGo` or `ReturnDone` on the last), and
  `ui_on_submit` to act on it.
- Validate when the user leaves a field or submits, not on every keystroke; the message goes
  under the field, in `kit.danger`, in words that say how to fix it.
- The submit button stays enabled and explains what is missing when pressed, or is disabled
  with the reason shown.

## Motion

- Animate to explain a change (something appears, moves or goes), not to decorate: 150 to
  250 ms, `EaseOut` for things entering, `EaseIn` for things leaving, `EaseSpring` for what
  follows a finger.
- Use `transition_ms` on the style for opacity, transforms and colors, and `layout_ms` for
  elements the layout moves.
- The platform turns these off when the user asks for reduced motion
  (`ui_reduce_motion()`); a custom animation (`use_frame`) must check it too.

## Accessibility

The kit handles most of this; for what you build yourself:

- Give every control a `label` that says what it does ("Delete task", not "Trash"), and a
  role (`role: RoleButton`, `RoleCheckbox`...) with its state (`checked`, `selected`,
  `expanded`).
- Images that carry meaning get a `label`; decorative ones get none.
- Headings get `role: RoleHeader` (`Title` and `Heading` have it), so screen reader users can
  jump between sections.
- Announce what changes out of sight with `ui_announce` (a toast does it already).
- Test with a large text size, in dark mode, and with the screen reader of one platform.

## Dark mode and themes

Follow the system (`kit_follow_system()` at the top of the root block) unless the user picks
a theme in the app's settings; then `kit_set(kit_dark())` or `kit_set(kit_light())`. Check
every screen in both: a color picked by hand, or an image with a white background, is what
usually breaks.

## Icons and images

- Use one icon style across the app: an icon font in `assets/fonts` (set with `Style.font`) or
  SVGs with the `svg` package, at 20 to 24 points, in `kit.ink` or `kit.primary`.
- Images have a fixed aspect ratio (`aspect_ratio`) and `cover` when cropped, so the layout
  does not jump while they load, and a `background` in `kit.track` as their placeholder.

## Before calling a screen done

1. Light and dark, phone and wide window, the largest text size.
2. Loading, empty and error states seen at least once (`ui agent web "screenshot shot.png"`).
3. Every control reachable and named (`ui agent web tree` shows the roles, labels and states).
4. Its flow covered by a `.uitest`.
