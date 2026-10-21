// The conformance scene in a real browser: frames relative to the scene, roles, and the
// handler log after a fixed script of events (tools/conformance.py compares them).
export async function runConformance(app, root) {
  const frame = () => new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('no frame')), 3000);
    requestAnimationFrame(() => { clearTimeout(timer); resolve(); });
  });
  await frame();
  const suite = root.querySelector('[aria-label="Suite"]');
  const base = suite.getBoundingClientRect();
  const frames = {};
  const roles = {};
  for (const node of root.querySelectorAll('[data-ui-id]')) {
    const kind = Number(node.dataset.kind);
    const label = kind === 7 ? node.getAttribute('alt') : kind === 4 ? node.textContent : node.getAttribute('aria-label');
    if (!label) continue;
    const r = node.getBoundingClientRect();
    frames[label] = [r.left - base.left, r.top - base.top, r.width, r.height].map(v => Math.round(v * 10) / 10);
    roles[label] = {3: 'text', 4: 'button', 5: 'textfield', 6: 'scroll', 7: 'image'}[kind] ?? 'group';
  }
  const tap = [...root.querySelectorAll('button')].find(button => button.textContent === 'Tap');
  tap.click();
  tap.click();
  const field = root.querySelector('input[aria-label="Field"]');
  field.value = 'abc';
  field.dispatchEvent(new Event('input', {bubbles: true}));
  await frame();
  return {status: 'DONE', frames, roles, log: app.ccall('ui_conformance_log', 'string', [], [])};
}
