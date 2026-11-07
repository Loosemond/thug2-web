// After the link: a call to thug2_code_slot (port/src/hook/web_hook.cpp) in front of every indirect call, so its
// target - a function pointer, which may be one of the original's code addresses - is looked up first. The call goes
// where the call_indirect's last operand (the target) is already on the stack: `call` takes it and leaves the slot.
//
//     node port/tools/route_indirect_calls.mjs build/web/thug2.wasm
//
// The function is found by name (the name section: --profiling-funcs) or export. A call already routed is left as it
// is, so running this twice changes nothing. Every instruction is decoded (an immediate's bytes are never taken for an
// opcode); one it doesn't know stops it.
import { readFileSync, writeFileSync } from "node:fs";

const path = process.argv[2];
const wasm = readFileSync(path);
const ROUTER = "thug2_code_slot";

let at = 0;
const byte = () => wasm[at++];
const u32 = () => {
  let result = 0, shift = 0, b;
  do { b = wasm[at++]; result += (b & 0x7F) * 2 ** shift; shift += 7; } while (b & 0x80);
  return result;
};
const skipLeb = () => { while (wasm[at++] & 0x80); };
const name = () => { const n = u32(); const s = wasm.toString("utf8", at, at + n); at += n; return s; };
const uleb = n => { const out = []; do { let b = n & 0x7F; n = Math.floor(n / 128); out.push(n ? b | 0x80 : b); } while (n); return out; };

if (wasm.readUInt32LE(0) !== 0x6D736100) throw new Error(`${path}: not a wasm module`);
const sections = [];
at = 8;
while (at < wasm.length) {
  const id = byte(), size = u32();
  sections.push({ id, start: at, end: at + size });
  at += size;
}

// The router's function index.
let importedFunctions = 0, router = -1;
for (const s of sections) {
  at = s.start;
  if (s.id === 2) {
    for (let n = u32(); n--;) {
      name(); name();
      const kind = byte();
      if (kind === 0) { ++importedFunctions; skipLeb(); }
      else if (kind === 1) { const t = byte(); if (t === 0x63 || t === 0x64) skipLeb(); limits(); }
      else if (kind === 2) limits();
      else if (kind === 3) { valtype(); byte(); }
      else if (kind === 4) { byte(); skipLeb(); }
      else throw new Error(`import kind ${kind}`);
    }
  } else if (s.id === 7) {
    for (let n = u32(); n--;) {
      const field = name(), kind = byte(), index = u32();
      if (field === ROUTER && kind === 0) router = index;
    }
  } else if (s.id === 0 && name() === "name") {
    while (at < s.end) {
      const sub = byte(), size = u32(), end = at + size;
      if (sub === 1)
        for (let n = u32(); n--;) { const index = u32(); if (name() === ROUTER) router = index; }
      at = end;
    }
  }
}
function limits() { const flags = byte(); skipLeb(); if (flags & 1) skipLeb(); }
function valtype() { const t = byte(); if (t === 0x63 || t === 0x64) skipLeb(); }
if (router < 0) throw new Error(`${path}: no function named ${ROUTER}`);
const routerCall = [0x10, ...uleb(router)];

