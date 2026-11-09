// The prerendered first frame (docs/hosts.md#the-first-frame): `ui build web` runs this with
// node on the web build. It starts the app on a small DOM of its own, as the page would at its
// root route, takes the first commit the renderer applied, and writes that DOM into
// index.html as static markup (the styles are inline, as the renderer sets them), with the
// commit's fingerprint. The page then shows it before the module loads, and the renderer adopts
// those elements when its own first commit is the same (hosts/web/renderer.mjs).
//
//   node prerender.mjs OUT_DIR      # OUT_DIR holds index.html, app.mjs and renderer.mjs
import {readFileSync, writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';

const VOID = new Set(['input', 'img']);
const escapeText = text => String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const escapeAttribute = text => escapeText(text).replace(/"/g, '&quot;');
const kebab = name => name.replace(/[A-Z]/g, c => '-' + c.toLowerCase());

// Inline styles by property, written as CSS in the order they were first set.
function makeStyle() {
  const values = new Map();
  return new Proxy({}, {
    get: (_, name) => name === 'cssText'
      ? [...values].map(([k, v]) => `${k.startsWith('webkit') ? '-' : ''}${kebab(k)}: ${v}`).join('; ')
      : (values.get(name) ?? ''),
    set: (_, name, value) => {
      if (value === '' || value === null || value === undefined) values.delete(name);
      else values.set(name, String(value));
      return true;
    },
  });
}

class Element {
  constructor(tag) {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.parentNode = null;
    this.dataset = {};
    this.style = makeStyle();
    this.attributes = new Map();
    this.text = null;
    this.className = '';
    this.id = '';
  }
  get textContent() { return this.text ?? this.children.map(c => c.textContent).join(''); }
  set textContent(value) { for (const c of [...this.children]) c.remove(); this.text = String(value); }
  get firstChild() { return this.children[0] ?? null; }
  get nextSibling() {
    const siblings = this.parentNode?.children;
    return siblings ? siblings[siblings.indexOf(this) + 1] ?? null : null;
  }
  get isConnected() { return true; }
  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  getAttribute(name) { return this.attributes.has(name) ? this.attributes.get(name) : null; }
  removeAttribute(name) { this.attributes.delete(name); }
  addEventListener() {}
  removeEventListener() {}
  focus() {}
  blur() {}
  click() {}
  appendChild(node) { this.insertBefore(node, null); }
  insertBefore(node, before) {
    node.remove();
    const at = before ? this.children.indexOf(before) : this.children.length;
    this.children.splice(at < 0 ? this.children.length : at, 0, node);
    node.parentNode = this;
    this.text = null;
  }
  remove() {
    if (this.parentNode) this.parentNode.children.splice(this.parentNode.children.indexOf(this), 1);
    this.parentNode = null;
  }
  replaceChildren() { for (const c of [...this.children]) c.remove(); this.text = null; }
  contains(node) { return node === this || this.children.some(c => c.contains(node)); }
  closest() { return null; }
  querySelectorAll() { return []; }
  html() {
    const tag = this.tagName.toLowerCase();
    let out = `<${tag}`;
    const attribute = (name, value) => { out += ` ${name}="${escapeAttribute(value)}"`; };
    if (this.id) attribute('id', this.id);
    for (const [name, value] of Object.entries(this.dataset)) attribute('data-' + kebab(name), value);
    if (this.className) attribute('class', this.className);
    for (const [name, value] of this.attributes) attribute(name, value);
    for (const name of ['type', 'min', 'max', 'step']) if (this[name] !== undefined) attribute(name, this[name]);
    if (this.tabIndex !== undefined) attribute('tabindex', this.tabIndex);
    if (this.value !== undefined && tag === 'input') attribute('value', this.value);
    if (this.checked) out += ' checked';
    const css = this.style.cssText;
    if (css) attribute('style', css);
    out += '>';
    if (VOID.has(tag)) return out;
    out += this.text !== null ? escapeText(this.text) : this.children.map(c => c.html()).join('');
    return out + `</${tag}>`;
  }
}

const out = process.argv[2];
if (!out) { console.error('usage: node prerender.mjs OUT_DIR'); process.exit(2); }
const head = new Element('head');
globalThis.document = {
  baseURI: 'http://localhost/',
  head,
  createElement: tag => new Element(tag),
  getElementById: id => head.children.find(c => c.id === id) ?? null,
  addEventListener() {},
};
globalThis.requestAnimationFrame = () => 0;
globalThis.cancelAnimationFrame = () => {};
// The app's warnings in node (a native view with no factory here) are not the build's.
console.warn = () => {};

const {createHost, fingerprint, fontFaces} = await import(pathToFileURL(join(out, 'renderer.mjs')).href);
const createApp = (await import(pathToFileURL(join(out, 'app.mjs')).href)).default;
const root = new Element('div');
root.id = 'app';
const host = createHost(root, () => {}, {});
// The first commit's mutations, for its fingerprint; commands (an alert, a fetch, the
// history) are the page's to carry out, not the build's.
let first = null;
const recorded = [];
const wrapper = {
  apply(m) { if (!first) recorded.push(m); host.apply(m); },
  commit() { host.commit(); if (!first) first = fingerprint(recorded); },
  command() {},
  schedule() {},
  cancel() {},
};
const app = await createApp({uiHost: wrapper, uiEmit: () => {}});
app.ccall('ui_report_link', null, ['string'], ['/']);
app._ui_start(0);
if (!first) { console.error('prerender: the app committed nothing at start'); process.exit(1); }

const page = join(out, 'index.html');
let html = readFileSync(page, 'utf8');
const marker = '<div id="app"></div>';
if (!html.includes(marker)) { console.error('prerender: index.html has no empty #app'); process.exit(1); }
const styles = head.children.filter(c => c.tagName === 'STYLE').map(c => `<style id="${c.id}">${c.textContent}</style>`).join('');
const fonts = fontFaces().map(f => `@font-face { font-family: ${f.family}; src: url(${JSON.stringify(f.file)}); }`).join(' ');
const markup = root.children.map(c => c.html()).join('');
// The markup is the root route's: a page opened on another route, or one a reload under
// `ui serve` restores, shows nothing until the app starts, as before.
const check = `<script>(() => { const app = document.getElementById('app'); try { const base = new URL(document.baseURI).pathname; ` +
  `let path = location.pathname.startsWith(base) ? '/' + location.pathname.slice(base.length) : location.pathname; ` +
  `if (path.endsWith('/index.html')) path = path.slice(0, -10); ` +
  `if (path + location.search !== app.dataset.uiRoute || sessionStorage.getItem('ui-reload')) { app.replaceChildren(); app.dataset.uiPrerender = 'replaced'; } ` +
  `} catch {} })();</script>`;
html = html.replace(marker, () => styles + (fonts ? `<style>${fonts}</style>` : '') +
  `<div id="app" data-ui-prerender="${first}" data-ui-route="/">${markup}</div>` + check);
writeFileSync(page, html);
console.log(`prerendered ${markup.length} bytes, fingerprint ${first}`);
process.exit(0);
