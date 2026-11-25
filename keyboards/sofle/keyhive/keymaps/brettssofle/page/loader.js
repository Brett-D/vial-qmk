// Stage 2 of the typed page delivery. deliver.c types it into the page that
// bootstrap.html opened (which checks its hash and runs it). gen_page_blob.py
// drops these comment lines and joins the rest with spaces, so: printable
// ASCII only, no "~", whole-line comments only, and every statement ends
// with ";" (no automatic semicolons).
//
// It receives the page in blocks (deliver.c): ',' + 96 base32 symbols
// (A-Z, 2-7; read from KeyboardEvent.code, so any keyboard layout) = 60
// bytes: block index u16 (bit 15 = repair block), gzip length (low 16
// bits), page kind (bit 0: 0 light, 1 full) + length bits 16-22 (bits 1-7),
// 51 page bytes, CRC-32. '...' ends a pass, and
// so do 3 s without keys (in case all three dots are lost). A block one
// symbol short or long is mended against its CRC; 192 symbols (a lost ',')
// are two blocks. Good blocks are kept across passes. Repair block r is the
// XOR of the page blocks s with H(r, s) odd (= deliver_hash()), so missing
// blocks are solved from any repair blocks (GF(2) elimination) as soon as
// there are enough. The page always says what to press next; window.SOFLE
// is the state (also read by the tests). When it's done, keys still coming
// (the rest of a repair round) are swallowed; the light page then opens in
// this tab by itself once they have stopped for 2 s (keyboard-only mode: a
// data: page has no WebHID), and can save itself (window.SOFLE.page). The
// full page offers Save / Use it here.
(() => {
  const SYM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  // g: 0 waiting for the stream, 1 receiving, 2 pass ended with blocks missing, 3 done.
  const st = window.SOFLE = { K: [], Q: [], N: 0, L: 0, J: 0, P: 1, good: 0, bad: 0, keys: 0, t0: 0, last: 0, g: 0, page: "" };
  let v = null;
  const T = (h) => { document.body.innerHTML = "<h2>Sofle config</h2>" + h; };
  T("<p>Press <b>Enter</b> to load the page. Don't type until it's done.");
  const H = (r, s) => {
    let h = Math.imul(r + 1, 0x9E3779B1) ^ Math.imul(s + 1, 0x85EBCA77);
    h ^= h >>> 15; h = Math.imul(h, 0x2C1B3C6D); h ^= h >>> 12; h = Math.imul(h, 0x297A2D39);
    return (h ^ h >>> 15) & 1;
  };
  const crc = (o) => {
    let c = -1;
    for (let i = 0; i < 56; i++) { c ^= o[i]; for (let j = 0; j < 8; j++) c = c >>> 1 ^ (c & 1 ? 0xEDB88320 : 0); }
    return (c ^ -1) >>> 0;
  };
  const take = (w) => {
    const o = [];
    let a = 0, l = 0;
    for (let i = 0; i < 96; i++) { a = (a << 5 | w[i]) & 4095; l += 5; if (l > 7) { l -= 8; o.push(a >> l & 255); } }
    if (crc(o) !== (o[56] << 24 | o[57] << 16 | o[58] << 8 | o[59]) >>> 0) return false;
    let x = o[0] << 8 | o[1];
    const len = (o[4] >> 1) * 65536 + (o[2] << 8 | o[3]);
    if (len !== st.L) { st.K = []; st.Q = []; st.L = len; st.N = Math.ceil(len / 51); }
    st.J = o[4] & 1;
    st.good++;
    const p = o.slice(5, 56);
    if (x < 32768) st.K[x] = p;
    else { x &= 32767; if (!st.Q.some((q) => q[0] === x)) st.Q.push([x, p]); }
    return true;
  };
  const block = (w) => {
    const n = w.length;
    if (n === 96 && take(w)) return;
    if (n === 95) for (let i = 0; i <= 95; i++) for (let s = 0; s < 32; s++) if (take([...w.slice(0, i), s, ...w.slice(i)])) return;
    if (n === 97) for (let i = 0; i < 97; i++) if (take([...w.slice(0, i), ...w.slice(i + 1)])) return;
    if (n === 192) { if (!take(w.slice(0, 96))) st.bad++; if (!take(w.slice(96))) st.bad++; return; }
    st.bad++;
  };
  const solve = () => {
    const K = st.K, Q = st.Q, N = st.N;
    if (!N) return false;
    const M = [];
    for (let s = 0; s < N; s++) if (!K[s]) M.push(s);
    const m = M.length;
    if (!m) return true;
    if (Q.length < m) return false;
    const E = Q.map((w) => {
      const q = w[1].slice();
      for (let s = 0; s < N; s++) if (K[s] && H(w[0], s)) for (let j = 0; j < 51; j++) q[j] ^= K[s][j];
      return [M.map((s) => H(w[0], s)), q];
    });
    for (let c = 0; c < m; c++) {
      let n = -1;
      for (let i = c; i < E.length; i++) if (E[i][0][c]) { n = i; break; }
      if (n < 0) return false;
      const t = E[c]; E[c] = E[n]; E[n] = t;
      for (let i = 0; i < E.length; i++) {
        if (i === c || !E[i][0][c]) continue;
        for (let j = c; j < m; j++) E[i][0][j] ^= E[c][0][j];
        for (let j = 0; j < 51; j++) E[i][1][j] ^= E[c][1][j];
      }
    }
    for (let c = 0; c < m; c++) K[M[c]] = E[c][1];
    return true;
  };
  const show = () => {
    let n = 0;
    for (let s = 0; s < st.N; s++) if (st.K[s]) n++;
    const rate = Math.round(st.keys / Math.max(1, (Date.now() - st.t0) / 1000));
    const stats = `<p>Pass ${st.P}: ${n} of ${st.N || "?"} blocks good, ${st.bad} bad, ${st.Q.length} repair blocks. ${st.keys} keys, ${rate} keys/s.`;
    if (st.g === 2) T(`<p><b>${st.N - n} of ${st.N} blocks missing.</b> Press the <b>right knob</b> on the keyboard to send repair blocks (within 30 s: its screen counts down).` + stats);
    else T("<p>Receiving the page... Don't type." + stats);
  };
  const finish = async () => {
    st.g = 3;
    try {
      const u = new Uint8Array(st.N * 51);
      st.K.forEach((p, s) => u.set(p, s * 51));
      const z = new Blob([u.slice(0, st.L)]).stream().pipeThrough(new DecompressionStream("gzip"));
      const text = await new Response(z).text();
      st.page = text;
      const f = st.J ? "sofle-config.html" : "sofle-light.html";
      const big = `<p style="font-size:40px;margin:20px 0"><b>Done.</b> Press <b>Esc</b> on the keyboard (or wait).</p>`;
      const open = () => { document.open(); document.write(text); document.close(); };
      if (!st.J) {
        T(big + "<p>The page opens here as soon as the keyboard stops typing.");
        const t = setInterval(() => { if (Date.now() - st.last > 2000) { clearInterval(t); open(); } }, 250);
        return;
      }
      T(big + `<p><a download="${f}"><button>Save as ${f}</button></a><p>Then open the saved file in Chrome or Edge: the mouse works there.<p><button id="u9">Use it here (keyboard-only mode)</button>`);
      document.querySelector("a").href = URL.createObjectURL(new Blob([text], { type: "text/html" }));
      document.getElementById("u9").onclick = open;
    } catch (x) {
      st.K = []; st.Q = []; st.g = 2;
      T(`<p>The page didn't unpack (${x}). Press the <b>right knob</b> to send more blocks.`);
    }
  };
  const endPass = () => {
    if (v) { block(v); v = null; }
    if (st.g !== 1) return;
    if (solve()) { finish(); return; }
    st.g = 2;
    show();
    st.P++;
  };
  window.onkeydown = (e) => {
    if (e.ctrlKey || e.altKey || e.metaKey) return;
    if (st.g === 3) {
      // Done: late stream keys are swallowed; Tab / Enter / Space reach Save and Use it here.
      if (/^(Key[A-Z]|Digit[2-7]|Comma|Period)$/.test(e.code)) { e.preventDefault(); st.last = Date.now(); }
      return;
    }
    e.preventDefault();
    const c = e.code;
    if (/Enter/.test(c)) {
      if (st.g === 0) T("<p>The keyboard isn't sending (it may have given up waiting). Press <b>Esc</b> on the keyboard, open a new tab and start again: CONFIG, then P.");
      else if (st.g === 2) show();
      return;
    }
    if (c === "Period") { if (st.g === 1) { st.keys++; endPass(); } return; }
    if (c === "Comma") {
      if (st.g !== 1) { st.g = 1; st.t0 = Date.now(); st.keys = 0; }
      st.keys++; st.last = Date.now();
      if (v) block(v);
      v = [];
      if (st.P > 1 && solve()) finish();
      return;
    }
    if (st.g !== 1 || !v || !/^(Key|Digit)/.test(c)) return;
    st.keys++; st.last = Date.now();
    const n = SYM.indexOf(c.slice(-1));
    if (n >= 0) v.push(n);
  };
  setInterval(() => { if (st.g === 1) { if (Date.now() - st.last > 3000) endPass(); else show(); } }, 500);
})();
