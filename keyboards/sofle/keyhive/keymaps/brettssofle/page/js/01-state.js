"use strict";
const DATA = /*__DATA__*/null;

const F = { HELLO:0x01, READY:0x02, BYE:0x03, UI:0x04, ROW:0x10, ENC:0x11, LED:0x20, TP:0x21, TPZ:0x22 };
const TABS = ["Layout", "LEDs", "Trackpad", "Pet"];
// The Pet tab needs WebHID / network mode and a firmware with the pet.
const tabList = () => (S.mode === "hid" && S.pet ? TABS : TABS.slice(0, 3));
// Layout tab selection: a key (row/col) or a knob action (slot 0-5; 0xFF/-1 = none).
const knobSel = (ui) => ui.slot >= 0 && ui.slot < 6;
const CHALLENGE = "ACDEFHJKLMNPRSTUWXY";
const KNOB_L = [4, 0], KNOB_R = [9, 5];
const SLOT_NAMES = ["Turn left", "Turn right", "Press"];
const HS = { NONE:0, ASK:1, ACTIVE:2, DENIED:3, TIMEOUT:4 };

// mode: null (not connected) | "bridge" (keyboard-only) | "hid" (WebHID, mouse)
/*__FX_CORE__*/
const S = {
  mode: null, state: "idle", nonce: 0, ready: null, ui: null, keymap: [], enc: [], led: null, tp: null, tpz: null,
  frames: 0, missed: 0, lastSeq: null, zHist: [], peaks: [], wasTouching: false, modeEffects: [], bridgeOn: false,
};
const $ = (id) => document.getElementById(id);

