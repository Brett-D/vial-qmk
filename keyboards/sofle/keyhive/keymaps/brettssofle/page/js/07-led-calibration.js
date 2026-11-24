// ---------------------------------------------------------------- LED calibration wizard
// The keyboard lights one physical LED at a time (0-36 left chain, 37-73 right
// chain, independent of the current map) and reports key presses instead of
// typing them (status bytes 17-20), so you just press the key on top of the lit
// LED. Releasing a key held for 1 s means "not under a key" (underglow). The
// result is saved on the keyboard, read back, checked on the right half (status
// byte 21), then tested: each pressed key lights the LED the saved map gives it.
const HALF = 37, NLED = 2 * HALF;
const CAL = { on: false, phase: "", idx: 0, res: [], fix: [], seq: -1, down: null, dupe: null, last: "", map: null, busy: false, looping: false };
const keyAt = {};
DATA.layout.forEach((k) => (keyAt[k.r + "," + k.c] = k));
function keyName(key) {
  const [r, c] = key.split(",").map(Number);
  const kc = S.keymap && S.keymap[0] && S.keymap[0][r] ? S.keymap[0][r][c] : null;
  const n = kc ? kcName(kc) : "";
  return n ? `${n} [${key}]` : `[${key}]`;
}
const ansText = (v) => v === "underglow" ? "underglow" : v === "none" ? "nothing lit" : v ? keyName(v) : "not answered";
const calBtn = (id, label, cls = "", disabled = false) => `<button id="${id}" class="${cls}"${disabled ? " disabled" : ""}>${label}</button>`;
const calCurrent = () => CAL.idx < NLED ? CAL.idx : CAL.fix[0];

async function calLight(i) {
  CAL.busy = true;
  try { const st = await cmd([0xf6, 0x08, i]); CAL.seq = st[17]; CAL.down = null; return st; }
  finally { CAL.busy = false; }
}
async function calLoop() {
  if (CAL.looping) return;
  CAL.looping = true;
  while (CAL.on && S.mode === "hid") {
    try { readStatus(await cmd([0xf6, 0x00, 1])); } catch (e) { log("cal poll: " + e.message); }
    await sleep(60);
  }
  CAL.looping = false;
}
// Called with every status reply (readStatus).
function calCapture(st) {
  if (!CAL.on) return;
  if (CAL.phase === "test") $("calRight").innerHTML = calRightText(st[21]);
  if (CAL.busy || st[17] === CAL.seq) return;
  CAL.seq = st[17];
  const key = st[18] + "," + st[19], down = !!(st[20] & 1), long = !!(st[20] & 2);
  if (CAL.phase === "test") { if (down) calTestKey(key); return; }
  if (CAL.phase !== "measure") return;
  if (down) {
    CAL.down = key;
    $("calBig").textContent = `${keyName(key)} — release to confirm (hold 1 s = underglow)`;
    return;
  }
  CAL.down = null;
  if (long) { calAnswer("underglow"); return; }
  if (!keyAt[key]) { $("calNote").textContent = `That key [${key}] isn't in the layout. Press the key on top of the lit LED.`; calHeadline(); return; }
  calAnswer(key);
}
function calHeadline() { $("calBig").textContent = "Press the key on top of the lit LED"; }

async function calStart() {
  Object.assign(CAL, { on: true, phase: "intro", idx: 0, res: Array(NLED).fill(undefined), fix: [], dupe: null, last: "" });
  $("ledCtl").classList.add("hidden"); $("calView").classList.remove("hidden");
  await calLight(0xff);
  calShow();
  calLoop();
}
async function calStop() {
  if (!CAL.on) return;
  CAL.on = false;
  try { await cmd([0xf6, 0x09]); } catch (e) { /* gone */ }
  $("calView").classList.add("hidden"); $("ledCtl").classList.remove("hidden");
}
async function calStartTest(map) {
  if (!CAL.on) {
    CAL.on = true;
    $("ledCtl").classList.add("hidden"); $("calView").classList.remove("hidden");
  }
  CAL.map = map || await calReadMap();
  CAL.phase = "test";
  CAL.busy = true;
  try { const st = await cmd([0xf6, 0x0e]); CAL.seq = st[17]; } finally { CAL.busy = false; }
  calShow();
  calLoop();
}

