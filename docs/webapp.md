# Web apps

`<ui/webapp>` makes an Eskiu program the backend of a web front end, the way Tauri does for
Rust: the page (a React bundle, or any HTML) runs in the platform's webview, calls the
program's commands and gets their answers, each side sends the other events, the program
serves files through URL schemes of its own, and it opens the system's dialogs. The page
talks to it through `@eskiu/webapp` (`js/webapp`), whose API has the shape of
`@tauri-apps/api`.

| Platform | Webview | Checked on |
|---|---|---|
| macOS | WKWebView (`hosts/macos/webapp.m`) | macOS 27, Apple silicon |
| Windows | WebView2 (`hosts/windows/webapp.cpp`) | Windows 11, built from macOS with llvm-mingw |
| Linux | WebKitGTK 4.1 or 4.0, GTK 3 (`hosts/linux/webapp.c`) | Ubuntu 24.04 (arm64) under Xvfb |

This is not the `--webview` renderer of [the hosts](hosts.md): that one runs a `<ui>` app's
own views in a webview. A web app has no `<ui>` views; its page is whatever web code it ships.

## A project

```
myapp/
  eskiu.toml          [deps] ui = ...
  ui.toml             [webapp] frontend = "dist"
  src/main.esk        the program, with its own main
  dist/index.html     the front end (a bundler's output)
```

```toml
[app]
name = "My App"

[webapp]
frontend = "dist"                     # a directory with index.html, bundled with the app
before_build = "npm run build"        # optional: run in the project's root before each build
dev_url = "http://localhost:5173"     # optional: what `ui run TARGET --dev` loads
```

```bash
ui build macos        # build/ui/macos/NAME.app, the front end in Resources/frontend
ui build linux        # build/ui/linux/NAME, frontend/ beside it
ui build windows      # build/ui/windows/NAME.exe, frontend/ and WebView2Loader.dll beside it
ui run macos          # builds and runs it here
ui run macos --dev    # the window loads dev_url (start the dev server first)
```

A project with `[webapp] frontend` builds as a web app for `macos`, `linux` and `windows`.
`ui build windows` works from Windows, macOS or Linux with MinGW-w64 (llvm-mingw's
`x86_64-w64-mingw32-clang++` on the `PATH`, or `CXX`); Linux builds on Linux and needs
`libwebkit2gtk-4.1-dev` (or 4.0). A build that is not a release reads `UI_WEBAPP_URL` at
start, so a window can load any dev server: `UI_WEBAPP_URL=http://localhost:3000 ui run macos`.
A Windows build that is not a release keeps its console, where the program's output goes.

## The program

```eskiu
import <ui/webapp>;

int main() {
    webapp_command("greet", void(*WebappCall call) {
        string name = call.string("name");
        if (name == null) { call.reject("greet: no name"); return; }
        String text;
        String_appendf(&text, "Hello, %s", name);
        call.resolve_string(text.data);
        String_free(&text);
    });
    webapp_protocol_dir("asset", "/");
    webapp_window(WebappWindow{title: "My App", width: 1280, height: 800, min_width: 640, min_height: 480});
    return webapp_run();
}
```

Registrations and windows come before `webapp_run`, which runs the platform's loop until the
last window closes or `webapp_quit`, and returns the exit code. Everything runs on the main
thread, but for `WebappReply`'s functions, `webapp_emit` and `webapp_quit`, which any
thread may call.

### Commands

`webapp_command(name, handler)`: the page's `invoke(name, args)` runs the handler with a
`*WebappCall`. Its arguments are borrowed for the call:

| | |
|---|---|
| `call.string(key)` | a string argument, or null |
| `call.int(key, fallback)`, `call.number(key, fallback)`, `call.bool(key, fallback)` | numbers and booleans |
| `call.arg(key)` | any argument as a `*JsonValue` (`<json>`) |
| `call.bytes(key, &length)` | a `Uint8Array` or `ArrayBuffer` argument, from malloc |
| `call.args_json(&out)` | all of them as JSON text |
| `call.window`, `call.id`, `call.command` | the window that asked, the call's id, its name |

