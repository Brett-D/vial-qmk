// ================================================================ rendering
let renderQueued = false;
function scheduleRender() { if (!renderQueued) { renderQueued = true; requestAnimationFrame(render); } }
// Only touch the DOM when the markup changes: replacing elements 25x a second
// (live Z) would swallow clicks whose mousedown and mouseup hit different nodes.
const htmlCache = new Map();
function setHtml(el, html) { if (htmlCache.get(el) !== html) { htmlCache.set(el, html); el.innerHTML = html; } }

function render() {
  renderQueued = false;
  const hid = S.mode === "hid", hidAsk = S.mode === "hidAsk", bridge = S.mode === "bridge";
  document.body.classList.toggle("bridgeish", !hid && !hidAsk);
  const pill = $("status");
  if (hid) { pill.className = "pill ok"; pill.textContent = S.state === "loading" ? "Reading keyboard…" : NET ? "Connected (network)" : "Connected (USB)"; }
  else if (hidAsk) { pill.className = "pill wait"; pill.textContent = "Confirm on the keyboard"; }
  else {
    const st = S.state;
    pill.className = "pill " + (st === "ready" ? "ok" : st === "challenge" ? "wait" : "");
    pill.textContent = { idle: "Not connected", challenge: "Press the letter", ready: "Keyboard-only mode", closed: "Keyboard-only mode off" }[st] || "Not connected";
  }
  $("stats").textContent = bridge && S.frames ? `${S.frames} frames` + (S.missed ? ` · ${S.missed} missed — hold the left knob 1 s to resend` : "") : "";
  $("disconnectBtn").classList.toggle("hidden", !hid);
  const canBackup = hid && S.state === "ready" && !!S.ui;
  $("backupBtn").classList.toggle("hidden", !canBackup);
  $("restoreBtn").classList.toggle("hidden", !canBackup);
  if (!canBackup) $("restorePanel").classList.add("hidden");
  $("hidYes").classList.toggle("hidden", !navigator.hid && !NET);
  $("hidNo").classList.toggle("hidden", !!navigator.hid || NET);
  $("bridgeConnect").classList.toggle("hidden", NET);
  $("connectBtn").disabled = hidAsk;

  const ready = (hid && S.state === "ready" && S.ui) || (bridge && S.state === "ready" && S.ui);
  $("connect").classList.toggle("hidden", !!ready);
  $("connectSteps").classList.toggle("hidden", bridge && S.state === "challenge");
  $("challenge").classList.toggle("hidden", !(bridge && S.state === "challenge"));
  $("closed").classList.toggle("hidden", !(bridge && S.state === "closed"));
  $("mismatch").classList.toggle("hidden", !(bridge && S.ready && S.ready.hash !== DATA.pickerHash));
  if (bridge && S.state === "challenge") $("code").textContent = CHALLENGE[S.nonce % CHALLENGE.length];
  $("editor").classList.toggle("hidden", !ready);
  $("picker").classList.toggle("hidden", !(ready && S.ui.tab === 0));
  renderHelp();
  if (!ready) return;

  const ui = S.ui;
  renderNote();
  setHtml($("tabs"), tabList().map((t, i) => `<button type="button" class="tab ${i === ui.tab ? "on" : ""}" data-tab="${i}" ${S.mode === "hid" ? "" : "tabindex=-1"}>${t}</button>`).join(""));
  $("keysView").classList.toggle("hidden", ui.tab !== 0);
  $("ledView").classList.toggle("hidden", ui.tab !== 1);
  $("padView").classList.toggle("hidden", ui.tab !== 2);
  $("petView").classList.toggle("hidden", ui.tab !== 3);
  if (ui.tab === 0) { renderKeys(ui); renderPicker(ui); }
  if (ui.tab === 1) renderLeds(ui);
  if (ui.tab === 2) renderPad(ui);
  if (ui.tab === 3) renderPet();
  if (NAV.on) navPaint(false);
}

