// ================================================================ WebHID mode
const HID = { dev: null, queue: Promise.resolve(), waiter: null, poll: null, zpoll: null, busy: false };
const isRaw = (d) => d.vendorId === DATA.usb.vid && d.productId === DATA.usb.pid && d.collections.some((c) => c.usagePage === 0xff60 && c.usage === 0x61);
const isLinux = /Linux/.test(navigator.userAgent) && !/Android/.test(navigator.userAgent);

function onReport(e) {
  if (!HID.waiter) return;
  const w = HID.waiter; HID.waiter = null;
  w.res(new Uint8Array(e.data.buffer, e.data.byteOffset, e.data.byteLength));
}
// One request at a time; the reply is the next input report.
function cmd(bytes) {
  const run = () => new Promise((res, rej) => {
    if (!HID.dev || !HID.dev.opened) return rej(new Error("not connected"));
    const buf = new Uint8Array(32); buf.set(bytes);
    const timer = setTimeout(() => { if (HID.waiter && HID.waiter.res === res) { HID.waiter = null; rej(new Error("timeout")); } }, HID.dev.net ? 5000 : 1000);
    HID.waiter = { res: (d) => { clearTimeout(timer); res(d); } };
    HID.dev.sendReport(0, buf).catch((e) => { clearTimeout(timer); HID.waiter = null; rej(e); });
  });
  const r = HID.queue.then(run, run);
  HID.queue = r.catch(() => {});
  return r;
}

function showHidErr(html) { const el = $("hidErr"); el.innerHTML = html; el.classList.toggle("hidden", !html); }
function showHidWait(html) { const el = $("hidWait"); el.innerHTML = html; el.classList.toggle("hidden", !html); }

const UDEV = `KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="${DATA.usb.vid.toString(16).padStart(4, "0")}", ATTRS{idProduct}=="${DATA.usb.pid.toString(16).padStart(4, "0")}", MODE="0660", TAG+="uaccess"`;
function openHelp() {
  let s = "<b>Couldn't open the keyboard.</b>\nIt's probably in use: close Vial / VIA (app or browser tab) and any other page connected to it, then click Connect again.";
  if (isLinux) s += `\n\n<b>On Linux</b> the browser also needs permission for the keyboard's hidraw device. Once, run:<pre>echo '${UDEV}' | sudo tee /etc/udev/rules.d/59-sofle.rules\nsudo udevadm control --reload-rules && sudo udevadm trigger</pre>then unplug and replug the keyboard. (Or use keyboard-only mode below, which needs no setup.)`;
  return s;
}

async function connect() {
  showHidErr(""); showHidWait("");
  let dev;
  if (NET) {
    if (!(dev = await netConnect())) return;
  } else try {
    dev = (await navigator.hid.getDevices()).find(isRaw);
    if (!dev) {
      const picked = await navigator.hid.requestDevice({ filters: [{ vendorId: DATA.usb.vid, productId: DATA.usb.pid, usagePage: 0xff60, usage: 0x61 }] });
      dev = picked.find(isRaw) || picked[0];
    }
  } catch (e) {
    showHidErr(e.name === "SecurityError" || e.name === "NotAllowedError"
      ? "<b>The browser blocked access to USB devices</b> (permission denied or blocked by policy). Use keyboard-only mode below."
      : "Couldn't ask for the keyboard: " + escapeHtml(e.message));
    return;
  }
  if (!dev) { showHidErr("No keyboard was chosen. Click <b>Connect</b> and pick <b>Sofle</b> in the list (if it isn't listed, the cable or firmware may be the problem)."); return; }
  try { if (!dev.opened) await dev.open(); } catch (e) { log("open: " + e.name + " " + e.message); showHidErr(openHelp()); return; }
  HID.dev = dev;
  dev.addEventListener("inputreport", onReport);
  let st;
  try { st = await cmd([0xf6, 0x00, 0]); } catch (e) {
    showHidErr(NET ? "The keyboard didn't answer over the network. Reload the page to try again." : "The keyboard didn't answer. Close Vial/VIA and other tabs using it, then try again.");
    await closeHid(); return;
  }
  if (st[0] !== 0xf6) { showHidErr("This keyboard's firmware doesn't support web config yet. Flash the new firmware (sofle_brettssofle_config-v2.uf2 or later)."); await closeHid(); return; }
  await requestSession();
}

