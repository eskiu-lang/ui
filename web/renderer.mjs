// Events back into the wasm app: 1 press, 2 change (text), 3 focus (1 or 0).
export function uiDispatch(app, id, generation, kind = 1, text = null, value = 0) {
  if (kind === 1) app._ui_dispatch(id, generation);
  else if (kind === 2) app.ccall('ui_dispatch_text', null, ['number', 'number', 'string'], [id, generation, text]);
  else app._ui_dispatch_focus(id, generation, value);
}

// A deliberately small synchronous renderer shared by the demos and browser tests.
// dispatch(id, generation, kind, text, value), with the kinds of uiDispatch.
export function createHost(root, dispatch) {
  const nodes = new Map();
  let rootId;
  const pending = [];
  const target = (event, tag) => {
    const node = event.target.closest(`${tag}[data-ui-id]`);
    return node && root.contains(node) && node.dataset.listening === 'true' ? node : null;
  };
  const ids = node => [Number(node.dataset.uiId), Number(node.dataset.generation)];
  root.addEventListener('click', event => {
    const button = target(event, 'button');
    if (button) dispatch(...ids(button), 1);
  });
  root.addEventListener('input', event => {
    const input = target(event, 'input');
    if (input) dispatch(...ids(input), 2, input.value);
  });
  root.addEventListener('focusin', event => {
    const input = target(event, 'input');
    if (input) dispatch(...ids(input), 3, null, 1);
  });
  root.addEventListener('focusout', event => {
    const input = target(event, 'input');
    if (input) dispatch(...ids(input), 3, null, 0);
  });
  function parentNode(id) {
    if (nodes.has(id)) return nodes.get(id);
    if (rootId === undefined) { rootId = id; return root; }
    if (id === rootId) return root;
    throw new Error(`unknown parent ${id}`);
  }
  return {
    nodes,
    apply(mutation) { pending.push(mutation); },
    commit() {
      for (const m of pending) {
        if (m.op === 1) {
          if (nodes.has(m.id)) throw new Error(`duplicate node ${m.id}`);
          const tag = {3: 'span', 4: 'button', 5: 'input', 7: 'img'}[m.kind] ?? 'div';
          const node = document.createElement(tag);
          if (m.kind === 5) node.type = 'text';
          node.dataset.uiId = m.id;
          node.dataset.generation = m.generation;
          node.dataset.kind = m.kind;
          // Yoga's defaults, as react-native-web sets them: sizes include padding, items do
          // not shrink, and their minimum size is zero.
          node.style.boxSizing = 'border-box';
          node.style.flexShrink = '0';
          node.style.minWidth = '0';
          node.style.minHeight = '0';
          if (m.kind <= 2 || m.kind === 6) {
            node.style.display = 'flex';
            node.style.flexDirection = m.kind === 2 ? 'row' : 'column';
            node.style.gap = '8px';
            if (m.kind === 6) { node.style.overflow = 'auto'; node.style.minHeight = '0'; }
          }
          nodes.set(m.id, node);
          continue;
        }
        const node = nodes.get(m.id);
        // The synthetic root has no host view.
        if (!node && m.op === 6 && m.id === rootId) { rootId = undefined; continue; }
        if (!node) throw new Error(`unknown node ${m.id}`);
        if (m.op === 3 && Number(node.dataset.kind) === 5) { if (node.value !== m.text) node.value = m.text; }
        else if (m.op === 3) node.textContent = m.text;
        else if (m.op === 9) {
          // String props: 1 label (an image's alt text), 2 placeholder, 3 image source.
          const image = Number(node.dataset.kind) === 7;
          const name = m.kind === 1 ? (image ? 'alt' : 'aria-label') : m.kind === 2 ? 'placeholder' : 'src';
          if (m.text) node.setAttribute(name, m.text); else node.removeAttribute(name);
        }
        else if (m.op === 2 && m.kind === 3) { if (m.before) node.focus(); else node.blur(); }
        else if (m.op === 4 || m.op === 5) {
          parentNode(m.parent).insertBefore(node, m.before ? nodes.get(m.before) : null);
        } else if (m.op === 6) { node.remove(); nodes.delete(m.id); }
        else if (m.op === 7 || m.op === 8) node.dataset.listening = String(m.op === 7);
        // Props: the code in kind, the value in before. Leaf views keep their platform
        // control padding and gap.
        else if (m.op === 2) {
          const container = Number(node.dataset.kind) <= 2 || Number(node.dataset.kind) === 6;
          const px = value => value ? `${value}px` : '';
          if (m.kind === 1 && container) node.style.gap = `${m.before}px`;
          else if (m.kind === 2 && container) node.style.padding = `${m.before}px`;
          else if (m.kind === 4) node.style.width = px(m.before);
          else if (m.kind === 5) node.style.height = px(m.before);
          else if (m.kind === 6) node.style.flexGrow = m.before ? String(m.before) : '';
        }
        else throw new Error(`unknown operation ${m.op}`);
      }
      pending.length = 0;
    }
  };
}
