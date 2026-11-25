// ================================================================ macros, shift pairs (v28)
// Brett: "add nice macros like delete line"; "the common double keys ... like
// equals and then via shift plus". Vial's dynamic macros (M0-M15, the VIA
// macro buffer) with names kept on the keyboard (0xF6 0x21, macros.c), and
// Vial's key overrides used as "shift pairs".

// ---- Vial's macro format: text bytes; 1,1|2|3,kc (tap / down / up, kc < 256);
// 1,5|6|7,lo,hi (16-bit keycode; a zero low byte as 0xFF01-style); 1,4,d0,d1 (wait ms = d0-1 + (d1-1)*255).
const MC = { count: 16, size: 0, names: [], loaded: false, edit: -1, steps: [], rec: false };
function mcDecode(bytes) {
  const steps = [];
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    if (b === 1) {
      const c = bytes[++i];
      if (c >= 1 && c <= 3) steps.push({ t: ["", "tap", "down", "up"][c], kc: bytes[++i] });
      else if (c >= 5 && c <= 7) { let kc = bytes[i + 1] | (bytes[i + 2] << 8); i += 2; if (kc > 0xff00) kc = (kc & 0xff) << 8; steps.push({ t: ["tap", "down", "up"][c - 5], kc }); }
      else if (c === 4) { steps.push({ t: "delay", ms: (bytes[i + 1] - 1) + (bytes[i + 2] - 1) * 255 }); i += 2; }
    } else {
      const ch = String.fromCharCode(b), last = steps[steps.length - 1];
      if (last && last.t === "text") last.s += ch; else steps.push({ t: "text", s: ch });
    }
  }
  return steps;
}
function mcEncode(steps) {
  const out = [];
  for (const s of steps) {
    if (s.t === "text") { for (const ch of s.s) { const c = ch.charCodeAt(0); if ((c >= 0x20 && c < 0x7f) || c === 10 || c === 9) out.push(c); } }
    else if (s.t === "delay") { const ms = Math.max(0, Math.min(65024, Math.round(s.ms) || 0)); out.push(1, 4, (ms % 255) + 1, Math.floor(ms / 255) + 1); }
    else if (s.kc > 0 && s.kc < 0x100) out.push(1, { tap: 1, down: 2, up: 3 }[s.t], s.kc);
    else if (s.kc >= 0x100) out.push(1, { tap: 5, down: 6, up: 7 }[s.t], ...((s.kc & 0xff) ? [s.kc & 0xff, s.kc >> 8] : [s.kc >> 8, 0xff]));
  }
  return out;
}
function mcDescribe(steps) {
  if (!steps.length) return "(empty)";
  return steps.map((s) => s.t === "text" ? JSON.stringify(s.s) : s.t === "delay" ? `wait ${s.ms} ms` : s.t === "tap" ? kcName(s.kc) : `${s.t === "down" ? "hold" : "release"} ${kcName(s.kc)}`).join("  →  ");
}
const mcUsed = (all) => all.reduce((n, st) => n + mcEncode(st).length + 1, 0);

async function macrosLoad() {
  if (S.mode !== "hid") return;
  const c = await cmd([0x0c]), z = await cmd([0x0d]);
  MC.count = Math.min(16, c[1]); MC.size = (z[1] << 8) | z[2];
  const bytes = [];
  let nul = 0;
  for (let off = 0; off < MC.size && nul < MC.count; off += 28) {
    const n = Math.min(28, MC.size - off), d = await cmd([0x0e, off >> 8, off & 0xff, n]);
    for (let i = 0; i < n; i++) { bytes.push(d[4 + i]); if (d[4 + i] === 0) nul++; }
  }
  S.macros = [];
  for (let m = 0, at = 0; m < MC.count; m++) {
    const end = bytes.indexOf(0, at), one = bytes.slice(at, end < 0 ? bytes.length : end);
    S.macros.push(mcDecode(one));
    at = end < 0 ? bytes.length : end + 1;
  }
  S.macroNames = [];
  for (let i = 0; i < MC.count; i++) {
    const d = await cmd([0xf6, 0x21, 0, i]);
    S.macroNames.push(d[2] === 0xa4 ? String.fromCharCode(...d.slice(4, 16)).replace(/\0.*$/, "") : "");
  }
  MC.loaded = true;
}
// Writes every macro (the buffer up to the last terminator, then a 0 in the buffer's last byte) and the names that changed.
async function macrosWrite(all, names) {
  const buf = [];
  all.forEach((st) => buf.push(...mcEncode(st), 0));
  if (buf.length > MC.size) throw new Error(`the macros need ${buf.length} bytes, the keyboard has ${MC.size}`);
  const ok = async (bytes) => { const d = await cmd(bytes); if (d[0] === 0xff) throw new Error("refused — confirm the connection again"); return d; };
  for (let off = 0; off < buf.length; off += 28) { const part = buf.slice(off, off + 28); await ok([0x0f, off >> 8, off & 0xff, part.length, ...part]); }
  if (buf.length < MC.size) await ok([0x0f, (MC.size - 1) >> 8, (MC.size - 1) & 0xff, 1, 0]);
  for (let i = 0; i < names.length; i++) {
    if ((S.macroNames[i] || "") === (names[i] || "")) continue;
    const enc = [...(names[i] || "").slice(0, 12)].map((ch) => ch.charCodeAt(0) & 0x7f);
    const d = await cmd([0xf6, 0x21, 1, i, ...enc, ...Array(12 - enc.length).fill(0)]);
    if (d[2] !== 0xa4 || d[16] !== 1) throw new Error("the keyboard refused the name");
  }
  await macrosLoad();
}

