// ================================================================ backup / restore
// Everything this page can change, as one JSON file: keymap (all layers),
// knobs, lighting (effects, colours, zones, idle), the measured LED map,
// trackpad press-to-click and the wiggler. Restore shows what would change and
// writes it back through the confirmed session (WebHID or the network API).
const BACKUP_FORMAT = "sofle-config-backup", BACKUP_VERSION = 1;
const LED_FIELDS = { enable: "Lights on", base: "Background", fxEffect: "On key press", fxBlend: "Key press over the background", fxColour: "Key-press colour mode",
  rxHue: "Key-press hue", rxSat: "Key-press saturation", fxSpeed: "Key-press speed", fxWidth: "Band width", fxRadius: "Reach", fxFade: "Fade", fxEchoes: "Echo rings", fxDim: "Dim background", fxBright: "Key-press brightness (0 = background's)",
  hue: "Hue", sat: "Saturation", val: "Brightness", curve: "Brightness curve", speed: "Speed", idle: "Turn off after", zone: "LED zones", ugHue: "Underglow hue", ugSat: "Underglow saturation" };
const CURVE_NAMES = ["Balanced", "Linear", "Smooth"];
const TP_FIELDS = { enable: "Press-to-click", threshold: "Press threshold (Z)", release: "Release Z" };

async function makeBackup() {
  await hidLoadLed();
  await hidLoadTp();
  readStatus(await cmd([0xf6, 0x00, 0]));
  const leds = {};
  for (const k of Object.keys(LED_FIELDS)) leds[k] = S.led[k];
  leds.gamma = 1; // v23: brightness values for an output curve, v25b: the one in leds.curve (older backups are converted on restore)
  return {
    format: BACKUP_FORMAT, version: BACKUP_VERSION, created: new Date().toISOString(),
    keyboard: "Keyhive Sofle (brettssofle)", pickerHash: DATA.pickerHash,
    layers: S.ready.layers, rows: DATA.rows, cols: DATA.cols, layerNames: DATA.layerNames,
    keymap: S.keymap.map((l) => l.map((r) => r.slice())),
    encoders: S.enc.map((l) => l.map((e) => e.slice())),
    leds,
    ledMap: S.mapMeasured ? await calReadMap() : null, // null = the built-in map
    trackpad: { enable: S.tp.enable, threshold: S.tp.threshold, release: S.tp.release },
    wiggler: { on: S.wiggle.on, every: S.wiggle.every },
    prefs: S.prefs ? { addrEnter: S.prefs.addrEnter } : undefined, // v26
    pet: S.pet ? { on: S.pet.on, canDie: S.pet.canDie, decay: S.pet.decay, name: S.pet.name, species: S.pet.species, stage: S.pet.stage,
                   ...(S.pet.glowOk ? { big: S.pet.big, glow: S.pet.glow, glowLevel: S.pet.glowLevel } : {}) } : undefined, // not its stats
  };
}
async function backupDownload() {
  try {
    const b = await makeBackup();
    const url = URL.createObjectURL(new Blob([JSON.stringify(b, null, 1)], { type: "application/json" }));
    const a = document.createElement("a");
    a.href = url; a.download = `sofle-backup-${b.created.slice(0, 10)}.json`;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
    toast("Backup downloaded");
  } catch (e) { toast("Backup failed: " + e.message, true); }
  render();
}

