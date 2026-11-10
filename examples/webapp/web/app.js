// The sample's page: each button calls the program (src/main.esk) through @eskiu/webapp,
// which the program serves at /@eskiu/webapp.js (a bundled front end imports the npm
// package instead). With ?selftest=1 it checks each feature and reports to the program.
import {
  invoke, listen, once, emit, convertFileSrc, schemeUrl, platform,
  open, save, message, ask, getCurrentWindow,
} from "/@eskiu/webapp.js";

const $ = (id) => document.getElementById(id);
const log = (...parts) => {
  const line = parts.map((p) => (typeof p === "string" ? p : JSON.stringify(p))).join(" ");
  $("log").textContent = `${line}\n${$("log").textContent}`;
};
const run = (id, fn) => $(id).addEventListener("click", () => fn().catch((e) => log("error:", String(e))));

listen("progress", (e) => log("progress", e.payload));
listen("pong", (e) => log("pong", e.payload));

run("greet", async () => log(await invoke("greet", { name: $("name").value })));
run("info", async () => log(await invoke("system_info")));
run("count", async () => log("counted", await invoke("slow_count", { n: 5 })));
run("bytes", async () => {
  const out = await invoke("pattern_bytes", { n: 8 });
  const back = await invoke("echo_bytes", { data: new Uint8Array([1, 2, 3, 255]) });
  log("bytes", Array.from(out), "echoed", Array.from(back));
});
run("ping", async () => emit("ping", { at: Date.now() }));
$("unsaved").addEventListener("change", (e) => invoke("set_unsaved", { on: e.target.checked }));
run("open", async () => log("open", await open({ multiple: true, title: "Pick files", filters: [{ name: "Images", extensions: ["png", "jpg"] }, { name: "All", extensions: ["*"] }] })));
run("folder", async () => {
  const dir = await open({ directory: true, title: "Pick a folder" });
  log("folder", dir);
  if (dir) log("contains", (await invoke("list_dir", { path: dir })).slice(0, 20));
});
run("save", async () => log("save", await save({ title: "Save as", defaultPath: "notes.txt", filters: [{ name: "Text", extensions: ["txt"] }] })));
run("message", async () => { await message("This came from the page.", { title: "Message", kind: "info" }); log("message closed"); });
run("ask", async () => log("ask", await ask("Do you like it?", { title: "Question", kind: "warning" })));
run("demo", async () => log(await (await fetch(schemeUrl("demo", "hello.txt?from=page"))).text()));
run("title", async () => getCurrentWindow().setTitle(`Web App at ${new Date().toLocaleTimeString()}`));
run("fullscreen", async () => getCurrentWindow().setFullscreen(!(await getCurrentWindow().isFullscreen())));
run("close", async () => getCurrentWindow().close());

invoke("tone_path").then((path) => { $("tone").src = convertFileSrc(path); });
log(`platform ${platform()}, secure context ${window.isSecureContext}`);

// ---- The self-test ----

const withTimeout = (promise, ms, what) =>
  Promise.race([promise, new Promise((_, reject) => setTimeout(() => reject(new Error(`${what}: timed out`)), ms))]);

async function selftest() {
  const results = [];
  const check = async (name, fn) => {
    try {
      const detail = await withTimeout(fn(), 5000, name);
      results.push({ name, ok: true, detail });
    } catch (error) {
      results.push({ name, ok: false, detail: String(error && error.message ? error.message : error) });
    }
  };
  const expect = (ok, what) => { if (!ok) throw new Error(what); };

  await check("a string answer", async () => {
    const text = await invoke("greet", { name: "Eskiu" });
    expect(text === "Hello, Eskiu, from Eskiu", text);
  });
  await check("a JSON answer", async () => {
    const info = await invoke("system_info");
    expect(info.pid > 0 && info.window === getCurrentWindow().id, JSON.stringify(info));
  });
  await check("an error", async () => {
    const error = await invoke("greet", { name: "" }).then(() => "resolved", (e) => e);
    expect(error === "greet: no name", String(error));
  });
  await check("an unknown command", async () => {
    const error = await invoke("nope").then(() => "resolved", (e) => e);
    expect(error === "no command nope", String(error));
  });
  await check("events both ways", async () => {
    const got = new Promise((resolve) => once("pong", (e) => resolve(e.payload)));
    await emit("ping", { n: 7 });
    const payload = await got;
    expect(payload && payload.n === 7, JSON.stringify(payload));
  });
  await check("an answer from a thread, with its events", async () => {
    const steps = [];
    const stop = await listen("progress", (e) => steps.push(e.payload.step));
    const n = await invoke("slow_count", { n: 3 });
    stop();
    expect(n === 3 && steps.join() === "1,2,3", `${n} ${steps}`);
  });
  await check("bytes both ways", async () => {
    const out = await invoke("pattern_bytes", { n: 70000 });
    expect(out instanceof Uint8Array && out.length === 70000 && out[300] === 300 % 256, `got ${out && out.length}`);
    const back = await invoke("echo_bytes", { data: new Uint8Array([1, 2, 3, 255]) });
    expect(Array.from(back).join() === "1,2,3,255", Array.from(back).join());
  });
  await check("a file", async () => {
    const files = await invoke("list_dir", { path: "/" });
    expect(Array.isArray(files) && files.length > 0, JSON.stringify(files));
  });
  await check("the program's scheme", async () => {
    const response = await fetch(schemeUrl("demo", "hello.txt?x=1"));
    const text = await response.text();
    expect(response.status === 200 && text === 'Hello from demo://, query "x=1"', `${response.status} ${text}`);
    const missing = await fetch(schemeUrl("demo", "nothing"));
    expect(missing.status === 404, `missing: ${missing.status}`);
  });
  await check("a range of a local file", async () => {
    const url = convertFileSrc(await invoke("tone_path"));
    const response = await fetch(url, { headers: { Range: "bytes=0-11" } });
    const bytes = new Uint8Array(await response.arrayBuffer());
    const text = String.fromCharCode(...bytes.slice(8, 12));
    expect(response.status === 206 && bytes.length === 12 && text === "WAVE", `${response.status} ${bytes.length} ${response.headers.get("content-range")}`);
    return response.headers.get("content-range");
  });
  await check("audio through the scheme", async () => {
    const audio = new Audio();
    audio.preload = "metadata";
    const loaded = new Promise((resolve, reject) => {
      audio.onloadedmetadata = () => resolve(audio.duration);
      audio.onerror = () => reject(new Error(`audio error ${audio.error && audio.error.code}`));
    });
    audio.src = convertFileSrc(await invoke("tone_path"));
    const duration = await loaded;
    expect(Math.abs(duration - 1) < 0.1, `duration ${duration}`);
    return duration;
  });
  await check("the window", async () => {
    await getCurrentWindow().setTitle("Self-test");
    expect((await getCurrentWindow().isFullscreen()) === false, "full screen");
  });
  await check("the page's graphics", async () => {
    const gl = document.createElement("canvas").getContext("webgl2");
    return { secureContext: window.isSecureContext, webgl2: !!gl, webgpu: !!navigator.gpu };
  });

  const ok = results.every((r) => r.ok);
  const report = results.map((r) => `${r.ok ? "ok  " : "FAIL"} ${r.name}${r.detail !== undefined ? `: ${JSON.stringify(r.detail)}` : ""}`).join("\n");
  await invoke("selftest_done", { ok, report: `platform ${platform()}\n${report}` });
}

if (new URLSearchParams(location.search).has("selftest")) selftest();