function calSet(title, big, html, buttons) {
  $("calTitle").textContent = title;
  $("calBig").textContent = big;
  $("calText").innerHTML = html;
  $("calNote").innerHTML = "";
  $("calBtns").innerHTML = buttons;
}
function calShow() {
  const p = CAL.phase;
  $("calDone").classList.toggle("hidden", p !== "done");
  $("calClickBox").classList.toggle("hidden", p !== "measure");
  $("calProg").classList.toggle("hidden", p !== "measure");
  if (p === "intro") {
    calSet("Calibrate LED positions", "",
      `<ol class="calSteps"><li>One LED at a time lights up white (${NLED} in all, left half first).</li>` +
      `<li><b>Press the key that sits on top of the lit LED.</b> The page moves on to the next LED by itself.</li>` +
      `<li>If the lit LED isn't under a key (it glows from under the board), <b>hold any key for 1 second</b>, or click <b>Underglow</b>.</li>` +
      `<li>If nothing lights up, click <b>Nothing lit</b>. Made a mistake? Click <b>Back</b>.</li></ol>` +
      `<p class="dim">While this runs, key presses only go to this page; they don't type anything. The mouse and trackpad work as usual.</p>`,
      calBtn("calGo", "Start", "primary") + calBtn("calCancel", "Cancel"));
    $("calGo").onclick = async () => { CAL.phase = "measure"; await calLight(calCurrent()); calShow(); };
    $("calCancel").onclick = calStop;
    return;
  }
  if (p === "measure") {
    const i = calCurrent(), half = i < HALF ? "left" : "right";
    $("calBar").style.width = (100 * Math.min(CAL.idx, NLED) / NLED) + "%";
    let html = "";
    if (CAL.dupe) html += `<p class="warnText"><b>${escapeHtml(keyName(CAL.dupe.key))}</b> is already set for LED ${CAL.dupe.prev + 1}. Press it again to move it to this LED (LED ${CAL.dupe.prev + 1} will be asked again at the end), or press the right key.</p>`;
    if (CAL.res[i] !== undefined) html += `<p class="dim">This LED is currently set to: ${escapeHtml(ansText(CAL.res[i]))}</p>`;
    if (CAL.last) html += `<p class="dim">${escapeHtml(CAL.last)}</p>`;
    calSet(CAL.idx >= NLED ? `Again: LED ${i + 1} (${half} half)` : `LED ${i + 1} of ${NLED} · ${half} half`,
      "Press the key on top of the lit LED", html,
      calBtn("calUg", "Underglow / not under a key") + calBtn("calNone", "Nothing lit") + calBtn("calBack", "Back", "", CAL.idx === 0) + calBtn("calCancel", "Cancel"));
    $("calUg").onclick = () => calAnswer("underglow");
    $("calNone").onclick = () => calAnswer("none");
    $("calBack").onclick = calBack;
    $("calCancel").onclick = calStop;
    calDrawKb(i);
    return;
  }
  if (p === "done") {
    const keys = CAL.res.filter((v) => v && v.includes(",")).length, ug = CAL.res.filter((v) => v === "underglow").length;
    const used = new Set(CAL.res), missing = DATA.layout.filter((k) => !used.has(k.r + "," + k.c)).map((k) => keyName(k.r + "," + k.c));
    calSet("All LEDs done", "",
      `<p>${keys} key LEDs, ${ug} underglow, ${NLED - keys - ug} not lit.</p>` +
      (missing.length ? `<p class="dim">Keys without an LED: ${escapeHtml(missing.join(", "))}</p>` : ""),
      "");
    $("calJson").value = calResultJson();
    return;
  }
  if (p === "test") {
    calSet("Test the LED map", "Press keys: the LED under each one should turn green",
      `<p class="dim">Key LEDs glow dim white and underglow LEDs dim blue. Key presses still go only to this page.</p>` +
      `<p id="calRight">${calRightText(S.rightMap)}</p>`,
      calBtn("calOk", "Looks right — done", "primary") + calBtn("calAgain", "Something's wrong — calibrate again"));
    $("calOk").onclick = async () => { await calStop(); toast("LED map checked"); render(); };
    $("calAgain").onclick = calStart;
  }
}
function calRightText(state) {
  return state === 1 ? "Right half: running the same map ✓"
    : state === 2 ? `<span class="warnText">The right half is running a different map. Unplug the keyboard, plug it back in and test again.</span>`
    : "Right half: checking… (if this stays, check the cable between the halves)";
}

function calDrawKb(cur) {
  if (!$("calClickBox").open) return;
  const U = Math.min(50, Math.floor(($("calKb").clientWidth || 800) / 16.6)), leftHalf = cur < HALF;
  const owner = {};
  CAL.res.forEach((v, i) => { if (v && v.includes(",")) owner[v] = i; });
  let html = "";
  for (const k of DATA.layout) {
    const key = k.r + "," + k.c, o = owner[key];
    const other = (k.r < 5) !== leftHalf;
    html += `<div class="k ${o !== undefined ? "done" : ""} ${other ? "other" : ""}" data-key="${key}" style="left:${k.x * U}px;top:${k.y * U}px;width:${U - 4}px;height:${k.h * U - 4}px">${o !== undefined ? "#" + (o + 1) : ""}</div>`;
  }
  $("calKb").innerHTML = html;
  $("calKb").style.height = (5.9 * U) + "px";
}
$("calClickBox").addEventListener("toggle", () => { if (CAL.on && CAL.phase === "measure") calDrawKb(calCurrent()); });
$("calKb").addEventListener("click", (e) => { const t = e.target.closest("[data-key]"); if (t && CAL.on) calAnswer(t.dataset.key); });