// Checks a parsed backup; returns an error message or null.
function backupProblem(b) {
  if (!b || b.format !== BACKUP_FORMAT) return "This isn't a Sofle config backup.";
  if (!(b.version >= 1 && b.version <= BACKUP_VERSION)) return `Backup format ${b.version} is newer than this page (${BACKUP_VERSION}). Use the page from the same or a newer firmware.`;
  if (b.rows !== DATA.rows || b.cols !== DATA.cols) return `The backup is for a ${b.rows}x${b.cols} matrix; this keyboard is ${DATA.rows}x${DATA.cols}.`;
  const kcOk = (v) => Number.isInteger(v) && v >= 0 && v <= 0xffff;
  if (!Array.isArray(b.keymap) || !b.keymap.every((l) => Array.isArray(l) && l.length === b.rows && l.every((r) => Array.isArray(r) && r.length === b.cols && r.every(kcOk))))
    return "The keymap in the backup is damaged.";
  if (b.encoders && !b.encoders.every((l) => l.every((e) => e.length === 2 && e.every(kcOk)))) return "The knob settings in the backup are damaged.";
  if (b.ledMap && !(Array.isArray(b.ledMap) && b.ledMap.length === NLED && b.ledMap.every((v) => Number.isInteger(v) && v >= 0 && v <= 255))) return "The LED map in the backup is damaged.";
  return null;
}