function renderNote() {
  const el = $("modeNote");
  if (S.mode === "bridge") {
    setHtml(el, `<div class="info"><b>Keyboard-only mode:</b> use the keyboard's knobs and keys (see “How to use”). Clicking does nothing in this mode${navigator.hid ? " — for mouse control, exit it and click Connect" : ""}.</div>`);
  } else if (S.mode === "hid") {
    let s = "";
    if (S.sessionLost) s += `<div class="note">The keyboard ended the session (idle, or unplugged). <button id="reconnectBtn">Reconnect</button></div>`;
    if (NAV.on) s += `<div class="info"><b>Right knob:</b> turn to move the orange highlight, press to select or change it (turn, then press again to finish), hold to switch tab.</div>`;
    if (S.bridgeOn) s += `<div class="note">Keyboard-only mode is also on, so the keyboard isn't typing. <button id="exitBridgeBtn">Turn it off</button></div>`;
    setHtml(el, s);
    const rc = $("reconnectBtn"); if (rc) rc.onclick = async () => { S.sessionLost = false; clearInterval(HID.poll); await requestSession(); };
    const eb = $("exitBridgeBtn"); if (eb) eb.onclick = async () => { await cmd([0xf6, 0x03]); pollStatus(); };
  } else setHtml(el, "");
}

function renderKeys(ui) {
  const hid = S.mode === "hid";
  setHtml($("layers"), DATA.layerNames.slice(0, S.ready.layers).map((n, i) =>
    `<button type="button" class="layer ${i === ui.layer ? "on" : ""}" data-layer="${i}" ${hid ? "" : "tabindex=-1"}>${i} ${n}${i === S.ready.defLayer ? " (default)" : ""}</button>`).join(""));
  const U = Math.min(52, Math.floor(($("kb").clientWidth || 860) / 16.6)), km = S.keymap[ui.layer];
  let html = "";
  for (const k of DATA.layout) {
    const isKnob = (k.r === KNOB_L[0] && k.c === KNOB_L[1]) || (k.r === KNOB_R[0] && k.c === KNOB_R[1]);
    const v = km[k.r][k.c];
    const selKey = !knobSel(ui) && k.r === ui.row && k.c === ui.col;
    const knobSlot = isKnob && knobSel(ui) && ui.slot % 3 === 2 && (ui.slot < 3) === (k.r === KNOB_L[0]);
    const cls = ["k", isKnob ? "knob" : "", v === 1 ? "trns" : "", v === 0 ? "none" : "", selKey || knobSlot ? "sel" : "", selKey && ui.held ? "held" : ""].join(" ");
    let text = escapeHtml(v === null ? "…" : keyText(v));
    if (!hid && (selKey || knobSlot) && ui.pending !== v) text += `<br><span class="new">→ ${escapeHtml(kcName(ui.pending))}</span>`;
    html += `<div class="${cls}" data-r="${k.r}" data-c="${k.c}" title="${hid ? "Click to change" : ""}" style="left:${k.x * U}px;top:${k.y * U}px;width:${U - 4}px;height:${k.h * U - 4}px">${text}</div>`;
  }
  setHtml($("kb"), html);
  $("kb").style.height = (5.9 * U) + "px";
  const enc = S.enc[ui.layer];
  setHtml($("knobs"), ["Left knob", "Right knob"].map((title, knob) => {
    const pressKc = km[knob ? KNOB_R[0] : KNOB_L[0]][knob ? KNOB_R[1] : KNOB_L[1]];
    const vals = enc ? [enc[knob][0], enc[knob][1], pressKc] : [null, null, pressKc];
    return `<div class="knobbox"><b>${title}</b>` + vals.map((v, i) => {
      const slot = knob * 3 + i, on = knobSel(ui) && ui.slot === slot;
      const pend = !hid && on && ui.pending !== v ? ` <span class="new" style="color:var(--warn)">→ ${escapeHtml(kcName(ui.pending))}</span>` : "";
      return `<div class="slot ${on ? "on" : ""}" data-slot="${slot}"><span>${SLOT_NAMES[i]}</span><span>${escapeHtml(kcName(v))}${pend}</span></div>`;
    }).join("") + "</div>";
  }).join(""));
}

function currentValue(ui) {
  if (!knobSel(ui)) return ui.row === 0xff ? null : S.keymap[ui.layer][ui.row][ui.col];
  const knob = ui.slot < 3 ? 0 : 1, i = ui.slot % 3;
  if (i === 2) return S.keymap[ui.layer][knob ? KNOB_R[0] : KNOB_L[0]][knob ? KNOB_R[1] : KNOB_L[1]];
  return S.enc[ui.layer] ? S.enc[ui.layer][knob][i] : null;
}

