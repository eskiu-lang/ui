# Changelog

All notable changes to ui are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- `<ui/webapp>` (`src/webapp.esk`): an Eskiu program as the backend of a web front end, such
  as a React bundle, in the desktop's webview, as Tauri does for Rust. The page runs the
  program's commands with JSON arguments and gets JSON, strings, bytes or errors back, in the
  call or later from any thread (`WebappCall`, `WebappReply`); events go both ways
  (`webapp_emit`, `webapp_listen`); URL schemes of the program serve files and bytes with
  ranges and CORS (`webapp_protocol`, `webapp_protocol_dir`); windows have a title, a size,
  a smallest size, full screen and a close the program or the page can refuse; file,
  folder, save, message and confirm dialogs are the system's. See `docs/webapp.md`.
- The web app hosts: WKWebView on macOS (`hosts/macos/webapp.m`, with the standard menu
  bar), WebView2 on Windows (`hosts/windows/webapp.cpp`, schemes as `https://NAME.localhost/`)
  and WebKitGTK on Linux (`hosts/linux/webapp.c`). A window's page stays on the app's origin;
  a link elsewhere opens in the browser.
- `@eskiu/webapp` (`js/webapp`): the page's module, in the shape of `@tauri-apps/api`
  (`invoke`, `listen`, `once`, `emit`, `convertFileSrc`, the dialogs of
  `@tauri-apps/plugin-dialog`, `getCurrentWindow`), with TypeScript types. The program also
  serves it as `/@eskiu/webapp.js`.
- `ui build` and `ui run` for `macos`, `linux` and `windows` build a project with `[webapp]
  frontend` in `ui.toml` as a web app (`before_build` runs the bundler first);
  `ui run TARGET --dev` loads `[webapp] dev_url`. A Windows web app builds from macOS and
  Linux with llvm-mingw.
- `examples/webapp`: each feature behind a button, and a self-test (`WEBAPP_SELFTEST=1`) in a
  real window, run by `tools/ci_host.sh macos` and `linux`; `tests/webapp.esk` and
  `tests/webapp.mjs` in `tools/ci.sh`.