async function requestSession() {
  S.mode = "hidAsk"; render();
  let st = await cmd([0xf6, 0x10]);
  while (st[3] === HS.ASK) {
    showHidWait(`<b>Confirm on the keyboard.</b> Its screen shows <b>CONF?</b> — press the <b>right knob</b> (or <kbd>Enter</kbd>). Press the left knob or <kbd>Esc</kbd> to refuse. (${st[10]} s)`);
    await sleep(300);
    try { st = await cmd([0xf6, 0x00, 0]); } catch (e) { break; }
  }
  showHidWait("");
  if (st[3] === HS.ACTIVE) { await startHidSession(st); return; }
  const why = { [HS.DENIED]: "<b>Refused on the keyboard.</b> Click Connect to ask again (wait a few seconds after a refusal).",
                [HS.TIMEOUT]: "<b>Not confirmed within 30 seconds.</b> Click Connect and press the right knob when the screen shows CONF?." }[st[3]]
              || "The keyboard didn't confirm. Click Connect to try again.";
  showHidErr(why);
  await closeHid();
}

// Crash record / stack use / why network mode ended (0xF6 0x17, diag.c).
async function readDiag() {
  let d;
  try { d = await cmd([0xf6, 0x17]); } catch (e) { return; }
  if (d[0] !== 0xf6 || d[2] !== 0xd1) return; // older firmware
  const u32 = (i) => (d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | (d[i + 3] << 24)) >>> 0, u16 = (i) => d[i] | (d[i + 1] << 8);
  const hex = (v) => "0x" + v.toString(16).padStart(8, "0");
  const crumbs = ["main loop", "network: receiving", "network: timers", "network: sending", "network: HTTP request", "network: page command", "network: start/stop"];
  const boots = ["power on", "reset button", "crash", "watchdog", "restart"];
  const exits = ["", "the Config key / both knobs", "10 min without traffic", "P (type the page instead)"];
  log(`keyboard: last start ${boots[d[24]] || d[24]}; stack free: main loop ${u16(18)} B, network ${u16(20)} B, interrupts ${u16(22)} B` +
      (d[25] ? `; network mode last ended by ${exits[d[25]] || d[25]}, ${u16(26)} s ago` : ""));
  if (!d[3]) { $("diagBanner").classList.add("hidden"); return; }
  $("diagText").textContent = `The keyboard restarted after a crash (HardFault at pc ${hex(u32(6))}, lr ${hex(u32(10))}, sp ${hex(u32(14))}, in ${crumbs[d[4]] || d[4]}` +
    (d[5] ? `, interrupt ${d[5]}` : "") + `; stack free: main ${u16(18)} B, network ${u16(20)} B). Please send this line to the developer.`;
  $("diagBanner").classList.remove("hidden");
}
$("diagCopy").addEventListener("click", () => navigator.clipboard && navigator.clipboard.writeText($("diagText").textContent).then(() => toast("Copied")));
$("diagClear").addEventListener("click", async () => { $("diagBanner").classList.add("hidden"); try { await cmd([0xf6, 0x17, 1]); } catch (e) { /* gone */ } });

async function startHidSession(st) {
  S.mode = "hid"; S.state = "loading";
  document.body.classList.add("hid");
  readStatus(st);
  render();
  await readDiag();
  try {
    await hidLoadAll();
  } catch (e) { log("load: " + e.message); toast("Lost the keyboard while reading: " + e.message, true); await closeHid(); return; }
  S.state = "ready";
  HID.poll = setInterval(pollStatus, 1500);
  render();
  toast("Connected — click a key, then a keycode");
}