// The code: each body copied, with the call put in front of each call_indirect / return_call_indirect.
const memarg = () => { const align = u32(); if (align & 0x40) skipLeb(); skipLeb(); };
const blocktype = () => {
  const b = wasm[at];
  if (b === 0x63 || b === 0x64) { ++at; skipLeb(); } else if ((b & 0xC0) === 0x40) ++at; else skipLeb();
};
function instruction(op) {
  if (op <= 0x01 || op === 0x05 || op === 0x0A || op === 0x0B || op === 0x0F || op === 0x19 || op === 0x1A ||
      op === 0x1B || (op >= 0x45 && op <= 0xC4) || op === 0xD1 || op === 0xD3 || op === 0xD4) return;
  if (op <= 0x04 || op === 0x06) return blocktype();
  if ((op >= 0x07 && op <= 0x09) || op === 0x0C || op === 0x0D || op === 0x10 || op === 0x12 || op === 0x14 ||
      op === 0x15 || op === 0x18 || (op >= 0x20 && op <= 0x26) || op === 0x3F || op === 0x40 || op === 0x41 ||
      op === 0x42 || op === 0xD0 || op === 0xD2 || op === 0xD5 || op === 0xD6) return skipLeb();
  if (op === 0x0E) { for (let n = u32(); n--;) skipLeb(); return skipLeb(); }
  if (op === 0x11 || op === 0x13) { skipLeb(); return skipLeb(); }
  if (op === 0x1C) { for (let n = u32(); n--;) valtype(); return; }
  if (op === 0x1F) {
    blocktype();
    for (let n = u32(); n--;) { const kind = byte(); if (kind < 2) skipLeb(); skipLeb(); }
    return;
  }
  if (op >= 0x28 && op <= 0x3E) return memarg();
  if (op === 0x43) { at += 4; return; }
  if (op === 0x44) { at += 8; return; }
  if (op === 0xFC) {
    const sub = u32();
    if (sub <= 7) return;
    if (sub === 8 || sub === 10 || sub === 12 || sub === 14) { skipLeb(); return skipLeb(); }
    if (sub === 9 || sub === 11 || sub === 13 || (sub >= 15 && sub <= 17)) return skipLeb();
    throw new Error(`0xFC ${sub}`);
  }
  if (op === 0xFD) {
    const sub = u32();
    if (sub <= 11 || sub === 92 || sub === 93) return memarg();
    if (sub === 12 || sub === 13) { at += 16; return; }
    if (sub >= 21 && sub <= 34) { ++at; return; }
    if (sub >= 84 && sub <= 91) { memarg(); ++at; return; }
    return;
  }
  if (op === 0xFE) {
    const sub = u32();
    if (sub === 3) { ++at; return; }
    if (sub <= 2 || (sub >= 0x10 && sub <= 0x4E)) return memarg();
    throw new Error(`0xFE ${sub}`);
  }
  throw new Error(`opcode 0x${op.toString(16)}`);
}

const code = sections.find(s => s.id === 10);
at = code.start;
const count = u32();
const bodies = [];
let routed = 0, already = 0;
for (let f = 0; f < count; ++f) {
  const size = u32(), end = at + size, out = [];
  const start = at;
  for (let n = u32(); n--;) { skipLeb(); valtype(); } // (the locals)
  let copied = start, previous = -1; // (where the last instruction started)
  try {
    while (at < end) {
      const begin = at, op = byte();
      if ((op === 0x11 || op === 0x13) && importedFunctions + f !== router) {
        const routedAlready = previous >= 0 && begin - previous === routerCall.length &&
          routerCall.every((b, i) => wasm[previous + i] === b);
        if (routedAlready) ++already;
        else {
          out.push(wasm.subarray(copied, begin), Buffer.from(routerCall));
          copied = begin;
          ++routed;
        }
      }
      instruction(op);
      previous = begin;
    }
  } catch (error) {
    throw new Error(`function ${importedFunctions + f} at byte ${at - 1}: unknown ${error.message}`);
  }
  if (at !== end) throw new Error(`function ${importedFunctions + f}: decoded past its end`);
  out.push(wasm.subarray(copied, end));
  const body = Buffer.concat(out);
  bodies.push(Buffer.from(uleb(body.length)), body);
}
const content = Buffer.concat([Buffer.from(uleb(count)), ...bodies]);
const after = wasm.subarray(code.end);
// (the section's header: its id, then its size, which ends where the content starts)
let header = code.start - 1;
while (wasm[header - 1] & 0x80) --header;
const result = Buffer.concat([wasm.subarray(0, header - 1), Buffer.from([10, ...uleb(content.length)]), content, after]);
if (routed) writeFileSync(path, result);
console.log(`${path}: ${routed} indirect calls routed through ${ROUTER}` + (already ? `, ${already} already` : ""));
