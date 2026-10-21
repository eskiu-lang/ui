// The clock module on the web: an emscripten library defining the externs. Module.uiEmit
// posts an event on a channel (use_native_event).
mergeInto(LibraryManager.library, {
  clock_start: function(ms) {
    clearInterval(Module.clockTimer);
    Module.clockTimer = setInterval(() => Module.uiEmit('clock', 'tick'), ms);
  },
  clock_stop: function() { clearInterval(Module.clockTimer); },
});
