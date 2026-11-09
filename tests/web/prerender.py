#!/usr/bin/env python3
"""The prerendered first frame on the web (docs/hosts.md#the-first-frame), in headless Chrome
on examples/counter's release build:

- the page shows the counter before the module runs (the module blocked);
- the module adopts the page's elements: each element is the one the page was parsed with,
  none is made again, and a press on the prerendered button counts;
- a page whose first commit is not the one prerendered drops the markup and shows the app's,
  with no element left over;
- a page opened on another route starts empty, as before;
- the first contentful paint with and without the prerendered frame (--measure N: N loads
  each, the medians), on the local server as it is and on a slow mobile network (150 ms of
  latency, 1.6 Mbit/s down, as DevTools' "Slow 4G").

    python3 tests/web/prerender.py [--measure N]

Needs emcc, node and Chrome (UI_CHROME).
"""
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

HERE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(HERE / "tools"))
sys.path.insert(0, str(HERE / "tests/web"))
from cdp import SLOW_4G, Chrome, first_paint  # noqa: E402
from serve import serve  # noqa: E402

COUNTER = HERE / "examples/counter"
WEB = COUNTER / "build/ui/web"

# Marks every app element when the document is parsed and before any module runs (the
# document becomes interactive before the deferred and module scripts run), and keeps the
# text the page showed then.
MARK = """
document.addEventListener('readystatechange', () => {
  if (document.readyState !== 'interactive') return;
  const app = document.getElementById('app');
  window.uiParsed = {text: app.innerText, count: app.querySelectorAll('[data-ui-id]').length};
  for (const element of app.querySelectorAll('[data-ui-id]')) element.uiParsed = true;
});
"""

failures = []


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def build():
    env = {k: v for k, v in os.environ.items() if k not in ("UI_DEV", "UI_TEST")}
    subprocess.run([HERE / "build/ui", "build", "web"], cwd=COUNTER, env=env, check=True, capture_output=True)
    if 'data-ui-prerender="' not in (WEB / "index.html").read_text():
        raise SystemExit("FAIL: index.html has no prerendered frame")


def main():
    measure = int(sys.argv[sys.argv.index("--measure") + 1]) if "--measure" in sys.argv else 0
    build()
    server, url = serve(WEB)
    with Chrome() as chrome:
        page = chrome.page()
        page.send("Runtime.enable")
        page.send("Page.addScriptToEvaluateOnNewDocument", {"source": MARK})

        # Content before the module: the module and the wasm never load.
        page.send("Network.enable")
        page.send("Network.setBlockedURLs", {"urls": ["*app.mjs", "*app.wasm"]})
        page.goto(url)
        page.wait("document.readyState === 'complete'")
        text = page.eval("document.getElementById('app').innerText")
        check("Count: 0" in text and "Increment" in text, f"the page shows the counter with no module ({text!r})")
        paint = page.wait("performance.getEntriesByName('first-contentful-paint')[0]?.startTime")
        check(paint > 0, f"a first contentful paint with no module ({paint:.1f} ms)")
        page.send("Network.setBlockedURLs", {"urls": []})

        # Adoption: the same elements, and their handlers work.
        page.goto(url)
        page.wait("document.getElementById('app').dataset.uiPrerender === 'adopted'")
        parsed = page.eval("window.uiParsed")
        check(parsed and "Count: 0" in parsed["text"], "the parsed page showed the counter before the module ran")
        kept = page.eval("[...document.querySelectorAll('#app [data-ui-id]')].every(e => e.uiParsed)")
        count = page.eval("document.querySelectorAll('#app [data-ui-id]').length")
        check(kept and count == parsed["count"], f"the module kept all {count} elements it found (none made again)")
        page.eval("[...document.querySelectorAll('#app button')].find(b => b.textContent === 'Increment').click()")
        page.wait("document.getElementById('app').innerText.includes('Count: 1')")
        same = page.eval("[...document.querySelectorAll('#app span')].find(s => s.textContent === 'Count: 1').uiParsed === true")
        check(same, "a press on the adopted button counts, on the adopted text")

        # Not the commit prerendered: the markup is replaced, nothing of it left.
        server.handler.rewrite = staticmethod(lambda html: re.sub(r'data-ui-prerender="[0-9a-f]+"', 'data-ui-prerender="00000000"', html)
                                              .replace("Eskiu counter", "Stale title"))
        page.goto(url)
        page.wait("document.getElementById('app').dataset.uiPrerender === 'replaced'")
        page.wait("document.getElementById('app').innerText.includes('Eskiu counter')")
        text = page.eval("document.getElementById('app').innerText")
        stale = page.eval("document.querySelectorAll('#app [data-ui-id]').length === "
                          "[...document.querySelectorAll('#app [data-ui-id]')].filter(e => !e.uiParsed).length")
        check("Stale title" not in text and text.count("Count: 0") == 1 and stale,
              "a different first commit replaces the markup: no prerendered element is left")
        server.handler.rewrite = None

        # Another route: the page starts empty and the app renders there.
        page.goto(url + "elsewhere")
        page.wait("document.readyState === 'complete' && !!document.querySelector('#app [data-ui-id]')")
        parsed = page.eval("window.uiParsed")
        check(parsed and parsed["count"] == 0, "a page on another route starts empty")

        if measure:
            blank = staticmethod(lambda html: re.sub(r'<div id="app" data-ui-prerender.*?</script>', '<div id="app"></div>', html, flags=re.S))
            for network in ("local", "slow"):
                if network == "slow":
                    page.send("Network.emulateNetworkConditions", SLOW_4G)
                with_frame = [first_paint(page, url) for _ in range(measure)]
                server.handler.rewrite = blank
                without = [first_paint(page, url) for _ in range(measure)]
                server.handler.rewrite = None
                print(f"first contentful paint ({network}): {statistics.median(without):.1f} ms without the prerendered frame, "
                      f"{statistics.median(with_frame):.1f} ms with it (medians of {measure})")
    server.shutdown()
    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
