// The web's side of the driver (docs/agents.md), in `ui serve` builds only: it takes the
// requests `ui agent web` posts to the dev server, hands each to the core (ui_driver), does
// what the core asks of the host (input, layout, logs, memory, its animations) and posts the
// answer back.
import {withStrings} from './renderer.mjs';

// The lines the page printed (console.log, warn, error, and uncaught errors), the last 500.
const logs = [];
function capture() {
  const note = (level, parts) => {
    logs.push(`${level}: ${parts.map(p => (typeof p === 'string' ? p : (() => { try { return JSON.stringify(p); } catch { return String(p); } })())).join(' ')}`);
    if (logs.length > 500) logs.shift();
  };
  for (const level of ['log', 'info', 'warn', 'error']) {
    const original = console[level];
    console[level] = (...parts) => { note(level, parts); original.apply(console, parts); };
  }
  addEventListener('error', e => note('error', [e.message]));
  addEventListener('unhandledrejection', e => note('error', [String(e.reason)]));
}

// The page's animations (CSS transitions and animations) while the driver owns the clock:
// each is paused when it starts, at the driver's time then, and moved to the driver's time
// after each request that moves it.
const started = new Map();
let virtual = false;
let now = 0;
function adopt(app) {
  if (!virtual || typeof document.getAnimations !== 'function') return;
  const time = app._ui_driver_time();
  for (const animation of document.getAnimations()) {
    if (started.has(animation)) continue;
    animation.pause();
    animation.currentTime = 0;
    started.set(animation, time);
  }
}
function seek(settle) {
  for (const [animation, start] of started) {
    if (animation.playState === 'idle') { started.delete(animation); continue; }
    const end = animation.effect?.getComputedTiming?.().endTime;
    const finite = Number.isFinite(end);
    if (finite && (settle || now - start >= end)) {
      try { animation.finish(); } catch {}
      started.delete(animation);
    } else animation.currentTime = Math.max(0, now - start);
  }
}
function release() {
  for (const animation of started.keys()) { try { animation.play(); } catch {} }
  started.clear();
}

// What the host does for a directive; its result text ("error: ..." when it cannot).
async function carry(app, host, d) {
  const node = d.node ? host.nodes.get(d.node) : null;
  if (d.node && !node) return `error: #${d.node} has no element on the page`;
  if (d.host === 'hello' || d.host === 'clock') {
    if (d.virtual && !virtual) { virtual = true; adopt(app); }
    now = d.now;
    if (!d.virtual && virtual) { virtual = false; release(); }
    else seek(d.settle);
    return '';
  }
  if (d.host === 'tap') {
    if (d.kind === 5) node.focus();
    else node.click();
    return '';
  }
  if (d.host === 'type') {
    node.focus();
    node.value = d.text;
    node.dispatchEvent(new Event('input', {bubbles: true}));
    return '';
  }
  if (d.host === 'scroll') {
    node.scrollTop += d.dy;
    node.dispatchEvent(new Event('scroll'));
    return String(Math.round(node.scrollTop));
  }
  if (d.host === 'layout') {
    const rows = [];
    for (const [id, element] of host.nodes) {
      const box = element.getBoundingClientRect();
      const shown = element.getClientRects().length > 0;
      const r = v => Math.round(v * 10) / 10;
      rows.push(`${id} ${r(box.left)} ${r(box.top)} ${r(box.width)} ${r(box.height)} ${shown ? 1 : 0}`);
    }
    return rows.join('\n');
  }
  if (d.host === 'logs') return logs.join('\n');
  if (d.host === 'perf') return String(app.HEAPU8?.length ?? performance.memory?.usedJSHeapSize ?? 0);
  return `error: the web host does not know ${d.host}`;
}

// One request: the core's answer, or the host's part and then the core's answer.
async function answer(app, host, request) {
  const directive = JSON.parse(withStrings(app, [request], r => app.UTF8ToString(app._ui_driver(r))));
  if (directive.host === undefined) return JSON.stringify(directive);
  let result;
  try { result = await carry(app, host, directive); } catch (e) { result = `error: ${e.message || e}`; }
  const done = withStrings(app, [result], r => app.UTF8ToString(app._ui_driver_done(r)));
  adopt(app);
  return done;
}

// Asks the dev server for requests while the page is open; the newest page (or one opened
// with #ui-agent, as the browser `ui agent` starts) takes them.
export function startDriver({app, host}) {
  capture();
  const commit = host.commit;
  host.commit = (...parts) => { commit.apply(host, parts); adopt(app); };
  const page = Math.random().toString(36).slice(2);
  const agent = location.hash === '#ui-agent' ? 1 : 0;
  let first = 1;
  (async () => {
    for (;;) {
      try {
        const version = globalThis.uiServeVersion ?? -1;
        const response = await fetch(`/__ui/agent/next?page=${page}&agent=${agent}&first=${first}&version=${version}`, {cache: 'no-store'});
        first = 0;
        if (response.status !== 200) {
          if (response.status !== 204) await new Promise(resolve => setTimeout(resolve, 1000));
          continue;
        }
        const request = await response.text();
        let text;
        try { text = await answer(app, host, request); } catch (e) {
          text = JSON.stringify({ok: false, error: `the page failed: ${e.message || e}`});
        }
        await fetch(`/__ui/agent/reply?page=${page}`, {method: 'POST', body: text, cache: 'no-store'});
      } catch {
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    }
  })();
}
