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
    roles[label] = {3: 'text', 4: 'button', 5: 'textfield', 6: 'scroll', 7: 'image', 8: 'button', 9: 'switch',
      10: 'progressbar'}[kind] ?? 'group';
  }
  // CSS colors as rrggbbaa.
  const hex = css => {
    const [r, g, b, a = 1] = css.match(/[\d.]+/g).map(Number);
    return [r, g, b, Math.round(a * 255)].map(v => v.toString(16).padStart(2, '0')).join('');
  };
  const css = label => getComputedStyle(root.querySelector(`[aria-label="${label}"]`));
  const painted = css('Painted');
  const title = css('Title');
  const styles = {
    Painted: {background: hex(painted.backgroundColor), radius: parseFloat(painted.borderTopLeftRadius),
      border: parseFloat(painted.borderTopWidth), borderColor: hex(painted.borderTopColor), opacity: Number(painted.opacity)},
    Title: {color: hex(title.color), fontSize: parseFloat(title.fontSize), fontWeight: Number(title.fontWeight)},
  };
  const tap = [...root.querySelectorAll('button')].find(button => button.textContent === 'Tap');
  tap.click();
  tap.click();
  const field = root.querySelector('input[aria-label="Field"]');
  field.value = 'abc';
  field.dispatchEvent(new Event('input', {bubbles: true}));
  await frame();
  root.querySelector('[aria-label="Press"]').click();
  root.querySelector('[aria-label="Toggle"]').click();
  await frame();
  // Scroll the feed: rows near 2000 points mount, rows from the top go.
  const feed = root.querySelector('[aria-label="Feed"]');
  feed.scrollTop = 2000;
  await frame(); await frame(); await frame();
  const feedBox = feed.getBoundingClientRect();
  const scrolled = {Feed: [feedBox.left - base.left, feedBox.top - base.top, feedBox.width, feedBox.height]};
  for (const row of feed.querySelectorAll('[aria-label^="Row "]')) {
    const r = row.getBoundingClientRect();
    scrolled[row.getAttribute('aria-label')] = [r.left - base.left, r.top - base.top, r.width, r.height].map(v => Math.round(v * 10) / 10);
  }
  return {status: 'DONE', frames, roles, styles, scrolled, log: app.ccall('ui_conformance_log', 'string', [], [])};
}
