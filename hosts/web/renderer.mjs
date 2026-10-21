// Events back into the wasm app: 1 press, 2 change (text), 3 focus (1 or 0), 4 toggle
// (1 or 0), 5 scroll (the offset in value, the viewport height in extra), 6 alert, 7 refresh,
// 9 a native view's event (the payload in text, its name in extra), 10 the browser went to a
// route (the route in text, its history index in value), 11 a node's frame (x in value, y in
// extra, then width and height), 13 hover (1 entered, 0 left), 14 a slider's value,
// 15 an answer (the request in id, its text), 16 a fetch's answer (status in value, the body
// in text, the headers in extra), 17 a pan (the phase in value, then dx, dy, vx, vy).
export function uiDispatch(app, id, generation, kind = 1, text = null, value = 0, extra = 0, width = 0, height = 0, more = 0, rest = 0) {
  if (kind === 1) app._ui_dispatch(id, generation);
  else if (kind === 2) app.ccall('ui_dispatch_text', null, ['number', 'number', 'string'], [id, generation, text]);
  else if (kind === 3) app._ui_dispatch_focus(id, generation, value);
  else if (kind === 4) app._ui_dispatch_toggle(id, generation, value);
  else if (kind === 5) app._ui_dispatch_scroll(id, generation, value, extra);
  else if (kind === 6) app._ui_dispatch_alert(id, value);
  else if (kind === 13) app._ui_dispatch_hover(id, generation, value);
  else if (kind === 14) app._ui_dispatch_value(id, generation, value);
  else if (kind === 17) app._ui_dispatch_pan(id, generation, value, extra, width, height, more);
  else if (kind === 16) app.ccall('ui_dispatch_fetch', null, ['number', 'number', 'string', 'string'], [id, value, text, extra]);
  else if (kind === 15) app.ccall('ui_dispatch_answer', null, ['number', 'string'], [id, text]);
  else if (kind === 11) app._ui_dispatch_layout(id, generation, value, extra, width, height, more, rest);
  else if (kind === 10) app.ccall('ui_dispatch_route', null, ['string', 'number'], [text, value]);
  else if (kind === 9) app.ccall('ui_dispatch_view', null, ['number', 'number', 'string', 'string'], [id, generation, extra, text]);
  else app._ui_dispatch_refresh(id, generation);
}

// Kinds (src/ui.esk): 0 scope, 1 column, 2 row, 3 text, 4 button, 5 text input, 6 scroll,
// 7 image, 8 pressable, 9 switch, 10 activity indicator, 11 status bar, 12 modal, 13 native
// view, 14 screen.
const TAGS = {3: 'span', 4: 'button', 5: 'input', 7: 'img', 9: 'input', 15: 'input'};
// The Easing members as CSS timing functions; spring overshoots a little, as the native hosts'.
const EASINGS = ['ease-in-out', 'ease-out', 'ease-in', 'linear', 'cubic-bezier(0.34, 1.56, 0.64, 1)'];
// An edge offset that is not set (kUnset in src/core/state.esk).
const UNSET = -1000000;
const CONTAINERS = new Set([0, 1, 2, 6, 8, 12, 14]);
const ALIGN = {1: 'flex-start', 2: 'center', 3: 'flex-end', 4: 'stretch'};
const JUSTIFY = {1: 'flex-start', 2: 'center', 3: 'flex-end', 4: 'space-between', 5: 'space-around'};
// The ring turns inside the node, so the node keeps its box for layout.
// Every node is a containing block for its absolute children, as in Yoga; the classes after
// this rule (a modal's fixed layer) take precedence.
const SPINNER_CSS = '[data-ui-id] { position: relative; }' +
  '@keyframes ui-spin { to { transform: rotate(360deg); } }' +
  '.ui-spinner { position: relative; width: 20px; height: 20px; }' +
  '.ui-spinner::after { content: ""; position: absolute; inset: 0; box-sizing: border-box; border-radius: 50%;' +
  ' border: 2px solid #c5cedd; border-top-color: #4a6fa5; animation: ui-spin .8s linear infinite; }' +
  '.ui-spinner[data-animating="false"]::after { visibility: hidden; animation: none; }' +
  '.ui-refresh { position: absolute; top: 8px; left: calc(50% - 10px); z-index: 1; display: none; }' +
  '.ui-refresh[data-animating="true"] { display: block; }' +
  '.ui-modal { position: fixed; inset: 0; z-index: 1000; background: rgba(0, 0, 0, .4); }' +
  '.ui-alert { border: 0; border-radius: 12px; padding: 1rem 1.25rem; min-width: 16rem; font: inherit; }' +
  '.ui-alert p { margin: .5rem 0 1rem; } .ui-alert div { display: flex; gap: .5rem; justify-content: flex-end; }';