function renderPicker(ui) {
  const hid = S.mode === "hid";
  $("pickBridge").classList.toggle("hidden", hid);
  $("pickHid").classList.toggle("hidden", !hid);
  const cur = currentValue(ui);
  const target = knobSel(ui)
    ? `${ui.slot < 3 ? "Left" : "Right"} knob: ${SLOT_NAMES[ui.slot % 3].toLowerCase()} (layer ${ui.layer})`
    : (ui.row === 0xff ? null : `Key ${ui.row},${ui.col} on layer ${ui.layer}`);
  $("pickTitle").textContent = target || (hid ? "Click a key or a knob action" : "Press a key to select it");
  $("curName").textContent = cur === null ? "–" : kcName(cur) || "(none)";
  $("pickArrow").classList.toggle("hidden", hid);
  $("newName").classList.toggle("hidden", hid);
  if (hid) {
    $("pickHint").textContent = target ? "Click a keycode below to assign it (saved immediately)." : "";
    renderHidList();
    return;
  }
  $("newName").textContent = kcName(ui.pending) || "(none)";
  $("pickHint").textContent = ui.pending === cur ? "Turn the right knob to choose a keycode." : "Press the right knob to apply.";
  let cat = 0;
  DATA.catStarts.forEach((s, i) => { if (ui.pick >= s) cat = i; });
  $("cats").innerHTML = DATA.cats.map((c, i) => `<span class="cat ${ui.pick >= 0 && i === cat ? "on" : ""}">${c}</span>`).join("");
  const n = DATA.picker.length;
  let rows = "";
  if (ui.pick < 0) rows = `<div class="on">${escapeHtml(kcName(ui.pending))} (copied, not in the list)</div>`;
  else for (let d = -6; d <= 6; d++) {
    const i = (ui.pick + d + n) % n, [, label] = DATA.picker[i];
    rows += `<div class="${d === 0 ? "on" : ""}">${escapeHtml(label || "(none)")}</div>`;
  }
  $("list").innerHTML = rows;
}

let lastHidListKey = "";
function renderHidList() {
  const q = $("search").value.trim().toLowerCase();
  const cur = S.ui ? currentValue(S.ui) : null;
  const key = q + "|" + hidCat + "|" + cur;
  if (key === lastHidListKey) return;
  lastHidListKey = key;
  $("hidCats").innerHTML = [["All", -1]].concat(DATA.cats.map((c, i) => [c, i])).map(([c, i]) =>
    `<span class="cat ${i === hidCat ? "on" : ""}" data-cat="${i}">${c}</span>`).join("");
  let html = "";
  DATA.cats.forEach((cat, ci) => {
    if (hidCat >= 0 && ci !== hidCat) return;
    const end = DATA.catStarts[ci + 1] || DATA.picker.length;
    const items = DATA.picker.slice(DATA.catStarts[ci], end).filter(([v, label]) =>
      !q || (label || "").toLowerCase().includes(q) || kcName(v).toLowerCase().includes(q) || cat.toLowerCase().includes(q));
    if (!items.length) return;
    html += `<div class="hdr">${cat}</div>` + items.map(([v, label]) =>
      `<div class="item ${v === cur ? "cur" : ""}" data-kc="${v}"><span>${escapeHtml(label || "(none)")}</span><span class="dim small">${v === 0 ? "nothing" : v === 1 ? "lower layer" : ""}</span></div>`).join("");
  });
  $("hidList").innerHTML = html || "<div class='dim' style='padding:8px'>No match. Use “keycode value” below for anything else.</div>";
}
function scrollToCurrent() {
  const el = $("hidList").querySelector(".item.cur");
  if (el) el.scrollIntoView({ block: "nearest" });
}
function renderAdv() {
  const kind = $("advKind").value;
  $("advLayer").classList.toggle("hidden", kind !== "lt");
  $("advMods").classList.toggle("hidden", kind === "lt");
}

function setIfIdle(id, value) { const el = $(id); if (document.activeElement !== el && String(el.value) !== String(value)) el.value = value; }

