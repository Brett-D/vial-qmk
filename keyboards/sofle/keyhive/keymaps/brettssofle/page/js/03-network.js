// ================================================================ network mode
// Served by the keyboard itself (USB network config mode, http://192.168.7.1):
// no WebHID there, so the same 32-byte commands go to its API by fetch
// (net/api.c) through a stand-in for the WebHID device.
const NET = location.protocol === "http:" && /^(192\.168\.7\.1|sofle\.local)$/i.test(location.hostname);
function netDevice(token) {
  const listeners = new Set();
  return {
    net: true, opened: true,
    async close() { this.opened = false; },
    addEventListener: (t, f) => listeners.add(f),
    removeEventListener: (t, f) => listeners.delete(f),
    async sendReport(id, buf, keepalive) {
      const hex = Array.from(buf, (b) => b.toString(16).padStart(2, "0")).join("");
      const r = await fetch("/api/hid", { method: "POST", headers: { "Content-Type": "text/plain", "X-Sofle-Token": token }, body: hex, cache: "no-store", keepalive: !!keepalive });
      if (!r.ok) throw new Error(r.status === 403 ? "refused by the keyboard (reload the page)" : "HTTP " + r.status);
      const bytes = new Uint8Array((await r.json()).r.match(/../g).map((h) => parseInt(h, 16)));
      listeners.forEach((f) => f({ data: new DataView(bytes.buffer) }));
    },
  };
}
async function netConnect() {
  try {
    const r = await fetch("/api/session", { method: "POST", cache: "no-store" });
    if (!r.ok) throw new Error("HTTP " + r.status);
    return netDevice((await r.json()).token);
  } catch (e) {
    showHidErr(`<b>Couldn't reach the keyboard</b> at ${escapeHtml(location.host)} (${escapeHtml(e.message)}). Its screen should show <b>NET</b>; if it went back to the normal keyboard, press Config, then <kbd>N</kbd>, and reload this page.`);
    return null;
  }
}

