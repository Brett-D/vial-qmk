// ================================================================ names
const R = DATA.ranges;
function modsStr(m) {
  const parts = [];
  if (m & 1) parts.push("Ctl"); if (m & 2) parts.push("Sft"); if (m & 4) parts.push("Alt"); if (m & 8) parts.push("Gui");
  return (m & 0x10 ? "R" : "L") + parts.join("+");
}
// v28: "Ctrl+Shift+T" (R for right-hand modifiers).
function modsName(m) {
  return [[1, "Ctrl"], [2, "Shift"], [4, "Alt"], [8, "Win"]].filter(([b]) => m & b).map(([, n]) => (m & 0x10 ? "R" : "") + n).join("+");
}
// The unshifted legend of a two-legend key ("= +" -> "=").
const legend1 = (v) => kcName(v).replace(/^(\S) \S$/, "$1");
function kcName(v) {
  if (v === null || v === undefined) return "…";
  if (v >= R.QK_MACRO && v < R.QK_MACRO + 16 && S.macroNames && S.macroNames[v - R.QK_MACRO]) return S.macroNames[v - R.QK_MACRO]; // v28
  const n = DATA.names[v];
  if (n !== undefined) return n;
  if (v >= R.QK_MODS && v < R.QK_MOD_TAP) return modsName((v >> 8) & 0x1f) + "+" + legend1(v & 0xff);
  if (v >= R.QK_MOD_TAP && v < R.QK_LAYER_TAP) return "MT(" + modsStr((v >> 8) & 0x1f) + "," + kcName(v & 0xff) + ")";
  if (v >= R.QK_LAYER_TAP && v < R.QK_LAYER_MOD) return "LT" + ((v >> 8) & 0xf) + "(" + kcName(v & 0xff) + ")";
  if (v >= R.QK_LAYER_MOD && v < R.QK_TO) return "LM(" + ((v >> 5) & 0xf) + "," + modsStr(v & 0x1f) + ")";
  const layerOps = [["QK_TO", "TO"], ["QK_MOMENTARY", "MO"], ["QK_DEF_LAYER", "DF"], ["QK_TOGGLE_LAYER", "TG"], ["QK_ONE_SHOT_LAYER", "OSL"],
                    ["QK_ONE_SHOT_MOD", null], ["QK_LAYER_TAP_TOGGLE", "TT"], ["QK_PERSISTENT_DEF_LAYER", "PDF"]];
  for (const [k, name] of layerOps) if (v >= R[k] && v < R[k] + 0x20) return name ? name + "(" + (v & 0x1f) + ")" : "OSM(" + modsStr(v & 0x1f) + ")";
  if (v >= R.QK_TAP_DANCE && v < R.QK_TAP_DANCE + 0x100) return "TD(" + (v & 0xff) + ")";
  if (v >= R.QK_MACRO && v < R.QK_MACRO + 0x80) return "M" + (v - R.QK_MACRO);
  if (v >= R.QK_KB && v < R.QK_KB + 0x40) return "KB" + (v - R.QK_KB);
  if (v >= R.QK_USER && v < R.QK_USER + 0x1c0) return "USER" + (v - R.QK_USER);
  return "0x" + v.toString(16).padStart(4, "0");
}
function keyText(v) { return v === 0 ? "" : kcName(v); }
// Colour picker + presets for a hue / saturation slider pair (QMK HSV 0-255); brightness stays its own slider.
const PRESETS = [["Red", 0, 255], ["Orange", 16, 255], ["Yellow", 36, 255], ["Green", 85, 255], ["Cyan", 128, 255], ["Blue", 170, 255],
                 ["Purple", 192, 255], ["Pink", 224, 180], ["Warm white", 28, 120], ["White", 0, 0]]; // = led_presets in config_mode.c
function hsvHex(h, s) {
  const H = h * 6 / 255, i = Math.floor(H), f = H - i, q = 255 * (1 - s / 255 * f), t = 255 * (1 - s / 255 * (1 - f)), p = 255 * (1 - s / 255);
  return "#" + [[255, t, p], [q, 255, p], [p, 255, t], [p, q, 255], [t, p, 255], [255, p, q]][i % 6].map((x) => Math.round(x).toString(16).padStart(2, "0")).join("");
}
function hexHs(x) {
  const n = parseInt(x.slice(1), 16), r = (n >> 16 & 255) / 255, g = (n >> 8 & 255) / 255, b = (n & 255) / 255;
  const mx = Math.max(r, g, b), d = mx - Math.min(r, g, b);
  const H = !d ? 0 : mx === r ? ((g - b) / d + 6) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return [Math.round(H * 255 / 6) % 255, mx ? Math.round(d / mx * 255) : 0];
}
// The sliders' own input handlers send it (live preview, Save, Undo as before).
function colourPickers() {
  document.querySelectorAll(".cpick").forEach((el) => {
    const H = $(el.dataset.h), Sl = $(el.dataset.s);
    el.innerHTML = `<input type="color" title="Pick a colour"> ` + PRESETS.map(([n, h, s]) => `<button class="sw" title="${n}" data-h="${h}" data-s="${s}" style="background:${hsvHex(h, s)}"></button>`).join("");
    const set = (h, s) => { H.value = h; Sl.value = s; H.dispatchEvent(new Event("input", { bubbles: true })); };
    el.querySelector("input").addEventListener("input", (e) => set(...hexHs(e.target.value)));
    el.addEventListener("click", (e) => { const b = e.target.closest("button.sw"); if (b) set(+b.dataset.h, +b.dataset.s); });
  });
}
function colourSync() {
  document.querySelectorAll(".cpick").forEach((el) => {
    const h = +$(el.dataset.h).value, s = +$(el.dataset.s).value, i = el.querySelector("input");
    if (i && document.activeElement !== i) i.value = hsvHex(h, s);
    el.querySelectorAll("button.sw").forEach((b) => b.classList.toggle("on", +b.dataset.h === h && +b.dataset.s === s));
  });
}

function hsvToCss(h, s, v) {
  h = (h / 256) * 360; s /= 255; v /= 255;
  const f = (n) => { const k = (n + h / 60) % 6; return v - v * s * Math.max(0, Math.min(k, 4 - k, 1)); };
  return `rgb(${Math.round(f(5) * 255)},${Math.round(f(3) * 255)},${Math.round(f(1) * 255)})`;
}

