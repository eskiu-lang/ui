import {benchmarkFrames} from './benchmark.mjs';

export async function runPlatformChecks(app, host, scheduler, root) {
  const check = (ok, message) => { if (!ok) throw new Error(message); };
  const find = text => [...root.querySelectorAll('button, span')].find(node => node.textContent === text);
  const frame = () => new Promise(resolve => requestAnimationFrame(resolve));
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
  const todo = await benchmarkFrames(find('Reverse'), scheduler);
  return {status: 'PASS', checks: ['event batching', 'counter', 'teardown/restart', 'keyed move', 'state identity'],
    measurements: {counter, todo}};
}