// An RGBA color (0: the platform's default) as CSS.
function cssColor(value) {
  const v = value >>> 0;
  if (v === 0) return '';
  return `rgba(${v >>> 24}, ${(v >>> 16) & 255}, ${(v >>> 8) & 255}, ${(v & 255) / 255})`;
}

// A deliberately small synchronous renderer shared by the demos and browser tests.
// dispatch(id, generation, kind, text, value, extra), with the kinds of uiDispatch.
// `views` maps a NativeView type to its factory: create(emit) returns {element, set(name,
// value), dispose()} (set and dispose optional); emit(name, payload) sends the view's events.
export function createHost(root, dispatch, views = {}) {
  const nodes = new Map();
  // A native view's instance by node id.
  const natives = new Map();
  // The nodes whose frame the app listens to (ui_on_layout), with the frame last reported.
  const layouts = new Map();
  // Pan listeners (ui_on_pan): a drag past a few pixels, then captured. A press that does
  // not move stays a click; the click after a drag is swallowed. Vertical touch drags scroll
  // the page (touch-action: pan-y).
  function pan(node, on) {
    if (!on || node.uiPan) { if (!on) node.uiPan = null; return; }
    let start = null, began = false, samples = [];
    const send = (phase, e) => {
      const dx = Math.round(e.clientX - start.x), dy = Math.round(e.clientY - start.y);
      let vx = 0, vy = 0;
      if (samples.length > 1) {
        const [t0, x0, y0] = samples[0], [t1, x1, y1] = samples[samples.length - 1];
        const dt = Math.max(1, t1 - t0) / 1000;
        vx = Math.round((x1 - x0) / dt); vy = Math.round((y1 - y0) / dt);
      }
      dispatch(...ids(node), 17, null, phase, dx, dy, vx, vy);
    };
    node.uiPan = true;
    node.style.touchAction = 'pan-y';
    node.addEventListener('pointerdown', e => {
      if (!node.uiPan || e.button > 0) return;
      start = {x: e.clientX, y: e.clientY}; began = false; samples = [[e.timeStamp, e.clientX, e.clientY]];
    });
    node.addEventListener('pointermove', e => {
      if (!start) return;
      samples.push([e.timeStamp, e.clientX, e.clientY]);
      if (samples.length > 5) samples.shift();
      if (!began && Math.hypot(e.clientX - start.x, e.clientY - start.y) < 6) return;
      // Captured only once it is a drag, so a press keeps its click target.
      if (!began) { began = true; node.setPointerCapture?.(e.pointerId); send(0, e); }
      send(1, e);
    });
    const finish = cancelled => e => {
      if (!start) return;
      if (began) { send(cancelled ? 3 : 2, e); node.uiPanned = true; }
      start = null;
    };
    node.addEventListener('pointerup', finish(false));
    node.addEventListener('pointercancel', finish(true));
    node.addEventListener('click', e => {
      if (node.uiPanned) { node.uiPanned = false; e.stopPropagation(); e.preventDefault(); }
    }, true);
  }
  // Hover listeners (ui_on_hover): a pointer entering and leaving the node.
  function hover(node, on) {
    if (on && !node.uiHover) {
      node.uiHover = [() => dispatch(...ids(node), 13, null, 1), () => dispatch(...ids(node), 13, null, 0)];
      node.addEventListener('mouseenter', node.uiHover[0]);
      node.addEventListener('mouseleave', node.uiHover[1]);
    } else if (!on && node.uiHover) {
      node.removeEventListener?.('mouseenter', node.uiHover[0]);
      node.removeEventListener?.('mouseleave', node.uiHover[1]);
      node.uiHover = null;
    }
  }
  function reportLayouts() {
    for (const [id, last] of layouts) {
      const node = nodes.get(id);
      if (!node || node.offsetWidth === undefined) continue;
      const box = node.getBoundingClientRect?.() ?? {left: 0, top: 0};
      const frame = [node.offsetLeft, node.offsetTop, node.offsetWidth, node.offsetHeight, Math.round(box.left), Math.round(box.top)];
      if (last && frame.every((v, i) => v === last[i])) continue;
      layouts.set(id, frame);
      dispatch(...ids(node), 11, null, ...frame);
    }
  }
  if (typeof addEventListener === 'function') {
    addEventListener('resize', () => reportLayouts());
    // Scrolling moves what is inside in the window: positions are checked again.
    addEventListener('scroll', () => { if (layouts.size) reportLayouts(); }, true);
  }
  function mountNative(node, type) {
    natives.get(Number(node.dataset.uiId))?.dispose?.();
    node.replaceChildren();
    const factory = views[type];
    if (!factory) { console.warn(`ui: no native view registered as ${type}`); return; }
    const emit = (name, payload = '') => dispatch(...ids(node), 9, String(payload), 0, String(name));
    const instance = factory.create(emit);
    instance.element.style.width = '100%';
    instance.element.style.height = '100%';
    instance.element.style.boxSizing = 'border-box';
    node.appendChild(instance.element);
    natives.set(Number(node.dataset.uiId), instance);
  }
  let rootId;
  const pending = [];
  if (document.head && !document.getElementById('ui-spinner-css')) {
    const style = document.createElement('style');
    style.id = 'ui-spinner-css';
    style.textContent = SPINNER_CSS;
    document.head.appendChild(style);
  }
  // The innermost element matching `selector` that listens, from the event's target up: a
  // row or a text inside a pressable is the pressable's.
  const listening = (event, selector) => {
    for (let node = event.target.closest?.(selector); node && root.contains(node); node = node.parentElement?.closest(selector)) {
      if (node.dataset.listening === 'true') return node;
    }
    return null;
  };
  const kindOf = node => Number(node.dataset.kind);
  // Open modals, newest last: Escape asks the newest to close.
  const modals = [];
  if (typeof document.addEventListener === 'function') {
    document.addEventListener('keydown', event => {
      const modal = modals.at(-1);
      if (event.key === 'Escape' && modal && modal.dataset.listening === 'true') dispatch(...ids(modal), 1);
    });
  }
  const ids = node => [Number(node.dataset.uiId), Number(node.dataset.generation)];
  // A press goes to the innermost button or pressable; a switch reports through change.
  root.addEventListener('click', event => {
    const node = listening(event, 'button[data-ui-id], div[data-ui-id]');
    if (node && (kindOf(node) === 4 || kindOf(node) === 8)) dispatch(...ids(node), 1);
    // A click on a modal's backdrop (outside its content) asks it to close.
    const target = event.target;
    if (target?.dataset?.kind === '12' && target.dataset.listening === 'true') dispatch(...ids(target), 1);
  });
  root.addEventListener('keydown', event => {
    const node = listening(event, 'div[data-ui-id]');
    if (node && kindOf(node) === 8 && (event.key === 'Enter' || event.key === ' ')) {
      event.preventDefault();
      dispatch(...ids(node), 1);
    }
  });
  root.addEventListener('input', event => {
    const input = listening(event, 'input[data-ui-id]');
    if (input && kindOf(input) === 5) dispatch(...ids(input), 2, input.value);
    if (input && kindOf(input) === 15) dispatch(...ids(input), 14, null, Number(input.value));
  });
  root.addEventListener('change', event => {
    const input = listening(event, 'input[data-ui-id]');
    if (input && kindOf(input) === 9) dispatch(...ids(input), 4, null, input.checked ? 1 : 0);
  });
  // Scroll does not bubble: listen in the capture phase. A virtual list also hears its
  // viewport once it is laid out.
  const reportScroll = node => dispatch(...ids(node), 5, null, Math.round(node.scrollTop), Math.round(node.clientHeight));
  root.addEventListener('scroll', event => {
    const node = event.target;
    if (node.dataset && kindOf(node) === 6 && node.dataset.listening === 'true') reportScroll(node);
  }, true);
  root.addEventListener('focusin', event => {
    const input = listening(event, 'input[data-ui-id]');
    if (input && kindOf(input) === 5) dispatch(...ids(input), 3, null, 1);
  });
  root.addEventListener('focusout', event => {
    const input = listening(event, 'input[data-ui-id]');
    if (input && kindOf(input) === 5) dispatch(...ids(input), 3, null, 0);
  });
  function parentNode(id) {
    if (nodes.has(id)) return nodes.get(id);
    if (rootId === undefined) { rootId = id; return root; }
    if (id === rootId) return root;
    throw new Error(`unknown parent ${id}`);
  }
  function create(m) {
    const node = document.createElement(TAGS[m.kind] ?? 'div');
    node.dataset.uiId = m.id;
    node.dataset.generation = m.generation;
    node.dataset.kind = m.kind;
    // Yoga's defaults, as react-native-web sets them: sizes include padding, items do not
    // shrink, and their minimum size is zero.
    node.style.boxSizing = 'border-box';
    node.style.flexShrink = '0';
    node.style.minWidth = '0';
    node.style.minHeight = '0';
    node.style.margin = '0';
    if (CONTAINERS.has(m.kind)) {
      node.style.display = 'flex';
      node.style.flexDirection = m.kind === 2 ? 'row' : 'column';
      node.style.gap = '8px';
      if (m.kind === 6) { node.style.overflow = 'auto'; node.style.flexShrink = '1'; }
    }
    if (m.kind === 5) node.type = 'text';
    if (m.kind === 15) { node.type = 'range'; node.step = '1'; }
    if (m.kind === 8) { node.setAttribute('role', 'button'); node.tabIndex = 0; node.style.cursor = 'pointer'; }
    if (m.kind === 9) { node.type = 'checkbox'; node.setAttribute('role', 'switch'); }
    if (m.kind === 10) { node.className = 'ui-spinner'; node.setAttribute('role', 'progressbar'); }
    if (m.kind === 11) node.style.display = 'none';
    if (m.kind === 13) node.style.overflow = 'hidden';
    if (m.kind === 12) { node.className = 'ui-modal'; node.setAttribute('role', 'dialog'); node.setAttribute('aria-modal', 'true'); modals.push(node); }
    nodes.set(m.id, node);
  }
  // Int props: the code in kind, the value in before. Leaf views keep their platform
  // control padding and gap.
  function prop(node, code, value) {
    const container = CONTAINERS.has(kindOf(node));
    const px = v => v ? `${v}px` : '';
    const s = node.style;
    switch (code) {
      case 1: if (container) s.gap = `${value}px`; break;
      case 2: if (container || kindOf(node) === 4 || kindOf(node) === 5) s.padding = `${value}px`; break;
      case 3: if (value) node.focus(); else node.blur(); break;
      case 4: s.width = px(value); ratioAlign(s); break;
      case 5: s.height = px(value); ratioAlign(s); break;
      case 6:
        // A growing child also shrinks to its parent, so a scroll under it stays in the window.
        s.flexGrow = value ? String(value) : '';
        if (kindOf(node) !== 6) s.flexShrink = value ? '1' : '';
        break;
      case 7: s.flexWrap = value ? 'wrap' : ''; break;
      case 8: s.backgroundColor = cssColor(value); break;
      case 9: s.color = cssColor(value); break;
      case 10: s.fontSize = px(value); break;
      case 11: s.fontWeight = value ? String(value) : ''; break;
      case 12: s.borderWidth = px(value); s.borderStyle = value ? 'solid' : ''; break;
      case 13: s.borderColor = cssColor(value); break;
      case 14: s.borderRadius = px(value); if (kindOf(node) !== 6) s.overflow = value ? 'hidden' : ''; break;
      case 15: s.opacity = value === 100 ? '' : String(value / 100); break;
      case 16: s.alignItems = ALIGN[value] ?? ''; break;
      case 17: s.justifyContent = JUSTIFY[value] ?? ''; break;
      case 18: node.checked = value !== 0; node.setAttribute('aria-checked', String(value !== 0)); break;
      case 19: node.dataset.animating = String(value !== 0); node.setAttribute('aria-busy', String(value !== 0)); break;
      case 20: case 21: break;  // the status bar: nothing to draw in a page
      case 22: refresh(node, value !== 0); break;
      case 23: s.display = value ? 'flex' : 'none'; break;  // a screen under the top is hidden
      case 31: hover(node, value !== 0); break;
      case 41: pan(node, value !== 0); break;
      case 42: s.minWidth = value ? `${value}px` : '0'; break;
      case 43: s.maxWidth = value ? `${value}px` : ''; break;
      case 44: s.minHeight = value ? `${value}px` : '0'; break;
      case 45: s.maxHeight = value ? `${value}px` : ''; break;
      case 47: s.objectFit = value ? 'cover' : ''; break;
      case 46: s.boxShadow = value ? `0 ${value / 2}px ${value * 2}px rgba(17, 24, 39, 0.14), 0 1px 2px rgba(17, 24, 39, 0.06)` : ''; break;
      case 35: case 36: case 37: case 38: {
        // Transforms: translate in points, scale in percent, rotate in degrees.
        const t = node.uiTransform ??= {35: 0, 36: 0, 37: 100, 38: 0};
        t[code] = value;
        const identity = !t[35] && !t[36] && t[37] === 100 && !t[38];
        s.transform = identity ? '' : `translate(${t[35]}px, ${t[36]}px) scale(${t[37] / 100}) rotate(${t[38]}deg)`;
        break;
      }
      case 39: case 40: {
        // A transition for opacity, the transforms and the colors.
        const m = node.uiMotion ??= {39: 0, 40: 0};
        m[code] = value;
        const ease = EASINGS[m[40]] ?? EASINGS[0];
        s.transition = m[39] ? ['opacity', 'transform', 'background-color', 'color', 'border-color']
          .map(p => `${p} ${m[39]}ms ${ease}`).join(', ') : '';
        break;
      }
      case 32: if (node.value !== String(value)) node.value = String(value); break;
      case 33: node.min = String(value); break;
      case 34: node.max = String(value); break;
      case 30: if (value) layouts.set(Number(node.dataset.uiId), null); else layouts.delete(Number(node.dataset.uiId)); break;
      case 24: s.position = value ? 'absolute' : ''; break;
      case 25: s.left = value === UNSET ? '' : `${value}px`; break;
      case 26: s.top = value === UNSET ? '' : `${value}px`; break;
      case 27: s.right = value === UNSET ? '' : `${value}px`; break;
      case 28: s.bottom = value === UNSET ? '' : `${value}px`; break;
      // Yoga sizes a stretched child from its ratio; CSS would stretch it, so it starts.
      case 29: s.aspectRatio = value ? String(value / 1000) : ''; ratioAlign(s); break;
      default: throw new Error(`unknown prop ${code}`);
    }
  }
  // An aspect ratio gives one side from the other. With one side set, the parent must not
  // stretch the other (Yoga lets the ratio win); with neither, the stretched side is the one
  // given and CSS works the other out.
  function ratioAlign(s) { s.alignSelf = s.aspectRatio && (s.height || s.width) ? 'flex-start' : ''; }
  // Pull to refresh: a wheel or drag upward at the top of the scroll, past a threshold, once
  // per gesture; the indicator sits over the top of the content.
  function refresh(node, on) {
    if (!node.uiRefresh) {
      const spinner = document.createElement('div');
      spinner.className = 'ui-spinner ui-refresh';
      node.style.position = 'relative';
      node.insertBefore(spinner, node.firstChild);
      node.uiRefresh = spinner;
      let pulled = 0, touchY = null;
      const pull = amount => {
        if (node.scrollTop > 0 || node.uiRefresh.dataset.animating === 'true') { pulled = 0; return; }
        pulled += amount;
        if (pulled > 80) { pulled = 0; dispatch(...ids(node), 7); }
      };
      node.addEventListener('wheel', event => { if (event.deltaY < 0) pull(-event.deltaY); else pulled = 0; });
      node.addEventListener('touchstart', event => { touchY = event.touches[0].clientY; pulled = 0; });
      node.addEventListener('touchmove', event => {
        if (touchY !== null) { pull(event.touches[0].clientY - touchY); touchY = event.touches[0].clientY; }
      });
      node.addEventListener('touchend', () => { touchY = null; pulled = 0; });
    }
    node.uiRefresh.dataset.animating = String(on);
  }

  // Commands: 1 an alert (title, message, buttons), 2 open a URL.
  function command(c) {
    if (c.command === 1) {
      const [title, message, ...buttons] = c.strings;
      const dialog = document.createElement('dialog');
      dialog.className = 'ui-alert';
      const heading = document.createElement('strong');
      heading.textContent = title;
      const text = document.createElement('p');
      text.textContent = message;
      const row = document.createElement('div');
      let answered = false;
      const answer = index => {
        if (answered) return;
        answered = true;
        dialog.close(); dialog.remove();
        dispatch(c.request, 0, 6, null, index);
      };
      buttons.forEach((name, index) => {
        const button = document.createElement('button');
        button.textContent = name;
        button.addEventListener('click', () => answer(index));
        row.appendChild(button);
      });
      dialog.addEventListener('cancel', () => answer(-1));
      dialog.append(heading, text, row);
      document.body.appendChild(dialog);
      dialog.showModal();
    } else if (c.command === 2) window.open(c.strings[0], '_blank', 'noopener');
    else if (c.command === 4) document.activeElement?.blur?.();
    else if (c.command === 5) {
      const [text, url] = c.strings;
      if (navigator.share) navigator.share({text: text || undefined, url: url || undefined}).catch(() => {});
      else copy([text, url].filter(Boolean).join(' '));
    } else if (c.command === 6) copy(c.strings[0]);
    else if (c.command === 7) {
      const answer = text => dispatch(c.request, 0, 15, text ?? '');
      if (navigator.clipboard?.readText) navigator.clipboard.readText().then(answer, () => answer(copied));
      else answer(copied);
    } else if (c.command === 8) navigator.vibrate?.([10, 15, 25, 40, 20, 30, 40][Number(c.strings[0])] ?? 10);
    else if (c.command === 9) announce(c.strings[0]);
    else if (c.command === 10) {
      // A fetch through the browser; the answer comes back as an event.
      const [url, method, headerText, body, timeout, hasBody] = c.strings;
      const headers = {};
      for (const line of headerText.split('\n')) {
        const colon = line.indexOf(':');
        if (colon > 0) headers[line.slice(0, colon).trim()] = line.slice(colon + 1).trim();
      }
      const options = {method, headers, body: hasBody === '1' ? body : undefined};
      if (Number(timeout) > 0 && typeof AbortSignal !== 'undefined' && AbortSignal.timeout) options.signal = AbortSignal.timeout(Number(timeout));
      fetch(url, options).then(async response => {
        const text = await response.text();
        const names = [...response.headers].map(([name, value]) => `${name}: ${value}`).join('\n');
        dispatch(c.request, 0, 16, text, response.status, names);
      }, error => dispatch(c.request, 0, 16, String(error), 0, ''));
    }
    else if (c.command === 3) navigate(c.strings);
  }
  // The page's URL follows the app's first Navigator: routes are paths under the page's
  // <base> ("/tasks/42" is BASE + "tasks/42"), and each history entry keeps its index in the
  // router's stack, which the browser's back and forward report.
  const hasHistory = typeof history !== 'undefined' && typeof location !== 'undefined';
  const base = () => new URL(document.baseURI).pathname;
  const toURL = route => base() + route.replace(/^\/+/, '');
  let historyIndex = 0;
  function route() {
    if (!hasHistory) return '/';
    const prefix = base();
    const path = location.pathname.startsWith(prefix) ? '/' + location.pathname.slice(prefix.length) : location.pathname;
    return path + location.search;
  }
  function navigate([mode, ...routes]) {
    if (!hasHistory) return;
    if (mode === 'push') history.pushState({ui: ++historyIndex}, '', toURL(routes[0]));
    else if (mode === 'replace') history.replaceState({ui: historyIndex}, '', toURL(routes[0]));
    else if (mode === 'back') history.back();
    else if (mode === 'reset') {
      historyIndex = 0;
      history.replaceState({ui: 0}, '', toURL(routes[0]));
      for (const next of routes.slice(1)) history.pushState({ui: ++historyIndex}, '', toURL(next));
    }
  }
  if (hasHistory && typeof addEventListener === 'function') {
    addEventListener('popstate', event => {
      historyIndex = event.state?.ui ?? 0;
      dispatch(0, 0, 10, route(), historyIndex);
    });
  }
  // The clipboard, kept here too where the page may not read it (no permission, no API).
  let copied = '';
  function copy(text) {
    copied = text;
    navigator.clipboard?.writeText?.(text).catch(() => {});
  }
  // Announcements for a screen reader: a polite live region.
  let live;
  function announce(text) {
    if (!live) {
      live = document.createElement('div');
      live.setAttribute('aria-live', 'polite');
      Object.assign(live.style, {position: 'absolute', width: '1px', height: '1px', overflow: 'hidden', clip: 'rect(0 0 0 0)'});
      document.body?.appendChild(live);
    }
    live.textContent = '';
    setTimeout(() => { live.textContent = text; }, 50);
  }
  return {
    nodes,
    command,
    route,
    apply(mutation) { pending.push(mutation); },
    commit() {
      for (const m of pending) {
        if (m.op === 1) {
          if (nodes.has(m.id)) throw new Error(`duplicate node ${m.id}`);
          create(m);
          continue;
        }
        const node = nodes.get(m.id);
        // The synthetic root has no host view.
        if (!node && m.op === 6 && m.id === rootId) { rootId = undefined; continue; }
        if (!node) throw new Error(`unknown node ${m.id}`);
        if (m.op === 2) prop(node, m.kind, m.before);
        else if (m.op === 3 && kindOf(node) === 5) { if (node.value !== m.text) node.value = m.text; }
        else if (m.op === 3) node.textContent = m.text;
        else if (m.op === 9 && m.kind === 4) mountNative(node, m.text);
        else if (m.op === 11) natives.get(m.id)?.set?.(m.name, m.text);
        else if (m.op === 9) {
          // String props: 1 label (an image's alt text), 2 placeholder, 3 image source.
          const name = m.kind === 1 ? (kindOf(node) === 7 ? 'alt' : 'aria-label') : m.kind === 2 ? 'placeholder' : 'src';
          if (m.text) node.setAttribute(name, m.text); else node.removeAttribute(name);
        } else if (m.op === 4 || m.op === 5) {
          parentNode(m.parent).insertBefore(node, m.before ? nodes.get(m.before) : null);
        } else if (m.op === 6) {
          natives.get(m.id)?.dispose?.(); natives.delete(m.id); layouts.delete(m.id);
          node.remove(); nodes.delete(m.id);
          if (modals.includes(node)) modals.splice(modals.indexOf(node), 1);
        }
        else if (m.op === 7 || m.op === 8) {
          node.dataset.listening = String(m.op === 7);
          if (m.op === 7 && kindOf(node) === 6 && typeof requestAnimationFrame === 'function') {
            requestAnimationFrame(() => { if (node.isConnected) reportScroll(node); });
          }
        }
        else throw new Error(`unknown operation ${m.op}`);
      }
      pending.length = 0;
      if (layouts.size) reportLayouts();
    }
  };
}
