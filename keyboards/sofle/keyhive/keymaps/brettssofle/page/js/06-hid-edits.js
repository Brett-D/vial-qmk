// ---------------------------------------------------------------- HID edits
async function setKey(layer, row, col, kc) {
  const d = await cmd([0x05, layer, row, col, kc >> 8, kc & 0xff]);
  if (d[0] === 0xff) throw new Error("refused — confirm the connection on the keyboard again");
  S.keymap[layer][row][col] = kc;
}
async function setEnc(layer, knob, cw, kc) {
  const d = await cmd([0xfe, 0x04, layer, knob, cw ? 1 : 0, kc >> 8, kc & 0xff]);
  if (d[0] === 0xff) throw new Error("refused — confirm the connection on the keyboard again");
  S.enc[layer][knob][cw ? 1 : 0] = kc;
}
function targetLabel(ui) {
  if (knobSel(ui)) return `${ui.slot < 3 ? "Left" : "Right"} knob: ${SLOT_NAMES[ui.slot % 3].toLowerCase()}`;
  return ui.row === 0xff ? null : `Key ${ui.row},${ui.col}`;
}
async function assign(kc) {
  const ui = S.ui;
  if (!ui || targetLabel(ui) === null) { toast("Click a key or a knob action first", true); return; }
  try {
    if (knobSel(ui)) {
      const knob = ui.slot < 3 ? 0 : 1, i = ui.slot % 3;
      if (i === 2) { const [r, c] = knob ? KNOB_R : KNOB_L; await setKey(ui.layer, r, c, kc); }
      else await setEnc(ui.layer, knob, i === 1, kc);
    } else {
      await setKey(ui.layer, ui.row, ui.col, kc);
    }
    toast(`${targetLabel(ui)} on layer ${ui.layer} = ${kcName(kc) || "(none)"}`);
  } catch (e) { toast("Not saved: " + e.message, true); }
  render();
}

