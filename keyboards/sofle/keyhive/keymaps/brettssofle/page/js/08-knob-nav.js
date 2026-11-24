// ---------------------------------------------------------------- Right-knob navigation
// While connected and this page has focus, the keyboard reports the right
// knob to the page (0xF6 0x12) instead of acting on it: turn = move the
// highlight, press = select / edit, long press = next tab. Losing focus (or
// hiding the page) hands the knob back; the keyboard also takes it back by
// itself if the page stops asking (1.5 s).
const NAV = { on: false, idx: 0, mode: "move", pick: 0, editEl: null, lastTurn: 0 };
// Not during LED calibration: every key press, the right knob's too, is a calibration answer there.
function navWanted() { return S.mode === "hid" && S.state === "ready" && !S.sessionLost && !!HID.dev && !CAL.on && document.hasFocus() && !document.hidden; }
const navVisible = (el) => el && el.offsetParent !== null && !el.disabled;
function navItems() {
  if (!S.ui) return [];
  if (NAV.mode === "pick") return [...$("hidList").querySelectorAll("[data-kc]")];
  if (S.ui.tab === 0) {
    const keys = [...$("kb").querySelectorAll("[data-r]")].map((el) => {
      const k = keyAt[el.dataset.r + "," + el.dataset.c];
      return { el, y: Math.floor(k.y + 0.3), x: k.x };
    }).sort((a, b) => a.y - b.y || a.x - b.x).map((o) => o.el);
    return [...$("layers").querySelectorAll("[data-layer]"), ...keys, ...$("knobs").querySelectorAll("[data-slot]")];
  }
  const root = S.ui.tab === 1 ? $("ledCtl") : S.ui.tab === 3 ? $("petView") : $("padView");
  return [...root.querySelectorAll("input:not([type=color]), select, button, summary")].filter(navVisible); // the knob uses the swatches
}
function navClamp(items) {
  const last = Math.max(0, items.length - 1);
  NAV.pick = Math.max(0, Math.min(NAV.pick, last));
  NAV.idx = Math.max(0, Math.min(NAV.idx, last));
}
function navPaint(scroll = true) {
  document.querySelectorAll(".navf, .navedit").forEach((el) => el.classList.remove("navf", "navedit"));
  if (!NAV.on) return;
  const items = navItems();
  if (!items.length) return;
  // The list can shrink (another tab, a hidden control): keep the stored index
  // on the highlighted item, so a press activates what is shown.
  navClamp(items);
  const el = items[NAV.mode === "pick" ? NAV.pick : NAV.idx];
  el.classList.add(NAV.mode === "edit" ? "navedit" : "navf");
  if (scroll) el.scrollIntoView({ block: "nearest" });
}
function navTurn(n) {
  const now = performance.now(), fast = Math.abs(n) > 2 || now - NAV.lastTurn < 90;
  NAV.lastTurn = now;
  if (NAV.mode === "edit") {
    const el = NAV.editEl;
    if (!el || !navVisible(el)) { NAV.mode = "move"; return navPaint(); }
    if (el.tagName === "SELECT") {
      el.selectedIndex = Math.max(0, Math.min(el.options.length - 1, el.selectedIndex + n)); // a poll can carry several detents
    } else {
      const step = +el.step || 1, span = +el.max - +el.min;
      const mult = fast && span / step > 40 ? 4 : 1;
      el.value = Math.max(+el.min, Math.min(+el.max, +el.value + n * step * mult));
    }
    el.dispatchEvent(new Event("input", { bubbles: true }));
    el.dispatchEvent(new Event("change", { bubbles: true }));
    return navPaint();
  }
  const items = navItems();
  if (!items.length) return;
  const step = NAV.mode === "pick" && fast ? n * 4 : n;
  if (NAV.mode === "pick") NAV.pick = Math.max(0, Math.min(items.length - 1, NAV.pick + step));
  else NAV.idx = ((NAV.idx + step) % items.length + items.length) % items.length;
  navPaint();
}
async function navPress() {
  const items = navItems();
  navClamp(items);
  if (NAV.mode === "edit") { NAV.mode = "move"; NAV.editEl = null; return navPaint(); }
  if (NAV.mode === "pick") {
    const el = items[NAV.pick];
    NAV.mode = "move";
    if (el) el.click(); // assign() saves it
    return navPaint();
  }
  const el = items[NAV.idx];
  if (!el) return;
  if (el.matches("#kb [data-r], #knobs [data-slot]")) {
    el.click();
    await sleep(50);
    const cur = currentValue(S.ui), list = [...$("hidList").querySelectorAll("[data-kc]")];
    NAV.mode = "pick";
    NAV.pick = Math.max(0, list.findIndex((o) => +o.dataset.kc === cur));
  } else if ((el.tagName === "INPUT" && el.type === "range") || el.tagName === "SELECT") {
    NAV.mode = "edit"; NAV.editEl = el;
  } else {
    el.click();
  }
  navPaint();
}
async function navLong() {
  if (NAV.mode !== "move") { NAV.mode = "move"; NAV.editEl = null; return navPaint(); } // back out of the picker / slider
  NAV.idx = 0;
  await setTab((S.ui.tab + 1) % tabList().length);
  navPaint();
}
async function navTick() {
  const want = navWanted();
  if (!want && !NAV.on) return;
  let d;
  try { d = await cmd([0xf6, 0x12, want ? 1 : 0]); } catch (e) { NAV.on = false; return; }
  const was = NAV.on;
  NAV.on = want && !!d[25];
  if (NAV.on !== was) { if (!NAV.on) NAV.mode = "move"; renderNote(); navPaint(); }
  if (!NAV.on) return;
  const turns = (d[22] << 24) >> 24;
  for (let i = 0; i < d[24]; i++) await navLong();
  if (turns) navTurn(turns);
  for (let i = 0; i < d[23]; i++) await navPress();
}
(async function navLoop() {
  for (;;) {
    await navTick();
    await sleep(NAV.on ? 50 : 250);
  }
})();
window.addEventListener("blur", () => navTick());
document.addEventListener("visibilitychange", () => navTick());

