# ui documentation

`ui` is a reactive UI framework in Eskiu: components are functions, state lives in signals,
and the same code draws native views on iOS and Android, the DOM on the web and desktop
windows on macOS, Linux and Windows.

| Guide | What it covers |
|---|---|
| [Getting started](getting-started.md) | Installing the `ui` tool, a first app, running it on each platform, a project's layout |
| [Architecture](architecture.md) | The layers (core, elements, kit, hosts, tool), what happens in a frame, the repository's layout |
| [State](state.md) | Signals, memos, effects, model signals, owned strings, saved data, timers, the hooks rule, errors and `ErrorBoundary` |
| [Components](components.md) | Every element and every kit component, with their arguments |
| [Style and layout](style-and-layout.md) | `Style`, flexbox through Yoga, colors, absolute positions, motion (transforms, transitions, drags, animated values) |
| [The platform](platform.md) | Insets, light and dark, accessibility, the language, the window, the status bar, back, links, system actions (share, clipboard, haptics), the network, alerts, modals |
| [Navigation](navigation.md) | Screens and pages: routers, navigators, back, links, the browser's history |
| [Native code](native.md) | Modules and views per platform, `ui.toml`, Gradle and Swift packages |
| [Protocol](protocol.md) | The commit format a renderer reads, events back, commits and ids |
| [Hosts](hosts.md) | How each platform draws the tree: web, iOS, Android, macOS, Linux, Windows |
| [Testing](testing.md) | The model tests, the host checks, the conformance suite, CI |
| [Releasing](releasing.md) | `ui release`: the App Store and Google Play, signing, build numbers, uploading |

What comes next for ui (recoverable exceptions, prerendering and server-side rendering, more
hosts) is in the roadmap, `docs/dev/roadmap.md` in the
[compiler repository](https://github.com/eskiu-lang/eskiu).