const MC_TEMPLATES = [
  ["Delete line", [{ t: "tap", kc: 0x4a }, { t: "tap", kc: 0x24d }, { t: "tap", kc: 0x24f }, { t: "tap", kc: 0x4c }]],
  ["Dupl. line", [{ t: "tap", kc: 0x4a }, { t: "tap", kc: 0x24d }, { t: "tap", kc: 0x106 }, { t: "tap", kc: 0x4d }, { t: "tap", kc: 0x28 }, { t: "tap", kc: 0x119 }]],
  ["Signature", [{ t: "text", s: "Best regards," }, { t: "tap", kc: 0x28 }, { t: "text", s: "Brett" }]],
  ["Email", [{ t: "text", s: "you@example.com" }]],
  ["Paste+Enter", [{ t: "tap", kc: 0x119 }, { t: "delay", ms: 50 }, { t: "tap", kc: 0x28 }]],
];
// Keys a step can use: the picker's keycodes (hold / release: 8-bit only).
function mcKeyOptions(sel, basicOnly) {
  return DATA.cats.map((cat, ci) => {
    const items = DATA.picker.slice(DATA.catStarts[ci], DATA.catStarts[ci + 1] || DATA.picker.length).filter(([v]) => v > 1 && (!basicOnly || v < 0x100) && !(v >= R.QK_MACRO && v < R.QK_MACRO + 16));
    return items.length ? `<optgroup label="${cat}">` + items.map(([v]) => `<option value="${v}" ${v === sel ? "selected" : ""}>${escapeHtml(kcName(v))}</option>`).join("") + "</optgroup>" : "";
  }).join("") + (sel > 1 && !DATA.picker.some(([v]) => v === sel) ? `<option value="${sel}" selected>${escapeHtml(kcName(sel))}</option>` : "");
}

