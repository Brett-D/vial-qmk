// The pet's motion: the same integer maths as pet/pet_motion.h (C/JS parity
// test: harness pet/pet_match.py). Inlined into the page by gen_config.py.
const PETM = (() => {
  const FLOOR = 55, TOP = 18, AIR_TOP = 9, SEG = 2000, TRAVEL = 1400;
  const M = { HAPPY: 1, SAD: 2, TIRED: 4, EXCITED: 8, ASLEEP: 16 }, P = { MIRROR: 1, AIR: 2, MOVING: 4 };
  const FX = { NONE: 0, SNEEZE: 1, EXCLAIM: 2, STAR: 3, FLAME: 4 };
  const SP = { pip: 0, cat: 1, dog: 2, owl: 3, dragon: 4, blob: 5, axolotl: 6 };
  const FR = {}; // frame name -> index (pet_ids.h order)
  const hash = (a) => {
    a = a >>> 0;
    a ^= a >>> 16; a = Math.imul(a, 0x7feb352d);
    a ^= a >>> 15; a = Math.imul(a, 0x846ca68b);
    a ^= a >>> 16;
    return a >>> 0;
  };
  const seg = (seed, k) => hash(((seed * 0x10001) ^ Math.imul(k, 0x9e3779b1)) >>> 0);
  const arc = (p, len, h) => (p >= len ? 0 : Math.floor((4 * h * p * (len - p)) / (len * len)));
  function area(bw, bh, fly, stage) {
    const depth = [12, 10, 8], air = [14, 46, 46], s = stage >= 1 && stage <= 3 ? stage - 1 : 2;
    const g = TOP - 1 + bh, ga = FLOOR - depth[s], m = AIR_TOP - 1 + bh, ma = FLOOR - air[s];
    return { bw, bh, fly, xr: bw < 32 ? 32 - bw : 0, gmin: g > ga ? Math.min(g, FLOOR) : ga, amin: m > ma ? Math.min(m, FLOOR) : ma };
  }
  const moves = (seed, k, stage) => k % 3 === 0 || (seg(seed, k) & 7) < [3, 4, 5][stage >= 1 && stage <= 3 ? stage - 1 : 2];
  const lastMove = (seed, k, stage) => { while (!moves(seed, k, stage)) k--; return k; };
  function point(a, seed, k) {
    const r = seg(seed, k), lo = a.fly && ((r >>> 24) & 3) !== 0 ? a.amin : a.gmin;
    return [((r >>> 8) & 0xff) % (a.xr + 1), lo + (((r >>> 16) & 0xff) % (FLOOR - lo + 1))];
  }
  function pose(a, species, stage, seed, mt, mood) {
    const k = Math.floor(mt / SEG), u = mt % SEG, j = lastMove(seed, k, stage), rj = seg(seed, j), r = seg(seed, k);
    const [ex, ef] = point(a, seed, j), [sx, sf] = j > 0 ? point(a, seed, lastMove(seed, j - 1, stage)) : [ex, ef];
    const ddx = ex - sx, mir = ddx > 0 || (ddx === 0 && ((rj >>> 26) & 1));
    const moving = j === k && u < TRAVEL && (ddx !== 0 || ef !== sf) && !(mood & M.ASLEEP);
    let e = 256;
    if (j === k && u < TRAVEL) { const f = Math.floor((u * 256) / TRAVEL); e = (f * f * (768 - 2 * f)) >> 16; }
    const o = { x: sx + ((ddx * e) >> 8), floor: sf + (((ef - sf) * e) >> 8), dx: 0, dy: 0, fx: FX.NONE, flags: mir ? P.MIRROR : 0, frame: 0, sy: 16 };
    const big = stage >= 3 ? 2 : stage === 2 ? 1 : 0, d = (v, n) => Math.floor(v / n);
    if (mood & M.ASLEEP) { if (o.floor < a.gmin) o.floor = a.gmin; o.frame = FR.sleep; return o; }
    if (moving) {
      o.flags |= P.MOVING;
      if (a.fly) {
        o.flags |= P.AIR;
        o.frame = d(u, 120) & 1 ? FR.fly2 : FR.fly1;
        o.dy = (d(u, 240) & 1) - (sf >= a.gmin && ef >= a.gmin ? arc(u, TRAVEL, 6) : 0);
        return o;
      }
      if (species === SP.pip) { const l = arc(u % 350, 350, 3 + big); o.dy = -l; o.frame = l > 1 ? FR.idle2 : FR.walk; }
      else if (species === SP.blob) { const p = u % 400; o.frame = p < 150 ? FR.walk : FR.idle; o.dy = p < 150 ? 0 : -arc(p - 150, 250, 4); }
      else if (species === SP.axolotl) { o.frame = d(u, 200) & 1 ? FR.walk : FR.idle; o.dy = d(u, 300) & 1 ? -1 : 0; }
      else if (species === SP.dog) { o.frame = d(u, 150) & 1 ? FR.walk : FR.idle; o.dy = d(u, 150) & 1 ? -1 : 0; }
      else if (species === SP.cat) {
        o.frame = d(u, 150) & 1 ? FR.walk : FR.idle;
        if (((rj >>> 27) & 1) && u + 450 >= TRAVEL) { o.frame = FR.happy; o.sy = 18; o.dy = -arc(u + 450 - TRAVEL, 450, 6); }
      } else o.frame = d(u, 150) & 1 ? FR.walk : FR.idle;
      return o;
    }
    const t = u;
    if (a.fly && o.floor < a.gmin) {
      o.flags |= P.AIR; o.frame = d(t, 220) & 1 ? FR.fly2 : FR.fly1; o.dy = d(t, 440) & 1 ? 1 : 0;
      const act = (r >>> 3) & 31;
      if (j === k || (mood & (M.SAD | M.TIRED | M.EXCITED))) return o;
      if (act >= 10 && act <= 12 && (d(t, 700) & 1)) o.flags ^= P.MIRROR;
      if (act === 31 && ((r >>> 28) & 1) === 0 && t >= 300 && t < 1500) {
        if (species === SP.dragon) o.fx = FX.FLAME;
        else { if (d(t, 100) & 1) o.flags ^= P.MIRROR; o.fx = FX.STAR; }
      }
      return o;
    }
    const ph = d(t, 250);
    o.frame = (ph >> 1) & 1 ? FR.idle2 : FR.idle;
    if (mood & M.SAD) { o.frame = FR.sad; o.dy = 1; return o; }
    if (mood & M.TIRED) { o.frame = d(t, 500) & 1 ? FR.blink : FR.tired; o.dy = d(t, 500) & 1 ? 1 : 0; return o; }
    if (mood & M.EXCITED) { o.frame = FR.happy; o.dy = -arc(t % 300, 300, 3); return o; }
    if ((mood & M.HAPPY) && ph % 16 >= 12) o.frame = FR.happy;
    if (ph % 16 === 5) o.frame = FR.blink;
    if (j === k) return o;
    const act = (r >>> 3) & 31;
    if (act >= 10 && act <= 12) { o.frame = FR.look; if (d(t, 700) & 1) o.flags ^= P.MIRROR; }
    else if (act >= 13 && act <= 14) { if (t >= 400 && t < 1300) o.frame = FR.yawn; }
    else if (act >= 15 && act <= 16) { if (t >= 300 && t < 1100) { o.frame = FR.happy; o.sy = 18; } }
    else if (act >= 17 && act <= 19) { if (t >= 200) o.sy = 14; }
    else if (act >= 20 && act <= 21) { if (t >= 200 && t < 1400) { o.frame = FR.walk; o.dx = d(t, 80) & 1; } }
    else if (act >= 22 && act <= 23) {
      if (t >= 400 && t < 700) o.frame = FR.sneeze;
      if (t >= 700 && t < 950) { o.frame = FR.sneeze; o.sy = 14; o.fx = FX.SNEEZE; }
    } else if (act === 31 && ((r >>> 28) & 1) === 0) {
      if (species === SP.dragon) { if (t >= 300 && t < 1500) { o.frame = FR.happy; o.fx = FX.FLAME; } }
      else if (species === SP.owl || species === SP.cat || species === SP.dog) {
        if (t >= 200 && t < 1200) { o.frame = FR.happy; if (d(t, 100) & 1) o.flags ^= P.MIRROR; o.dy = -arc(t - 200, 1000, 6); o.fx = FX.STAR; }
      } else if (t >= 200 && t < 1200) { o.frame = FR.surprise; o.dy = -arc(t > 300 ? t - 300 : 0, 600, 8); o.fx = FX.EXCLAIM; }
    }
    return o;
  }
  // Seed: species + name, as pet_draw.c.
  function seedOf(species, name) {
    let s = (species * 40503 + 1) & 0xffff;
    for (let i = 0; i < 5 && i < name.length; i++) s = (Math.imul(s, 31) + name.charCodeAt(i)) & 0xffff;
    return s;
  }
  const rows = (h, sy) => Math.floor((h * sy + 8) / 16), row = (r, h, out) => Math.floor((r * h) / out);
  return { FLOOR, M, P, FX, FR, hash, seg, area, pose, seedOf, rows, row, arc, init(frames) { frames.forEach((f, i) => (FR[f] = i)); } };
})();