// LEDs: live preview through the VIA lighting commands, saved with Save.
// One flush at a time; Save / restore await the running one (it also sends
// changes made meanwhile). While Undo reloads the saved state, nothing is sent.
let ledFlushP = null, ledHold = false;
function flushLed() {
  if (ledHold) return Promise.resolve(true);
  if (!ledFlushP) ledFlushP = ledFlushLoop().finally(() => { ledFlushP = null; });
  return ledFlushP;
}
async function ledFlushLoop() {
  const send = async (bytes) => { const d = await cmd(bytes); if (d[0] === 0xff) throw new Error("refused — reconnect"); };
  try {
    for (;;) {
      if (ledHold) break; // Undo: stop at the next step
      // Snapshot first: sliders keep changing S.led while a command is in flight.
      const l = { ...S.led }, s = S.ledSent;
      if (l.enable && s.enable !== 1) { await send([0xf6, 0x05, 1]); s.enable = 1; continue; }
      if (FX_FIELDS.some((k) => l[k] !== s[k])) {
        // Background + key-press effect and its settings (0xF6 0x1C 1); [15] = 0: not allowed together.
        const d = await cmd(fxBytes(l));
        if (d[0] === 0xff) throw new Error("refused — reconnect");
        for (const k of FX_FIELDS) s[k] = l[k];
        if (!d[15]) toast("The keyboard didn't take that combination", true);
        continue;
      }
      if (l.ssOk && SS_FIELDS.some((k) => l[k] !== s[k])) {
        const d = await cmd(ssBytes(l)); // v27 screensaver (preview; saved with the lighting)
        if (d[0] === 0xff) throw new Error("refused — reconnect");
        for (const k of SS_FIELDS) s[k] = l[k];
        if (!d[10]) toast("The keyboard didn't take the screensaver settings", true);
        continue;
      }
      if (["hue", "sat", "val", "speed"].some((k) => l[k] !== s[k])) {
        await send([0xf6, 0x15, l.speed, l.hue, l.sat, l.val]);
        Object.assign(s, { hue: l.hue, sat: l.sat, val: l.val, speed: l.speed });
        continue;
      }
      if (!l.enable && s.enable !== 0) { await send([0xf6, 0x05, 0]); s.enable = 0; continue; }
      if (l.zone !== s.zone || l.ugHue !== s.ugHue || l.ugSat !== s.ugSat) {
        await send([0xf6, 0x06, l.zone, l.ugHue, l.ugSat]);
        Object.assign(s, { zone: l.zone, ugHue: l.ugHue, ugSat: l.ugSat });
        continue;
      }
      break;
    }
    return true;
  } catch (e) { toast("Lighting change failed: " + e.message, true); return false; }
}
function ledInput() {
  const l = S.led;
  l.enable = $("ledOn").checked ? 1 : 0;
  l.base = +$("ledBase").value; l.fxEffect = +$("fxEffect").value;
  if (fxInfo(l.base).kind & FXK.SOLO) l.fxEffect = 0; // reacts to keys itself: used on its own
  if (!l.base && !l.fxEffect) l.enable = 0;          // nothing chosen = lights off
  l.rxHue = +$("rxHue").value; l.rxSat = +$("rxSat").value;
  for (const k of ["fxBlend", "fxColour", "fxSpeed", "fxWidth", "fxRadius", "fxFade", "fxEchoes", "fxDim"]) l[k] = +$(k).value;
  l.fxBright = $("fxBrightSame").checked ? 0 : +$("fxBright").value;
  l.hue = +$("ledHue").value; l.sat = +$("ledSat").value;
  l.val = +$("ledVal").value; l.speed = +$("ledSpeed").value; l.idle = +$("ledIdle").value; l.curve = +$("ledCurve").value;
  l.zone = +$("ledZone").value; l.ugHue = +$("ugHue").value; l.ugSat = +$("ugSat").value;
  if (l.fxMax) l.fxMax = +$("fxMax").value;
  if (l.ssOk) {
    for (const k of ["ssAfter", "ssEffect", "ssHue", "ssSat", "ssVal", "ssSpeed"]) l[k] = +$(k).value;
    l.ssPet = $("ssPet").checked;
  }
  l.dirty = 1;
  flushLed();
  render();
}
async function ledSave() {
  try {
    if (!(await flushLed())) throw new Error("the lighting changes didn't reach the keyboard");
    const d = await cmd([0x09]);
    if (d[0] === 0xff) throw new Error("refused");
    await cmd([0xf6, 0x01, S.led.idle]);
    await cmd([0xf6, 0x07]);
    await cmd([0xf6, 0x1c, 2]);
    await hidLoadLed();
    toast("Lighting saved");
  } catch (e) { toast("Not saved: " + e.message, true); }
  render();
}
async function ledRevert() {
  if (!S.led || !S.led.dirty) return;
  ledHold = true; // no preview writes until the saved state is back
  try {
    if (ledFlushP) await ledFlushP; // let the command in flight finish first
    await cmd([0xf6, 0x04]);
    await hidLoadLed();
  } catch (e) { log("revert: " + e.message); }
  ledHold = false;
  render();
}
function buildLedSelects() {
  const name = (id) => DATA.ledEffects[id] || "Effect " + id, T = DATA.fxTable;
  const base = $("ledBase"), fx = $("fxEffect");
  const group = (label, items) => { const g = document.createElement("optgroup"); g.label = label; items.forEach((o) => g.appendChild(o)); if (items.length) base.appendChild(g); };
  const opt = (label, id, tip) => { const o = new Option(label, id); if (tip) o.title = tip; return o; };
  base.innerHTML = ""; fx.innerHTML = "";
  base.add(new Option("Off (dark)", 0));
  const qmkBg = [], solo = [];
  for (const id of S.effects) {
    if (!id || id === DATA.ledLayeredId || id === DATA.ledDirectId || id >= DATA.fxTable.ownBg || DATA.ledReact.includes(id)) continue; // key-press effects: "On key press" now
    const k = fxInfo(id).kind;
    if (k & FXK.SOLO) solo.push(opt(name(id), id, "Reacts to keys itself, so it's used on its own"));
    else qmkBg.push(opt(name(id), id, k & FXK.DARK ? "Dark / sparse: pick a key-press colour that stands out" : ""));
  }
  group("Backgrounds", qmkBg);
  group("More backgrounds", T.backgrounds.map(([i, , label, k]) => opt(label, T.ownBg + i, k & FXK.DARK ? "Dark / sparse: pick a key-press colour that stands out" : "")));
  T.solos.forEach(([i, , label]) => solo.push(opt(label, T.ownSolo + i, "Reacts to keys itself, so it's used on its own")));
  group("On their own (react to keys themselves)", solo);
  fx.add(new Option("None", 0));
  T.overlays.forEach(([i, , label]) => fx.add(new Option(label, i)));
  $("ledZone").innerHTML = DATA.ledZones.map((t, i) => `<option value="${i}">${t}</option>`).join("");
  $("ledIdle").innerHTML = DATA.ledIdle.map((t, i) => `<option value="${i}">${t}</option>`).join("");
  $("ledVal").max = S.ledInfo.max;
  // v27 screensaver: our own backgrounds, and the level tables (kb_config.c).
  $("ssAfter").innerHTML = SS.after.map((m, i) => `<option value="${i}">${m ? m + (m > 1 ? " minutes" : " minute") : "Off"}</option>`).join("");
  $("ssEffect").innerHTML = T.backgrounds.map(([i, , label]) => `<option value="${i}">${label}</option>`).join("");
  $("ssSat").innerHTML = SS.sat.map((v, i) => [v, i]).sort((a, b) => b[0] - a[0]).map(([v, i]) => `<option value="${i}">${Math.round(v / 255 * 100)}%</option>`).join("");
  $("ssVal").innerHTML = SS.val.map((v, i) => [v, i]).sort((a, b) => a[0] - b[0]).map(([v, i]) => `<option value="${i}">${v}%${i ? "" : " (default)"}</option>`).join("");
  $("ssSpeed").innerHTML = SS.speed.map((v, i) => [v, i]).sort((a, b) => a[0] - b[0]).map(([v, i]) => `<option value="${i}">${["Slow", "Normal", "Fast", "Fastest"][[64, 128, 192, 255].indexOf(v)]}</option>`).join("");
}
["ledOn", "ledBase", "fxEffect", "rxHue", "rxSat", "ledHue", "ledSat", "ledVal", "ledCurve", "ledSpeed", "ledIdle", "ledZone", "ugHue", "ugSat",
 "fxBlend", "fxColour", "fxSpeed", "fxWidth", "fxRadius", "fxFade", "fxEchoes", "fxDim", "fxBright", "fxBrightSame", "fxMax",
 "ssAfter", "ssEffect", "ssHue", "ssSat", "ssVal", "ssSpeed", "ssPet"].forEach((id) => $(id).addEventListener("input", ledInput));
