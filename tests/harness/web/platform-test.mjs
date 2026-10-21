import {benchmarkFrames} from './benchmark.mjs';

export async function runPlatformChecks(app, host, scheduler, root) {
  const check = (ok, message) => { if (!ok) throw new Error(message); };
  const find = text => [...root.querySelectorAll('button, span')].find(node => node.textContent === text);
  // A frame that never comes is a failure at the step that waited for it, not a hang.
  let step = 'start';
  const frame = () => new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`no frame after: ${step}`)), 3000);
    requestAnimationFrame(() => { clearTimeout(timer); resolve(); });
  });
  const click = async text => { check(find(text), `missing ${text}`); find(text).click(); await frame(); };
  const initialFrames = scheduler.frames;
  for (let i = 0; i < 100; i++) find('Increment').click();
  check(find('Count: 0'), 'batch should wait for its frame');
  await frame();
  check(find('Count: 100'), 'counter burst value');
  check(scheduler.frames === initialFrames + 1, 'counter burst must render once');
  const counter = await benchmarkFrames(find('Increment'), scheduler);
  app._ui_stop();
  check(host.nodes.size === 0 && root.children.length === 0, 'counter teardown');
  app._ui_start(1);
  const row = find('Try Eskiu UI').parentElement;
  await click('Edit');
  check(row.textContent.includes('Editing'), 'row editing state');
  await click('Reverse');
  check(find('Try Eskiu UI').parentElement === row, 'keyed host node identity');
  check(row.textContent.includes('Editing'), 'state survives keyed move');
  // Typing into the real input: native input and focus events, then Add reads the draft.
  step = 'find input';
  const field = root.querySelector('input');
  check(field && field.getAttribute('aria-label') === 'New task', 'input with its label');
  step = 'focus';
  field.focus();
  field.value = 'Write the docs';
  step = 'input event';
  field.dispatchEvent(new Event('input', {bubbles: true}));
  await frame();
  step = 'add';
  await click('Add');
  check(find('Write the docs'), 'typed task added');
  check(field.value === '', 'draft cleared');
  check(document.activeElement === field, 'focus stays in the field');
  const todo = await benchmarkFrames(find('Reverse'), scheduler);
  return {status: 'PASS', checks: ['event batching', 'counter', 'teardown/restart', 'keyed move', 'state identity',
    'text input', 'label', 'focus'],
    measurements: {counter, todo}};
}