function renderLeds(ui) {
  const l = S.led, hid = S.mode === "hid";
  $("ledUnsaved").classList.toggle("show", !!(l && l.dirty));
  $("ledRows").classList.toggle("hidden", hid);
  $("ledCtl").classList.toggle("hidden", !hid);
  if (!l) { $("ledRows").innerHTML = "<div class='dim'>Loading…</div>"; return; }
  if (hid) {
    $("ledOn").checked = !!l.enable;
    setIfIdle("ledBase", l.base); setIfIdle("fxEffect", l.fxEffect); setIfIdle("rxHue", l.rxHue); setIfIdle("rxSat", l.rxSat);
    for (const k of ["fxBlend", "fxColour", "fxSpeed", "fxWidth", "fxRadius", "fxFade", "fxEchoes", "fxDim"]) setIfIdle(k, l[k]);
    $("fxBrightSame").checked = !l.fxBright;
    if (l.fxBright) setIfIdle("fxBright", l.fxBright);
    const info = fxInfo(l.base), solo = !!(info.kind & FXK.SOLO), on = !!l.fxEffect && !solo;
    $("fxSec").classList.toggle("off", solo);
    $("fxSolo").classList.toggle("hidden", !solo);
    $("fxEffect").title = solo ? "This effect reacts to keys itself, so it's used on its own" : "";
    document.querySelectorAll("#fxSec .fxOn").forEach((r) => r.classList.toggle("hidden", !on));
    document.querySelectorAll(".rxRow").forEach((r) => r.classList.toggle("hidden", !on || l.fxColour !== 0));
    document.querySelectorAll("#fxSec .fxBrightRow").forEach((r) => r.classList.toggle("hidden", !on || !l.fxBright));
    $("fxBrightV").textContent = l.fxBright + "%";
    document.querySelectorAll("#fxSec .fxEchoRow").forEach((r) => r.classList.toggle("hidden", !on || l.fxEffect !== FX.E.WAVE));
    $("rxHueV").textContent = Math.round(l.rxHue / 256 * 360) + "°";
    $("rxSwatch").style.background = hsvToCss(l.rxHue, l.rxSat, 255);
    $("rxSatV").textContent = Math.round(l.rxSat / 255 * 100) + "%";
    const pct = (v, n) => Math.round(v / n * 100) + "%";
    $("fxSpeedV").textContent = Math.round((30 + l.fxSpeed * 15) / 14) + " keys/s";
    $("fxWidthV").textContent = pct(l.fxWidth, 15); $("fxRadiusV").textContent = Math.round((24 + l.fxRadius * 16) / 14) + " keys";
    $("fxFadeV").textContent = pct(l.fxFade, 15); $("fxEchoesV").textContent = l.fxEchoes + 2 + " rings"; $("fxDimV").textContent = l.fxDim ? "−" + pct(l.fxDim * 28, 255) : "no";
    // Soft warnings (led_fx_table.h: dark backgrounds, own colours); nothing is blocked.
    const warn = [];
    if (on && info.kind & FXK.DARK && l.fxBlend === 2) warn.push("Tint keeps the background's brightness: over a dark background the key-press effect is hard to see. Try Replace.");
    const mainHue = info.hue === 255 ? l.hue : info.hue;
    if (on && l.fxColour === 0 && l.rxSat > 120 && Math.abs(((l.rxHue - mainHue + 384) % 256) - 128) > 104) warn.push("The key-press colour is close to the background's: try \"Opposite of the background\" or another colour.");
    if (on && l.fxColour === 1 && info.kind & FXK.OWNHUE) warn.push("This background has its own colours, so \"Opposite\" follows the colour setting, not what you see: a fixed colour may stand out better.");
    $("fxWarn").textContent = warn.join(" ");
    $("fxWarn").classList.toggle("hidden", !warn.length);
    setIfIdle("ledHue", l.hue); setIfIdle("ledSat", l.sat); setIfIdle("ledVal", l.val);
    setIfIdle("ledSpeed", l.speed); setIfIdle("ledIdle", l.idle);
    $("ledHueV").textContent = Math.round(l.hue / 256 * 360) + "°";
    $("ledSwatch").style.background = hsvToCss(l.hue, l.sat, 255);
    $("ledSatV").textContent = Math.round(l.sat / 255 * 100) + "%";
    $("ledValV").textContent = Math.round(l.val / Math.max(l.max, 1) * 100) + "%";
    $("ledSpeedV").textContent = Math.round(l.speed / 255 * 100) + "%";
    $("mapState").textContent = S.mapMeasured ? "LED map: measured ✓" : "LED map: built-in (may not match your board)";
    setIfIdle("ledZone", l.zone); setIfIdle("ugHue", l.ugHue); setIfIdle("ugSat", l.ugSat);
    document.querySelectorAll(".ugRow").forEach((r) => r.classList.toggle("hidden", l.zone !== 1));
    $("ugHueV").textContent = Math.round(l.ugHue / 256 * 360) + "°";
    $("ugSwatch").style.background = hsvToCss(l.ugHue, l.ugSat, 255);
    $("ugSatV").textContent = Math.round(l.ugSat / 255 * 100) + "%";
    colourSync();
    return;
  }
  const effect = DATA.ledEffects[l.effect] || "Effect " + l.effect;
  const ug = `${Math.round(l.ugHue / 256 * 360)}°<span class="swatch" style="background:${hsvToCss(l.ugHue, l.ugSat, 255)}"></span>`;
  const rows = [
    ["Lights", l.enable ? "On" : "Off"],
    ["Effect", `${effect} <span class="dim small">(mode ${l.mode})</span>`],
    ["Hue", `${Math.round(l.hue / 256 * 360)}°<span class="swatch" style="background:${hsvToCss(l.hue, l.sat, 255)}"></span>`],
    ["Saturation", Math.round(l.sat / 255 * 100) + "%"],
    ["Brightness", `${Math.round(l.val / Math.max(l.max, 1) * 100)}% <span class="dim small">(${l.val}/${l.max})</span>`],
    ["Speed", Math.round(l.speed / 255 * 100) + "%"],
    ["LED zones", DATA.ledZones[l.zone] || l.zone],
    ["Underglow colour", l.zone === 1 ? ug : `<span class="dim">${ug} (used by “solid underglow”)</span>`],
    ["Underglow saturation", Math.round(l.ugSat / 255 * 100) + "%"],
    ["Turn off after no use", DATA.ledIdle[l.idle] || l.idle],
  ];
  setHtml($("ledRows"), rows.map(([k, v], i) => `<div class="row ${i === ui.ledField ? "on" : ""}"><span>${k}</span><span>${v}</span></div>`).join(""));
}