// v27: the screensaver now, here (the preview fades in and out as on the keyboard) and on the keyboard.
$("ssPreview").addEventListener("click", async () => {
  try {
    if (!(await flushLed())) return;
    const d = await cmd([0xf6, 0x20, 2]);
    if (d[0] === 0xff || !d[10]) throw new Error("refused");
    PV.ss = { born: performance.now() };
  } catch (e) { toast("Preview failed: " + e.message, true); }
});
$("fxBrightSame").addEventListener("change", () => { if (!$("fxBrightSame").checked && S.led && !S.led.fxBright) $("fxBright").value = Math.max(1, Math.round(S.led.val / Math.max(S.led.max, 1) * 100)); ledInput(); });
// Mouse wiggler (Trackpad tab): applies at once, not saved.
async function wgInput() {
  try { readStatus(await cmd([0xf6, 0x16, $("wgOn").checked ? 1 : 0, +$("wgEvery").value])); toast($("wgOn").checked ? "Mouse wiggler on" : "Mouse wiggler off"); }
  catch (e) { toast("Failed: " + e.message, true); }
  render();
}
["wgOn", "wgEvery"].forEach((id) => $(id).addEventListener("input", wgInput));
// v26 keyboard preferences (0xF6 0x1E): Enter after the typed network address.
// v27: [5]-[7] the right OLED: brightness 1-5, sleep index, off ([8] = 0 on older firmware: no display settings).
const prefsOf = (d) => ({ addrEnter: !!(d[3] & 1), disp: d[8] ? { bright: d[5], sleep: d[6], off: !!d[7] } : null });
async function prefsLoad() {
  try { const d = await cmd([0xf6, 0x1e, 0]); S.prefs = d[0] === 0xf6 && d[2] === 0xa1 ? prefsOf(d) : null; } catch (e) { S.prefs = null; }
}
async function prefsSet(p) {
  const q = p.disp || null;
  const d = await cmd([0xf6, 0x1e, 1, p.addrEnter ? 1 : 0, 0, ...(q ? [0x80 | q.bright, 0x80 | q.sleep, 0x80 | (q.off ? 1 : 0)] : [])]);
  if (d[2] !== 0xa1 || d[4] !== 1) throw new Error("refused");
  S.prefs = prefsOf(d);
}
async function dispInput() {
  if (!S.prefs || !S.prefs.disp) return;
  const disp = { bright: +$("dispBright").value, sleep: +$("dispSleep").value, off: $("dispOff").checked };
  try { await prefsSet({ addrEnter: S.prefs.addrEnter, disp }); toast(disp.off ? "Screen off (a knob press wakes it)" : "Display saved"); }
  catch (e) { toast("Failed: " + e.message, true); }
  render();
}
["dispBright", "dispSleep", "dispOff"].forEach((id) => $(id).addEventListener(id === "dispBright" ? "change" : "input", dispInput));
$("netEnter").addEventListener("input", async () => {
  try { await prefsSet({ addrEnter: $("netEnter").checked }); toast($("netEnter").checked ? "Enter after the address: on" : "Enter after the address: off"); }
  catch (e) { toast("Failed: " + e.message, true); }
  render();
});
$("ledSave").addEventListener("click", ledSave);
$("ledRevert").addEventListener("click", ledRevert);

