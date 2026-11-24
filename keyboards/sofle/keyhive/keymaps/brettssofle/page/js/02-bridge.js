// ================================================================ keyboard-only mode (bridge)
const SYM = {};
"ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("").forEach((c, i) => (SYM["Key" + c] = i));
for (let d = 2; d <= 7; d++) SYM["Digit" + d] = 24 + d;

function crc8(bytes) {
  let crc = 0;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
  }
  return crc;
}
function base32ToBytes(symbols) {
  const out = [];
  let acc = 0, bits = 0;
  for (const s of symbols) {
    acc = (acc << 5) | s; bits += 5;
    if (bits >= 8) { bits -= 8; out.push((acc >> bits) & 0xff); acc &= (1 << bits) - 1; }
  }
  return out;
}
let rx = null;
function feedCode(code) {
  if (code === "Comma") { rx = []; return true; }
  if (code === "Period") { if (rx) handleSymbols(rx); rx = null; return true; }
  if (code in SYM) { if (rx) { rx.push(SYM[code]); if (rx.length > 120) rx = null; } return true; }
  return false;
}
document.addEventListener("keydown", (e) => {
  if (S.mode === "hid" || S.mode === "hidAsk") return;
  if (e.ctrlKey || e.altKey || e.metaKey) return; // the keyboard never sends modifiers
  if (/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
  if (feedCode(e.code)) e.preventDefault();
}, true);
window.addEventListener("blur", () => document.body.classList.add("blurred"));
window.addEventListener("resize", () => { scheduleRender(); if (CAL.on && CAL.phase !== "choice") calDrawKb(CAL.phase === "done" ? -1 : calCurrent()); });
window.addEventListener("focus", () => document.body.classList.remove("blurred"));
$("focusWarn").addEventListener("click", () => window.focus());

function handleSymbols(symbols) {
  const bytes = base32ToBytes(symbols);
  if (bytes.length < 3 || crc8(bytes.slice(0, -1)) !== bytes[bytes.length - 1]) {
    log("bad frame (" + symbols.length + " symbols)");
    S.missed++;
    return;
  }
  const type = bytes[0], seq = bytes[1], p = bytes.slice(2, -1);
  if (type !== F.HELLO && S.lastSeq !== null && seq !== ((S.lastSeq + 1) & 0xff)) S.missed++;
  S.lastSeq = seq;
  S.frames++;
  handleFrame(type, p);
  scheduleRender();
}
const u16 = (p, i) => p[i] | (p[i + 1] << 8);

function handleFrame(type, p) {
  if (type === F.HELLO) { S.mode = "bridge"; }
  if (S.mode !== "bridge") return;
  switch (type) {
    case F.HELLO:
      S.state = "challenge"; S.nonce = u16(p, 1); S.missed = 0;
      log("HELLO protocol " + p[0]);
      break;
    case F.READY: {
      const [proto, layers, rows, cols, encoders, defLayer] = p;
      S.ready = { proto, layers, rows, cols, encoders, defLayer, hash: u16(p, 6), count: u16(p, 8) };
      S.state = "ready";
      S.keymap = Array.from({ length: layers }, () => Array.from({ length: rows }, () => Array(cols).fill(null)));
      S.enc = Array.from({ length: layers }, () => null);
      log("READY " + JSON.stringify(S.ready));
      break;
    }
    case F.BYE: S.state = "closed"; log("BYE"); break;
    case F.UI:
      S.ui = { tab: p[0], layer: p[1], row: p[2], col: p[3], pick: (u16(p, 4) << 16) >> 16, pending: u16(p, 6),
               slot: p[8], ledField: p[9], tpField: p[10], held: !!(p[11] & 4) };
      break;
    case F.ROW: {
      if (!S.ready) return;
      const [layer, row] = p;
      for (let c = 0; c < S.ready.cols; c++) S.keymap[layer][row][c] = u16(p, 2 + c * 2);
      break;
    }
    case F.ENC:
      if (!S.ready) return;
      S.enc[p[0]] = [[u16(p, 1), u16(p, 3)], [u16(p, 5), u16(p, 7)]];
      break;
    case F.LED:
      S.led = { enable: p[0], mode: p[1], effect: p[2], hue: p[4], sat: p[5], val: p[6], speed: p[7], idle: p[8], max: p[9], dirty: p[10],
                zone: p[11], ugHue: p[12], ugSat: p[13] };
      break;
    case F.TP: S.tp = { enable: p[0], threshold: p[1], release: p[2], dirty: p[3] }; break;
    case F.TPZ: addZ({ z: p[0], peak: p[1], lastPeak: p[2], touching: p[3], pressed: p[4] }); break;
    default: log("unknown frame type " + type);
  }
  if (type !== F.TPZ && type !== F.ROW) log(Object.keys(F).find((k) => F[k] === type) + " " + p.join(","));
}

function addZ(z) {
  if (S.wasTouching && !z.touching) { S.peaks.unshift(z.lastPeak); S.peaks.length = Math.min(S.peaks.length, 16); }
  S.wasTouching = !!z.touching;
  S.tpz = z;
  S.zHist.push(z.touching ? z.z : 0);
  if (S.zHist.length > 150) S.zHist.shift();
}

