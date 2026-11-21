# Stability

What an app can count on from one release of ui to the next, and what "Preview" means. The
compiler and the standard library follow Eskiu's own policy (Eskiu's `docs/lang/stability.md`);
this is ui's, on the same lines.

## The public surface

The public surface is every name in `docs/api.md` without a leading `_`: the elements, the
kit, Style and its enums, the hooks and state, the APIs (`ui_*` functions an app calls) and
`ui.toml`'s keys (docs/getting-started.md). Names that start with `_` are internal: they can
change in any release, and an app or a package should not use them.

A few internal names have no `_` because hosts or packages reach them, and they are not part
of the promise either:

- The runtime's own types: `Ui`, `Node`, `Source`, `Edge`, `Pending`, `KeyTable`, `Commit`
  and their methods. A package that needs one (a hook built on `ui_slot`, a native module) is tied to the
  ui release it was built for, and says so in its `eskiu.toml`.
- The host ABI: what `hosts/core.h` lists (`ui_start`, `ui_dispatch_*`, `ui_report_*`,
  `ui_tick`, `ui_splash`...) and the commit format of `src/core/wire.esk`. They change with
  the hosts in this repository, which are built from the same release.

Some names without `_` are public for the authors of components, packages and tests rather
than for apps, and are part of the promise: `ui_own`, `ui_own_fn` and the `ui_release_*`
functions (a component that keeps closures frees them with these), `ui_slot` (a hook's
storage), and `ui_decode` with `Mutation` (a test reads a commit with them).

## Versions

ui follows semantic versioning. Before 1.0:

- A patch release (0.7.0 to 0.7.1) fixes bugs and can add names. It does not rename or remove a
  public name, or change what one does in a way a working app would notice.
- A minor release (0.7 to 0.8) can rename, remove or change public names. When it can, it
  keeps the old name for that release as a wrapper marked `deprecated("use NEW")` (the
  compiler warns at each use and names the replacement), and the next minor release removes
  it. The CHANGELOG lists each such change under "Changed", with what to edit.
- From 1.0, breaking changes wait for a major release, with the same deprecation step first.

The compiler version an app needs is in `eskiu.toml`'s `eskiu` key and in the CHANGELOG.

## Preview and general availability

A part of ui is in Preview while it may still change in a minor release without a deprecation
step, or while it is not yet checked on its platform the way the rest is. What is in Preview
says so in its docs.

| Part | Status |
|---|---|
| The core, the elements, the kit, state and navigation | Leaving Preview with 1.0 |
| The iOS, Android and web hosts | Leaving Preview with 1.0 |
| The macOS, Linux and Windows hosts | Preview: no conformance runs of their own yet, and gaps listed in docs/hosts.md |
| The device packages (ui-camera, ui-location, ui-notifications, ...) | Preview: tested against stand-ins, each package's own docs |
| `ui agent` and `ui test` | Leaving Preview with 1.0 on the web, iOS and Android |

A part leaves Preview when:

- its public surface is in `docs/api.md`, documented, and follows the rules above;
- it has the accessibility every element needs (a role, its states, `disabled`, the system's
  text size) and the text input a form needs (secure entry, keyboard, return and submit,
  multiline);
- its errors say what failed and where, on every platform it runs on;
- CI runs it (on its host, for a host) and `ui test` runs on it without flaky failures;
- its docs match what it does.
