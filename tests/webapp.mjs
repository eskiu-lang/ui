// The page's side of <ui/webapp> (js/webapp/bridge.js and index.js) on a stand-in window: what
// it posts to the program, how it takes the program's answers and events, bytes both ways,
// convertFileSrc per platform, the dialogs' options and the close a page intercepts.
//
//   node tests/webapp.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const bridge = readFileSync(new URL('../js/webapp/bridge.js', import.meta.url), 'utf8');

// A window of `os` whose posts land in `posted`; `answer(message)` is the program's reply.
function page(os) {
  const posted = [];
  const post = (text) => posted.push(JSON.parse(text));
  const fetched = [];
  const window = {
    __ESKIU_HOST__: { os, window: 3 },
    webkit: { messageHandlers: { eskiu: { postMessage: post } } },
    chrome: { webview: { postMessage: post } },
  };
  const context = vm.createContext({
    window, btoa, console, Promise, Map, Set, Object, Array, ArrayBuffer, Uint8Array, JSON, String, Error,
    fetch: async (url) => { fetched.push(url); return { ok: true, arrayBuffer: async () => new Uint8Array([9, 8, 7]).buffer }; },
  });
  vm.runInContext(bridge, context);
  return { eskiu: window.__ESKIU__, posted, fetched, window };
}

const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
let passed = 0;
async function test(name, fn) { await fn(); passed++; console.log(`PASS ${name}`); }

await test('invoke posts and resolves', async () => {
  const { eskiu, posted } = page('macos');
  const result = eskiu.invoke('greet', { name: 'Ada' });
  assert.deepEqual(posted[0], { kind: 'invoke', id: 1, cmd: 'greet', args: { name: 'Ada' } });
  eskiu.receive({ kind: 'reply', id: 1, ok: true, value: 'Hello' });
  assert.equal(await result, 'Hello');
});

await test('invoke rejects with the error', async () => {
  const { eskiu } = page('linux');
  const result = eskiu.invoke('fail');
  eskiu.receive({ kind: 'reply', id: 1, ok: false, error: 'it broke' });
  await assert.rejects(result, (e) => e === 'it broke');
});

await test('bytes in arguments and in answers', async () => {
  const { eskiu, posted, fetched } = page('windows');
  const result = eskiu.invoke('echo', { data: new Uint8Array([0, 1, 2, 255]), nested: [new Uint8Array([7]).buffer] });
  assert.deepEqual(posted[0].args, { data: { $bytes: 'AAEC/w==' }, nested: [{ $bytes: 'Bw==' }] });
  eskiu.receive({ kind: 'reply', id: 1, ok: true, bytes: '5' });
  const bytes = await result;
  assert.deepEqual(Array.from(bytes), [9, 8, 7]);
  assert.deepEqual(fetched, ['https://app.localhost/__eskiu/bytes/5']);
});

await test('events from the program, listen and its end', async () => {
  const { eskiu } = page('macos');
  const seen = [];
  const stop = await eskiu.listen('tick', (e) => seen.push([e.payload, e.windowId]));
  eskiu.receive({ kind: 'event', event: 'tick', window: 0, payload: { n: 1 } });
  stop();
  eskiu.receive({ kind: 'event', event: 'tick', window: 0, payload: { n: 2 } });
  assert.deepEqual(seen, [[{ n: 1 }, 0]]);
});

await test('once', async () => {
  const { eskiu } = page('macos');
  let count = 0;
  await eskiu.once('ready', () => count++);
  await tick();
  eskiu.receive({ kind: 'event', event: 'ready', window: 3, payload: null });
  eskiu.receive({ kind: 'event', event: 'ready', window: 3, payload: null });
  assert.equal(count, 1);
});

await test('emit to the program', async () => {
  const { eskiu, posted } = page('linux');
  await eskiu.emit('ping', { at: 1 });
  assert.deepEqual(posted[0], { kind: 'emit', event: 'ping', payload: { at: 1 } });
});

await test('convertFileSrc per platform', async () => {
  assert.equal(page('macos').eskiu.convertFileSrc('/Users/me/My Game/a#1.png'), 'asset://localhost/Users/me/My%20Game/a%231.png');
  assert.equal(page('linux').eskiu.convertFileSrc('/home/me/x.ogg', 'rtp'), 'rtp://localhost/home/me/x.ogg');
  assert.equal(page('windows').eskiu.convertFileSrc('C:\\Games\\Mine\\a b.png'), 'https://asset.localhost/C:/Games/Mine/a%20b.png');
  assert.equal(page('windows').eskiu.schemeUrl('rtp', 'maps/a.json'), 'https://rtp.localhost/maps/a.json');
});

await test('dialogs as @tauri-apps/plugin-dialog', async () => {
  const { eskiu, posted } = page('macos');
  eskiu.dialog.open({ multiple: true, filters: [{ name: 'PNG', extensions: ['png'] }] });
  eskiu.dialog.ask('Delete it?', { title: 'Delete', kind: 'warning' });
  eskiu.dialog.message('Done', 'Export');
  assert.deepEqual(posted[0].args, { multiple: true, filters: [{ name: 'PNG', extensions: ['png'] }], type: 'open' });
  assert.deepEqual(posted[1].args, { title: 'Delete', kind: 'warning', message: 'Delete it?', type: 'confirm', okLabel: 'Yes', cancelLabel: 'No' });
  assert.deepEqual(posted[2].args, { title: 'Export', message: 'Done', type: 'message' });
});

await test('onCloseRequested: kept open, then closed', async () => {
  const { eskiu, posted } = page('macos');
  let keep = true;
  const pending = eskiu.window.onCloseRequested((event) => { if (keep) event.preventDefault(); });
  await tick();
  assert.deepEqual(posted.at(-1).args, { on: true, op: 'interceptClose' });
  eskiu.receive({ kind: 'reply', id: posted.at(-1).id, ok: true, value: null });
  await pending;
  eskiu.receive({ kind: 'event', event: 'ui:close-requested', window: 3, payload: null });
  await tick();
  assert.ok(!posted.some((m) => m.args && m.args.op === 'destroy'));
  keep = false;
  eskiu.receive({ kind: 'event', event: 'ui:close-requested', window: 3, payload: null });
  await tick();
  assert.deepEqual(posted.at(-1).args, { op: 'destroy' });
});

await test('index.js wraps the bridge', async () => {
  const { window, posted } = page('macos');
  globalThis.window = window;
  const api = await import('../js/webapp/index.js');
  assert.equal(api.isEskiu(), true);
  assert.equal(api.platform(), 'macos');
  const result = api.invoke('x', { a: 1 });
  window.__ESKIU__.receive({ kind: 'reply', id: posted.at(-1).id, ok: true, value: 2 });
  assert.equal(await result, 2);
  assert.equal(api.getCurrentWindow().id, 3);
  delete globalThis.window;
  await assert.rejects(api.invoke('x'), /not in an Eskiu window/);
  assert.equal(api.isEskiu(), false);
});

console.log(`PASS webapp page (${passed} tests)`);
