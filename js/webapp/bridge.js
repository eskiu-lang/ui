// The page's side of <ui/webapp>. Every host runs it before the page's own scripts, after
// `window.__ESKIU_HOST__ = {os, window}`, and it defines `window.__ESKIU__`, which the
// @eskiu/webapp module (index.js) wraps. Messages to the program are JSON text through the
// webview's message channel; the program answers by calling `__ESKIU__.receive(message)`.
(() => {
  if (window.__ESKIU__) return;
  const host = window.__ESKIU_HOST__ || {};
  const os = host.os || "unknown";
  const post = (text) => {
    if (os === "windows") window.chrome.webview.postMessage(text);
    else window.webkit.messageHandlers.eskiu.postMessage(text);
  };

  // A scheme the program serves, as this webview reaches it: WebView2 has no custom schemes
  // without registration, so there it is https://NAME.localhost/.
  const schemeUrl = (scheme, path = "") =>
    (os === "windows" ? `https://${scheme}.localhost/` : `${scheme}://localhost/`) + path;

  // Bytes in arguments travel as {"$bytes": base64}; the program reads them with
  // WebappCall_bytes.
  const toBase64 = (bytes) => {
    let text = "";
    for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(text);
  };
  const encode = (value) => {
    if (value instanceof ArrayBuffer) return { $bytes: toBase64(new Uint8Array(value)) };
    if (ArrayBuffer.isView(value)) return { $bytes: toBase64(new Uint8Array(value.buffer, value.byteOffset, value.byteLength)) };
    if (Array.isArray(value)) return value.map(encode);
    if (value && typeof value === "object" && Object.prototype.toString.call(value) === "[object Object]" && !(typeof value.toJSON === "function")) {
      const out = {};
      for (const key of Object.keys(value)) out[key] = encode(value[key]);
      return out;
    }
    return value;
  };

  const pending = new Map();
  let next = 1;
  const listeners = new Map();

  const invoke = (cmd, args = {}) => new Promise((resolve, reject) => {
    const id = next++;
    pending.set(id, { resolve, reject });
    try {
      post(JSON.stringify({ kind: "invoke", id, cmd, args: encode(args) }));
    } catch (error) {
      pending.delete(id);
      reject(error);
    }
  });

  const listen = (event, handler) => {
    if (!listeners.has(event)) listeners.set(event, new Set());
    const entry = (e) => handler(e);
    listeners.get(event).add(entry);
    return Promise.resolve(() => { listeners.get(event)?.delete(entry); });
  };

  const once = (event, handler) => {
    let stop = null;
    const promise = listen(event, (e) => { if (stop) stop(); handler(e); });
    promise.then((unlisten) => { stop = unlisten; });
    return promise;
  };

  const emit = (event, payload = null) => {
    post(JSON.stringify({ kind: "emit", event, payload: encode(payload) }));
    return Promise.resolve();
  };

  // The program's answers and events.
  const receive = (message) => {
    if (message.kind === "reply") {
      const waiting = pending.get(message.id);
      if (!waiting) return;
      pending.delete(message.id);
      if (!message.ok) waiting.reject(message.error);
      else if (message.bytes) {
        // A binary answer waits in the program until the page fetches it, once.
        fetch(schemeUrl("app", `__eskiu/bytes/${message.bytes}`))
          .then((r) => (r.ok ? r.arrayBuffer() : Promise.reject(new Error(`bytes: HTTP ${r.status}`))))
          .then((buffer) => waiting.resolve(new Uint8Array(buffer)), waiting.reject);
      } else waiting.resolve(message.value);
    } else if (message.kind === "event") {
      const set = listeners.get(message.event);
      if (!set) return;
      const e = { event: message.event, payload: message.payload, windowId: message.window };
      for (const handler of [...set]) {
        try { handler(e); } catch (error) { console.error(error); }
      }
    }
  };

  // A local file through a scheme the program serves (webapp_protocol_dir): the absolute
  // path, each part percent-encoded. Windows paths keep their drive ("C:/...").
  const convertFileSrc = (path, protocol = "asset") => {
    const parts = String(path).replace(/\\/g, "/").split("/");
    const encoded = parts.map((part, i) => (i === 0 && /^[A-Za-z]:$/.test(part) ? part : encodeURIComponent(part))).join("/");
    return schemeUrl(protocol, encoded.replace(/^\//, ""));
  };

  // Dialogs, in the shape of @tauri-apps/plugin-dialog.
  const options = (value) => (typeof value === "string" ? { title: value } : value || {});
  const dialog = {
    open: (o = {}) => invoke("ui:dialog", { ...o, type: "open" }),
    save: (o = {}) => invoke("ui:dialog", { ...o, type: "save" }),
    message: (text, o) => invoke("ui:dialog", { ...options(o), message: String(text), type: "message" }).then(() => undefined),
    ask: (text, o) => invoke("ui:dialog", { ...options(o), message: String(text), type: "confirm", okLabel: options(o).okLabel || "Yes", cancelLabel: options(o).cancelLabel || "No" }),
    confirm: (text, o) => invoke("ui:dialog", { ...options(o), message: String(text), type: "confirm" }),
  };

  // This page's window.
  const windowOp = (op, args = {}) => invoke("ui:window", { ...args, op });
  const closeHandlers = new Set();
  let closeListening = false;
  const currentWindow = {
    id: host.window || 0,
    setTitle: (title) => windowOp("setTitle", { title }),
    setSize: (width, height) => windowOp("setSize", { width, height }),
    setMinSize: (width, height) => windowOp("setMinSize", { width, height }),
    setFullscreen: (on) => windowOp("setFullscreen", { on: !!on }),
    isFullscreen: () => windowOp("isFullscreen"),
    focus: () => windowOp("focus"),
    openDevtools: () => windowOp("devtools"),
    close: () => windowOp("close"),
    destroy: () => windowOp("destroy"),
    // The handler runs when the user closes the window; event.preventDefault() keeps it open.
    onCloseRequested: async (handler) => {
      closeHandlers.add(handler);
      if (!closeListening) {
        closeListening = true;
        await listen("ui:close-requested", async () => {
          let prevented = false;
          const event = { preventDefault: () => { prevented = true; } };
          for (const h of [...closeHandlers]) await h(event);
          if (!prevented) windowOp("destroy");
        });
      }
      await windowOp("interceptClose", { on: true });
      return () => {
        closeHandlers.delete(handler);
        if (closeHandlers.size === 0) windowOp("interceptClose", { on: false });
      };
    },
  };

  // WebKitGTK plays media only from http(s), file: and blob: URLs: on Linux, a media
  // element's source on one of the program's schemes is read whole into a blob first.
  if (os === "linux" && window.HTMLMediaElement) {
    const native = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, "src");
    const setAttribute = Element.prototype.setAttribute;
    const custom = (url) => /^[a-z][a-z0-9+.-]*:/i.test(url) && !/^(https?|file|blob|data|about):/i.test(url);
    const blobs = new WeakMap();
    const load = (element, url) => {
      fetch(url)
        .then((r) => (r.ok ? r.blob() : Promise.reject(new Error(`HTTP ${r.status}`))))
        .then((blob) => {
          if (blobs.has(element)) URL.revokeObjectURL(blobs.get(element));
          const local = URL.createObjectURL(blob);
          blobs.set(element, local);
          native.set.call(element, local);
        }, () => native.set.call(element, url));
    };
    Object.defineProperty(HTMLMediaElement.prototype, "src", {
      configurable: true,
      enumerable: native.enumerable,
      get() { return native.get.call(this); },
      set(url) { const text = String(url); if (custom(text)) load(this, text); else native.set.call(this, text); },
    });
    Element.prototype.setAttribute = function (name, value) {
      if (this instanceof HTMLMediaElement && String(name).toLowerCase() === "src" && custom(String(value))) load(this, String(value));
      else setAttribute.call(this, name, value);
    };
    const NativeAudio = window.Audio;
    window.Audio = function Audio(src) {
      const audio = new NativeAudio();
      if (src !== undefined) audio.src = src;
      return audio;
    };
    window.Audio.prototype = NativeAudio.prototype;
  }

  window.__ESKIU__ = Object.freeze({
    os, invoke, listen, once, emit, receive, convertFileSrc, schemeUrl, dialog,
    window: currentWindow,
    quit: (code = 0) => invoke("ui:quit", { code }),
  });
})();