function renderMacros() {
  const hid = S.mode === "hid";
  if (!hid || !S.macros) { setHtml($("mcList"), "<div class='dim'>Loading…</div>"); return; }
  setHtml($("mcList"), S.macros.map((st, i) => `<div class="mcRow"><span class="dim">M${i}</span><span class="mcName">${escapeHtml(S.macroNames[i] || (st.length ? "Macro " + i : "—"))}</span>` +
    `<span class="mcPrev">${escapeHtml(mcDescribe(st))}</span><button data-mc="${i}">${st.length ? "Edit" : "New"}</button></div>`).join(""));
  $("mcEdit").classList.toggle("hidden", MC.edit < 0);
  if (MC.edit >= 0) {
    $("mcTitle").textContent = `Macro M${MC.edit}`;
    const html = MC.steps.map((s, i) => {
      const kind = `<select data-k="${i}">` + [["tap", "Tap"], ["down", "Hold"], ["up", "Release"], ["text", "Type"], ["delay", "Wait"]].map(([v, l]) => `<option value="${v}" ${v === s.t ? "selected" : ""}>${l}</option>`).join("") + "</select>";
      const val = s.t === "text" ? `<input type="text" data-s="${i}" value="${escapeHtml(s.s.replace(/\n/g, "\\n"))}" title="\\n = Enter">`
        : s.t === "delay" ? `<input type="number" data-ms="${i}" min="0" max="65000" step="10" value="${s.ms}"> ms`
        : `<select data-kc="${i}">${mcKeyOptions(s.kc, s.t !== "tap")}</select>`;
      return `<div class="mcStep">${i + 1}. ${kind}${val}<button data-up="${i}" title="Move up">↑</button><button data-dn="${i}" title="Move down">↓</button><button data-del="${i}" title="Remove">✕</button></div>`;
    }).join("");
    setHtml($("mcSteps"), html || "<div class='dim small'>No steps yet: add some below, record them, or start from a template.</div>");
    if (document.activeElement !== $("mcName")) $("mcName").value = MC.name;
    $("mcPreview").textContent = mcDescribe(MC.steps);
    const all = S.macros.map((st, i) => (i === MC.edit ? MC.steps : st));
    $("mcBytes").textContent = `${mcUsed(all)} of ${MC.size} bytes`;
    $("mcRec").classList.toggle("on", MC.rec);
    $("mcRec").textContent = MC.rec ? "■ Stop recording" : "● Record";
  }
  renderKo();
}
$("mcTpl").innerHTML = MC_TEMPLATES.map(([n], i) => `<option value="${i}">${escapeHtml(n)}</option>`).join("");
$("mcList").addEventListener("click", (e) => {
  const b = e.target.closest("[data-mc]"); if (!b) return;
  MC.edit = +b.dataset.mc; MC.steps = JSON.parse(JSON.stringify(S.macros[MC.edit])); MC.name = S.macroNames[MC.edit] || "";
  render();
});
$("mcSteps").addEventListener("input", (e) => {
  const t = e.target, d = t.dataset;
  if (d.k !== undefined) { const s = MC.steps[+d.k], k = t.value; MC.steps[+d.k] = k === "text" ? { t: k, s: "" } : k === "delay" ? { t: k, ms: 100 } : { t: k, kc: s.kc && (k === "tap" || s.kc < 0x100) ? s.kc : 4 }; }
  else if (d.kc !== undefined) MC.steps[+d.kc].kc = +t.value;
  else if (d.s !== undefined) { MC.steps[+d.s].s = t.value.replace(/\\n/g, "\n"); $("mcPreview").textContent = mcDescribe(MC.steps); return; }
  else if (d.ms !== undefined) { MC.steps[+d.ms].ms = +t.value; $("mcPreview").textContent = mcDescribe(MC.steps); return; }
  render();
});
$("mcSteps").addEventListener("click", (e) => {
  const d = e.target.dataset, swap = (a, b) => { if (b >= 0 && b < MC.steps.length) [MC.steps[a], MC.steps[b]] = [MC.steps[b], MC.steps[a]]; };
  if (d.up !== undefined) swap(+d.up, +d.up - 1); else if (d.dn !== undefined) swap(+d.dn, +d.dn + 1); else if (d.del !== undefined) MC.steps.splice(+d.del, 1); else return;
  render();
});
document.querySelectorAll("[data-add]").forEach((b) => b.addEventListener("click", () => {
  const t = b.dataset.add;
  MC.steps.push(t === "text" ? { t, s: "" } : t === "delay" ? { t, ms: 100 } : { t, kc: t === "tap" ? 4 : 0xe1 });
  render();
}));
$("mcName").addEventListener("input", () => { MC.name = $("mcName").value.replace(/[^\x20-\x7e]/g, "").slice(0, 12); });
$("mcUseTpl").addEventListener("click", () => {
  const [n, st] = MC_TEMPLATES[+$("mcTpl").value];
  MC.steps = JSON.parse(JSON.stringify(st)); if (!MC.name) MC.name = n;
  render();
});
$("mcCancel").addEventListener("click", () => { mcRecord(false); MC.edit = -1; render(); });
$("mcClear").addEventListener("click", () => { MC.steps = []; MC.name = ""; render(); });
$("mcSave").addEventListener("click", async () => {
  mcRecord(false);
  const all = S.macros.map((st, i) => (i === MC.edit ? MC.steps : st)), names = S.macroNames.map((n, i) => (i === MC.edit ? MC.name : n));
  try { await macrosWrite(all, names); toast(`Macro M${MC.edit} saved`); MC.edit = -1; }
  catch (e) { toast("Not saved: " + e.message, true); }
  render();
});

