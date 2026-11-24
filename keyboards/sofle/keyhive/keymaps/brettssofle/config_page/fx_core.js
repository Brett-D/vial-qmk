// ================================================================ FX core
// The lighting engine's maths, line for line as led_fx_core.h (the firmware):
// integer arithmetic only (Math.imul / >>> for 32-bit unsigned, Math.trunc
// for C's division), the sine table from the same header (DATA.fxSin), so
// the preview shows what the LEDs do. The development test harness (outside this repo) checks that the
// two agree value for value.
const FX = (() => {
  const T = DATA.fxTable, SIN = DATA.fxSin;
  const E = {}, B = {};
  T.overlays.forEach(([id, NAME]) => (E[NAME] = id));
  T.backgrounds.forEach(([id, NAME]) => (B[NAME] = T.ownBg + id));
  T.solos.forEach(([id, NAME]) => (B[NAME] = T.ownSolo + id));
  const REPLACE = 0, ADD = 1, TINT = 2, FIXED = 0, OPPOSITE = 1, RAINBOW = 2, RANDOM = 3;
  const u8 = (v) => v & 255;
  const u32 = (v) => v >>> 0;
  const mul32 = (a, b) => Math.imul(a, b) >>> 0;
  const q16 = (a, k) => Math.floor((a * k) / 65536); // (uint32)(a * k) >> 16, products here stay < 2^32
  const div255 = (x) => (x + 1 + (x >>> 8)) >>> 8;
  const y5 = (y) => (y * 5) >> 2;
  function sin8(a) {
    a &= 255;
    const i = a & 63;
    switch (a >> 6) {
      case 0: return 128 + SIN[i];
      case 1: return 128 + SIN[64 - i];
      case 2: return 128 - SIN[i];
      default: return 128 - SIN[64 - i];
    }
  }
  function hash32(h) {
    h = u32(h);
    h = u32(h ^ (h >>> 16)); h = mul32(h, 0x7feb352d);
    h = u32(h ^ (h >>> 15)); h = mul32(h, 0x846ca68b);
    return u32(h ^ (h >>> 16));
  }
  function isqrt(n) {
    let r = 0, bit = 1 << 16; // n < 2^18
    while (bit > n) bit = Math.floor(bit / 4);
    while (bit) {
      if (n >= r + bit) { n -= r + bit; r = Math.floor(r / 2) + bit; } else r = Math.floor(r / 2);
      bit = Math.floor(bit / 4);
    }
    return r;
  }
  function prepare(s) {
    const p = { s, R: 24 + s.radius * 16, w: 3 + s.width * 2 };
    p.spacing = 2 * p.w + 8;
    p.rings = s.effect === E.WAVE ? s.echoes + 2 : 1;
    p.R3 = Math.floor(p.R / 3); p.HR = 2 * p.w + 8; p.TT = 400 + s.radius * 200; p.HT = 1500 + s.radius * 300;
    p.speed_q16 = Math.floor((30 + s.speed * 15) * 65536 / 1000);
    p.band_k = Math.floor(255 * 65536 / p.w);
    p.lin_k = Math.floor(255 * 65536 / p.R);
    p.trail_k = Math.floor(255 * 65536 / (3 * p.w));
    p.tt_k = Math.floor(255 * 65536 / p.TT);
    p.hr_k = Math.floor(255 * 65536 / p.HR);
    p.ht_k = Math.floor(255 * 65536 / p.HT);
    return p;
  }
  function event(p, x, y, t, pressMs) {
    const e = { x, y5: y5(y), t, r: q16(t, p.speed_q16) };
    e.seed = hash32(u32(x * 73 + y * 151 + mul32(pressMs >>> 11, 2654435761)));
    e.tx = (x + 60 + (e.seed % 104)) % 224;
    e.ty5 = (e.seed >>> 8) % 81;
    const sx = e.tx - e.x, sy = e.ty5 - e.y5;
    e.len = isqrt(sx * sx + sy * sy);
    e.inv_len = e.len ? Math.floor(65536 / e.len) : 0;
    e.fade = fade(p, e.r);
    return e;
  }
  function alive(p, e) {
    if (p.s.effect === E.TRAIL) return e.t < p.TT;
    if (p.s.effect === E.HEAT) return e.t < p.HT;
    return e.r < p.R + (p.rings - 1) * p.spacing + p.w;
  }
  // v23: smoothstep (fx_smooth8), soft ring edges, eased fade with a soft tail, echo decay table.
  const smooth8 = (x) => Math.floor(x * x * (765 - 2 * x) / 65025);
  const band = (p, x) => (x >= p.w ? 0 : smooth8(255 - q16(x, p.band_k)));
  function fade(p, r) {
    if (r >= p.R) return 0;
    const lin = q16(r, p.lin_k);
    let f = 255 - div255(p.s.fade * 17 * smooth8(lin));
    if (lin > 204) f = div255(f * smooth8((255 - lin) * 5));
    return f;
  }
  const ECHO = [255, 160, 100, 62];
  const ringAt = (p, d, rr, f) => div255(band(p, d > rr ? d - rr : rr - d) * f);
  function intensity(p, e, lx, ly) {
    const ly5 = y5(ly), dx = lx - e.x, dy = ly5 - e.y5, ax = Math.abs(dx), ay = Math.abs(dy), r = e.r, fx = p.s.effect;
    const reach = fx === E.HEAT ? p.HR : fx === E.TRAIL || fx === E.GLOW ? 9 : fx === E.LIGHTNING ? 255 : r + p.w + 1;
    if (ax > reach || ay > reach) return 0;
    const d = isqrt(ax * ax + ay * ay);
    switch (fx) {
      case E.RING: return ringAt(p, d, r, e.fade);
      case E.WAVE: {
        let best = 0;
        for (let k = 0; k < p.rings; k++) {
          const back = k * p.spacing;
          if (back > r) break;
          const v = div255(ringAt(p, d, r - back, k ? fade(p, r - back) : e.fade) * ECHO[k & 3]);
          if (v > best) best = v;
        }
        return best;
      }
      case E.SPLASH: return div255((d <= r ? 255 : band(p, d - r)) * e.fade);
      case E.GLOW: return d > 8 || r >= p.R ? 0 : smooth8(255 - q16(r, p.lin_k));
      case E.CROSS: case E.NEXUS: {
        const onRow = ay <= 6, onCol = ax <= 6;
        if (!onRow && !onCol) return 0;
        const a = onRow && onCol ? 0 : onRow ? ax : ay;
        const v = fx === E.CROSS ? (a <= r ? 255 : band(p, a - r)) : band(p, a > r ? a - r : r - a);
        return div255(v * e.fade);
      }
      case E.SHOCKWAVE: {
        let v;
        if (d > r) v = band(p, d - r);
        else {
          const back = r - d;
          v = back >= 3 * p.w ? 0 : (smooth8(255 - q16(back, p.trail_k)) * 3) >> 2;
          if (back < p.w) { const edge = band(p, back); if (edge > v) v = edge; }
        }
        return div255(v * e.fade);
      }
      case E.SPARKS: {
        const hs = hash32(u32(((lx << 8) | ly5) ^ e.seed));
        if ((hs & 7) || d > r || d < 6) return 0;
        return div255(e.fade * (128 + ((hs >>> 8) & 127)));
      }
      case E.DROPLETS: {
        const rr = r < p.R3 ? r : p.R3;
        let ring = div255(band(p, d > rr ? d - rr : rr - d) * e.fade);
        if (r >= p.R3 && d > p.R3 && d <= 2 * p.R3 && !(hash32(u32(((lx << 8) | ly5) ^ e.seed)) & 3)) {
          const drop = div255(e.fade * 200);
          if (drop > ring) ring = drop;
        }
        return ring;
      }
      case E.LIGHTNING: {
        if (!e.len || r >= p.R) return 0;
        const sx = e.tx - e.x, sy = e.ty5 - e.y5, dot = dx * sx + dy * sy;
        if (dot < 0 || dot > e.len * e.len) return 0;
        const along = q16(dot, e.inv_len);
        if (along > 3 * r) return 0;
        const c = dx * sy - dy * sx;
        let side = q16(Math.abs(c), e.inv_len);
        if (c < 0) side = -side;
        const zig = (sin8(u8(along * 12 + (e.seed & 255))) - 128) >> 4;
        const ao = Math.abs(side - zig);
        if (ao > 8) return 0;
        const v = 255 - ao * 28, flick = (e.seed >>> ((e.t >> 5) & 15)) & 1 ? 255 : 140;
        return div255(div255(v * flick) * e.fade);
      }
      case E.TRAIL: return d > 8 || e.t >= p.TT ? 0 : smooth8(255 - q16(e.t, p.tt_k));
      case E.HEAT:
        if (d >= p.HR || e.t >= p.HT) return 0;
        return div255(smooth8(255 - q16(d, p.hr_k)) * smooth8(255 - q16(e.t, p.ht_k)));
    }
    return 0;
  }
  function hue(p, e, baseHue, lx, ly) {
    switch (p.s.colour) {
      case OPPOSITE: return u8(baseHue + 128);
      case RAINBOW: { const dx = lx - e.x, dy = y5(ly) - e.y5; return u8(p.s.hue + u8(isqrt(dx * dx + dy * dy) * 2)); }
      case RANDOM: return e.seed >>> 24;
    }
    return p.s.hue;
  }
  const sat = (p) => (p.s.colour === FIXED ? p.s.sat : 255);
  // Key-press brightness (v21): its own share of the cap, or the background's setting (0).
  const overlayV = (bright, frameV, cap) => (bright ? Math.floor(Math.min(bright, 100) * cap / 100) : frameV);
  // 8.8 fixed point from v23 (fx_cap16 / fx_blend16): b is 0..65535, c 8-bit.
  function cap16(rgb, c) { for (let i = 0; i < 3; i++) if (rgb[i] > c * 256) rgb[i] = c * 256; }
  function blend16(mode, b, c, a, V) {
    const top = Math.max(b[0], b[1], b[2]), lum = top > V * 128 ? top : V * 128;
    for (let i = 0; i < 3; i++) {
      const c16 = c[i] * 256;
      if (mode === ADD) { b[i] = Math.min(65535, b[i] + Math.floor(c16 * a / 255)); continue; }
      const target = Math.min(65535, mode === TINT && V ? Math.floor(c[i] * lum / V) : c16);
      b[i] = target >= b[i] ? b[i] + Math.floor((target - b[i]) * a / 255) : b[i] - Math.floor((b[i] - target) * a / 255);
    }
  }
  // Gamma 2.2 at the LEDs (fx_gamma16), scaled to the cap; DATA.fxGamma = fx_gamma_q.
  const gammaK = (capv) => (capv ? Math.floor(4294901760 / (capv * 256)) : 0);
  function gamma16(x, capv, k) {
    const top = capv * 256, G = DATA.fxGamma;
    if (x > top) x = top;
    const t = Math.min(65535, Math.floor(x * k / 65536)), i = t >> 10;
    const y = G[i] + Math.floor((G[i + 1] - G[i]) * (t & 1023) / 1024);
    return Math.floor(y * top / 65536);
  }
  // QMK's hsv_to_rgb (quantum/color.c, without the CIE curve).
  function hsv2rgb(h, s, v) {
    if (!s) return [v, v, v];
    const region = Math.floor(h * 6 / 255), rem = u8((h * 2 - region * 85) * 3);
    const p = (v * (255 - s)) >> 8, q = (v * (255 - ((s * rem) >> 8))) >> 8, t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    return [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q], [v, t, p]][region];
  }
  const scale = (v, k) => (v * k) >> 8;
  // led_fx_core.h fx_typing_wpm: words per minute from the last presses (ms ago).
  function typingWpm(ago) {
    let newest = 65535, oldest = 0, cnt = 0;
    for (const a of ago) { if (a > 3000) continue; cnt++; if (a < newest) newest = a; if (a > oldest) oldest = a; }
    if (cnt < 2 || newest > 2000) return 0;
    const span = Math.max(150, oldest - newest);
    return Math.min(255, Math.floor((cnt - 1) * 12000 / span));
  }
  function background(id, c, x, y) {
    const ts = mul32(c.t, c.spd + 32) >>> 7, Y = y5(y);
    const o = { h: c.hue, s: c.sat, v: c.val };
    switch (id) {
      case B.AURORA: {
        const a = sin8(x * 3 + (ts >>> 3)), b = sin8(Y * 5 - (ts >>> 4) + (a >> 2)), d = sin8((x + Y) * 2 + (ts >>> 5));
        o.h = u8(c.hue + ((a + d) >> 3) - 32); o.v = scale(c.val, 40 + ((b * 215) >> 8)); break;
      }
      case B.FIRE: {
        const n1 = sin8(x * 7 + (ts >>> 3)), n2 = sin8(x * 3 - (ts >>> 2) + Y * 6), fl = hash32(u32(mul32(ts >>> 6, 31) + x * 7 + y)) & 63;
        let heat = ((Y * 3 * (n1 + n2)) >> 8) + fl - 40;
        heat = heat < 0 ? 0 : heat > 255 ? 255 : heat;
        o.h = heat >> 3; o.s = heat > 200 ? 255 - (heat - 200) * 2 : 255; o.v = scale(c.val, heat); break;
      }
      case B.STARFIELD: {
        const hs = hash32(u32(x * 257 + y * 7 + 1)), period = 1500 + (hs & 2047), local = u32(ts + (hs >>> 11)) % period;
        const tw = local >= 400 ? 0 : local < 200 ? (local * 326) >> 8 : ((400 - local) * 326) >> 8;
        if ((hs >>> 24) < 90) { o.s = c.sat >> 2; const v = scale(c.val, tw); o.v = v > c.val >> 4 ? v : c.val >> 4; } else o.v = c.val >> 5;
        break;
      }
      case B.OCEAN: {
        const a = sin8(x * 4 + Y * 2 - (ts >>> 3)), b = sin8(x * 2 - Y * 3 + (ts >>> 4)), w = (a + b) >> 1;
        o.h = u8(136 + (w >> 2) - 32); o.s = w > 220 ? 255 - (w - 220) * 4 : 255; o.v = scale(c.val, 80 + ((w * 175) >> 8)); break;
      }
      case B.COMET: {
        const pos = (ts >>> 2) % 600, head = (pos < 300 ? pos : 600 - pos) - 38, behind = pos < 300 ? head - x : x - head;
        const inn = behind < -4 || behind > 70 ? 0 : behind < 0 ? 255 : 255 - ((behind * 933) >> 8);
        o.h = u8(c.hue + (behind > 0 && behind <= 70 ? behind >> 1 : 0));
        const v = scale(c.val, inn); o.v = v > c.val >> 5 ? v : c.val >> 5; break;
      }
      case B.LAVA: {
        let f = 0;
        for (let k = 0; k < 3; k++) {
          const cx = 112 + (((sin8((ts >>> 5) + k * 85) - 128) * 100) >> 7), cy = 40 + (((sin8((ts >>> 6) + k * 70 + 40) - 128) * 36) >> 7);
          const d = Math.abs(x - cx) + Math.abs(Y - cy);
          if (d < 60) f += (60 - d) * 4;
        }
        if (f > 255) f = 255;
        o.h = u8(c.hue + (f >> 3)); o.v = scale(c.val, u8(30 + ((f * 225) >> 8))); break;
      }
      case B.CANDLE: {
        const fl = hash32(u32((ts >>> 6) + x * 13 + y * 3)) & 63, slow = sin8((ts >>> 3) + x * 5) >> 3;
        o.h = 18 + (fl >> 4); o.s = 230; o.v = scale(c.val, u8(150 + fl + slow)); break;
      }
      case B.SUNRISE: {
        const ph = sin8((ts >>> 9) + 192);
        o.h = u8(10 + (((80 - (Y > 80 ? 80 : Y)) * ph * 7) >> 10)); o.s = 255 - (ph >> 2); o.v = scale(c.val, u8(40 + ((ph * 215) >> 8))); break;
      }
      case B.BREATHE_GRADIENT:
        o.h = u8(c.hue + (x >> 1)); o.v = scale(c.val, u8(48 + ((sin8(ts >>> 3) * 207) >> 8))); break;
      case B.CODE_RAIN: {
        const col = (x * 19) >> 8, cs = hash32(u32(col * 977 + 3)), sp = 20 + (cs & 31);
        const head = u32((mul32(ts, sp) >>> 9) + (cs >>> 8)) % 120, back = head - Y;
        if (back < 0 || back >= 30) o.v = 0;
        else { o.s = back < 3 ? c.sat >> 2 : c.sat; o.v = scale(c.val, u8(255 - ((back * 2176) >> 8))); }
        break;
      }
      case B.TYPING_SPEED: {
        const wpm = c.wpm > 120 ? 120 : c.wpm;
        o.h = 85 - ((wpm * 181) >> 8); o.s = 255; o.v = x <= (wpm * 478) >> 8 ? c.val : c.val >> 5; break;
      }
      case B.TYPING_VU: {
        let e = 0;
        for (let j = 0; j < c.n; j++) if (Math.abs(x - c.hx[j]) < 16 && c.ht[j] < 1200) e += 255 - ((c.ht[j] * 218) >> 10);
        const up = 80 - (Y > 80 ? 80 : Y), k = (up * 272) >> 8;
        o.h = k > 85 ? 0 : 85 - k; o.s = 255; o.v = up * 6 < e ? c.val : c.val >> 5; break;
      }
      default: o.v = 0;
    }
    return o;
  }
  return { E, B, prepare, event, alive, intensity, hue, sat, overlayV, cap16, blend16, smooth8, gammaK, gamma16, hsv2rgb, typingWpm, background, sin8, hash32, isqrt, y5 };
})();
