// ================================================================ the OLED pet (pet.c)
// 0xF6 0x18 get, 0x19 care (1 feed, 2 play, 3 sleep/wake, 4 clean, 5 new egg
// [species]), 0x1A settings (flags 1 on / 2 can die, decay speed, name),
// 0x1B species + stage (restoring a backup), 0x1D (v26) LED reactions + big
// pet: 0 get, 1 set (flags, strength, big), 2 test (reaction).
const PET = { poll: null, busy: false, pick: 0 };
const PET_MOODS = ["happy", "fine", "hungry", "sad", "tired", "dirty", "asleep", "a ghost", "switched off", "still an egg"];
const PET_STAGES = ["egg", "baby", "child", "adult"];
function petParse(d) {
  if (d[0] !== 0xf6 || d[2] !== 0x9e) return null;
  let name = "";
  for (let i = 14; i < 24 && d[i]; i++) name += String.fromCharCode(d[i]);
  return { on: !!(d[3] & 1), canDie: !!(d[3] & 2), asleep: !!(d[3] & 4), dead: !!(d[3] & 8), big: !!(d[3] & 16), food: d[4], joy: d[5], energy: d[6], clean: d[7],
           decay: d[8], age: (d[9] | (d[10] << 8) | (d[11] << 16) | (d[12] << 24)) >>> 0, mood: d[13], name, result: d[26],
           species: d[28] < 7 ? d[28] : 0, stage: d[29] < 4 ? d[29] : 3, progress: d[30], anim: d[25] };
}
// The LED reactions (pet.c G_*, led_fx_core.h fx_pet): [fx_pet, G_* bit, name, what].
const PET_GLOWS = [[1, 4, "Eating", "warm pulses from the screen"], [2, 8, "Playing", "a rainbow ball hopping across the keys"],
  [3, 16, "Happy", "a rainbow sweep (also cleaning and growing up)"], [4, 32, "Hungry, sad or dirty", "slow blue breathing on the underglow, at most every 4 min"],
  [5, 64, "Sleeping", "a very dim slow pulse (every 2 min while asleep)"], [6, 128, "Hatching", "a starburst from the screen"]];