// Record: keys pressed on the keyboard while the page has focus (as the browser sees them).
const MC_CODES = (() => {
  const m = {};
  for (let i = 0; i < 26; i++) m["Key" + String.fromCharCode(65 + i)] = 4 + i;
  for (let i = 1; i <= 9; i++) m["Digit" + i] = 0x1d + i;
  m.Digit0 = 0x27;
  for (let i = 1; i <= 12; i++) m["F" + i] = 0x39 + i;
  Object.assign(m, { Enter: 0x28, Escape: 0x29, Backspace: 0x2a, Tab: 0x2b, Space: 0x2c, Minus: 0x2d, Equal: 0x2e, BracketLeft: 0x2f, BracketRight: 0x30,
    Backslash: 0x31, Semicolon: 0x33, Quote: 0x34, Backquote: 0x35, Comma: 0x36, Period: 0x37, Slash: 0x38, CapsLock: 0x39, PrintScreen: 0x46,
    ScrollLock: 0x47, Pause: 0x48, Insert: 0x49, Home: 0x4a, PageUp: 0x4b, Delete: 0x4c, End: 0x4d, PageDown: 0x4e, ArrowRight: 0x4f,
    ArrowLeft: 0x50, ArrowDown: 0x51, ArrowUp: 0x52, ContextMenu: 0x65, ControlLeft: 0xe0, ShiftLeft: 0xe1, AltLeft: 0xe2, MetaLeft: 0xe3,
    ControlRight: 0xe4, ShiftRight: 0xe5, AltRight: 0xe6, MetaRight: 0xe7 });
  return m;
})();
function mcKey(e) {
  if (!MC.rec || e.repeat) return;
  if (e.type === "keydown" && e.code === "Escape") { e.preventDefault(); e.stopImmediatePropagation(); mcRecord(false); render(); return; }
  const kc = MC_CODES[e.code];
  e.preventDefault(); e.stopImmediatePropagation();
  if (!kc) return;
  const t = e.type === "keydown" ? "down" : "up", last = MC.steps[MC.steps.length - 1];
  if (t === "up" && last && last.t === "down" && last.kc === kc) last.t = "tap"; // pressed and let go: a tap
  else if (t === "up" && !MC.steps.some((s) => s.t === "down" && s.kc === kc)) return; // let go of a key pressed before recording
  else MC.steps.push({ t, kc });
  render();
}
function mcRecord(on) {
  if (on === MC.rec) return;
  MC.rec = on;
  for (const ev of ["keydown", "keyup"]) (on ? window.addEventListener : window.removeEventListener).call(window, ev, mcKey, true);
  if (on) toast("Recording: type on the keyboard, Esc to stop");
}
$("mcRec").addEventListener("click", () => { mcRecord(!MC.rec); $("mcRec").blur(); render(); });