It answers once:

| | The page gets |
|---|---|
| `call.resolve(json)` | the value of the JSON text (null for null) |
| `call.resolve_string(text)` | a string |
| `call.resolve_bytes(data, length)` | a `Uint8Array`: the bytes travel apart from the JSON (the page fetches them once from the app's scheme), so a megabyte costs a copy, not a base64 text |
| `call.reject(message)` | the promise rejects with `message` |
| `call.later()` | nothing yet: a `WebappReply` answers later, from any thread |

A handler that returns without answering, and without `later()`, rejects the call ("the
command NAME did not answer"); an unknown command rejects with "no command NAME". In a build
that is not a release, a `resolve` whose text is not JSON rejects instead of leaving the
page's promise waiting.

A long command answers later from a thread of its own, so the window keeps drawing:

```eskiu
webapp_command("export", void(*WebappCall call) {
    WebappReply reply = call.later();
    thread_create(void() {
        for (i in 0..100) {
            // ... a step ...
            webapp_emit(reply.window, "export-progress", "{\"step\":1}");
        }
        reply.resolve_string("/path/to/the/export");
    });
});
```

### Events

`webapp_emit(window, event, json)` sends an event to one window's page, or to every page for
window 0, from any thread. `webapp_listen(event, handler)` runs the handler for each of the
page's `emit(event, payload)`, with a `WebappEvent` (`window`, `event`, `payload` as JSON
text and `value` as a `*JsonValue`, borrowed for the call).

### Schemes

`webapp_protocol(scheme, handler)` answers `scheme://localhost/PATH` (on Windows,
`https://scheme.localhost/PATH`: WebView2 takes no other scheme without registering it before
it starts). The handler gets a `*WebappRequest` with the path (decoded, without the leading
`/`), the query, the method, the `Range` header and the window, and answers once:

| | |
|---|---|
| `request.file(path)` | the file, with its type from its extension; false and a 404 when it is not a file |
| `request.bytes(data, length, mime)` | bytes (copied) |
| `request.text(text, mime)` | a text |
| `request.status(code)` | a status with no body |

A request the handler does not answer is a 404. Every answer is open to any origin
(`Access-Control-Allow-Origin: *`, so the page fetches it from `app://` or from a dev
server's origin), uncached, and answers a CORS preflight. A file's or bytes' answer honors a
single `Range` (`206` with `Content-Range`, `416` past the end); an open range (`bytes=N-`)
gets at most 8 MiB, and a media element asks for the rest, so `<audio>` and `<video>` seek in
big files without reading them whole. A file is read off the main thread on macOS, streamed
from disk on Linux, and read for the request on Windows.

`webapp_protocol_dir(scheme, root)` serves the files under `root`; with `"/"` (or `""`) the
path is an absolute path, as the page's `convertFileSrc(path, scheme)` makes it (`C:/...` on
Windows). A path with a `..` part or a backslash is refused with a 403. Schemes are
registered before the first window.

The front end itself is the scheme `app`: `app://localhost/` (`https://app.localhost/` on
Windows) is its `index.html`, a path that is not a file and has no extension (a route of the
page's router) gets `index.html` too, and `/@eskiu/webapp.js` is the client module.

### Windows

`webapp_window(WebappWindow{...})` opens a window and returns its id:

| Field | |
|---|---|
| `title` | none: the page's title |
| `url` | none: the front end's root; a path (`"/settings"`): a page of the front end; or any URL |
| `width`, `height` | the content's size in points (1024 by 768 when 0) |
| `min_width`, `min_height` | the smallest content size |
| `fullscreen` | it opens full screen |
| `fixed_size` | it cannot be resized |
| `devtools` | the developer tools in a release too (every other build has them) |

`webapp_set_title`, `webapp_set_size`, `webapp_set_min_size`, `webapp_set_fullscreen`,
`webapp_is_fullscreen` and `webapp_close` (no asking) act on a window by its id. Several
windows work alike; the program ends when the last one closes. `webapp_on_close(handler)`
is asked before a window closes (its close button, Cmd+W, the page's `close()`, and on macOS
each window at Quit); false keeps it open, for unsaved work. A page with `onCloseRequested`
decides itself, before the handler.

What each platform adds:

- **macOS.** The app's menu (About, Hide, Quit), Edit (the page's fields take Undo, Cut,
  Copy, Paste, Select All through the responder chain), View (Reload, Full Screen) and Window
  (Minimize, Zoom, Close). The page's `alert`, `confirm`, `prompt` and `<input type="file">`
  are AppKit's. Media plays without a gesture; the page's element full screen works.
- **Windows.** Per-monitor DPI (sizes in points), F11 full screen, F5 and Ctrl+R reload with
  the developer tools, F12 opens them (WebView2's). WebView2 keeps its data in
  `%LOCALAPPDATA%\NAME`. It needs the WebView2 Runtime, which Windows 10 and 11 carry.
- **Linux.** F11 full screen; with the developer tools, F5 and Ctrl+R reload and F12 opens
  the inspector. The window's icon is `icon.png` beside the program.

The developer tools are the webview's own: Safari's Web Inspector (right click, Inspect
Element; `getCurrentWindow().openDevtools()`), Edge's DevTools, WebKitGTK's inspector.

### Dialogs

```eskiu
List<String> paths;
int count = webapp_file_dialog(window, WebappDialog{title: "Import", multiple: true,
    filters: "Images\tpng,jpg\nAll\t*"}, &paths);
webapp_message(window, "Exported", "The game is in ~/Games/demo.", WEBAPP_INFO);
bool sure = webapp_confirm(window, "Delete map", "This cannot be undone.", WEBAPP_WARNING, "Delete", "Keep");
```

`WebappDialog` picks files (or `directory: true`, folders), several with `multiple`, or a
file to save with `save` (`default_path` is where it starts, and the name it proposes). They
are NSOpenPanel and NSSavePanel and NSAlert on macOS, `IFileOpenDialog`, `IFileSaveDialog`
and a task dialog on Windows, `GtkFileChooserNative` (the portal's, under Flatpak) and
`GtkMessageDialog` on Linux. Each is modal and returns when it closes.

## The page

```js
import { invoke, listen, emit, convertFileSrc, open, ask, getCurrentWindow } from "@eskiu/webapp";

const text = await invoke("greet", { name: "Ada" });
const stop = await listen("export-progress", (e) => console.log(e.payload));
const dir = await open({ directory: true, title: "Open project" });
img.src = convertFileSrc(`${dir}/title.png`);
```

| | |
|---|---|
| `invoke(cmd, args)` | a promise of the command's answer (a `Uint8Array` for bytes); `Uint8Array` and `ArrayBuffer` arguments travel as bytes |
| `listen(event, handler)`, `once(event, handler)` | a promise of a function that stops it; `handler({event, payload, windowId})` |
| `emit(event, payload)` | to the program's `webapp_listen` |
| `convertFileSrc(path, scheme = "asset")` | a local file's URL through a `webapp_protocol_dir` scheme |
| `schemeUrl(scheme, path)` | a scheme's URL on this platform |
| `open(options)`, `save(options)`, `message(text, options)`, `ask(text, options)`, `confirm(text, options)` | the dialogs of `@tauri-apps/plugin-dialog`, with the same options (`title`, `defaultPath`, `directory`, `multiple`, `filters`, `kind`, `okLabel`, `cancelLabel`) and answers |
| `getCurrentWindow()` | `setTitle`, `setSize`, `setMinSize`, `setFullscreen`, `isFullscreen`, `focus`, `openDevtools`, `close` (asks, as the close button), `destroy`, `onCloseRequested(handler)` (`event.preventDefault()` keeps it open) |
| `isEskiu()`, `platform()`, `quit(code)` | |

A bundled front end depends on the package by path (`"@eskiu/webapp": "file:../ui/js/webapp"`
in its `package.json`), or imports `/@eskiu/webapp.js`, which the program serves. Outside an
Eskiu window (a plain browser on the dev server) `isEskiu()` is false and the calls reject.

Every host runs `js/webapp/bridge.js` before the page's scripts; it defines
`window.__ESKIU__`, which the module wraps. The messages are JSON text: the page posts
`{"kind": "invoke", "id", "cmd", "args"}` and `{"kind": "emit", "event", "payload"}` through
the webview's channel (`window.webkit.messageHandlers.eskiu`, `window.chrome.webview`), and
the program calls `__ESKIU__.receive({"kind": "reply", "id", "ok", "value" | "error" |
"bytes"})` and `({"kind": "event", "event", "window", "payload"})`.

## Security

A window's page stays on its own origin: the program's schemes, or the origin of the URL it
opened (a dev server's). A link or a redirect anywhere else opens in the system's browser,
and a link to a new window (`target="_blank"`, `window.open`) does too, so no other site
runs with the bridge. There is no allowlist of commands per page as Tauri's capabilities
have: every page of the app can run every command, and a `webapp_protocol_dir(scheme, "/")`
serves any file the program can read. Give a scheme a narrower root when the page does not
need the whole disk.

## Moving from Tauri

| Tauri | `<ui/webapp>` |
|---|---|
| `#[tauri::command] fn f(a: String) -> Result<T, E>` | `webapp_command("f", ...)`, `call.string("a")`, `call.resolve(json)` or `call.reject(message)` |
| `async` commands on Tauri's pool | `call.later()` and a thread; answer through the `WebappReply` |
| `app.emit("name", payload)` | `webapp_emit(0, "name", json)` |
| `register_uri_scheme_protocol("rtp", ...)` | `webapp_protocol("rtp", ...)` with `request.file` or `request.bytes` |
| the asset protocol and `convertFileSrc` | `webapp_protocol_dir("asset", "/")` and the same `convertFileSrc` |
| `tauri-plugin-dialog` | built in (`open`, `save`, `message`, `ask`, `confirm`) |
| `tauri.conf.json` `frontendDist`, `devUrl`, `beforeBuildCommand` | `[webapp] frontend`, `dev_url`, `before_build` |
| `app.windows` in `tauri.conf.json` | `webapp_window(WebappWindow{...})` in `main` |
| `http://NAME.localhost` on Windows | `https://NAME.localhost` (`schemeUrl` gives it) |

In the page, the imports from `@tauri-apps/api/core`, `@tauri-apps/api/event`,
`@tauri-apps/api/window` and `@tauri-apps/plugin-dialog` become imports from
`@eskiu/webapp`, with the same names.

## Testing

- `tests/webapp.esk` runs the module against a host stand-in (`tests/webapp_host.c`): the
  commands and their answers (later ones from a thread too), events both ways, the schemes
  (the front end and its routes, ranges, paths out of the root, preflights), the built-in
  dialog and window commands and the close a page intercepts.
- `tests/webapp.mjs` runs the page's side in Node on a stand-in window.
- `examples/webapp` is a sample: each feature behind a button, and with `WEBAPP_SELFTEST=1`
  a self-test in a real window (commands, errors, events, an answer from a thread, bytes
  both ways, the program's scheme, a range, `<audio>` through the scheme, the window, and
  whether the page has a secure context, WebGL2 and WebGPU), which ends the program with its
  result. `tools/ci_host.sh macos` and `linux` run it.

## What is not there yet

- Menus of the app's own (macOS has the standard menu bar; Windows and Linux have none),
  tray icons, notifications, global shortcuts.
- The system's file drag and drop as events (the page's own drop events work, without paths).
- `ui release` and `ui serve` for web apps: no installer or archive yet, and no rebuild on
  change (a dev server reloads the page; the program is built again with `ui run`).
- Window position, maximize and resize events; frameless or transparent windows.
- Cross-origin isolation (`SharedArrayBuffer` needs `Cross-Origin-Opener-Policy` and
  `-Embedder-Policy` on the front end's answers).
- Request bodies: a scheme's handler sees the method, not a `POST` body (send data through
  `invoke`, bytes included).
- A permission system per page, as Tauri's capabilities.
