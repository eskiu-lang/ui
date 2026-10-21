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
          const tag = {3: 'span', 4: 'button', 5: 'input'}[m.kind] ?? 'div';
          const node = document.createElement(tag);
          if (m.kind === 5) node.type = 'text';
          node.dataset.uiId = m.id;
          node.dataset.generation = m.generation;
          node.dataset.kind = m.kind;
          if (m.kind <= 2) {
            node.style.display = 'flex';
            node.style.flexDirection = m.kind === 2 ? 'row' : 'column';
            node.style.gap = '8px';
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
          const name = m.kind === 1 ? 'aria-label' : 'placeholder';
          if (m.text) node.setAttribute(name, m.text); else node.removeAttribute(name);
        }
        else if (m.op === 2 && m.kind === 3) { if (m.before) node.focus(); else node.blur(); }
        else if (m.op === 4 || m.op === 5) {
          parentNode(m.parent).insertBefore(node, m.before ? nodes.get(m.before) : null);
        } else if (m.op === 6) { node.remove(); nodes.delete(m.id); }
        else if (m.op === 7 || m.op === 8) node.dataset.listening = String(m.op === 7);
        // Props: the code in kind (1 gap, 2 padding), the value in before.
        else if (m.op === 2 && node.dataset.kind <= 2) {
          if (m.kind === 1) node.style.gap = `${m.before}px`;
          else if (m.kind === 2) node.style.padding = `${m.before}px`;
        } else if (m.op === 2) { /* Leaf views keep their platform control padding. */ }
        else throw new Error(`unknown operation ${m.op}`);
      }
      pending.length = 0;
    }
  };
}