const G_ON = 1, G_PAGE = 2;
async function petGlowLoad(p) {
  const d = await cmd([0xf6, 0x1d, 0]);
  if (d[0] !== 0xf6 || d[2] !== 0x9f) { p.glowOk = false; return p; } // older firmware
  return Object.assign(p, { glowOk: true, glow: d[3], glowLevel: d[4], big: !!d[5] });
}
async function petLoad() {
  const p = petParse(await cmd([0xf6, 0x18]));
  if (p) await petGlowLoad(p);
  if (p && PV2.seenAnim >= 0 && p.anim !== PV2.seenAnim && p.anim) petAnimStart(p.anim); // done on the keyboard
  if (p) PV2.seenAnim = p.anim;
  S.pet = p;
  return p;
}
function startPetPoll() {
  if (PET.poll) return;
  PET.poll = setInterval(async () => {
    if (S.mode !== "hid" || !S.ui || S.ui.tab !== 3) return stopPetPoll();
    if (PET.busy) return;
    PET.busy = true;
    try { await petLoad(); scheduleRender(); } catch (e) { /* skip */ }
    PET.busy = false;
  }, 1000);
}
function stopPetPoll() { clearInterval(PET.poll); PET.poll = null; }
const speciesName = (i) => DATA.petArt.speciesNames[i] || "Pip";
async function petAct(a, species) {
  try {
    const p = petParse(await cmd(species === undefined ? [0xf6, 0x19, a] : [0xf6, 0x19, a, species]));
    if (!p) throw new Error("this firmware has no pet");
    if (S.pet && S.pet.glowOk) Object.assign(p, { glowOk: true, glow: S.pet.glow, glowLevel: S.pet.glowLevel }); // 0x19's reply hasn't them
    S.pet = p;
    if (p.result === 1 && p.anim) petAnimStart(p.anim, a === 5);
    const done = ["", "Fed", "Played", p.asleep ? "Good night" : "Woke up", "Cleaned", `A new ${speciesName(p.species).toLowerCase()} egg`][a];
    toast(p.result === 1 ? done : p.result === 2 ? (p.stage === 0 ? "It's still an egg — wait for it to hatch" : `${p.name || "The pet"} doesn't want to right now`) : "Refused — confirm the connection on the keyboard again", p.result !== 1);
  } catch (e) { toast("Failed: " + e.message, true); }
  render();
}
async function petSettings(over) {
  const s = Object.assign({ on: $("petOn").checked, canDie: $("petDie").checked, decay: +$("petDecay").value, name: $("petName").value }, over || {});
  if (!over && S.pet && S.pet.glowOk) { // the Pet tab's own controls (a restore brings its values, or none)
    const g = PET_GLOWS.reduce((m, [, bit]) => { const c = $("pgList").querySelector(`[data-g="${bit}"]`); return m | (c && c.checked ? bit : 0); }, 0);
    Object.assign(s, { big: $("petBig").checked, glow: ($("pgOn").checked ? G_ON : 0) | (+$("pgPage").value ? G_PAGE : 0) | g, glowLevel: +$("pgLevel").value });
  }
  const name = (s.name || "").replace(/[^\x20-\x7e]/g, "").slice(0, 9) || "Pip";
  const b = [0xf6, 0x1a, (s.on ? 1 : 0) | (s.canDie ? 2 : 0), s.decay];
  for (let i = 0; i < 10; i++) b.push(i < name.length ? name.charCodeAt(i) : 0);
  let p = petParse(await cmd(b));
  if (!p || p.result !== 1) throw new Error("refused");
  if (s.species !== undefined && s.stage !== undefined && (s.species !== p.species || s.stage !== p.stage)) {
    p = petParse(await cmd([0xf6, 0x1b, s.species, s.stage]));
    if (!p || p.result !== 1) throw new Error("refused");
  }
  await petGlowLoad(p);
  if (p.glowOk && s.glow !== undefined && (s.glow !== p.glow || s.glowLevel !== p.glowLevel || !!s.big !== p.big)) {
    const d = await cmd([0xf6, 0x1d, 1, s.glow & 255, Math.min(100, Math.max(1, s.glowLevel | 0)), s.big ? 1 : 0]);
    if (d[2] !== 0x9f || d[6] !== 1) throw new Error("refused");
    Object.assign(p, { glow: d[3], glowLevel: d[4], big: !!d[5] });
  }
  S.pet = p;
  return p;
}
async function petSettingsInput() {
  try { await petSettings(); toast("Pet settings saved"); } catch (e) { toast("Not saved: " + e.message, true); }
  render();
}
// The live view: the OLED's play area (rows 8-57; big pet: 8-80, as with the
// menu closed), the same motion and effects as pet_draw.c (pet_core.js =
// pet_motion.h). Its own clock, so not frame-for-frame.
const PV2 = { mt4: 0, last: 0, x: null, floor: 0, anim: 0, animT: 0, seenAnim: -1 };
// gen_pet_art.py's "width:hexrows" -> [width, row, ...] (bit width-1 = leftmost pixel).
const PET_BM = [];
function petArt(i) {
  if (!PET_BM[i]) {
    const [w, hex] = DATA.petArt.art[i].split(":"), n = (+w + 3) >> 2, bm = [+w];
    for (let k = 0; k < hex.length; k += n) bm.push(parseInt(hex.substr(k, n), 16));
    PET_BM[i] = bm;
  }
  return PET_BM[i];
}
function petBitmap(species, stage, frame) {
  const A = DATA.petArt, sp = A.species[species] || "pip";
  return petArt(stage === 0 ? A.egg[sp] : A.ix[`${sp}/${A.stages[stage]}`][A.frames.indexOf(frame)]);
}
function drawBitmap(g, bm, ox, oy, S4, mirror, sy = 16) { // oy: the top row; sy/16 as tall (pet_draw.c sprite)
  const w = bm[0], h = bm.length - 1, out = PETM.rows(h, sy);
  for (let y = 0; y < out; y++) {
    const v = bm[1 + PETM.row(y, h, out)];
    for (let x = 0; x < w; x++) if ((v >> (w - 1 - x)) & 1) g.fillRect((ox + (mirror ? w - 1 - x : x)) * S4, (oy + y) * S4, S4 - 1, S4 - 1);
  }
}
function petAnimStart(type, newEgg) {
  PV2.anim = type; PV2.animT = Date.now();
  // The LED reaction the keyboard plays with it (pet.c glow), in the preview.
  const fx = { 1: 1, 2: 2, 5: 3, 3: 5, 7: 6, 8: newEgg ? 0 : 7 }[type], p = S.pet;
  if (fx && p && p.glowOk && p.glow & G_ON && p.glow & PET_GLOWS[fx === 7 ? 2 : fx - 1][1]) PV.pet = { type: fx, born: performance.now(), level: p.glowLevel };
}
async function petGlowTest(type) {
  PV.pet = { type, born: performance.now(), level: S.pet && S.pet.glowLevel ? S.pet.glowLevel : 70 };
  try {
    const d = await cmd([0xf6, 0x1d, 2, type]);
    if (d[6] === 2) toast("Not now: the keyboard is busy with LED calibration, typed delivery or config mode", true);
    else if (d[6] !== 1) toast("Refused — confirm the connection on the keyboard again", true);
  } catch (e) { toast("Failed: " + e.message, true); }
}
function drawPet(p) {
  const cv = $("petCanvas"), now = Date.now(), A = DATA.petArt, S4 = 3, Y0 = 8;
  const FL = p.big ? PETM.FLOOR_BIG : PETM.FLOOR, H = FL - Y0 + 3; // v26 big pet: the meadow goes down to 78
  if (cv.height !== H * S4) cv.height = H * S4;
  const g = cv.getContext("2d");
  if (!PETM.FR.idle && PETM.FR.idle !== 0) PETM.init(A.frames);
  g.fillStyle = "#0a0a1e"; g.fillRect(0, 0, cv.width, cv.height);
  if (!p.on) return;
  g.fillStyle = "#8cdcff";
  const px = (x, y) => { if (x >= 0 && x < 32 && y >= Y0 && y < Y0 + H) g.fillRect(x * S4, (y - Y0) * S4, S4 - 1, S4 - 1); };
  const icon = (name, ox, oy, cols = 0xff) => A.icons[name].forEach((row, y) => { for (let x = 0; x < 8; x++) if (row[x] === "#" && (cols & (0x80 >> x))) px(ox + x, oy + y); });
  const clear = (x0, y0, x1, y1) => { g.fillStyle = "#0a0a1e"; g.fillRect(x0 * S4, (y0 - Y0) * S4, (x1 - x0 + 1) * S4, (y1 - y0 + 1) * S4); g.fillStyle = "#8cdcff"; };
  const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v), tri = (t, per, span) => { const q = t % per, h = per >> 1; return Math.floor(((q < h ? q : per - q) * span) / h); };
  const ANIM = { EAT: 1, PLAY: 2, SLEEP: 3, WAKE: 4, CLEAN: 5, NO: 6, HATCH: 7, GROW: 8 };
  const sp = p.species, st = p.stage, spName = A.species[sp] || "pip", egg = st === 0, ph = Math.floor(now / 250);
  const animMs = PV2.anim === ANIM.GROW || PV2.anim === ANIM.HATCH ? 3000 : PV2.anim === ANIM.PLAY ? 3000 : 2000, el = now - PV2.animT;
  const animating = PV2.anim && el < animMs, sad = p.food < 20 || p.joy < 20, tired = p.energy < 20, M = PETM.M;
  const mood = (p.mood === 0 ? M.HAPPY : 0) | (sad ? M.SAD : 0) | (tired ? M.TIRED : 0) | (p.asleep ? M.ASLEEP : 0);
  const speed = sad || tired ? 2 : st === 1 ? 3 : 4;
  const dt = PV2.last ? Math.min(250, now - PV2.last) : 0;
  PV2.last = now;
  if (!(egg || p.dead || p.asleep || animating)) PV2.mt4 += dt * speed;
  const bx = egg ? [16, 18] : A.box[`${spName}/${A.stages[st]}`];
  const area = PETM.area(bx[0], bx[1], !egg && A.flyers.includes(`${spName}/${A.stages[st]}`), st, FL);
  const P = egg ? { x: 8, floor: FL, dx: 0, dy: 0, frame: 0, flags: 0, fx: 0 } : PETM.pose(area, sp, st, PETM.seedOf(sp, p.name || ""), Math.floor(PV2.mt4 / 4), mood);
  let wantX = P.x, mir = !!(P.flags & PETM.P.MIRROR), f = A.frames[P.frame], dx = P.dx, dy = P.dy, sy = P.sy;
  if (p.dead) { f = "dead"; dy = ph % 4 < 2 ? -1 : 1; }
  const toy = A.play[spName];
  if (animating && PV2.anim === ANIM.PLAY && (toy === "ball" || toy === "yarn")) {
    const per = toy === "ball" ? 1500 : 2000;
    wantX = clamp(tri(el, per, 24) + 4 - (bx[0] >> 1), 0, area.xr);
    mir = el % per < per / 2;
  }
  if (PV2.x === null) { PV2.x = wantX; PV2.floor = P.floor; }
  else { PV2.x += clamp(wantX - PV2.x, -4, 4); PV2.floor += clamp(P.floor - PV2.floor, -4, 4); }
  let showEgg = egg;
  if (animating) {
    f = p.asleep ? "sleep" : p.dead ? "dead" : "happy"; dx = 0; dy = 0; sy = 16;
    if (PV2.anim === ANIM.EAT) f = Math.floor(el / 200) & 1 ? "happy" : "eat";
    if (PV2.anim === ANIM.PLAY) {
      if (toy === "ball") dy = -PETM.arc(el % 400, 400, 3);
      if (toy === "yarn") { dy = -PETM.arc(el % 600, 600, 5); sy = Math.floor(el / 300) & 1 ? 18 : 16; }
      if (toy === "feather") f = Math.floor(el / 120) & 1 ? "fly2" : "fly1";
    }
    if (PV2.anim === ANIM.NO && !p.asleep && !p.dead && !egg) { f = "sad"; dx = ph & 1 ? 1 : -1; }
    if (PV2.anim === ANIM.HATCH && el < 1800) { showEgg = true; dx = ph & 1 ? 1 : -1; }
  }
  if (egg && !animating && ph % 12 < 2) dx = ph & 1 ? 1 : -1;
  if (showEgg) sy = 16;
  const bm = petBitmap(sp, showEgg ? 0 : st, f), fw = bm[0], fh = PETM.rows(bm.length - 1, sy);
  const left = showEgg ? ((32 - fw) >> 1) + dx : PV2.x + ((bx[0] - fw) >> 1) + dx, bot = (showEgg ? FL : PV2.floor) + dy;
  const top = bot + 1 - fh, cx = left + (fw >> 1);
  let foodAbove = false;
  if (animating && PV2.anim === ANIM.EAT && !showEgg) { // turn to where the food fits; no room either side: above (pet_draw.c)
    const roomL = left >= 8, roomR = left + fw <= 24;
    if (mir ? !roomR : !roomL) { if (mir ? roomL : roomR) mir = !mir; else foodAbove = true; }
  }
  const fy = clamp(top - 9, 9, FL - 7), side = mir ? left + fw : left - 8;
  const moodIcons = ["heart", "smile", "apple", "tear", "bolt", "poop", "moon", "no", "smile", "egg"];
  if (!animating && top >= 17) icon(moodIcons[p.mood] || "smile", 12, 9);
  drawBitmap(g, bm, left, top - Y0, S4, mir && !showEgg, sy);
  if (animating) {
    if (PV2.anim === ANIM.EAT) {
      const b = Math.floor((el * 9) / 2000), keep = b >= 8 ? 0 : mir && !foodAbove ? 0xff >> b : (0xff << b) & 0xff;
      const ix = foodAbove ? cx - 4 : side, iy = foodAbove ? clamp(top - 9, 9, FL - 7) : clamp(top + Math.floor(fh / 3), 9, FL - 7);
      clear(ix, iy, ix + 7, iy + 7); icon(A.food[spName], ix, iy, keep);
    } else if (PV2.anim === ANIM.PLAY) {
      if (toy === "ball" || toy === "yarn") { const tx = tri(el, toy === "ball" ? 1500 : 2000, 24), ty = toy === "ball" ? FL - 8 - PETM.arc(el % 500, 500, 14) : FL - 7; clear(tx, ty, tx + 7, ty + 7); icon(toy, tx, ty); }
      else if (toy === "feather") icon(toy, 4 + tri(el, 1200, 18), 9 + Math.floor((el * (FL - 19)) / 3000));
      else if (toy === "flame") icon(toy, cx - 4 + (Math.floor(el / 200) & 1 ? 3 : -3), clamp(fy - (Math.floor(el / 100) % 4) * 2, 9, FL - 7));
      else for (let k = 0; k < 2; k++) { const q = (el + k * 500) % 1000; icon(toy, cx - 4 + (k ? 7 : -7) + (Math.floor(q / 250) & 1), clamp(top - 6 - Math.floor((q * 20) / 1000), 9, FL - 5)); }
    } else if (PV2.anim === ANIM.CLEAN) {
      const sx = -8 + Math.floor(((el % 1000) * 40) / 1000), sy = el < 1000 ? top : top + (fh >> 1);
      icon("sparkle", sx, sy); icon("sparkle", sx - 14, sy + 6);
    } else if (PV2.anim === ANIM.WAKE || (PV2.anim === ANIM.HATCH && el >= 1800)) { icon("sparkle", ph & 1 ? 0 : 24, fy); if (PV2.anim === ANIM.WAKE) icon("sparkle", ph & 1 ? 24 : 0, fy + 10); }
    else if (PV2.anim === ANIM.NO) icon("no", cx - 4, fy);
    else if (PV2.anim === ANIM.GROW) { icon("star", ph & 1 ? 0 : 24, ph & 1 ? fy : fy + 6); icon("sparkle", ph & 1 ? 24 : 0, ph & 1 ? fy + 8 : fy); }
  } else if (!p.dead && !egg) {
    const X = PETM.FX;
    if (P.fx === X.SNEEZE) for (let k = 0; k < 3; k++) px(mir ? left + fw + 1 + k * 2 : left - 2 - k * 2, top + (fh >> 1) - 1 + (k & 1) * 2);
    else if (P.fx === X.EXCLAIM) { for (let y = fy + 1; y < fy + 6; y++) px(cx, y); px(cx, fy + 7); }
    else if (P.fx === X.STAR) icon("star", cx - 4, fy);
    else if (P.fx === X.FLAME) { icon("flame", cx - 4, fy); icon("flame", cx - 4 + (Math.floor(now / 150) & 1 ? 5 : -5), clamp(fy - 7, 9, FL - 7)); }
    if (spName === "dragon" && f === "happy" && !P.fx && ph & 1) icon("flame", cx - 4, fy);
  }
  if (p.asleep && !(animating && PV2.anim === ANIM.NO)) icon("z", clamp(left + fw - 4, 0, 24), clamp(top - 6 - (ph % 3) * 3, 9, FL - 7));
  if (!p.dead && st && p.clean < 40) { clear(23, FL - 8, 31, FL + 1); icon("poop", 24, FL - 7); }
}
function renderPetPicker() {
  setHtml($("petPick"), DATA.petArt.species.map((sp, i) => `<button type="button" class="${i === PET.pick ? "on" : ""}" data-sp="${i}"><canvas width="120" height="126" data-sp="${i}"></canvas><span>${speciesName(i)}</span></button>`).join(""));
  $("petPick").querySelectorAll("canvas").forEach((cv) => {
    const g = cv.getContext("2d"), i = +cv.dataset.sp, bm = petBitmap(i, 3, "idle");
    g.fillStyle = "#0a0a1e"; g.fillRect(0, 0, 120, 126); g.fillStyle = "#8cdcff";
    drawBitmap(g, bm, (40 - bm[0]) >> 1, 40 - (bm.length - 1), 3);
  });
}
function renderPet() {
  const p = S.pet;
  if (!p) return;
  const kind = speciesName(p.species).toLowerCase();
  $("petTitle").textContent = `${p.name || "Pip"} the ${kind}`;
  const bar = (label, v) => `<div class="row"><span>${label}</span><div class="pbar"><i style="width:${v}%"></i></div><span class="v">${v}</span></div>`;
  setHtml($("petStats"), bar("Food", p.food) + bar("Fun", p.joy) + bar("Energy", p.energy) + bar("Clean", p.clean));
  const d = Math.floor(p.age / 1440), h = Math.floor((p.age % 1440) / 60), m = p.age % 60;
  const stage = p.stage < 3 ? `${PET_STAGES[p.stage]} (${p.progress}% of the way to ${PET_STAGES[p.stage + 1]})` : "adult";
  $("petMood").innerHTML = p.on ? `${escapeHtml(p.name || "Pip")} is <b>${PET_MOODS[p.mood] || "?"}</b> · ${stage} · age ${d ? d + " d " : ""}${d || h ? h + " h" : m + " min"} (counted while the keyboard is on)` : "The pet is switched off (its OLED page is skipped).";
  $("petSleepBtn").textContent = p.asleep ? "Wake up" : "Sleep";
  $("petCare").querySelectorAll("button").forEach((b) => (b.disabled = !p.on || p.dead || p.stage === 0));
  if (document.activeElement !== $("petOn")) $("petOn").checked = p.on;
  if (document.activeElement !== $("petDie")) $("petDie").checked = p.canDie;
  if (document.activeElement !== $("petDecay")) $("petDecay").value = p.decay;
  if (document.activeElement !== $("petName")) $("petName").value = p.name;
  document.querySelectorAll("#petView .pgOnly").forEach((e) => e.classList.toggle("hidden", !p.glowOk));
  if (p.glowOk) {
    if (!$("pgList").children.length) setHtml($("pgList"), PET_GLOWS.map(([t, bit, name, what]) => `<div class="row"><span>${name}</span><label><input type="checkbox" data-g="${bit}"> ${what}</label><button type="button" class="small" data-test="${t}">Test</button></div>`).join(""));
    if (document.activeElement !== $("petBig")) $("petBig").checked = p.big;
    if (document.activeElement !== $("pgOn")) $("pgOn").checked = !!(p.glow & G_ON);
    setIfIdle("pgLevel", p.glowLevel); setIfIdle("pgPage", p.glow & G_PAGE ? 1 : 0);
    $("pgLevelV").textContent = p.glowLevel + "%";
    $("pgList").querySelectorAll("[data-g]").forEach((c) => { if (document.activeElement !== c) c.checked = !!(p.glow & +c.dataset.g); });
    document.querySelectorAll("#petView .pgRow").forEach((e) => e.classList.toggle("off", !(p.glow & G_ON)));
  }
  $("petNew").textContent = p.dead ? "Hatch a new egg…" : "New pet…";
  drawPet(p);
}
$("petCare").addEventListener("click", (e) => { const b = e.target.closest("[data-act]"); if (b) petAct(+b.dataset.act); });
["petOn", "petDie", "petDecay", "petName", "petBig", "pgOn", "pgLevel", "pgPage"].forEach((id) => $(id).addEventListener("change", petSettingsInput));
$("pgList").addEventListener("change", (e) => { if (e.target.matches("[data-g]")) petSettingsInput(); });
$("pgList").addEventListener("click", (e) => { const b = e.target.closest("[data-test]"); if (b) petGlowTest(+b.dataset.test); });
$("pgLevel").addEventListener("input", () => { $("pgLevelV").textContent = $("pgLevel").value + "%"; });
$("petNew").addEventListener("click", () => { PET.pick = S.pet ? S.pet.species : 0; renderPetPicker(); $("petPicker").classList.remove("hidden"); });
$("petPick").addEventListener("click", (e) => { const b = e.target.closest("[data-sp]"); if (b) { PET.pick = +b.dataset.sp; renderPetPicker(); } });
$("petPickCancel").addEventListener("click", () => $("petPicker").classList.add("hidden"));
$("petHatch").addEventListener("click", async () => {
  const p = S.pet;
  if (p && !p.dead && p.stage > 0 && !confirm(`Start over with a new ${speciesName(PET.pick).toLowerCase()} egg? ${p.name || "Your pet"} the ${speciesName(p.species).toLowerCase()} will be gone (the name and settings stay).`)) return;
  $("petPicker").classList.add("hidden");
  await petAct(5, PET.pick);
});
setInterval(() => { if (S.mode === "hid" && S.ui && S.ui.tab === 3 && S.pet) drawPet(S.pet); }, 100);

