// The service worker of an app with [updates] (docs/updates.md): it serves the files of the
// asset bundle in use from Cache Storage at the assets' own URLs, and makes the bundle the page
// downloaded the one in use when the page loads again. The page (hosts/web/renderer.mjs,
// command 12) keeps the files by hash and writes the state: {active, ready}, each null or
// {version, files: {path: hash}}.
const CACHE = 'eskiu-ui-updates';
const scope = new URL(self.registration.scope).pathname;
const stateURL = new URL(scope + '__ui/updates/state', self.location.origin).href;
const fileURL = hash => new URL(scope + '__ui/updates/' + hash, self.location.origin).href;
const TYPES = {png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp',
  svg: 'image/svg+xml', json: 'application/json', txt: 'text/plain; charset=utf-8', css: 'text/css',
  ttf: 'font/ttf', otf: 'font/otf', woff: 'font/woff', woff2: 'font/woff2', mp3: 'audio/mpeg', wav: 'audio/wav',
  mp4: 'video/mp4', wasm: 'application/wasm', html: 'text/html; charset=utf-8'};

let state = null;
async function load() {
  const cache = await caches.open(CACHE);
  const saved = await cache.match(stateURL);
  state = saved ? await saved.json() : {active: null, ready: null};
  return cache;
}
// The waiting bundle becomes the one in use; the files no bundle needs are dropped.
async function promote() {
  const cache = await load();
  if (!state.ready) return;
  state.active = state.ready;
  state.ready = null;
  await cache.put(stateURL, new Response(JSON.stringify(state), {headers: {'Content-Type': 'application/json'}}));
  const kept = new Set(Object.values(state.active.files).map(fileURL));
  for (const request of await cache.keys()) {
    if (request.url !== stateURL && !kept.has(request.url)) await cache.delete(request);
  }
}
let loading = load();

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
// The page wrote a new state ('state'), or asked to use the waiting bundle now ('apply').
self.addEventListener('message', event => {
  if (event.data === 'state') loading = loading.then(load, load);
  else if (event.data === 'apply') loading = loading.then(promote, promote);
});
self.addEventListener('fetch', event => {
  const request = event.request;
  const url = new URL(request.url);
  if (request.method !== 'GET' || url.origin !== self.location.origin || !url.pathname.startsWith(scope)) return;
  // A page loading: the bundle downloaded last becomes the one in use, before its assets are
  // asked for (the page itself comes from the network as always).
  if (request.mode === 'navigate') {
    loading = loading.then(promote, promote);
    return;
  }
  const path = decodeURIComponent(url.pathname.slice(scope.length));
  if (path.startsWith('__ui/')) return;
  // Known not to be updated: not intercepted at all.
  if (state && !state.active?.files?.[path] && !state.ready) return;
  event.respondWith((async () => {
    await loading.catch(() => {});
    const hash = state?.active?.files?.[path];
    if (!hash) return fetch(request);
    const cached = await (await caches.open(CACHE)).match(fileURL(hash));
    if (!cached) return fetch(request);
    const extension = path.split('.').pop().toLowerCase();
    return new Response(cached.body, {headers: {'Content-Type': TYPES[extension] ?? 'application/octet-stream'}});
  })());
});