// What restoring would change, per section.
function backupDiff(b) {
  const layers = Math.min(b.layers || b.keymap.length, S.ready.layers), out = [];
  const name = (l) => DATA.layerNames[l] || "layer " + l;
  const keys = [];
  for (let l = 0; l < layers; l++) for (let r = 0; r < DATA.rows; r++) for (let c = 0; c < DATA.cols; c++)
    if (b.keymap[l][r][c] !== S.keymap[l][r][c]) keys.push({ l, r, c, from: S.keymap[l][r][c], to: b.keymap[l][r][c] });
  const perLayer = {};
  keys.forEach((k) => (perLayer[k.l] = (perLayer[k.l] || 0) + 1));
  out.push({ id: "keymap", title: "Keymap", n: keys.length, data: keys,
    lines: Object.keys(perLayer).map((l) => `${name(+l)}: ${perLayer[l]} key${perLayer[l] > 1 ? "s" : ""}`)
      .concat(keys.slice(0, 6).map((k) => `  ${name(k.l)} ${k.r},${k.c}: ${kcName(k.from) || "(none)"} → ${kcName(k.to) || "(none)"}`))
      .concat(layers < (b.layers || 0) ? [`(the backup has ${b.layers} layers; this keyboard has ${S.ready.layers})`] : []) });
  const enc = [];
  if (b.encoders) for (let l = 0; l < Math.min(layers, b.encoders.length); l++) for (let e = 0; e < 2; e++) for (let cw = 0; cw < 2; cw++)
    if (b.encoders[l][e][cw] !== S.enc[l][e][cw]) enc.push({ l, e, cw, from: S.enc[l][e][cw], to: b.encoders[l][e][cw] });
  out.push({ id: "encoders", title: "Knobs", n: enc.length, data: enc,
    lines: enc.slice(0, 6).map((k) => `${name(k.l)} ${k.e ? "right" : "left"} knob ${k.cw ? "turn right" : "turn left"}: ${kcName(k.from) || "(none)"} → ${kcName(k.to) || "(none)"}`) });
  if (b.leds) b.leds = ledsFromOldBackup(b.leds);
  const led = b.leds ? Object.keys(LED_FIELDS).filter((k) => b.leds[k] !== undefined && b.leds[k] !== S.led[k]) : [];
  const fx = (v) => (v ? fxInfo(v).label || "effect " + v : "none");
  const ov = (v) => (v ? (DATA.fxTable.overlays.find((o) => o[0] === v) || [0, 0, "effect " + v])[2] : "none");
  out.push({ id: "leds", title: "Lighting", n: led.length, data: b.leds,
    lines: led.map((k) => `${LED_FIELDS[k]}: ${k === "base" ? fx(S.led[k]) + " → " + fx(b.leds[k]) : k === "fxEffect" ? ov(S.led[k]) + " → " + ov(b.leds[k]) : k === "zone" ? DATA.ledZones[S.led[k]] + " → " + DATA.ledZones[b.leds[k]] : k === "idle" ? DATA.ledIdle[S.led[k]] + " → " + DATA.ledIdle[b.leds[k]] : k === "curve" ? CURVE_NAMES[S.led[k]] + " → " + CURVE_NAMES[b.leds[k]] : S.led[k] + " → " + b.leds[k]}`) });
  const mapNow = S.backupMap || null;
  let mapLines = [], mapN = 0;
  if (b.ledMap === undefined) { /* not in the backup */ }
  else if (!b.ledMap && S.mapMeasured) { mapN = 1; mapLines = ["measured map → built-in map"]; }
  else if (b.ledMap && !S.mapMeasured) { mapN = 1; mapLines = ["built-in map → the measured map from the backup"]; }
  else if (b.ledMap && mapNow) { mapN = b.ledMap.filter((v, i) => v !== mapNow[i]).length; if (mapN) mapLines = [`${mapN} LED${mapN > 1 ? "s" : ""} mapped differently`]; }
  out.push({ id: "ledMap", title: "LED map (calibration)", n: mapN, data: b.ledMap, lines: mapLines });
  const tp = b.trackpad ? Object.keys(TP_FIELDS).filter((k) => b.trackpad[k] !== S.tp[k]) : [];
  out.push({ id: "trackpad", title: "Trackpad", n: tp.length, data: b.trackpad,
    lines: tp.map((k) => `${TP_FIELDS[k]}: ${k === "enable" ? (S.tp[k] ? "on" : "off") + " → " + (b.trackpad[k] ? "on" : "off") : S.tp[k] + " → " + b.trackpad[k]}`) });
  const wg = b.wiggler && (b.wiggler.on !== S.wiggle.on || b.wiggler.every !== S.wiggle.every);
  out.push({ id: "wiggler", title: "Mouse wiggler (until unplugged)", n: wg ? 1 : 0, data: b.wiggler,
    lines: wg ? [`${S.wiggle.on ? "on" : "off"} → ${b.wiggler.on ? "on" : "off"}, every ${($("wgEvery").options[b.wiggler.every] || {}).text || b.wiggler.every}`] : [] });
  const pf = b.prefs && S.prefs && b.prefs.addrEnter !== undefined && b.prefs.addrEnter !== S.prefs.addrEnter;
  if (b.prefs && S.prefs) out.push({ id: "prefs", title: "Network mode", n: pf ? 1 : 0, data: b.prefs, lines: pf ? [`Enter after the address: ${S.prefs.addrEnter ? "on" : "off"} → ${b.prefs.addrEnter ? "on" : "off"}`] : [] });
  if (b.pet && S.pet) {
    const f = ["on", "canDie", "decay", "name", "species", "stage", ...(S.pet.glowOk ? ["big", "glow", "glowLevel"] : [])].filter((k) => b.pet[k] !== undefined && b.pet[k] !== S.pet[k]);
    const lbl = { on: "Pet", canDie: "Can die", decay: "Needs care", name: "Name", species: "Species", stage: "Stage", big: "Big pet", glow: "LED reactions (which)", glowLevel: "LED reaction strength" };
    const val = (k, v) => (k === "decay" ? ["slowly", "normally", "quickly"][v] : k === "species" ? speciesName(v) : k === "stage" ? PET_STAGES[v] : typeof v === "boolean" ? (v ? "on" : "off") : v);
    out.push({ id: "pet", title: "Pet (settings, species, stage)", n: f.length, data: b.pet, lines: f.map((k) => `${lbl[k]}: ${val(k, S.pet[k])} → ${val(k, b.pet[k])}`) });
  }
  return out;
}

