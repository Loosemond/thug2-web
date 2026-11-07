// Opens a page in headless Chrome (DevTools protocol), prints its console for `seconds`, then the WebGPU adapter, the
// animation frames a second and the page's stats line, and saves a screenshot.
//   node port/tools/drive_page.mjs <url> <seconds> <screenshot.png>
// KEYS="ArrowDown Enter ArrowUp:3000 KeyA" presses keys after that (one a second; ":ms" holds one), AFTER=<s> waits
// before the screenshot, SHOTS=<n> takes n more a second apart first. CHROME_FLAGS adds Chrome flags: headless Chrome holds animation frames to a few a second unless
// "--disable-gpu-vsync --disable-frame-rate-limit" - its frame rate is no measure of the build's.
// PHONE=1 makes the page a phone's (852x393 landscape, touch: the page shows its touch controls); TAPS="#a #stick:up:3000"
// then touches the pad's controls (a selector, a direction on the stick, ":ms" holds), after KEYS.
// The profile goes in a new folder under $SCRATCH (else the system's temporary folder).
import { spawn } from "node:child_process";
import { writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
const [url, seconds = "30", shot = "shot.png"] = process.argv.slice(2);
const port = 9333;
const profile = mkdtempSync(join(process.env.SCRATCH || tmpdir(), "chrome-"));
const chrome = spawn("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", [
  "--headless=new", `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, "--no-first-run",
  "--no-default-browser-check", "--enable-unsafe-webgpu", "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows", ...(process.env.CHROME_FLAGS ? process.env.CHROME_FLAGS.split(" ") : []), "--window-size=1400,900", "about:blank"], { stdio: "ignore" });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; ++i) {
  try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); break; } catch { await sleep(200); }
}
const page = targets.find(t => t.type === "page");
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener("open", r));
let id = 0; const pending = new Map();
const send = (method, params = {}) => new Promise(r => { pending.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
const t0 = Date.now();
ws.addEventListener("message", e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result ?? m.error); pending.delete(m.id); return; }
  const at = ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
  if (m.method === "Runtime.consoleAPICalled")
    console.log(at, m.params.type, m.params.args.map(a => a.value ?? a.description ?? "").join(" "));
  else if (m.method === "Runtime.exceptionThrown")
    console.log(at, "EXCEPTION", m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text);
  else if (m.method === "Log.entryAdded")
    console.log(at, "log", m.params.entry.level, m.params.entry.text, m.params.entry.url ?? "");
});
await send("Runtime.enable"); await send("Log.enable"); await send("Page.enable");
if (process.env.PHONE) {
  await send("Emulation.setDeviceMetricsOverride", { width: 852, height: 393, deviceScaleFactor: 3, mobile: true,
    screenOrientation: { type: "landscapePrimary", angle: 90 } });
  await send("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  await send("Emulation.setEmitTouchEventsForMouse", { enabled: true, configuration: "mobile" });
}
await send("Page.navigate", { url });
await sleep(Number(seconds) * 1000);
const adapter = await send("Runtime.evaluate", { expression: "navigator.gpu.requestAdapter().then(a => JSON.stringify({vendor: a.info.vendor, arch: a.info.architecture, device: a.info.device, desc: a.info.description, fallback: a.info.isFallbackAdapter}))", awaitPromise: true, returnByValue: true });
console.log("adapter:", adapter.result?.value);
const raf = await send("Runtime.evaluate", { expression: "new Promise(r => { let n = 0; const t0 = performance.now(); const f = () => { ++n; if (performance.now() - t0 < 1000) requestAnimationFrame(f); else r(n); }; requestAnimationFrame(f); })", awaitPromise: true, returnByValue: true });
console.log("animation frames in 1 s:", raf.result?.value);
for (let i = 0; i < 4; ++i) {
  const s = await send("Runtime.evaluate", { expression: "document.getElementById('stats').textContent", returnByValue: true });
  console.log("stats:", s.result?.value); await sleep(1000);
}
// EVAL="<js>": run in the page before the keys (to instrument it)
if (process.env.EVAL) console.log("eval:", (await send("Runtime.evaluate", { expression: process.env.EVAL, returnByValue: true })).result?.value);
// KEYS="ArrowDown ArrowDown Enter": pressed one a second, then a second screenshot
const vk = { ArrowDown: 40, ArrowUp: 38, ArrowLeft: 37, ArrowRight: 39, Enter: 13, Escape: 27, Space: 32, KeyA: 65,
  KeyD: 68, KeyS: 83, KeyW: 87 };
for (const item of (process.env.KEYS || "").split(" ").filter(Boolean)) {
  const [key, hold = "150"] = item.split(":"); // ("ArrowUp:3000": held 3 s)
  for (const type of ["rawKeyDown", "keyUp"]) {
    await send("Input.dispatchKeyEvent", { type, key, code: key, windowsVirtualKeyCode: vk[key] ?? 0 });
    await sleep(type === "rawKeyDown" ? Number(hold) : 150);
  }
  console.log("pressed", key); await sleep(1000);
}
const offsets = { up: [0, -0.8], down: [0, 0.8], left: [-0.8, 0], right: [0.8, 0] };
for (const item of (process.env.TAPS || "").split(" ").filter(Boolean)) {
  const parts = item.split(":");
  const hold = /^\d+$/.test(parts.at(-1)) ? Number(parts.pop()) : 150;
  const [selector, direction] = parts;
  const box = await send("Runtime.evaluate", { expression: `JSON.stringify(document.querySelector("${selector}").getBoundingClientRect())`, returnByValue: true });
  const r = JSON.parse(box.result.value);
  const [ox, oy] = offsets[direction] || [0, 0];
  const x = r.left + r.width / 2 * (1 + ox), y = r.top + r.height / 2 * (1 + oy);
  await send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y, id: 1 }] });
  await sleep(hold);
  await send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
  console.log("touched", item); await sleep(1000);
}
if (process.env.KEYS || process.env.TAPS) await sleep(Number(process.env.AFTER || 3) * 1000);
// SHOTS=n: n more screenshots a second apart before the last (shot-1.png ...): is something still moving?
for (let i = 1; i <= Number(process.env.SHOTS || 0); ++i) {
  const extra = await send("Page.captureScreenshot", { format: "png" });
  writeFileSync(shot.replace(/\.png$/, `-${i}.png`), Buffer.from(extra.data, "base64"));
  await sleep(1000);
}
const status = await send("Runtime.evaluate", { expression: "document.getElementById('status').textContent + ' | ' + document.getElementById('stats').textContent", returnByValue: true });
console.log("page status:", status.result?.value);
const shotData = await send("Page.captureScreenshot", { format: "png" });
writeFileSync(shot, Buffer.from(shotData.data, "base64"));
ws.close(); chrome.kill();
process.exit(0);
