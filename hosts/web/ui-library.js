// One call per frame: the commit is read straight from wasm memory (the format in
// src/ui.esk); the host copies what it keeps before returning to Eskiu.
mergeInto(LibraryManager.library, {
  ui_host_commit__deps: ['$UTF8ToString'],
  ui_host_commit: function(ptr, len) {
    const host = Module['uiHost'];
    const bytes = HEAPU8.subarray(ptr, ptr + len);
    const u32 = at => (bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (bytes[at + 3] << 24)) >>> 0;
    let at = 0;
    while (at < len) {
      const m = {op: bytes[at], id: u32(at + 1), generation: 0, parent: 0, before: 0, kind: 0, text: null};
      at += 5;
      if (m.op === 1) { m.generation = u32(at); m.kind = bytes[at + 4]; at += 5; }
      else if (m.op === 2) { m.kind = bytes[at]; m.before = u32(at + 1) | 0; at += 5; }
      else if (m.op === 3) { const n = u32(at); m.text = UTF8ToString(ptr + at + 4, n); at += 4 + n + 1; }
      else if (m.op === 11) {
        // A native view's named prop: the name, then the value.
        const n = u32(at); m.name = UTF8ToString(ptr + at + 4, n); at += 4 + n + 1;
        const v = u32(at); m.text = UTF8ToString(ptr + at + 4, v); at += 4 + v + 1;
      }
      else if (m.op === 9) { m.kind = bytes[at]; const n = u32(at + 1); m.text = UTF8ToString(ptr + at + 5, n); at += 5 + n + 1; }
      else if (m.op === 10) {
        // A command: the request id, the command, its strings.
        const command = bytes[at], count = bytes[at + 1];
        at += 2;
        const strings = [];
        for (let i = 0; i < count; i++) { const n = u32(at); strings.push(UTF8ToString(ptr + at + 4, n)); at += 4 + n + 1; }
        host.command({request: m.id, command, strings});
        continue;
      }
      else if (m.op === 4 || m.op === 5) { m.parent = u32(at); m.before = u32(at + 4); at += 8; }
      host.apply(m);
    }
    host.commit();
  },
  ui_host_schedule: function() { Module['uiHost'].schedule(); },
  ui_host_cancel: function() { Module['uiHost'].cancel(); },
  // Storage (ui_storage_*): the whole store as one localStorage entry per app (by its base).
  ui_host_storage_load__deps: ['$stringToNewUTF8'],
  ui_host_storage_load: function() {
    let text = null;
    try { text = localStorage.getItem('eskiu-ui:' + new URL(document.baseURI).pathname); } catch (e) {}
    return text === null ? 0 : stringToNewUTF8(text);
  },
  ui_host_storage_save: function(ptr) {
    try { localStorage.setItem('eskiu-ui:' + new URL(document.baseURI).pathname, UTF8ToString(ptr)); } catch (e) {}
  },
  // Timers for use_interval and use_timeout, by token.
  ui_host_timer_start: function(token, ms, repeat) {
    const timers = Module['uiTimers'] || (Module['uiTimers'] = new Map());
    const fire = () => { if (!repeat) timers.delete(token); Module['_ui_dispatch_timer'](token); };
    timers.set(token, repeat ? setInterval(fire, ms) : setTimeout(fire, ms));
  },
  ui_host_timer_stop: function(token) {
    const timers = Module['uiTimers'];
    if (!timers || !timers.has(token)) return;
    clearTimeout(timers.get(token)); clearInterval(timers.get(token));
    timers.delete(token);
  }
});
