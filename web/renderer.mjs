// Events back into the wasm app: 1 press, 2 change (text), 3 focus (1 or 0), 4 toggle
// (1 or 0), 5 scroll (the offset in value, the viewport height in extra).
export function uiDispatch(app, id, generation, kind = 1, text = null, value = 0, extra = 0) {
  if (kind === 1) app._ui_dispatch(id, generation);
  else if (kind === 2) app.ccall('ui_dispatch_text', null, ['number', 'number', 'string'], [id, generation, text]);
  else if (kind === 3) app._ui_dispatch_focus(id, generation, value);
  else if (kind === 4) app._ui_dispatch_toggle(id, generation, value);
  else if (kind === 5) app._ui_dispatch_scroll(id, generation, value, extra);
  else if (kind === 6) app._ui_dispatch_alert(id, value);
  else app._ui_dispatch_refresh(id, generation);
}

// Kinds (src/ui.esk): 0 scope, 1 column, 2 row, 3 text, 4 button, 5 text input, 6 scroll,
// 7 image, 8 pressable, 9 switch, 10 activity indicator, 11 status bar, 12 modal.
const TAGS = {3: 'span', 4: 'button', 5: 'input', 7: 'img', 9: 'input'};
const CONTAINERS = new Set([0, 1, 2, 6, 8, 12]);
const ALIGN = {1: 'flex-start', 2: 'center', 3: 'flex-end', 4: 'stretch'};
const JUSTIFY = {1: 'flex-start', 2: 'center', 3: 'flex-end', 4: 'space-between', 5: 'space-around'};
// The ring turns inside the node, so the node keeps its box for layout.
const SPINNER_CSS = '@keyframes ui-spin { to { transform: rotate(360deg); } }' +
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
// dispatch(id, generation, kind, text, value), with the kinds of uiDispatch.
export function createHost(root, dispatch) {
  const nodes = new Map();
  let rootId;
  const pending = [];
  if (document.head && !document.getElementById('ui-spinner-css')) {
    const style = document.createElement('style');
    style.id = 'ui-spinner-css';
    style.textContent = SPINNER_CSS;
    document.head.appendChild(style);
  }
  const listening = (event, selector) => {
    const node = event.target.closest(selector);
    return node && root.contains(node) && node.dataset.listening === 'true' ? node : null;
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
    if (CONTAINERS.has(m.kind)) {
      node.style.display = 'flex';
      node.style.flexDirection = m.kind === 2 ? 'row' : 'column';
      node.style.gap = '8px';
      if (m.kind === 6) node.style.overflow = 'auto';
    }
    if (m.kind === 5) node.type = 'text';
    if (m.kind === 8) { node.setAttribute('role', 'button'); node.tabIndex = 0; node.style.cursor = 'pointer'; }
    if (m.kind === 9) { node.type = 'checkbox'; node.setAttribute('role', 'switch'); }
    if (m.kind === 10) { node.className = 'ui-spinner'; node.setAttribute('role', 'progressbar'); }
    if (m.kind === 11) node.style.display = 'none';
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
      case 2: if (container) s.padding = `${value}px`; break;
      case 3: if (value) node.focus(); else node.blur(); break;
      case 4: s.width = px(value); break;
      case 5: s.height = px(value); break;
      case 6: s.flexGrow = value ? String(value) : ''; break;
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
      default: throw new Error(`unknown prop ${code}`);
    }
  }
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
  }
  return {
    nodes,
    command,
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
        else if (m.op === 9) {
          // String props: 1 label (an image's alt text), 2 placeholder, 3 image source.
          const name = m.kind === 1 ? (kindOf(node) === 7 ? 'alt' : 'aria-label') : m.kind === 2 ? 'placeholder' : 'src';
          if (m.text) node.setAttribute(name, m.text); else node.removeAttribute(name);
        } else if (m.op === 4 || m.op === 5) {
          parentNode(m.parent).insertBefore(node, m.before ? nodes.get(m.before) : null);
        } else if (m.op === 6) {
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
    }
  };
}