// ---- shift pairs: Vial's key overrides (0xFE 0x0D: 0 counts, 5 get, 6 set). An entry:
// trigger, replacement, layers (16-bit), trigger mods, negative mods, suppressed mods, options.
const KO = { count: 0, edit: -1, shiftMods: 0x22, opts: 0x87 }; // either Shift; enabled + Vial's default activations
async function koLoad() {
  if (S.mode !== "hid") return;
  const c = await cmd([0xfe, 0x0d, 0]);
  KO.count = c[0] === 0xff ? 0 : c[2];
  S.ko = [];
  for (let i = 0; i < KO.count; i++) {
    const d = await cmd([0xfe, 0x0d, 5, i]);
    S.ko.push(d[0] ? null : { trigger: d[1] | (d[2] << 8), replacement: d[3] | (d[4] << 8), layers: d[5] | (d[6] << 8), tmods: d[7], neg: d[8], sup: d[9], opt: d[10] });
  }
}
const koEmpty = (k) => !k || !k.trigger;
// Field by field (a backup file's keys can come in any order).
const koKey = (k) => (koEmpty(k) ? "" : [k.trigger, k.replacement, k.layers, k.tmods, k.neg, k.sup, k.opt].join(","));
const mcKeyOf = (steps) => JSON.stringify((steps || []).map((s) => [s.t, s.t === "text" ? s.s : s.t === "delay" ? s.ms : s.kc]));
const koIsShift = (k) => !koEmpty(k) && k.tmods && !(k.tmods & ~0x22) && !k.neg;
// The shift pair for keycode v on a layer (the diagram).
function koFor(v, layer) {
  return (S.ko || []).find((k) => koIsShift(k) && k.opt & 0x80 && k.trigger === v && k.layers & (1 << layer)) || null;
}
async function koWrite(i, k) {
  const e = k || { trigger: 0, replacement: 0, layers: 0, tmods: 0, neg: 0, sup: 0, opt: 0 };
  const d = await cmd([0xfe, 0x0d, 6, i, e.trigger & 0xff, e.trigger >> 8, e.replacement & 0xff, e.replacement >> 8, e.layers & 0xff, e.layers >> 8, e.tmods, e.neg, e.sup, e.opt]);
  if (d[0] !== 0) throw new Error(d[0] === 0xff ? "refused — confirm the connection again" : "the keyboard didn't take it");
}
function renderKo() {
  if (!S.ko) { setHtml($("koList"), "<div class='dim small'>Loading…</div>"); return; }
  if (!KO.count) { setHtml($("koList"), "<div class='dim small'>This firmware has no key overrides.</div>"); return; }
  const layerList = (m) => (m & 0x3f) === 0x3f ? "all layers" : "layers " + DATA.layerNames.map((n, i) => (m & (1 << i) ? n : null)).filter(Boolean).join(", ");
  const rows = S.ko.map((k, i) => {
    if (koEmpty(k)) return "";
    const what = koIsShift(k) ? `<b>${escapeHtml(kcName(k.trigger))}</b> with Shift → <b>${escapeHtml(kcName(k.replacement))}</b>`
      : `${escapeHtml(modsName((k.tmods | k.tmods >> 4) & 0xf) || "")}+${escapeHtml(kcName(k.trigger))} → ${escapeHtml(kcName(k.replacement))} <span class="dim">(made in Vial)</span>`;
    return `<div class="koRow"><span>${what} <span class="dim small">${layerList(k.layers)}${k.opt & 0x80 ? "" : ", off"}</span></span>` +
      (koIsShift(k) ? `<button data-koedit="${i}">Edit</button>` : "") + `<button data-kodel="${i}">Delete</button></div>`;
  }).join("");
  setHtml($("koList"), rows || "<div class='dim small'>None yet.</div>");
  const used = S.ko.filter((k) => !koEmpty(k)).length;
  $("koNote").textContent = `${used} of ${KO.count} used`;
  $("koSave").textContent = KO.edit >= 0 ? "Save shift pair" : "Add shift pair";
  $("koCancel").classList.toggle("hidden", KO.edit < 0);
}
function koFillEditor(k) {
  $("koTrig").innerHTML = mcKeyOptions(k ? k.trigger : 0x36, true);
  $("koOut").innerHTML = mcKeyOptions(k ? k.replacement : 0x33, false);
  const m = k ? k.layers : 0xffff;
  $("koLayers").innerHTML = DATA.layerNames.map((n, i) => `<label><input type="checkbox" value="${i}" ${m & (1 << i) ? "checked" : ""}> ${escapeHtml(n)}</label>`).join(" ");
}
koFillEditor(null);
$("koList").addEventListener("click", async (e) => {
  const d = e.target.dataset;
  if (d.koedit !== undefined) { KO.edit = +d.koedit; koFillEditor(S.ko[KO.edit]); render(); return; }
  if (d.kodel === undefined) return;
  try { await koWrite(+d.kodel, null); await koLoad(); toast("Shift pair deleted"); } catch (x) { toast("Not deleted: " + x.message, true); }
  render();
});
$("koCancel").addEventListener("click", () => { KO.edit = -1; koFillEditor(null); render(); });
$("koSave").addEventListener("click", async () => {
  const trigger = +$("koTrig").value, replacement = +$("koOut").value;
  let layers = [...$("koLayers").querySelectorAll("input:checked")].reduce((a, i) => a | (1 << +i.value), 0);
  if (!layers) { toast("Tick at least one layer", true); return; }
  if ((layers & 0x3f) === 0x3f) layers = 0xffff; // all (as Vial)
  if (trigger === replacement) { toast("The shifted output is the same key", true); return; }
  let i = KO.edit >= 0 ? KO.edit : S.ko.findIndex((k, j) => koIsShift(k) && k.trigger === trigger);
  if (i < 0) i = S.ko.findIndex(koEmpty);
  if (i < 0) { toast(`All ${KO.count} key overrides are used`, true); return; }
  try {
    await koWrite(i, { trigger, replacement, layers, tmods: KO.shiftMods, neg: 0, sup: KO.shiftMods, opt: KO.opts });
    await koLoad();
    toast(`${kcName(trigger)} with Shift → ${kcName(replacement)}`);
    KO.edit = -1; koFillEditor(null);
  } catch (x) { toast("Not saved: " + x.message, true); }
  render();
});
