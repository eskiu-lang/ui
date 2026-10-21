// Runs the REAL wasm app and DOM renderer with a small recording DOM (no npm deps).
import assert from 'node:assert/strict';
import {performance} from 'node:perf_hooks';
import {writeFileSync} from 'node:fs';
import createApp from '../build/web/app.mjs';
import {createHost, uiDispatch} from '../web/renderer.mjs';
import {createFrameScheduler} from '../web/scheduler.mjs';

function fakeFrames(tick) {
  let sequence = 0;
  const callbacks = new Map();
  const scheduler = createFrameScheduler(tick, {
    requestFrame: callback => { callbacks.set(++sequence, callback); return sequence; },
    cancelFrame: id => callbacks.delete(id),
  });
  return {
    scheduler,
    frame() {
      const queued = [...callbacks.values()]; callbacks.clear();
      for (const callback of queued) callback();
      return queued.length;
    }
  };
}

class Element {
  constructor(tag) { this.tagName = tag; this.children = []; this.dataset = {}; this.style = {}; this.textContent = ''; this.parentNode = null; this.listeners = {}; this.attributes = {}; this.value = ''; }
  setAttribute(name, value) { this.attributes[name] = value; }
  removeAttribute(name) { delete this.attributes[name]; }
  focus() { globalThis.document.activeElement = this; }
  blur() { if (globalThis.document.activeElement === this) globalThis.document.activeElement = null; }
  addEventListener(kind, callback) { this.listeners[kind] = callback; }
  insertBefore(node, before) {
    node.remove();
    const index = before ? this.children.indexOf(before) : this.children.length;
    assert(index >= 0, 'insert reference belongs to parent');
    this.children.splice(index, 0, node); node.parentNode = this;
  }
  remove() {
    if (this.parentNode) this.parentNode.children.splice(this.parentNode.children.indexOf(this), 1);
    this.parentNode = null;
  }
  contains(node) { return this === node || this.children.some(child => child.contains(node)); }
  closest(selector) { return selector.startsWith(this.tagName + '[') ? this : this.parentNode?.closest(selector); }
}
globalThis.document = {createElement: tag => new Element(tag)};
const root = new Element('root');
let app;
const host = createHost(root, (...event) => uiDispatch(app, ...event));
const frames = fakeFrames(() => app._ui_tick());
host.schedule = () => frames.scheduler.request();
host.cancel = () => frames.scheduler.cancel();
app = await createApp({uiHost: host});
const find = text => [...host.nodes.values()].filter(node => node.textContent === text);
function click(node) {
  assert(node, 'button exists');
  root.listeners.click({target: node});
}
const timings = {};
app._ui_start(0);
const start = performance.now();
for (let i = 0; i < 1000; i++) click(find('Increment')[0]);
assert.equal(find('Count: 0').length, 1, 'DOM waits for animation frame');
assert.equal(frames.frame(), 1, 'a thousand clicks coalesce into one host frame');
assert.equal(frames.scheduler.frames, 1);
assert.equal(frames.frame(), 0, 'idle scheduler stops');
timings.counter1000Ms = performance.now() - start;
assert.equal(find('Count: 1000').length, 1);
assert.equal(root.children[0].style.padding, '16px');
app._ui_stop();
assert.equal(host.nodes.size, 0);
assert.equal(root.children.length, 0);
app._ui_start(2);
assert.equal(find('Progress: 0').length, 1);
assert.equal(frames.frame(), 1, 'effect schedules first follow-up');
assert.equal(find('Progress: 1').length, 1);
assert.equal(frames.frame(), 1, 'effect schedules second follow-up');
assert.equal(find('Progress: 2').length, 1);
assert.equal(frames.frame(), 0, 'settled effects stop requesting frames');
app._ui_stop();
app._ui_start(0);
click(find('Increment')[0]);
assert(frames.scheduler.pending);
app._ui_stop();
assert(!frames.scheduler.pending, 'shutdown cancels pending callback');
assert.equal(frames.frame(), 0);
app._ui_tick(); // A stale callback after shutdown is a no-op.
assert.equal(host.nodes.size, 0);

// New wasm instance resets the synthetic root ID.
const todoRoot = new Element('root');
let todo;
const todoHost = createHost(todoRoot, (...event) => uiDispatch(todo, ...event));
const todoFrames = fakeFrames(() => todo._ui_tick());
todoHost.schedule = () => todoFrames.scheduler.request();
todoHost.cancel = () => todoFrames.scheduler.cancel();
todo = await createApp({uiHost: todoHost});
const todoFind = text => [...todoHost.nodes.values()].filter(node => node.textContent === text);
const todoClick = text => {
  todoRoot.listeners.click({target: todoFind(text)[0]});
  todoFrames.frame();
};
todo._ui_start(1);
assert.equal(todoFind('Pending: 2').length, 1);
const firstRow = todoFind('Try Eskiu UI')[0].parentNode;
todoClick('Edit');
assert(firstRow.children.some(node => node.textContent === 'Editing'));
todoClick('Reverse');
assert.equal(todoFind('Try Eskiu UI')[0].parentNode, firstRow, 'key preserves DOM node');
assert(firstRow.children.some(node => node.textContent === 'Editing'), 'key preserves editing state');
assert.equal(firstRow.parentNode.children.filter(node => node.dataset.kind === 2).at(-1), firstRow);
todoClick('Done');
assert.equal(todoFind('Pending: 1').length, 1);
todoClick('Add');
assert.equal(todoFind('Pending: 1').length, 1, 'Add with an empty draft does nothing');
const logo = [...todoHost.nodes.values()].find(node => node.tagName === 'img');
assert(logo && logo.attributes.src === 'logo.png' && logo.attributes.alt === 'Eskiu' && logo.style.width === '48px', 'logo image');
const field = [...todoHost.nodes.values()].find(node => node.tagName === 'input');
assert.equal(field.attributes['aria-label'], 'New task');
assert.equal(field.attributes.placeholder, 'New task');
todoRoot.listeners.focusin({target: field});
field.value = 'Write the docs';
todoRoot.listeners.input({target: field});
todoFrames.frame();
todoClick('Add');
assert.equal(todoFind('Pending: 2').length, 1);
assert.equal(todoFind('Write the docs').length, 1, 'the typed title is a new row');
assert.equal(field.value, '', 'the draft clears after Add');
const todoStart = performance.now();
for (let i = 0; i < 1000; i++) todoClick('Reverse');
timings.todoReverse1000Ms = performance.now() - todoStart;
while (todoFind('Delete').length) todoClick('Delete');
assert.equal(todoFind('Nothing to do').length, 1);
assert.equal(todoFind('Pending: 0').length, 1);
todo._ui_stop();
assert.equal(todoHost.nodes.size, 0);
assert.equal(todoRoot.children.length, 0);
timings.wasmBytes = (await import('node:fs')).statSync(new URL('../build/web/app.wasm', import.meta.url)).size;
writeFileSync(new URL('../build/web-results.json', import.meta.url), JSON.stringify(timings, null, 2) + '\n');
console.log('PASS wasm batching, effect follow-ups, restart, cancellation, TODO identity and teardown', timings);
