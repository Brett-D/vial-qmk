"use strict";
const DATA = /*__DATA__*/null;

const F = { HELLO:0x01, READY:0x02, BYE:0x03, UI:0x04, ROW:0x10, ENC:0x11, LED:0x20, TP:0x21, TPZ:0x22 };
const TABS = ["Layout", "LEDs", "Trackpad", "Pet", "Macros"];
// [label, tab id]. The Pet tab needs WebHID / network mode and a firmware with the pet;
// v28's Macros tab (macro editor, shift pairs) WebHID / network mode.
const tabList = () => TABS.map((t, i) => [t, i]).filter(([, i]) => i < 3 || (S.mode === "hid" && (i !== 3 || S.pet)));
// Layout tab selection: a key (row/col) or a knob action (slot 0-5; 0xFF/-1 = none).
const knobSel = (ui) => ui.slot >= 0 && ui.slot < 6;
const CHALLENGE = "ACDEFHJKLMNPRSTUWXY";
const KNOB_L = [4, 0], KNOB_R = [9, 5];
const SLOT_NAMES = ["Turn left", "Turn right", "Press"];
const HS = { NONE:0, ASK:1, ACTIVE:2, DENIED:3, TIMEOUT:4 };
// v29: why the keyboard ended the session (status byte 31); GONE: the network page lost it.
const END = { NONE:0, KEY:1, IDLE:2, NET:3, PAGE:4, GONE:99 };

// mode: null (not connected) | "bridge" (keyboard-only) | "hid" (WebHID, mouse)
/*__FX_CORE__*/
/*__PET_CORE__*/
const S = {
  mode: null, state: "idle", nonce: 0, ready: null, ui: null, keymap: [], enc: [], led: null, tp: null, tpz: null,
  frames: 0, missed: 0, lastSeq: null, zHist: [], peaks: [], wasTouching: false, modeEffects: [], bridgeOn: false,
};
const $ = (id) => document.getElementById(id);