async function calAnswer(v) {
  if (CAL.phase !== "measure" || CAL.busy) return;
  const i = calCurrent();
  if (v && v.includes(",")) {
    const prev = CAL.res.findIndex((x, j) => x === v && j !== i);
    if (prev >= 0) {
      if (!CAL.dupe || CAL.dupe.key !== v || CAL.dupe.led !== i) {
        CAL.dupe = { key: v, led: i, prev };
        calShow();
        return;
      }
      CAL.res[prev] = undefined;
      if (!CAL.fix.includes(prev)) CAL.fix.push(prev);
    }
  }
  CAL.dupe = null;
  CAL.res[i] = v;
  CAL.last = `Last: LED ${i + 1} → ${ansText(v)} ✓`;
  if (CAL.idx < NLED) CAL.idx++;
  CAL.fix = CAL.fix.filter((j) => CAL.res[j] === undefined);
  if (CAL.idx >= NLED && !CAL.fix.length) {
    CAL.phase = "done";
    await calLight(0xff);
  } else {
    await calLight(calCurrent());
  }
  calShow();
}
async function calBack() {
  if (CAL.busy) return;
  if (CAL.idx >= NLED) CAL.idx = NLED - 1;
  else if (CAL.idx > 0) CAL.idx--;
  else return;
  CAL.dupe = null; CAL.last = "";
  await calLight(calCurrent());
  calShow();
}

function calCodes() {
  return CAL.res.map((v) => v === "underglow" ? 0xfe : v && v.includes(",") ? (([r, c]) => r * DATA.cols + c)(v.split(",").map(Number)) : 0xff);
}
function calResultJson() {
  return JSON.stringify({ leds: CAL.res.map((v) => (v === undefined || v === null ? "none" : v)), note: "measured by key press" });
}
async function calReadMap() {
  const out = [];
  for (let off = 0; off < NLED; off += 26) {
    const d = await cmd([0xf6, 0x0a, off]);
    out.push(...Array.from(d.slice(4, 4 + Math.min(26, NLED - off))));
  }
  return out;
}
async function calSave() {
  const codes = calCodes();
  $("calDone").classList.add("hidden");
  calSet("Saving…", "", "", "");
  try {
    for (let off = 0; off < codes.length; off += 26) {
      const part = codes.slice(off, off + 26);
      await cmd([0xf6, 0x0b, off, part.length, ...part]);
    }
    const st = await cmd([0xf6, 0x0c]);
    readStatus(st);
    if (!(st[16] & 2)) throw new Error("the keyboard rejected the map (a key used twice?)");
    const back = await calReadMap();
    const bad = back.findIndex((v, i) => v !== codes[i]);
    if (bad >= 0) throw new Error(`read-back differs at LED ${bad + 1}`);
    $("calText").textContent = "Saved. Checking the right half…";
    let right = 0;
    for (let t = 0; t < 40 && right !== 1; t++) {
      await sleep(100);
      const s2 = await cmd([0xf6, 0x00, 1]);
      readStatus(s2);
      right = s2[21];
    }
    toast(right === 1 ? "LED map saved on both halves" : "Saved, but the right half didn't confirm it", right !== 1);
    await calStartTest(back);
  } catch (e) {
    toast("Not saved: " + e.message, true);
    CAL.phase = "done";
    calShow();
  }
}
function calTestKey(key) {
  const [r, c] = key.split(",").map(Number), led = CAL.map ? CAL.map.indexOf(r * DATA.cols + c) : -1;
  $("calNote").innerHTML = led >= 0
    ? `<b>${escapeHtml(keyName(key))}</b> → LED ${led + 1} (${led < HALF ? "left" : "right"} half) should be green now`
    : `<b>${escapeHtml(keyName(key))}</b> has no LED in the map`;
}
$("calStart").addEventListener("click", calStart);
$("calTest").addEventListener("click", () => calStartTest());
$("calSave").addEventListener("click", calSave);
$("calCopy").addEventListener("click", async () => { $("calJson").select(); try { await navigator.clipboard.writeText($("calJson").value); toast("Copied"); } catch (e) { document.execCommand("copy"); toast("Copied"); } });
$("calRestart").addEventListener("click", calStart);
$("calClose").addEventListener("click", calStop);
$("mapReset").addEventListener("click", async () => {
  if (!confirm("Go back to the built-in LED map (forget the measured one)?")) return;
  try { await cmd([0xf6, 0x0d]); readStatus(await cmd([0xf6, 0x00, 1])); toast("Using the built-in LED map"); render(); }
  catch (e) { toast("Failed: " + e.message, true); }
});