function readStatus(st) {
  S.hidState = st[3]; S.bridgeOn = st[4] !== 0; S.vialUnlocked = !!st[9];
  S.ledInfo = { idle: st[5], max: st[6], modes: st[7], enabled: st[8] };
  S.mapMeasured = !!(st[16] & 1);
  S.rightMap = st[21];
  S.wiggle = { on: !!(st[30] & 1), every: (st[30] >> 1) & 3 };
  calCapture(st);
}

async function pollStatus() {
  if (S.mode !== "hid" || S.sessionLost) return;
  try {
    const st = await cmd([0xf6, 0x00, S.ui && S.ui.tab === 1 ? 1 : 0]);
    HID.pollFails = 0;
    readStatus(st);
    if (st[3] !== HS.ACTIVE) sessionEnded(st[31] || END.NONE);
    renderNote();
  } catch (e) {
    log("poll: " + e.message);
    // Network mode: the keyboard went back to a plain keyboard (or the cable came out).
    if (NET && ++HID.pollFails >= 2) { sessionEnded(END.GONE); renderNote(); }
  }
}
// v29: the keyboard ended the session (Brett: "clean up the exit"): say so at once, and why.
function sessionEnded(why) {
  if (S.sessionLost) return;
  S.sessionLost = why || -1;
  if (NET) { clearInterval(HID.poll); HID.poll = null; } // network mode is off now: nothing to poll
  toast(why === END.KEY ? "Config mode ended on the keyboard" : "The keyboard ended the session", true);
}

async function hidLoadAll() {
  const R = DATA.rows, C = DATA.cols;
  const layers = (await cmd([0x11]))[1];
  S.ready = { layers, rows: R, cols: C, encoders: 2, defLayer: null };
  S.keymap = Array.from({ length: layers }, () => Array.from({ length: R }, () => Array(C).fill(null)));
  const total = layers * R * C * 2, bytes = [];
  for (let off = 0; off < total; off += 28) {
    const n = Math.min(28, total - off);
    const d = await cmd([0x12, off >> 8, off & 0xff, n]);
    for (let i = 0; i < n; i++) bytes.push(d[4 + i]);
  }
  for (let i = 0; i < total / 2; i++) {
    const l = Math.floor(i / (R * C)), r = Math.floor((i % (R * C)) / C), c = i % C;
    S.keymap[l][r][c] = (bytes[i * 2] << 8) | bytes[i * 2 + 1];
  }
  S.enc = [];
  for (let l = 0; l < layers; l++) {
    S.enc[l] = [];
    for (let e = 0; e < 2; e++) { const d = await cmd([0xfe, 0x03, l, e]); S.enc[l][e] = [(d[0] << 8) | d[1], (d[2] << 8) | d[3]]; }
  }
  S.effects = [];
  for (let gt = 0, done = false; !done;) {
    const d = await cmd([0x08, 0x42, gt & 0xff, gt >> 8]); // VialRGB: supported effects with id > gt
    done = true;
    for (let i = 2; i + 1 < 32; i += 2) {
      const id = d[i] | (d[i + 1] << 8);
      if (id === 0xffff) break;
      if (id > gt) { S.effects.push(id); gt = id; done = false; }
    }
    if (S.effects.length > 64) break;
  }
  buildLedSelects();
  await hidLoadLed();
  await hidLoadTp();
  try { await petLoad(); } catch (e) { S.pet = null; }
  try { await macrosLoad(); await koLoad(); } catch (e) { S.macros = S.ko = null; log("macros / shift pairs: " + e.message); } // v28
  S.ui = { tab: 0, layer: 0, row: 0xff, col: 0xff, slot: -1, pick: -1, pending: null, ledField: -1, tpField: -1 };
}

