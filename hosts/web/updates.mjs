// Asset updates (src/apis/updates.esk, docs/updates.md), command 12. On the web the files go
// into Cache Storage by hash, and the service worker (ui-sw.js) serves the bundle in use at
// the assets' URLs. The desktop webview hosts (app://localhost/, https://app.assets/) run no
// service worker: they keep the files in the app's data directory and serve them in place of
// the bundled ones, and the page passes each operation to them at __ui/updates/
// (hosts/webview_updates.h).
// `fetch URL HASH PATH` downloads one and checks its SHA-256 (answered "" or why not);
// `ready VERSION "PATH HASH"...` records the bundle for the next load (and registers the
// worker); `apply` uses it now.
export function createUpdates(dispatch, base) {
  const hosted = typeof location !== 'undefined' && (location.protocol === 'app:' || location.host === 'app.assets');
  async function viaHost(op, rest) {
    const query = new URLSearchParams();
    if (op === 'fetch') {
      query.set('url', rest[0]);
      query.set('hash', rest[1]);
      query.set('path', rest[2]);
    } else if (op === 'ready') {
      query.set('version', rest[0]);
      for (const line of rest.slice(1)) query.append('file', line);
    }
    const response = await fetch(new URL(`__ui/updates/${op}?${query}`, document.baseURI).href, {cache: 'no-store'});
    return response.ok ? await response.text() : `the app could not keep the bundle (${response.status})`;
  }
  return async function update(c) {
    const [op, ...rest] = c.strings;
    const answer = text => { if (c.request) dispatch(c.request, 0, 15, text); };
    try {
      if (hosted) { answer(await viaHost(op, rest)); return; }
      const scope = base();
      const at = path => new URL(scope + '__ui/updates/' + path, location.origin).href;
      const cache = await caches.open('eskiu-ui-updates');
      if (op === 'fetch') {
        const [url, hash] = rest;
        if (await cache.match(at(hash))) { answer(''); return; }
        const response = await fetch(url, {cache: 'no-store'});
        if (!response.ok) { answer(`a file of the bundle did not download (${response.status})`); return; }
        const bytes = await response.arrayBuffer();
        const digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map(b => b.toString(16).padStart(2, '0')).join('');
        if (digest !== hash) { answer('a file of the bundle does not match its hash'); return; }
        await cache.put(at(hash), new Response(bytes));
        answer('');
        return;
      }
      const saved = await cache.match(at('state'));
      const state = saved ? await saved.json() : {active: null, ready: null};
      if (op === 'ready') {
        const [version, ...lines] = rest;
        state.ready = {version, files: Object.fromEntries(lines.map(line => line.split(' ')))};
      } else if (op === 'apply' && state.ready) {
        state.active = state.ready;
        state.ready = null;
      }
      await cache.put(at('state'), new Response(JSON.stringify(state), {headers: {'Content-Type': 'application/json'}}));
      if (navigator.serviceWorker) {
        const registration = await navigator.serviceWorker.register(new URL('ui-sw.js', document.baseURI).href, {scope});
        (registration.active ?? navigator.serviceWorker.controller)?.postMessage('state');
      }
      answer('');
    } catch (error) {
      answer(`the page could not keep the bundle: ${error}`);
    }
  };
}
