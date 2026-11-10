// @eskiu/webapp: the page's API of an Eskiu web app (<ui/webapp>), in the shape of
// @tauri-apps/api so a front end moves over by changing its imports. Each call goes to
// `window.__ESKIU__`, which the host defines before the page runs (bridge.js); outside an
// Eskiu window the calls reject (isEskiu() says which).

const bridge = () => {
  const b = globalThis.window && globalThis.window.__ESKIU__;
  if (!b) throw new Error("@eskiu/webapp: not in an Eskiu window");
  return b;
};
const call = (fn) => {
  try { return fn(bridge()); } catch (error) { return Promise.reject(error); }
};

/** True inside an Eskiu window. */
export const isEskiu = () => !!(globalThis.window && globalThis.window.__ESKIU__);

/** The platform: "macos", "windows" or "linux". */
export const platform = () => (isEskiu() ? bridge().os : "web");

/** Runs the program's command `cmd` with `args`; resolves with its answer or rejects with its error. */
export const invoke = (cmd, args) => call((b) => b.invoke(cmd, args));

/** Calls `handler` for each event `event` the program emits; resolves with a function that stops it. */
export const listen = (event, handler) => call((b) => b.listen(event, handler));

/** As listen, for the next event only. */
export const once = (event, handler) => call((b) => b.once(event, handler));

/** Sends the program the event `event` with `payload` (webapp_listen). */
export const emit = (event, payload) => call((b) => b.emit(event, payload));

/** The URL of the local file `path` through the program's scheme `protocol` (webapp_protocol_dir). */
export const convertFileSrc = (path, protocol = "asset") => bridge().convertFileSrc(path, protocol);

/** The URL of `path` under a scheme the program serves (webapp_protocol). */
export const schemeUrl = (scheme, path = "") => bridge().schemeUrl(scheme, path);

/** Native dialogs, as @tauri-apps/plugin-dialog. */
export const open = (options) => call((b) => b.dialog.open(options));
export const save = (options) => call((b) => b.dialog.save(options));
export const message = (text, options) => call((b) => b.dialog.message(text, options));
export const ask = (text, options) => call((b) => b.dialog.ask(text, options));
export const confirm = (text, options) => call((b) => b.dialog.confirm(text, options));

/** This page's window. */
export const getCurrentWindow = () => bridge().window;

/** Ends the program with `code`. */
export const quit = (code = 0) => call((b) => b.quit(code));