async function hidLoadLed() {
  const st = await cmd([0xf6, 0x00, 1]); readStatus(st);
  const m = await cmd([0x08, 0x41]); // VialRGB mode: id, speed, h, s, v
  const id = m[2] | (m[3] << 8);
  const cur = st[11]; // current effect (VialRGB id; ledLayeredId = background + key-press effect, or one of ours)
  const f = await cmd([0xf6, 0x1c, 0]); // v20 lighting: base, effect, blend, colour, hue, sat, speed, width, reach, fade, echoes, dim
  const layered = cur === DATA.ledLayeredId;
  S.led = { enable: S.ledInfo.enabled, mode: id || cur, hue: m[5], sat: m[6], val: m[7], speed: m[4], idle: S.ledInfo.idle, max: S.ledInfo.max,
            zone: st[12], ugHue: st[13], ugSat: st[14],
            base: layered ? f[3] : cur, fxEffect: layered ? f[4] : 0, fxBlend: f[5], fxColour: f[6], rxHue: f[7], rxSat: f[8],
            fxSpeed: f[9], fxWidth: f[10], fxRadius: f[11], fxFade: f[12], fxEchoes: f[13], fxDim: f[14], fxBright: f[16] <= 100 ? f[16] : 0,
            curve: f[17] & 0x80 ? f[17] & 0x7f : 2, fxMax: f[18] & 0x80 ? f[18] & 0x7f : 0, dirty: 0 }; // v25b; older firmware: gamma 2.2 (v23-v25); v27: effects at once (0 = older)
  const sv = await cmd([0xf6, 0x20, 0]); // v27 screensaver
  S.led.ssOk = sv[2] === 0xa3;
  if (S.led.ssOk) Object.assign(S.led, { ssAfter: sv[3], ssEffect: sv[4], ssPet: !!(sv[5] & 1), ssHue: sv[6], ssSat: sv[7], ssVal: sv[8], ssSpeed: sv[9] });
  S.ledSent = { ...S.led };
}
// Lighting combinations (led_fx_table.h, the firmware checks the same).
const FXK = DATA.fxTable.kinds;
function fxInfo(base) { // kind and main hue of a background (VialRGB id or ours)
  const T = DATA.fxTable;
  for (const [i, , label, kind, hue] of T.backgrounds) if (base === T.ownBg + i) return { kind, hue, label, own: true };
  for (const [i, , label, kind, hue] of T.solos) if (base === T.ownSolo + i) return { kind, hue, label, own: true };
  for (const [i, , label, kind, hue] of T.qmk) if (base === i) return { kind, hue, label };
  return { kind: FXK.BG, hue: 255, label: DATA.ledEffects[base] || "" };
}
const FX_FIELDS = ["base", "fxEffect", "fxBlend", "fxColour", "rxHue", "rxSat", "fxSpeed", "fxWidth", "fxRadius", "fxFade", "fxEchoes", "fxDim", "fxBright", "curve", "fxMax"];
// v27 screensaver (0xF6 0x20); the level tables are kb_config.c's.
const SS_FIELDS = ["ssAfter", "ssEffect", "ssPet", "ssHue", "ssSat", "ssVal", "ssSpeed"];
const SS = { after: [0, 1, 2, 5, 10, 15, 30], sat: [255, 219, 182, 146, 109, 73, 36, 0], val: [40, 10, 20, 30, 50, 60, 80, 100], speed: [128, 64, 192, 255] };
const IDLE_MIN = [0, 1, 2, 5, 10, 30]; // "Turn off after no use" (user_config.c)
const ssBytes = (l) => [0xf6, 0x20, 1, l.ssAfter, l.ssEffect, l.ssPet ? 1 : 0, l.ssHue, l.ssSat, l.ssVal, l.ssSpeed];
// [15]: key-press brightness with bit 7 set (v21; without it the keyboard keeps its value); [16]: the output curve, the same way (v25b); [17]: effects at once (v27).
const fxBytes = (l) => [0xf6, 0x1c, 1, l.base, l.fxEffect, l.fxBlend, l.fxColour, l.rxHue, l.rxSat, l.fxSpeed, l.fxWidth, l.fxRadius, l.fxFade, l.fxEchoes, l.fxDim, 0x80 | (l.fxBright || 0), 0x80 | (l.curve || 0), l.fxMax ? 0x80 | l.fxMax : 0];
// Old backups / pages: QMK key-press effects -> the nearest overlay (as led_fx.c: effect_of_react).