// v27 loop profile (0xF6 0x1F, perf.h): both halves, every second while measuring.
const PERF_NAMES = ["Scan + split sync", "Keys, knobs, trackpad / OLED I2C", "LED background", "LED flush (key effects, pet, screensaver)", "OLED drawing",
  "LED re-sends (dither)", "Pet", "Sync to the right half", "Config / network", "EEPROM writes"];
const PERF = { timer: null, last: null };
function perfParse(d) {
  if (d[0] !== 0xf6 || d[2] !== 0xa2 || !(d[3] & 2)) return null;
  const r = d.slice(4), u16 = (i) => r[i] | (r[i + 1] << 8), ms = (v) => v / 100;
  return { loop: ms(u16(0)), worst: ms(u16(2)), worstSec: r[4], spikes: r[5], avg: r[6] * 0.02, sec: PERF_NAMES.map((_, i) => ms(u16(7 + 2 * i))) };
}
async function perfPoll() {
  if (S.mode !== "hid") { perfStop(); return; }
  try {
    const L = perfParse(await cmd([0xf6, 0x1f, 0])), R = perfParse(await cmd([0xf6, 0x1f, 1]));
    PERF.last = { L, R, at: new Date() };
  } catch (e) { perfStop(); toast("Measuring stopped: " + e.message, true); }
  renderPerf();
}
function perfStop() { clearInterval(PERF.timer); PERF.timer = null; renderPerf(); }
function perfRows() {
  const p = PERF.last;
  if (!p) return [];
  const f = (h, fn) => (h ? fn(h) : "—"), ms = (v) => v.toFixed(2) + " ms";
  const rows = [["Longest pass, last 10 s", f(p.L, (h) => ms(h.loop)), f(p.R, (h) => ms(h.loop))],
    ["Average pass", f(p.L, (h) => ms(h.avg)), f(p.R, (h) => ms(h.avg))],
    ["Worst since reset", f(p.L, (h) => `${ms(h.worst)} (${PERF_NAMES[h.worstSec] || "?"})`), f(p.R, (h) => `${ms(h.worst)} (${PERF_NAMES[h.worstSec] || "?"})`)],
    ["Passes over 10 ms", f(p.L, (h) => String(h.spikes)), f(p.R, (h) => String(h.spikes))]];
  PERF_NAMES.forEach((n, i) => rows.push(["  " + n, f(p.L, (h) => ms(h.sec[i])), f(p.R, (h) => ms(h.sec[i]))]));
  if (p.L) rows.push(["Longest a key can wait (≈)", ms(p.L.loop + 5), p.R ? ms(p.R.loop + p.L.loop + 5) : "—"]);
  return rows;
}
function renderPerf() {
  $("perfRun").textContent = PERF.timer ? "Stop" : "Measure";
  const rows = perfRows();
  $("perfNote").textContent = PERF.last ? (PERF.last.R ? "" : "(no numbers from the right half: flash it with v27 too)") : "";
  setHtml($("perfOut"), rows.length ? `<table class="perf"><tr><th></th><th>Left</th><th>Right</th></tr>${rows.map(([k, a, b]) => `<tr><td>${escapeHtml(k)}</td><td>${a}</td><td>${b}</td></tr>`).join("")}</table>` : "");
}
$("perfRun").addEventListener("click", () => { if (PERF.timer) perfStop(); else { PERF.timer = setInterval(perfPoll, 1000); perfPoll(); } });
$("perfReset").addEventListener("click", async () => { try { await cmd([0xf6, 0x1f, 2]); } catch (e) { /* gone */ } perfPoll(); });
$("perfCopy").addEventListener("click", () => {
  const t = `Sofle responsiveness (${DATA.buildId || "v27+"}), ${PERF.last ? PERF.last.at.toISOString() : "no data"}\n` + perfRows().map(([k, a, b]) => `${k}: L ${a}, R ${b}`).join("\n");
  if (navigator.clipboard) navigator.clipboard.writeText(t).then(() => toast("Copied"));
});
