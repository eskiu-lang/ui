function percentile(values, fraction) {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * fraction))];
}

// The scheduler's afterTick forces layout. These are CPU/layout and rAF cadence
// measurements; neither is a measurement of physical display presentation time.
export async function benchmarkFrames(button, scheduler, sampleCount = 120) {
  const intervals = [];
  const work = [];
  for (let i = 0; i < sampleCount + 10; i++) {
    const start = await new Promise(resolve => requestAnimationFrame(resolve));
    const before = scheduler.frames;
    button.click();
    const end = await new Promise(resolve => requestAnimationFrame(resolve));
    if (scheduler.frames !== before + 1) throw new Error('expected one committed frame per sample');
    if (i >= 10) {
      work.push(scheduler.durations.at(-1));
      intervals.push(end - start);
    }
  }
  return {
    samples: sampleCount,
    workP50Ms: percentile(work, .5),
    workP95Ms: percentile(work, .95),
    rafIntervalP95Ms: percentile(intervals, .95),
    budgetMs: 1000 / 60,
    withinWorkBudget: percentile(work, .95) <= 1000 / 60,
    userAgent: navigator.userAgent,
    viewport: {width: innerWidth, height: innerHeight},
    recordedAt: new Date().toISOString()
  };
}
