# ui documentation

`ui` is a reactive UI framework in Eskiu: components are functions, state lives in signals,
and the same code draws native views on iOS and Android, the DOM on the web and a webview
window on macOS.

| Guide | What it covers |
|---|---|
| [Getting started](getting-started.md) | Installing the `ui` tool, a first app, running it on each platform, a project's layout |
| [Architecture](architecture.md) | The layers (core, elements, kit, hosts, tool), what happens in a frame, the repository's layout |
| [State](state.md) | Signals, memos, effects, model signals, owned strings, the hooks rule |
| [Components](components.md) | Every element and every kit component, with their arguments |
| [Style and layout](style-and-layout.md) | `Style`, flexbox through Yoga, colors, the web's Yoga defaults |
| [The platform](platform.md) | Insets and the keyboard, the window, foreground state, the status bar, back, links, alerts, modals |
| [Native code](native.md) | Modules and views per platform, `ui.toml`, Gradle and Swift packages |
| [Protocol](protocol.md) | The commit format a renderer reads, events back, commits and ids |
| [Hosts](hosts.md) | How each platform draws the tree: web, iOS, Android, macOS |
| [Testing](testing.md) | The model tests, the host checks, the conformance suite, CI |

The design behind it is in the UI plan, `docs/dev/ui-plan.md` in the
[compiler repository](https://github.com/eskiu-lang/eskiu).