function renderPad(ui) {
  const t = S.tp, hid = S.mode === "hid";
  $("tpUnsaved").classList.toggle("show", !!(t && t.dirty));
  $("tpRows").classList.toggle("hidden", hid);
  $("tpCtl").classList.toggle("hidden", !hid);
  $("wgCtl").classList.toggle("hidden", !hid);
  if (!t) { $("tpRows").innerHTML = "<div class='dim'>Loading…</div>"; return; }
  if (hid) {
    $("tpOn").checked = !!t.enable;
    if (S.wiggle) { $("wgOn").checked = S.wiggle.on; setIfIdle("wgEvery", S.wiggle.every); }
    setIfIdle("tpThr", t.threshold); setIfIdle("tpRel", t.release);
    $("tpThrV").textContent = t.threshold; $("tpRelV").textContent = t.release;
  } else {
    const rows = [["Press-to-click", t.enable ? "On" : "Off"], ["Threshold (press)", t.threshold], ["Release Z", t.release]];
    setHtml($("tpRows"), rows.map(([k, v], i) => `<div class="row ${i === ui.tpField ? "on" : ""}"><span>${k}</span><span>${v}</span></div>`).join(""));
  }
  drawZ(t);
  setHtml($("peaks"), S.peaks.map((p) => `<span class="${p >= t.threshold ? "click" : ""}">${p}</span>`).join("") || "<span class='dim'>touch the trackpad…</span>");
}

