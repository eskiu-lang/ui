// A deliberately small synchronous renderer shared by the demos and browser tests.
export function createHost(root, dispatch) {
  const nodes = new Map();
  let rootId;
  const pending = [];
  root.addEventListener('click', event => {
    const button = event.target.closest('button[data-ui-id]');
    if (button && root.contains(button) && button.dataset.listening === 'true') {
      dispatch(Number(button.dataset.uiId), Number(button.dataset.generation));
    }
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
          const node = document.createElement(m.kind === 4 ? 'button' : m.kind === 3 ? 'span' : 'div');
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
        if (m.op === 3) node.textContent = m.text;
        else if (m.op === 4 || m.op === 5) {
          parentNode(m.parent).insertBefore(node, m.before ? nodes.get(m.before) : null);
        } else if (m.op === 6) { node.remove(); nodes.delete(m.id); }
        else if (m.op === 7 || m.op === 8) node.dataset.listening = String(m.op === 7);
        else if (m.op === 2 && node.dataset.kind <= 2) {
          node.style.gap = `${m.kind}px`; node.style.padding = `${m.before}px`;
        } else if (m.op === 2) { /* Leaf views keep their platform control padding. */ }
        else throw new Error(`unknown operation ${m.op}`);
      }
      pending.length = 0;
    }
  };
}