let RESTORE = null;
async function restorePick(file) {
  let b;
  try { b = JSON.parse(await file.text()); } catch (e) { toast("Can't read that file: " + e.message, true); return; }
  const bad = backupProblem(b);
  if (bad) { toast(bad, true); return; }
  try {
    await hidLoadLed(); await hidLoadTp();
    S.backupMap = S.mapMeasured ? await calReadMap() : null;
  } catch (e) { toast("Couldn't read the keyboard: " + e.message, true); return; }
  const diff = backupDiff(b), total = diff.reduce((s, d) => s + d.n, 0);
  RESTORE = { b, diff };
  $("restoreInfo").innerHTML = `<b>${escapeHtml(file.name)}</b> — saved ${escapeHtml((b.created || "?").replace("T", " ").slice(0, 16))}` +
    (b.pickerHash !== DATA.pickerHash ? ` <span class="dim">(from another firmware build; keycodes are stored as numbers)</span>` : "") +
    (total ? `<br>${total} change${total > 1 ? "s" : ""}. Untick what you want to keep as it is now.` : "<br>Everything already matches this backup.");
  $("restoreList").innerHTML = diff.map((d) => `<label><input type="checkbox" data-sec="${d.id}" ${d.n ? "checked" : "disabled"}> <b>${d.title}</b> — ${d.n ? d.n + " change" + (d.n > 1 ? "s" : "") : "no change"}` +
    (d.lines.length ? `<div class="chg">${escapeHtml(d.lines.join("\n"))}</div>` : "") + `</label>`).join("");
  $("restoreApply").disabled = !total;
  $("restoreProgress").textContent = "";
  $("restorePanel").classList.remove("hidden");
  $("restorePanel").scrollIntoView({ block: "nearest" });
}

