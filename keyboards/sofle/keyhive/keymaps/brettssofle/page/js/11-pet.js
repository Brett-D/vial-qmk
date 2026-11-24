// ================================================================ the OLED pet (pet.c)
// 0xF6 0x18 get, 0x19 care (1 feed, 2 play, 3 sleep/wake, 4 clean, 5 new egg
// [species]), 0x1A settings (flags 1 on / 2 can die, decay speed, name),
// 0x1B species + stage (restoring a backup).
const PET = { poll: null, busy: false, pick: 0 };
const PET_MOODS = ["happy", "fine", "hungry", "sad", "tired", "dirty", "asleep", "a ghost", "switched off", "still an egg"];
const PET_STAGES = ["egg", "baby", "child", "adult"];
function petParse(d) {
  if (d[0] !== 0xf6 || d[2] !== 0x9e) return null;
  let name = "";
  for (let i = 14; i < 24 && d[i]; i++) name += String.fromCharCode(d[i]);
  return { on: !!(d[3] & 1), canDie: !!(d[3] & 2), asleep: !!(d[3] & 4), dead: !!(d[3] & 8), food: d[4], joy: d[5], energy: d[6], clean: d[7],
           decay: d[8], age: (d[9] | (d[10] << 8) | (d[11] << 16) | (d[12] << 24)) >>> 0, mood: d[13], name, result: d[26],
           species: d[28] < 7 ? d[28] : 0, stage: d[29] < 4 ? d[29] : 3, progress: d[30] };
}
async function petLoad() { const p = petParse(await cmd([0xf6, 0x18])); S.pet = p; return p; }
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
    S.pet = p;
    const done = ["", "Fed", "Played", p.asleep ? "Good night" : "Woke up", "Cleaned", `A new ${speciesName(p.species).toLowerCase()} egg`][a];
    toast(p.result === 1 ? done : p.result === 2 ? (p.stage === 0 ? "It's still an egg — wait for it to hatch" : `${p.name || "The pet"} doesn't want to right now`) : "Refused — confirm the connection on the keyboard again", p.result !== 1);
  } catch (e) { toast("Failed: " + e.message, true); }
  render();
}
async function petSettings(over) {
  const s = Object.assign({ on: $("petOn").checked, canDie: $("petDie").checked, decay: +$("petDecay").value, name: $("petName").value }, over || {});
  const name = (s.name || "").replace(/[^\x20-\x7e]/g, "").slice(0, 9) || "Pip";
  const b = [0xf6, 0x1a, (s.on ? 1 : 0) | (s.canDie ? 2 : 0), s.decay];
  for (let i = 0; i < 10; i++) b.push(i < name.length ? name.charCodeAt(i) : 0);
  let p = petParse(await cmd(b));
  if (!p || p.result !== 1) throw new Error("refused");
  if (s.species !== undefined && s.stage !== undefined && (s.species !== p.species || s.stage !== p.stage)) {
    p = petParse(await cmd([0xf6, 0x1b, s.species, s.stage]));
    if (!p || p.result !== 1) throw new Error("refused");
  }
  S.pet = p;
  return p;
}
async function petSettingsInput() {
  try { await petSettings(); toast("Pet settings saved"); } catch (e) { toast("Not saved: " + e.message, true); }
  render();
}
// The same frame choice as pet_draw.c.
function petFrame(p, t) {
  const ph = (t / 250) | 0;
  let f = ((ph >> 1) & 1) ? "idle2" : "idle";
  if (p.dead) f = "dead";
  else if (p.asleep) f = "sleep";
  else if (p.food < 20 || p.joy < 20) f = "sad";
  else if (p.energy < 20) f = "tired";
  else if (p.mood === 0 && ph % 16 >= 12) f = "happy";
  if (!p.dead && !p.asleep && ph % 16 === 5) f = "blink";
  return f;
}
// Pre-rendered frame (gen_pet_art.py): [width, hex row, ...].
function petBitmap(species, stage, frame) {
  const A = DATA.petArt, sp = A.species[species] || "pip";
  return stage === 0 ? A.egg[sp] : A.frame[`${sp}/${A.stages[stage]}/${frame}`];
}
function drawBitmap(g, bm, ox, oy, S4) {
  const w = bm[0];
  for (let y = 1; y < bm.length; y++) {
    const v = parseInt(bm[y], 16);
    for (let x = 0; x < w; x++) if ((v >> (w - 1 - x)) & 1) g.fillRect((ox + x) * S4, (oy + y - 1) * S4, S4 - 1, S4 - 1);
  }
}
function drawPet(p) {
  const cv = $("petCanvas"), g = cv.getContext("2d"), t = Date.now(), A = DATA.petArt, S4 = 3;
  g.fillStyle = "#0a0a1e"; g.fillRect(0, 0, cv.width, cv.height);
  if (!p.on) return;
  g.fillStyle = "#8cdcff";
  const bm = petBitmap(p.species, p.stage, petFrame(p, t));
  const wob = p.stage === 0 && ((t / 250) | 0) % 12 < 2 ? (((t / 250) | 0) & 1 ? 1 : -1) : 0;
  const dy = p.dead ? ((((t / 250) | 0) % 4 < 2) ? -1 : 1) : 0;
  drawBitmap(g, bm, ((40 - bm[0]) >> 1) + wob, 38 - (bm.length - 1) + dy, S4); // stands on y 38 of 40
  const icon = (name, ox, oy) => A.icons[name].forEach((row, y) => { for (let x = 0; x < 8; x++) if (row[x] === "#") g.fillRect((ox + x) * S4, (oy + y) * S4, S4 - 1, S4 - 1); });
  if (p.asleep) icon("z", 31, 2);
  if (!p.dead && p.stage && p.clean < 40) icon("poop", 31, 30);
}
function renderPetPicker() {
  setHtml($("petPick"), DATA.petArt.species.map((sp, i) => `<button type="button" class="${i === PET.pick ? "on" : ""}" data-sp="${i}"><canvas width="96" height="96" data-sp="${i}"></canvas><span>${speciesName(i)}</span></button>`).join(""));
  $("petPick").querySelectorAll("canvas").forEach((cv) => {
    const g = cv.getContext("2d"), i = +cv.dataset.sp, bm = petBitmap(i, 3, "idle");
    g.fillStyle = "#0a0a1e"; g.fillRect(0, 0, 96, 96); g.fillStyle = "#8cdcff";
    drawBitmap(g, bm, (32 - bm[0]) >> 1, 30 - (bm.length - 1), 3);
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
  $("petNew").textContent = p.dead ? "Hatch a new egg…" : "New pet…";
  drawPet(p);
}
$("petCare").addEventListener("click", (e) => { const b = e.target.closest("[data-act]"); if (b) petAct(+b.dataset.act); });
["petOn", "petDie", "petDecay", "petName"].forEach((id) => $(id).addEventListener("change", petSettingsInput));
$("petNew").addEventListener("click", () => { PET.pick = S.pet ? S.pet.species : 0; renderPetPicker(); $("petPicker").classList.remove("hidden"); });
$("petPick").addEventListener("click", (e) => { const b = e.target.closest("[data-sp]"); if (b) { PET.pick = +b.dataset.sp; renderPetPicker(); } });
$("petPickCancel").addEventListener("click", () => $("petPicker").classList.add("hidden"));
$("petHatch").addEventListener("click", async () => {
  const p = S.pet;
  if (p && !p.dead && p.stage > 0 && !confirm(`Start over with a new ${speciesName(PET.pick).toLowerCase()} egg? ${p.name || "Your pet"} the ${speciesName(p.species).toLowerCase()} will be gone (the name and settings stay).`)) return;
  $("petPicker").classList.add("hidden");
  await petAct(5, PET.pick);
});
setInterval(() => { if (S.mode === "hid" && S.ui && S.ui.tab === 3 && S.pet) drawPet(S.pet); }, 250);

