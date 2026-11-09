#!/usr/bin/env python3
"""Asset updates on the web (docs/updates.md), in headless Chrome against a local update
server: tests/web/updates-app is built for release, a changed asset and a new one are
published with `ui publish`, and served from another origin (127.0.0.1:8793, the app's
[updates] url).

- the app downloads the bundle after its first frame and keeps it for the next load: the
  running page still gets the old file;
- the next load uses it: the service worker serves the new file and the added one at their
  own URLs, and the core says which version is in use;
- a bundle whose file does not match its hash, and one whose signature does not, are refused
  and the page keeps the bundle it has.

    python3 tests/web/updates.py

Needs emcc and Chrome (UI_CHROME). The signing key is made for the test in build/.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(HERE / "tools"))
sys.path.insert(0, str(HERE / "tests/web"))
from cdp import Chrome  # noqa: E402
from serve import serve  # noqa: E402

APP = HERE / "build/updates-app"
WEB = APP / "build/ui/web"
BUNDLE = APP / "build/ui/updates"
UPDATES = HERE / "build/updates-server"

failures = []


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def ui(*args):
    env = {k: v for k, v in os.environ.items() if k not in ("UI_DEV", "UI_TEST")}
    env["UI_UPDATE_KEY_FILE"] = str(APP / "build/test.key")
    result = subprocess.run([HERE / "build/ui", *args], cwd=APP, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"ui {' '.join(args)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def publish(version):
    """`ui publish` into the update server's directory."""
    ui("publish", "--version", str(version))
    shutil.rmtree(UPDATES, ignore_errors=True)
    shutil.copytree(BUNDLE, UPDATES)


def text(page, test_id):
    return page.eval(f"[...document.querySelectorAll('#app span')].map(s => s.textContent).find(t => t.startsWith('{test_id}: '))")


def fetched(page, path):
    return page.eval(f"fetch('{path}', {{cache: 'no-store'}}).then(r => r.ok ? r.text() : 'HTTP ' + r.status)")


def load(page, url):
    page.goto("about:blank")
    page.goto(url)
    page.wait("document.readyState === 'complete' && !!document.querySelector('#app [data-ui-id]')")


def main():
    shutil.rmtree(APP, ignore_errors=True)
    shutil.copytree(HERE / "tests/web/updates-app", APP, ignore=shutil.ignore_patterns("build"))
    (APP / "eskiu.lock").write_text((APP / "eskiu.lock").read_text().replace("../../..", str(HERE)))
    (APP / "eskiu.toml").write_text((APP / "eskiu.toml").read_text().replace("../../..", str(HERE)))
    ui("build", "web")
    # The bundle: data.txt changed, new.txt added, logo.png as built (not downloaded).
    (APP / "assets/data.txt").write_text("second\n")
    (APP / "assets/new.txt").write_text("added\n")
    publish(100)
    manifest = (UPDATES / "manifest.txt").read_text()
    check(manifest.startswith("eskiu-ui-update 1\napp org.eskiu.test.updates\nbase 1.0.0\nversion 100\n")
          and re.search(r"^sig [0-9a-f]{128}$", manifest, re.M) and manifest.count("\nfile ") == 3,
          "ui publish writes a signed manifest of the three assets")

    app_server, url = serve(WEB)
    update_server, _ = serve(UPDATES, cors=True, port=8793)
    with Chrome() as chrome:
        page = chrome.page()
        page.send("Runtime.enable")
        load(page, url)
        page.wait("[...document.querySelectorAll('#app span')].some(s => s.textContent === 'Ready: 100')")
        check(text(page, "Version") == "Version: ", "the first load uses the assets it was built with")
        check(fetched(page, "data.txt") == "first\n", "bundle 100 waits: the running page still gets the old file")
        kept = page.eval("caches.open('eskiu-ui-updates').then(c => c.keys()).then(k => k.length)")
        check(kept == 3, f"the two changed files and the state are in Cache Storage ({kept} entries)")

        load(page, url)
        page.wait("!!navigator.serviceWorker.controller")
        check(text(page, "Version") == "Version: 100" and text(page, "Ready") == "Ready: ", "the next load uses bundle 100")
        check(fetched(page, "data.txt") == "second\n", "the service worker serves the changed file at its URL")
        check(fetched(page, "new.txt") == "added\n", "and the added one, which the app's server does not have")
        check(page.eval("document.querySelector('#app img').complete && document.querySelector('#app img').naturalWidth > 0"),
              "the unchanged image still loads")

        # A file that does not match its hash.
        (APP / "assets/data.txt").write_text("third\n")
        publish(200)
        changed = re.search(r"^file data.txt \d+ ([0-9a-f]{64})$", (UPDATES / "manifest.txt").read_text(), re.M).group(1)
        (UPDATES / "files" / changed).write_text("tampered\n")
        load(page, url)
        page.wait("[...document.querySelectorAll('#app span')].some(s => s.textContent.startsWith('Error: ') && s.textContent.length > 7)")
        check("does not match its hash" in text(page, "Error"), f"a file that does not match its hash is refused ({text(page, 'Error')})")
        load(page, url)
        check(text(page, "Version") == "Version: 100" and fetched(page, "data.txt") == "second\n", "and the page keeps bundle 100")

        # A signature that does not match.
        publish(300)
        manifest = (UPDATES / "manifest.txt").read_text()
        (UPDATES / "manifest.txt").write_text(manifest.replace("version 300", "version 301"))
        load(page, url)
        page.wait("[...document.querySelectorAll('#app span')].some(s => s.textContent.includes('signature'))")
        check("signature" in text(page, "Error"), f"a manifest changed after it was signed is refused ({text(page, 'Error')})")
        load(page, url)
        check(text(page, "Version") == "Version: 100" and fetched(page, "data.txt") == "second\n", "and the page keeps bundle 100")
    app_server.shutdown()
    update_server.shutdown()
    if failures:
        sys.exit(1)
    print("PASS web updates")


if __name__ == "__main__":
    main()