async function restoreApply() {
  if (!RESTORE) return;
  const want = new Set([...$("restoreList").querySelectorAll("input:checked")].map((el) => el.dataset.sec));
  const sec = (id) => RESTORE.diff.find((d) => d.id === id);
  const { b } = RESTORE;
  const ok = async (bytes) => { const d = await cmd(bytes); if (d[0] === 0xff) throw new Error("the keyboard refused (confirm the connection again)"); return d; };
  const prog = (t) => { $("restoreProgress").textContent = t; };
  $("restoreApply").disabled = true;
  const problems = [];
  try {
    if (want.has("keymap")) {
      // Whole 28-byte chunks of the keymap buffer that contain a change.
      const R = DATA.rows, C = DATA.cols, touched = new Set();
      sec("keymap").data.forEach((k) => touched.add(Math.floor(((k.l * R + k.r) * C + k.c) * 2 / 28)));
      let i = 0;
      for (const chunk of [...touched].sort((x, y) => x - y)) {
        prog(`Keymap ${++i}/${touched.size}…`);
        const off = chunk * 28, total = S.ready.layers * R * C * 2, n = Math.min(28, total - off), bytes = [];
        for (let o = off; o < off + n; o += 2) {
          const idx = o / 2, l = Math.floor(idx / (R * C)), r = Math.floor((idx % (R * C)) / C), c = idx % C;
          const kc = l < b.keymap.length ? b.keymap[l][r][c] : S.keymap[l][r][c];
          bytes.push(kc >> 8, kc & 0xff);
        }
        await ok([0x13, off >> 8, off & 0xff, n, ...bytes]);
      }
    }
    if (want.has("encoders")) {
      const list = sec("encoders").data;
      for (let i = 0; i < list.length; i++) {
        prog(`Knobs ${i + 1}/${list.length}…`);
        const k = list[i];
        await setEnc(k.l, k.e, k.cw, k.to);
      }
    }
    if (want.has("leds")) {
      prog("Lighting…");
      Object.assign(S.led, b.leds, { dirty: 1 });
      if (!(await flushLed())) throw new Error("the lighting didn't reach the keyboard");
      await ok([0x09]);
      await ok([0xf6, 0x01, S.led.idle]);
      await ok([0xf6, 0x07]);
      await ok([0xf6, 0x1c, 2]);
    }
    if (want.has("ledMap")) {
      prog("LED map…");
      if (!b.ledMap) await ok([0xf6, 0x0d]);
      else {
        for (let off = 0; off < NLED; off += 26) {
          const part = b.ledMap.slice(off, off + 26);
          await ok([0xf6, 0x0b, off, part.length, ...part]);
        }
        const st = await ok([0xf6, 0x0c]);
        readStatus(st);
        if (!(st[16] & 2)) problems.push("the keyboard rejected the LED map from the backup");
      }
    }
    if (want.has("trackpad")) {
      prog("Trackpad…");
      const t = b.trackpad;
      await ok([0xf5, 0x01, t.enable ? 1 : 0, t.threshold, t.release]);
    }
    if (want.has("pet")) {
      prog("Pet…");
      await petSettings(b.pet);
    }
    if (want.has("prefs")) {
      prog("Network mode…");
      await prefsSet(b.prefs);
    }
    if (want.has("wiggler")) {
      prog("Mouse wiggler…");
      readStatus(await ok([0xf6, 0x16, b.wiggler.on ? 1 : 0, b.wiggler.every]));
    }
    // Read everything back and check it took.
    prog("Checking…");
    const ui = S.ui;
    await hidLoadAll();
    S.ui = { ...S.ui, tab: ui.tab, layer: ui.layer };
    if (S.ui.tab === 3) startPetPoll();
    const after = backupDiff(b);
    for (const d of after) if (want.has(d.id) && d.n && d.id !== "ledMap") problems.push(`${d.title}: ${d.n} not applied` + (d.lines[0] ? ` (${d.lines.find((x) => x.includes("→")) || d.lines[0]})` : ""));
    if (want.has("ledMap") && b.ledMap) {
      const back = await calReadMap();
      const n = b.ledMap.filter((v, i) => v !== back[i]).length;
      if (n) problems.push(`LED map: ${n} LEDs differ after saving`);
    }
  } catch (e) {
    problems.push(e.message);
  }
  $("restoreApply").disabled = false;
  if (problems.length) {
    prog("");
    toast("Restore incomplete — see the log", true);
    problems.forEach((p) => log("restore: " + p));
    $("restoreInfo").innerHTML += `<div class="err">Not everything was restored:\n${escapeHtml(problems.join("\n"))}` +
      (problems.some((p) => /Bootloader/.test(p)) ? "\n(The Bootloader key can only be set after unlocking in Vial.)" : "") + `</div>`;
  } else {
    $("restorePanel").classList.add("hidden");
    RESTORE = null;
    toast("Restored and checked");
  }
  render();
}
$("backupBtn").addEventListener("click", backupDownload);
$("restoreBtn").addEventListener("click", () => { $("restoreFile").value = ""; $("restoreFile").click(); });
$("restoreFile").addEventListener("change", () => { const f = $("restoreFile").files[0]; if (f) restorePick(f); });
$("restoreApply").addEventListener("click", restoreApply);
$("restoreCancel").addEventListener("click", () => { $("restorePanel").classList.add("hidden"); RESTORE = null; });

// Advanced builder options.
$("advLayer").innerHTML = DATA.layerNames.map((n, i) => `<option value="${i}">layer ${i} ${n}</option>`).join("");
$("advBase").innerHTML = DATA.picker.filter(([v]) => v > 1 && v < 0x100).map(([v, l]) => `<option value="${v}">${escapeHtml(l)}</option>`).join("");
renderAdv();

// Test hook: feed a string of KeyboardEvent.code values separated by spaces.
window.sofleFeed = (codes) => codes.split(/\s+/).filter(Boolean).forEach(feedCode);
if (!document.hasFocus()) document.body.classList.add("blurred");
if (NET) {
  $("whereFrom").textContent = "Served by the keyboard (" + location.host + ").";
  $("hidHow").innerHTML = "Confirm on the keyboard: its screen shows <b>CONF?</b> — press the <b>right knob</b> (or <kbd>Enter</kbd>). If you missed it, click <b>Connect</b>.";
  $("hidVial").textContent = "Works in any browser. Back to the plain keyboard: Config key (or hold both knobs 2 s).";
}
colourPickers();
render();
if (NET) connect(); // the keyboard's own page: ask for the session right away
setInterval(() => { if (S.mode === "bridge" && S.state === "ready") scheduleRender(); }, 1000);
