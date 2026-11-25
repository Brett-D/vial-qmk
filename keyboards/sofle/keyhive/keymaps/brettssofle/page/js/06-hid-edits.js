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
}
["ledOn", "ledBase", "fxEffect", "rxHue", "rxSat", "ledHue", "ledSat", "ledVal", "ledCurve", "ledSpeed", "ledIdle", "ledZone", "ugHue", "ugSat",
 "fxBlend", "fxColour", "fxSpeed", "fxWidth", "fxRadius", "fxFade", "fxEchoes", "fxDim", "fxBright", "fxBrightSame"].forEach((id) => $(id).addEventListener("input", ledInput));
$("fxBrightSame").addEventListener("change", () => { if (!$("fxBrightSame").checked && S.led && !S.led.fxBright) $("fxBright").value = Math.max(1, Math.round(S.led.val / Math.max(S.led.max, 1) * 100)); ledInput(); });
// Mouse wiggler (Trackpad tab): applies at once, not saved.
async function wgInput() {
  try { readStatus(await cmd([0xf6, 0x16, $("wgOn").checked ? 1 : 0, +$("wgEvery").value])); toast($("wgOn").checked ? "Mouse wiggler on" : "Mouse wiggler off"); }
  catch (e) { toast("Failed: " + e.message, true); }
  render();
}
["wgOn", "wgEvery"].forEach((id) => $(id).addEventListener("input", wgInput));
$("ledSave").addEventListener("click", ledSave);
$("ledRevert").addEventListener("click", ledRevert);