function drawZ(t) {
  const z = S.tpz || { z: 0, peak: 0, lastPeak: 0, touching: 0, pressed: 0 };
  const c = $("zbar"), g = c.getContext("2d"), W = c.width, H = c.height, x = (v) => 10 + (W - 20) * v / 63;
  g.clearRect(0, 0, W, H);
  g.fillStyle = "#2a2f3a"; g.fillRect(10, 20, W - 20, 26);
  g.fillStyle = z.pressed ? "#3fcf7f" : "#4fa3ff"; g.fillRect(10, 20, x(z.touching ? z.z : 0) - 10, 26);
  const line = (v, color, label) => { g.fillStyle = color; g.fillRect(x(v) - 1, 12, 3, 42); g.font = "11px system-ui"; g.fillText(label + " " + v, Math.min(x(v) + 4, W - 70), 64); };
  line(t.release, "#4fa3ff", "release"); line(t.threshold, "#ffb648", "threshold");
  if (z.peak) { g.fillStyle = "#e6e8ec"; g.fillRect(x(z.peak) - 1, 16, 2, 34); }
  g.fillStyle = "#e6e8ec"; g.font = "12px system-ui";
  g.fillText(`Z ${z.touching ? z.z : "–"}   peak ${z.peak}   last ${z.lastPeak}   ${z.pressed ? "CLICK HELD" : ""}`, 10, 13);
  const h = $("zhist"), hg = h.getContext("2d"), HW = h.width, HH = h.height, y = (v) => HH - 6 - (HH - 12) * v / 63;
  hg.clearRect(0, 0, HW, HH);
  hg.strokeStyle = "#ffb648"; hg.beginPath(); hg.moveTo(0, y(t.threshold)); hg.lineTo(HW, y(t.threshold)); hg.stroke();
  hg.strokeStyle = "#4fa3ff"; hg.beginPath(); hg.moveTo(0, y(t.release)); hg.lineTo(HW, y(t.release)); hg.stroke();
  hg.strokeStyle = "#e6e8ec"; hg.beginPath();
  S.zHist.forEach((v, i) => { const px = HW - (S.zHist.length - i) * (HW / 150); i ? hg.lineTo(px, y(v)) : hg.moveTo(px, y(v)); });
  hg.stroke();
}

function renderHelp() {
  let items, foot = "";
  if (S.mode === "hid") {
    items = [["Layout", "Click a key, or a knob action (turn left / turn right / press), then click a keycode in the list. It's saved right away."],
             ["Layers", "Click a layer name above the keyboard."], ["Hold/tap keys", "Use the box under the keycode list."],
             ["LEDs", "Drag the sliders; the lights preview live. Click Save to keep them. Effects light the wrong keys? Click Calibrate LED positions and press the key on top of each lit LED."],
             ["Trackpad", "Touch the pad to see Z live. Click Save to keep settings."],
        ["Pet", "Feed, play, put to bed and clean the pet on the right OLED; name it and choose how fast it needs care."],
             ["Right knob", "While this page has focus: turn to move the highlight, press to select or change, hold to switch tab."]];
    foot = "The keyboard keeps typing normally while connected. Disconnect (or close the page) to end the session.";
  } else if (S.mode === "bridge" && S.state === "ready" && S.ui) {
    const per = {
      0: [["Any key", "Select it"], ["Hold a key + tap another", "Copy the other key's default-layer keycode"],
          ["Left knob turn", "Change layer"], ["Hold left knob + turn", "Select a knob action (turn left / turn right / press)"],
          ["Hold left knob + tap a key", "Copy that key's keycode to the knob action"],
          ["Right knob turn", "Choose keycode (turn fast to skip)"],
          ["Hold right knob + turn", "Jump between categories"], ["Right knob press", "Apply to the selection"]],
      1: [["Left knob turn", "Choose setting"], ["Right knob turn", "Change it (the lights preview live)"],
          ["Hold right knob + turn", "On a hue or saturation: colour presets (red, orange, yellow, green, cyan, blue, purple, pink, warm white, white)"], ["Right knob press", "Save"]],
      2: [["Left knob turn", "Choose setting"], ["Right knob turn", "Change it"], ["Right knob press", "Save"], ["Touch the trackpad", "See Z live"]],
    };
    items = per[S.ui.tab].concat([["Left knob press", "Next tab (hold ~1 s: resend everything)"], ["Hold right knob ~0.6 s", "Next tab"]]);
    foot = "Exit: Raise + Config, hold both knob buttons 2 s, hold Esc 2 s, or 5 min without input. Unsaved LED/trackpad changes are dropped on exit.";
  } else {
    items = navigator.hid || NET
      ? [["With the mouse", "Click Connect, then press the right knob on the keyboard to confirm."], ["Without WebHID", "Use keyboard-only mode."]]
      : [["Keyboard-only mode", "Follow the steps on the left."]];
  }
  setHtml($("help"), items.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join(""));
  $("helpFoot").textContent = foot;
}

let toastTimer;
function toast(msg, bad) {
  const t = $("toast"); t.textContent = msg; t.className = "show" + (bad ? " bad" : "");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => (t.className = ""), bad ? 4000 : 2000);
}
function escapeHtml(s) { return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const logLines = [];
function log(s) { logLines.unshift(new Date().toLocaleTimeString() + "  " + s); logLines.length = Math.min(logLines.length, 200); $("log").textContent = logLines.join("\n"); }

