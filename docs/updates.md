# Updates without a store release

An app with `[updates]` in its `ui.toml` updates its assets (images, fonts, data files in
`assets/`) without a new release in the stores: `ui publish` writes a signed bundle of them,
you upload it, and each installed app downloads it after its first frame and uses it from its
next start. Only assets are updated, never code: the code is native, and the stores' rules
keep code changes to their releases.

```toml
[updates]
url = "https://cdn.example.com/tasks/"   # where the bundle is served (required)
public_key = "1f0c...64 hex digits"       # the key apps check bundles with (see below)
include = ["screens", "logo.png"]         # what of assets/ a bundle may replace (default: all)
```

```bash
ui publish                  # build/ui/updates/: manifest.txt and files/
ui publish --version 42     # a version of your own (it must grow)
```

Then upload what `build/ui/updates/` holds to the URL, `manifest.txt` with a short cache time
(the files are named by their hash and never change).

## Keys

Bundles are signed with Ed25519. The secret key never goes in the project: the first
`ui publish` (or the first build with `[updates]`) makes one in
`$ESKIU_HOME/keys/APP_ID/updates.key` (`~/.eskiu` by default), readable by its owner only, and
prints its public half. `UI_UPDATE_KEY_FILE` names another key file, for a CI job that
publishes with a key from its secrets.

Each build with `[updates]` compiles in the public key: `[updates] public_key`, or the key of
the machine that builds. Put the public key in `ui.toml` once it is made, so a build on
another machine checks bundles with the same key; `ui publish` stops when its key is not that
one. An app built with one key refuses bundles signed with another, so keep the secret key safe
and backed up: lose it and the apps already installed take no more bundles until a release
with a new public key.

## What a bundle is

```
eskiu-ui-update 1
app com.example.tasks
base 1.2.0
version 59184022
file logo.png 2048 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
file screens/home.json 312 60303ae22b998861bce3b28f33eec1be758a213c86c93c076dbe9f558c11c752
sig 3f2a...128 hex digits
```

- `app` is the app's id and `base` its version (`[app] version`): an app takes only bundles
  for its own id and its own version, since a bundle is made for the code it ships with. A
  release with a new version starts afresh from the assets it ships, and its first bundle is
  published from it.
- `version` only grows: `ui publish` takes the seconds since 2024, as a release's build
  number. An app takes a bundle newer than the one it has and ignores the others.
- each `file` line is a path in `assets/`, its size and its SHA-256; the file is served as
  `files/SHA256`.
- `sig` is the Ed25519 signature of every byte before it.

## What the app does

After its first frame (never before it: the check does not hold the start up), the app asks
for `manifest.txt`. When the bundle is newer and for this app and version, the app checks the
signature against the public key compiled in, downloads only the files that differ from the
ones it was built with or already has, and checks each file's size and SHA-256. A file or a
signature that does not match stops the update: nothing of it is used, and the next start
asks again. Once every file is checked, the bundle waits for the next start.

At the next start, before the app renders, the bundle becomes the one in use:

- `Image(style, "logo.png")` shows the updated `logo.png`;
- `ui_asset_path("screens/home.json")` gives the updated file's path (iOS, Android);
- on the web, the page's requests for the assets (images, `ui_fetch("screens/home.json")`)
  get the updated files at their own URLs, from the service worker.

```eskiu
void UpdateBanner() {
    Column(Style{}) {
        // Reading these subscribes: the banner appears when a bundle has downloaded.
        if (ui_update_ready()[0] != 0) {
            Button("Use the new content") { ui_update_apply(); }
        }
    }
}
```

| Call | Does |
|---|---|
| `ui_update_version()` | The version of the bundle in use, `""` with the assets the app was built with |
| `ui_update_ready()` | The version of a newer bundle downloaded and checked, waiting for the next start, or `""` |
| `ui_update_error()` | Why the last check installed nothing (a download, a hash, the signature), or `""` |
| `ui_update_apply()` | Use the waiting bundle now: Images and `ui_asset_path` take its files from the next frame (on the web, what the page loads from then on; images already shown keep theirs until the page loads again) |
| `ui_update_check()` | Ask for a newer bundle again now (the app does once after its first frame), as when the app comes back to the foreground |

The calls exist in builds with `[updates]` only. `ui serve`'s builds leave updates out, so
the assets you edit are what you see; `[updates] dev = true` keeps them, to try a bundle under
`ui serve`.

## Where the files go

| Platform | Storage | Crypto |
|---|---|---|
| iOS | the app's Application Support directory, `ui-updates/HASH.EXT` | Ed25519 and SHA-256 in Eskiu (`src/apis/ed25519.esk`, `<hash>`) |
| Android | the app's files directory, `ui-updates/HASH.EXT` | the same |
| Web | Cache Storage (`eskiu-ui-updates`), served by the service worker `ui-sw.js` | Ed25519 in Eskiu (the wasm module); each file's SHA-256 by WebCrypto |
| macOS (AppKit) | Application Support/BUNDLE_ID, `ui-updates/HASH.EXT` (the iOS path; not yet run there) | as iOS |
| Linux (GTK) | `$XDG_DATA_HOME/ID`, `ui-updates/HASH.EXT` (the iOS path; not yet run there) | as iOS |
| Linux and macOS `--webview`, Windows | not yet: these run the web build from the app's own files, where a service worker does not run | |

The bundle in use and the waiting one are kept as their manifests in the app's saved data
(`ui.update.active`, `ui.update.ready`); files no bundle needs are removed at the next check.
The signature is checked again at each start. The Ed25519 code is TweetNaCl's, checked
against RFC 8032's test vectors (`tests/ed25519.esk`).

On the web, the page registers `ui-sw.js` (copied next to `index.html` in a build with
`[updates]`) once a bundle has downloaded; the worker makes it the one in use when the page
loads again. Service workers need HTTPS (or `localhost`). Serve the bundle from another origin
with CORS (`Access-Control-Allow-Origin`), since the page fetches it.

## Tests

`tests/ed25519.esk` checks the signatures, `tests/updates.esk` the manifest's checks (a changed
byte, another key, another app or version, a path out of `assets/`), a download of only the
changed files, a file that does not match its hash, the next start, `ui_update_apply` and the
removal of old files. `tests/web/updates.py` runs a bundle end to end in headless Chrome
against a local update server, and `tools/ci.sh` checks `ui publish`'s output.
