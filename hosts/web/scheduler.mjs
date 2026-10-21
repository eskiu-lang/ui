// One pending host callback, even when a burst of events changes many signals.
export function createFrameScheduler(tick, {
  requestFrame = callback => requestAnimationFrame(callback),
  cancelFrame = handle => cancelAnimationFrame(handle),
  now = () => performance.now(),
  afterTick = () => {}
} = {}) {
  let pending = null;
  let frames = 0;
  const durations = [];
  return {
    request() {
      if (pending !== null) return;
      pending = requestFrame(() => {
        pending = null;
        const start = now();
        tick();
        afterTick();
        durations.push(now() - start);
        if (durations.length > 4096) durations.shift();
        frames++;
      });
    },
    cancel() {
      if (pending !== null) cancelFrame(pending);
      pending = null;
    },
    get pending() { return pending !== null; },
    get frames() { return frames; },
    get durations() { return [...durations]; }
  };
}