// Trackpad: live Z through 0xF5 get; settings saved with Save.
function tpInput() {
  const t = S.tp;
  t.enable = $("tpOn").checked ? 1 : 0;
  t.threshold = +$("tpThr").value;
  t.release = Math.min(+$("tpRel").value, t.threshold - 1);
  t.dirty = 1;
  render();
}
async function tpSave() {
  try {
    const d = await cmd([0xf5, 0x01, S.tp.enable, S.tp.threshold, S.tp.release]);
    if (d[0] === 0xff) throw new Error("refused");
    S.tp = { enable: d[7], threshold: d[8], release: d[9], dirty: 0 };
    toast("Trackpad settings saved");
  } catch (e) { toast("Not saved: " + e.message, true); }
  render();
}
async function tpRevert() { await hidLoadTp(); render(); }
["tpOn", "tpThr", "tpRel"].forEach((id) => $(id).addEventListener("input", tpInput));
$("tpSave").addEventListener("click", tpSave);
$("tpRevert").addEventListener("click", tpRevert);

function startZPoll() {
  if (HID.zpoll) return;
  HID.zpoll = setInterval(async () => {
    if (S.mode !== "hid" || !S.ui || S.ui.tab !== 2) return stopZPoll();
    if (HID.zbusy) return;
    HID.zbusy = true;
    try { const d = await cmd([0xf5, 0x00]); addZ({ z: d[1], peak: d[2], lastPeak: d[3], touching: d[4], pressed: d[10] }); scheduleRender(); }
    catch (e) { /* skip a sample */ }
    HID.zbusy = false;
  }, NET ? 100 : 40);
}
function stopZPoll() { clearInterval(HID.zpoll); HID.zpoll = null; }

async function setTab(tab) {
  if (S.mode !== "hid" || !S.ui || tab === S.ui.tab) return;
  if (S.ui.tab === 1) { await calStop(); await ledRevert(); }
  if (S.ui.tab === 2 && S.tp && S.tp.dirty) await hidLoadTp();
  if (S.ui.tab === 3) stopPetPoll();
  S.ui.tab = tab;
  if (tab === 1) { try { await hidLoadLed(); } catch (e) { log(e.message); } }
  if (tab === 2) startZPoll();
  if (tab === 3) { try { await petLoad(); } catch (e) { log(e.message); } startPetPoll(); }
  render();
}

// Clicks (WebHID mode only).
$("tabs").addEventListener("click", (e) => { const t = e.target.closest("[data-tab]"); if (t) setTab(+t.dataset.tab); });
$("layers").addEventListener("click", (e) => { const t = e.target.closest("[data-layer]"); if (t && S.mode === "hid") { S.ui.layer = +t.dataset.layer; render(); } });
$("kb").addEventListener("click", async (e) => {
  const t = e.target.closest("[data-r]"); if (!t || S.mode !== "hid") return;
  if (S.ui.tab !== 0) await setTab(0);
  S.ui.row = +t.dataset.r; S.ui.col = +t.dataset.c; S.ui.slot = -1; render(); scrollToCurrent();
});
$("knobs").addEventListener("click", async (e) => {
  const t = e.target.closest("[data-slot]"); if (!t || S.mode !== "hid") return;
  if (S.ui.tab !== 0) await setTab(0);
  S.ui.slot = +t.dataset.slot; S.ui.row = S.ui.col = 0xff; render(); scrollToCurrent();
});
let hidCat = -1;
$("hidCats").addEventListener("click", (e) => { const t = e.target.closest("[data-cat]"); if (t) { hidCat = +t.dataset.cat; renderHidList(); } });
$("search").addEventListener("input", renderHidList);
$("hidList").addEventListener("click", (e) => { const t = e.target.closest("[data-kc]"); if (t) assign(+t.dataset.kc); });
$("advKind").addEventListener("change", renderAdv);
$("advApply").addEventListener("click", () => {
  const kind = $("advKind").value, base = +$("advBase").value & 0xff;
  const mods = [...$("advMods").querySelectorAll("input:checked")].reduce((a, i) => a | +i.value, 0);
  if (kind === "lt") assign(R.QK_LAYER_TAP | ((+$("advLayer").value & 0xf) << 8) | base);
  else if (!(mods & 0xf)) toast("Tick at least one modifier", true);
  else if (kind === "mt") assign(R.QK_MOD_TAP | ((mods & 0x1f) << 8) | base);
  else assign(((mods & 0x1f) << 8) | base);
});
$("rawApply").addEventListener("click", () => {
  const v = parseInt($("rawKc").value.trim(), $("rawKc").value.trim().startsWith("0x") ? 16 : 10);
  if (Number.isInteger(v) && v >= 0 && v <= 0xffff) assign(v); else toast("Enter a number like 0x5223", true);
});
$("connectBtn").addEventListener("click", connect);
$("disconnectBtn").addEventListener("click", closeHid);

