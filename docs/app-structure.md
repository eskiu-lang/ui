# Structuring an app

How to lay out a ui app so it stays easy to change as it grows, for people and for coding
agents alike. [ui-starter](https://github.com/eskiu-packages/ui-starter) follows it and is the
place to start from; the [design guide](design.md) covers how the screens should look.

## The layout

```
myapp/
  eskiu.toml            the package: name, the eskiu version, dependencies (ui, ...)
  ui.toml               the app: name, id, icon, permissions, [env] values
  AGENTS.md             how an agent runs, drives and tests this app (ui new writes it)
  src/
    main.esk            the entry: ui_app_start sets things up and mounts App
    app.esk             the root: theme, navigation, tabs or sidebar, ToastHost
    theme.esk           the app's design tokens on top of the kit's theme
    words.esk           the app's text, one place per language
    model/
      model.esk         the app's state: a Model struct of signals, imports its parts
      tasks.esk         one part per domain: its types and the operations on them
      storage.esk       what is saved and loaded (ui_storage_*, files)
    services/
      api.esk           the network: requests with ui_fetch, parsed into the model's types
    components/
      task_row.esk      views used by more than one screen
      empty_state.esk
    screens/
      home.esk          one file per route
      task_detail.esk
  assets/               images, fonts and sounds the app loads by name
  native/               code per platform, when the app needs some (ios/, android/, web/)
  tests/
    ui/                 .uitest files: one per flow (tests/ui/add_task.uitest)
    model.esk           the model's logic, tested without a screen
```

A small app can start with `src/main.esk`, `src/app.esk`, `src/model.esk` and a `screens/`
folder, and grow into the rest. Keep each file under about 600 lines: when one gets longer,
split it by topic and import the parts in order.

## What may use what

Dependencies go one way, so each layer can change without touching the ones under it:

```
screens  ->  components  ->  theme, words
   |              |
   v              v
 model   <-   services
```

- **model** holds the state and the rules: types, signals, the operations that change them,
  saving and loading. It imports `<ui>` for signals only; it never calls a view
  (`Column`, `Text`, kit components), so its logic can be tested without a screen.
- **services** talk to the outside (the network, files, native modules) and hand results to
  the model as its own types. A request's failure is a `Result` or a state the model keeps
  (loading, loaded, failed with a reason), never a crash.
- **components** are views that take what they show as parameters and report what the user
  does through callbacks. They read the theme and the words; they do not reach into the model
  or the router.
- **screens** put components together for one route: they read the model, call its
  operations, and navigate. A screen is the only place that knows the router.
- **app.esk** wires the screens to the router, applies the theme and the words, and mounts
  what every screen shares (the tab bar, `ToastHost`).

## Naming

- Components are PascalCase functions named for what they show: `TaskRow`, `EmptyState`,
  `TaskDetailScreen` (screens end in `Screen`).
- Model operations follow the language's convention: `Type_method` for a type's operations
  (`TaskList_add(&model.tasks, title)`, called as `model.tasks.add(title)`), `module_verb` for
  free functions (`storage_load`, `api_fetch_tasks`).
- Files are snake_case and named after their main type or screen.
- Every element a test or an agent needs gets a `test_id` named for its role, not its look:
  `Style{test_id: "add_task"}`, not `"blue_button"`. Kit components take one through
  `ui_style_next(Style{test_id: "save"})`.

## State

- App state that outlives a screen lives in the model as model signals (`signal_new<T>`).
  State of one view (an open menu, a field being typed) is `use_signal` inside it.
- A list of records keeps its items in the model and bumps a version signal when it changes;
  each row is keyed by the record's id (`Style{key: key(task.id)}`), never by its position.
- Routes are strings (`"/tasks/42"`). A screen gets its parameters with `route_param`; it
  does not keep pointers into the model across renders.
- Tabs keep their state with `KeepAlive`; a pushed screen keeps it on the router's stack.
- What must survive a restart goes through `model/storage.esk`, saved when it changes, loaded
  in `ui_app_start` before mounting.

## Text and languages

- Words the user sees live in `words.esk`, not inline in the views: a struct of strings with
  one constructor per language, picked from `ui_locale()` in `ui_app_start`.
- Set the kit's words in the same place: `kit_use_text(kit_text_for(ui_locale()))`. It also
  gives screen readers the words the platform does not translate.
- Build sentences with `textf` from the words' patterns, not by gluing pieces together:
  word order changes between languages.

## Errors

- Put an `ErrorBoundary` around each screen's content in `app.esk`, with a fallback that says
  what happened and offers a way back (`ui_boundary_reset`). A crash in one screen then never
  takes the app down.
- `ui_on_error` in `ui_app_start` can send runtime errors to your logging.
- Duplicate keys and hooks outside a render stop the program in every build: `ui test` and
  `ui agent` find them before users do.

## Tests

- `tests/model.esk` tests the model's operations directly: `eskiuc test` runs it.
- `tests/ui/*.uitest` tests flows on the running app: one file per flow, written against
  test ids, with `clock settle` after anything that animates. `ui test web` runs them;
  `ui test web ios android` runs them on each platform.
- Before saying a change works, run the flows it touches and look at a screenshot
  (`ui agent web "screenshot shot.png"`).

## Checklist for a new screen

1. A file in `src/screens/`, a route in `app.esk`, its words in `words.esk`.
2. Built from components; any new one that another screen could use goes in
   `src/components/`.
3. Loading, empty and error states, not only the happy path ([design guide](design.md#states)).
4. A `test_id` on every control, and a `.uitest` for its main flow.
5. Checked in light and dark, in a phone and a wide window, and with a large text size.
