// ---- LED preview: our backgrounds and every key-press effect use the FX core,
// the keyboard's own maths, composited as led_fx_pixel does; QMK's
// backgrounds are approximated (noted under the preview).
const PV = { ev: [], dim: 255, demo: 0, last: 0 };
const PV_W = 600, PV_H = 230, pvX = (x) => 20 + x * 560 / 224, pvY = (y) => 20 + y * 190 / 64;
function pvQmkBase(id, x, y, t, l) {
  const n = (DATA.ledEffects[id] || "").toLowerCase(), V = l.val, h = l.hue, s = l.sat, sp = (l.speed + 32) / 128;
  const hs = (k) => FX.hash32(k >>> 0);
  if (/digital rain/.test(n)) return FX.background(FX.B.CODE_RAIN, { t, hue: 85, sat: 255, val: V, spd: l.speed + 64, wpm: 0, n: 0, hx: [], ht: [] }, x, y);
  if (/raindrop|jellybean|pixel rain/.test(n)) { const k = hs(x * 31 + y * 7 + Math.floor(t * sp / 900) * 977); return { h: /jelly/.test(n) ? k & 255 : (h + ((k & 63) - 32)) & 255, s: /jelly/.test(n) ? 255 : s, v: /pixel/.test(n) && k >>> 30 ? 0 : V }; }
  if (/breath/.test(n)) return { h, s, v: Math.floor(V * FX.sin8(Math.floor(t * sp / 16)) / 255) };
  if (/rainbow|cycle|spiral|pinwheel|swirl|band/.test(n)) return { h: (h + x + Math.floor(t * sp / 20)) & 255, s, v: V };
  if (/gradient/.test(n)) return { h: (h + (x >> 1)) & 255, s, v: V };
  if (/heatmap|reactive/.test(n)) return { h, s, v: 0 };
  return { h, s, v: V };
}
function pvFire(row, col) {
  const led = DATA.fxLeds.find((d) => d[2] & 1 && d[3] === row && d[4] === col);
  if (!led) return;
  PV.ev.push({ x: led[0], y: led[1], born: performance.now() });
  if (PV.ev.length > 8) PV.ev.shift(); // the keyboard remembers the last 8 presses
}
function pvFrame(now, cv = $("fxCanvas")) {
  const l = S.led;
  if (!cv || !l || S.mode !== "hid" || !cv.offsetParent) return;
  const g = cv.getContext("2d"), t = Math.floor(now) >>> 0;
  const info = fxInfo(l.base), solo = !!(info.kind & FXK.SOLO), effect = solo ? 0 : l.fxEffect;
  const s = { base: l.base, effect, blend: l.fxBlend, colour: l.fxColour, hue: l.rxHue, sat: l.rxSat, speed: l.fxSpeed, width: l.fxWidth, radius: l.fxRadius, fade: l.fxFade, echoes: l.fxEchoes, dim: l.fxDim, bright: l.fxBright || 0 };
  const p = FX.prepare(s), V = Math.max(1, l.val);
  if (PV.demo && now >= PV.demo) { const keys = DATA.fxLeds.filter((d) => d[2] & 1); const k = keys[(FX.hash32(t) >>> 8) % keys.length]; pvFire(k[3], k[4]); PV.demo = PV.demoLeft-- > 0 ? now + 450 : 0; }
  const ev = [];
  for (const e of PV.ev) { const el = Math.min(65535, Math.floor(now - e.born)); const x = FX.event(p, e.x, e.y, el, (Math.floor(e.born) >>> 0)); if (FX.alive(p, x)) ev.push(x); }
  PV.ev = PV.ev.filter((e) => now - e.born < 6000);
  const shown = ev.slice(FX.firstEvent(ev.length, l.fxMax || 8)); // v27: only the newest "max effects at once"
  // v27 screensaver preview: fades in, 10 s, fades out (screensaver.c), mixed under the key-press layer.
  let ssK = 0, ssCtx = null;
  if (l.ssOk && (PV.ss || PV.ssLevel)) {
    const on = !!PV.ss && now - PV.ss.born < 10000;
    PV.ssLevel = FX.ssStep(PV.ssLevel || 0, Math.min(100, Math.max(0, Math.floor(now - (PV.ssT || now)))), on);
    if (!on) PV.ss = null;
    ssK = FX.ssShare(PV.ssLevel);
    ssCtx = { t, hue: l.ssHue, sat: SS.sat[l.ssSat], val: Math.floor(SS.val[l.ssVal] * (l.max || 255) / 100), spd: SS.speed[l.ssSpeed], wpm: 0, n: 0, hx: [], ht: [] };
  }
  PV.ssT = now;
  const want = effect && ev.length ? 255 - l.fxDim * 28 : 255;
  PV.dim = PV.dim < want ? Math.min(want, PV.dim + 24) : Math.max(want, PV.dim - 24);
  const ctx = { t, hue: l.hue, sat: l.sat, val: l.val, spd: l.speed, wpm: FX.typingWpm(PV.ev.map((e) => Math.min(65535, Math.floor(now - e.born)))),
                n: PV.ev.length, hx: PV.ev.map((e) => e.x), ht: PV.ev.map((e) => Math.min(65535, Math.floor(now - e.born))) };
  g.fillStyle = "#0c0d10"; g.fillRect(0, 0, PV_W, PV_H);
  // v26: the pet's LED reaction (Test, or a care action on the Pet tab), over everything (led_pet_pixel).
  let pg = null;
  if (PV.pet) { const el = Math.floor(now - PV.pet.born); if (el < FX.PET_MS[PV.pet.type]) pg = FX.petPrepare(PV.pet.type, el); else PV.pet = null; }
  for (const [x, y, flag] of DATA.fxLeds) {
    let rgb = [0, 0, 0];
    if (l.enable && l.base) {
      const o = l.base >= DATA.fxTable.ownBg ? FX.background(l.base, ctx, x, y) : pvQmkBase(l.base, x, y, t, l);
      rgb = FX.hsv2rgb(o.h, o.s, o.v);
    }
    if (ssK && l.enable && !(flag & 2 && l.zone === 2)) {
      const o = FX.background(DATA.fxTable.ownBg + l.ssEffect, ssCtx, x, y);
      rgb = FX.ssMix(rgb, FX.hsv2rgb(o.h, o.s, o.v), ssK);
    }
    rgb = rgb.map((v) => v * 256); // 8.8, as led_fx_pixel
    if (l.enable && effect) {
      if (PV.dim !== 255) rgb = rgb.map((v) => (v * PV.dim) >> 8);
      let best = 0, bj = 0;
      shown.forEach((e, j) => { const a = FX.intensity(p, e, x, y); if (effect === FX.E.HEAT) best += a; else if (a > best) { best = a; bj = j; } });
      if (best) {
        const v = FX.overlayV(s.bright, l.val, l.max || 255), top = FX.hsv2rgb(FX.hue(p, shown[bj], l.hue, x, y), FX.sat(p), v);
        FX.blend16(s.blend, rgb, top, Math.min(255, best), v);
      }
    }
    if (pg && pg.type && l.enable && !(flag & 2 && l.zone === 2) && !(PV.ssLevel && !l.ssPet)) { // screensaver_pet_ok
      const o = FX.petPx(pg, x, y, flag & 2 ? 1 : 0), a = FX.petAlpha(pg, o.a, FX.petK(PV.pet.level));
      if (a) { const v = FX.div255(FX.overlayV(l.fxBright || 0, l.val, l.max || 255) * o.v); FX.blend16(0, rgb, FX.hsv2rgb(o.h, o.s, v), a, v); }
    }
    FX.cap16(rgb, l.max || 255);
    // The keyboard sends these values through its output curve (FX.curve16); a screen
    // applies gamma 2.2 to the colours it is given, so show curve^(1/2.2): the light the LEDs give.
    const cap = l.max || 255, gk = FX.gammaK(cap), cx = pvX(x), cy = pvY(y);
    g.fillStyle = `rgb(${rgb.map((v) => Math.min(255, Math.round(255 * Math.pow(FX.curve16(l.curve || 0, Math.round(v), cap, gk) / (cap * 256), 1 / 2.2)))).join(",")})`;
    if (flag & 1) { g.fillRect(cx - 15, cy - 15, 30, 30); g.strokeStyle = "#2a2d35"; g.strokeRect(cx - 15.5, cy - 15.5, 31, 31); }
    else { g.beginPath(); g.arc(cx, cy, 5, 0, 7); g.fill(); }
  }
  $("fxApprox").textContent = l.base && l.base < DATA.fxTable.ownBg ? "(QMK backgrounds are approximated here; key-press effects are exact.)" : "";
}
(function pvLoop(now) { try { if (!PV.loopOff && now - PV.last > 30) { PV.last = now; pvFrame(now, $("fxCanvas")); pvFrame(now, $("pgCanvas")); } } catch (e) { /* preview only */ } requestAnimationFrame(pvLoop); })(0);
$("fxCanvas").addEventListener("click", async (ev) => {
  const r = ev.target.getBoundingClientRect(), mx = (ev.clientX - r.left) * PV_W / r.width, my = (ev.clientY - r.top) * PV_H / r.height;
  let best = null, bd = 1e9;
  for (const d of DATA.fxLeds) { if (!(d[2] & 1)) continue; const dx = pvX(d[0]) - mx, dy = pvY(d[1]) - my, q = dx * dx + dy * dy; if (q < bd) { bd = q; best = d; } }
  if (!best || bd > 30 * 30) return;
  pvFire(best[3], best[4]);
  if (S.mode === "hid" && HID.dev && HID.dev.opened) { try { await cmd([0xf6, 0x1c, 4, best[3], best[4]]); } catch (e) { /* preview still runs */ } }
});
$("fxDemo").addEventListener("click", () => { PV.demoLeft = 7; PV.demo = performance.now(); });
// Brightness values are for an output curve: v22 and older linear, v23-v25 gamma 2.2,
// v25b the backup's own (leds.curve). Older ones move to Balanced with the same
// full-level light (the keyboard converted its own settings once, the same way).
function ledsFromOldBackup(x) { // v19 and older: base + react (VialRGB ids); v22 and older: no gamma
  if (x.curve === undefined) {
    const from = x.gamma === 1 ? FX.CURVE.SMOOTH : FX.CURVE.LINEAR, to = FX.CURVE.BALANCED;
    x = { ...x, gamma: 1, curve: to };
    const max = S.led && S.led.max ? S.led.max : 100;
    if (x.val) x.val = Math.max(1, FX.curveConvert(from, to, x.val, max));
    if (x.fxBright) x.fxBright = Math.max(1, Math.min(100, Math.floor((FX.curveConvert(from, to, Math.floor(x.fxBright * max / 100), max) * 100 + (max >> 1)) / max)));
  }
  if (x.react === undefined || x.fxEffect !== undefined) return x;
  const y = { ...x };
  delete y.react;
  if (x.react === 255) return y; // light page: "keep the key-press effect"
  if (DATA.ledSolo.includes(x.react)) { y.base = x.react; y.fxEffect = 0; } else y.fxEffect = fxOfReact(x.react);
  return y;
}
function fxOfReact(react) {
  const n = (DATA.ledEffects[react] || "").toLowerCase(), E = FX.E;
  if (!react) return 0;
  if (/simple/.test(n)) return E.GLOW;
  if (/wide/.test(n)) return E.SPLASH;
  if (/cross/.test(n)) return E.CROSS;
  if (/nexus/.test(n)) return E.NEXUS;
  return E.RING;
}
async function hidLoadTp() {
  await prefsLoad();
  const d = await cmd([0xf5, 0x00]);
  S.tp = { enable: d[7], threshold: d[8], release: d[9], dirty: 0 };
}

