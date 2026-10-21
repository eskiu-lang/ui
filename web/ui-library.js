// The host copies string data before returning to Eskiu.
mergeInto(LibraryManager.library, {
  ui_host_apply__deps: ['$UTF8ToString'],
  ui_host_apply: function(op, id, generation, parent, before, kind, text) {
    Module['uiHost'].apply({op, id, generation, parent, before, kind,
      text: text ? UTF8ToString(text) : null});
  },
  ui_host_ready: function() { Module['uiHost'].commit(); },
  ui_host_schedule: function() { Module['uiHost'].schedule(); },
  ui_host_cancel: function() { Module['uiHost'].cancel(); }
});