async function closeHid() {
  clearInterval(HID.poll); HID.poll = null; stopZPoll();
  if (CAL.on && HID.dev && HID.dev.opened) await calStop();
  if (S.mode === "hid" && S.led && S.led.dirty && HID.dev && HID.dev.opened) { try { await cmd([0xf6, 0x04]); } catch (e) { /* gone */ } }
  const dev = HID.dev; HID.dev = null;
  if (dev) {
    try { if (dev.opened) { await sendEnd(dev); await dev.close(); } } catch (e) { /* already gone */ }
    dev.removeEventListener("inputreport", onReport);
  }
  document.body.classList.remove("hid");
  if (S.mode === "hid" || S.mode === "hidAsk") { S.mode = null; S.state = "idle"; S.ui = null; }
  render();
}
async function sendEnd(dev) {
  const buf = new Uint8Array(32); buf[0] = 0xf6; buf[1] = 0x11;
  try { await dev.sendReport(0, buf, true); } catch (e) { /* ignore */ }
}
if (navigator.hid) navigator.hid.addEventListener("disconnect", (e) => {
  if (e.device === HID.dev) { toast("Keyboard unplugged", true); HID.dev = null; closeHid(); }
});
window.addEventListener("pagehide", () => { if (HID.dev && HID.dev.opened) sendEnd(HID.dev); });

