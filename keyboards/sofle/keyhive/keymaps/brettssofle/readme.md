# Brett's Sofle (Keyhive) — Vial, Sea-Picro RP2040, Cirque trackpad

Keymap for a Keyhive Sofle v2 with:

* **Sea-Picro EXT V1.0** (RP2040) on both halves
* **23 mm Cirque Pinnacle** trackpad (I2C) on the **left** half — the left half is the USB/master side
* **OLED** on the **right** half only
* Rotary encoders on both halves, WS2812 underglow

## Features

| Feature | Details |
| --- | --- |
| Vial | Configure everything live at [vial.rocks](https://vial.rocks) (Chrome/Edge, WebHID) or the Vial desktop app. Unlock combo: hold the four corner keys (top-left `` ` ``, top-right `` ` ``, Left Shift, Right Shift). |
| Layers | 0 QWERTY, 1 Colemak-DH, 2 Lower, 3 Raise, 4 Mouse (automatic), 5 spare |
| `CYCLE` (Raise + top-left) | Switches the default layer between QWERTY and Colemak-DH (saved) |
| `CONFIG` (Raise + top-right) | Config mode: edit the keyboard from any browser with nothing installed (see [Config mode](#config-mode)) |
| Encoders | Left: volume, press = mute. Right: page up/down |
| Underglow | Turns off after 1 minute (changeable in config mode: never / 1 / 2 / 5 / 10 / 30 min) without key, knob or trackpad use; comes back on the next input |
| Right OLED | Default layer, active layer, Caps/Num lock, active mods (S C A G). Sleeps after 1 minute of no input on **either** half. Shows **CONFG** in config mode |

### Trackpad

| Gesture | Result |
| --- | --- |
| Move a finger | Move the cursor (with glide/momentum on release) |
| Slide around the outer ring | Circular scroll: start on the **right** side of the ring for vertical scroll, on the **left** side for horizontal |
| Tap | Left click (double tap = double click) |
| Press harder | **Left button down** once you press past the threshold, and it stays down at normal pressure, so press then move to **drag**. Released when you lift (or touch very lightly) |

While (and shortly after) you touch the trackpad, the **Mouse layer** turns on
automatically:

| Key | Mouse layer |
| --- | --- |
| **T** / **Y** | Left button — hold and move your finger to **drag** windows / select text |
| **R** / **U** | Right button |
| H | Drag scroll (hold, then move your finger to scroll) |
| J / K / L | Left / right / middle button |

The layer stays on while a mouse key is held, turns off ~0.65 s after you lift
your finger, and turns off immediately when you press any other key, so typing
isn't affected. All of these keys (and `Drag scroll`, under *User* keycodes) can
be remapped in Vial.

Two-finger taps aren't possible: in the absolute mode needed for glide and
circular scroll, the Pinnacle reports only one finger.

#### Press-to-click and `trackpad_z.html`

The Cirque reports **Z** (signal strength / contact size, 0–63, not true
force). Press-to-click latches: when Z crosses the **threshold** (default 51)
for two polls (~20 ms) the left button goes down, and it stays down while you
keep touching at normal pressure, so you can press and then drag. It releases
when you lift your finger or Z falls below the **release Z** (default 24, well
under light-touch levels). The cursor is held still for 60 ms at press and
release so a hard press doesn't nudge it, a press never also counts as a tap,
and it's ignored during ring scrolls.
To tune it, open `trackpad_z.html` straight from disk in Edge/Chrome (close the
Vial tab first) and click *Connect*. It graphs Z live with the press/release
lines, has *Record LIGHT* / *Record FIRM* modes to measure your touches, and
lets you switch press-to-click on/off and change the threshold and release Z.
*Apply & save* stores them on the keyboard (EEPROM; a new firmware build resets
them to the defaults). It talks raw HID command `0xF5`. The same settings, with
a live Z graph, are also in config mode's **Trackpad** tab (no WebHID needed).

Uncomment `TRACKPAD_Z_DEBUG` in `config.h` to also show live Z, peak, last
peak and a touch counter on the right OLED.
#### Tuning

Change these in `config.h` and rebuild:

| Define | Default | Meaning |
| --- | --- | --- |
| `TRACKPAD_TAP_TERM` | 200 ms | Longest touch that counts as a tap |
| `TRACKPAD_MOVE_THRESHOLD` | 6 | Movement (or scrolling) that cancels a tap |
| `TRACKPAD_DRAG_SCROLL_DIVISOR` | 8 | Higher = slower drag scroll |
| `TRACKPAD_LONG_PRESS_RIGHT_CLICK` | off | Touch and hold still = right click (can misfire during slow ring scrolls) |
| `TRACKPAD_LONG_PRESS_TERM` | 500 ms | Hold time for the long-press right click |
| `TRACKPAD_PRESS_DEFAULT_ENABLED` / `_THRESHOLD` / `_RELEASE_Z` | 1 / 51 / 24 | Press-to-click defaults (live values are set in `trackpad_z.html`) |
| `TRACKPAD_PRESS_DEBOUNCE` | 2 polls | Polls Z must stay at/above the threshold before pressing |
| `TRACKPAD_PRESS_MIN_MS` | 30 ms | Shortest press before a low Z can release it |
| `TRACKPAD_PRESS_FREEZE_MS` | 60 ms | Cursor held still after press and release |
| `AUTO_MOUSE_TIME` | 650 ms | How long the Mouse layer stays on after you lift your finger |
| `CIRQUE_PINNACLE_TIMEOUT` | 5 ms | Give up on a failed trackpad I2C transfer after this long |

The I2C bus runs at QMK's 100 kHz default, like the original firmware.

## Config mode

Configure the keymap, knobs, underglow and trackpad press-to-click from
`config_page/sofle-config.html` — a single file you open from disk (keep a
copy on any computer). Nothing to install, no internet. Two ways to use it:

### With the mouse (Chrome / Edge — WebHID)

1. Open the page and click **Connect**, then pick **Sofle** in the browser's list.
2. The right OLED shows **CONF?** — press the **right knob** (or Enter) to
   allow it (left knob / Esc refuses; it gives up after 30 s).
3. Click a key or a knob action, then click a keycode — it's saved
   immediately. LEDs and trackpad have sliders with live preview and **Save**.

The keyboard keeps typing normally while connected. The session ends when you
click **Disconnect**, close the page, or 90 s after the page stops polling.
Close Vial/VIA first (only one app can use the config channel). On Linux the
browser needs a one-time udev rule for `/dev/hidraw*`; the page shows it if
connecting fails.

**Write protection:** without a session confirmed on the keyboard, raw HID
commands that change the keyboard (keymap/encoder/lighting sets, macros,
QMK settings, trackpad 0xF5 set, …) are refused, so another web page can't
silently rewrite it. Reading still works. **The Vial app is affected too:
unlock it first** (Vial → Security → Unlock, then hold the four corner keys),
otherwise its changes are refused. `trackpad_z.html` can read live Z, but
needs Vial unlocked to save — use the Trackpad tab here instead.

### Keyboard-only mode (any browser, incl. Firefox — no WebHID, no udev)

1. Open the page and click it so it has focus.
2. Press **Raise + top-right key** (`CONFIG`) or hold both knob buttons for
   2 s. The OLED asks **CONF?** — press the **right knob** (or Space/Enter).
3. Press the letter the page shows (it only appears on the page that received
   the keyboard's hello, so this proves the right window has focus).

The keyboard stays a plain keyboard: in this mode it reads its own keys and
knobs to drive the editor and *types* short coded messages (only A–Z, 2–7,
`,` and `.` — never Enter, Tab, modifiers or F-keys) into the page, which
decodes them. The page only displays; clicking does nothing in this mode.
Keys don't type normally while it is on.

| Control | Keys tab | Knobs tab | LEDs / Trackpad tabs |
| --- | --- | --- | --- |
| Left knob turn | Change layer | Choose knob action (turn left / right / press) | Choose setting |
| Left knob press | Next tab (hold ~1 s: resend everything to the page) | ← | ← |
| Right knob turn | Choose keycode (turn fast to skip ahead) | ← | Change value (LEDs preview live) |
| Hold right knob + turn | Jump to the next/previous keycode category | ← | – |
| Right knob press | Apply to the selected key | Apply to the knob action | Save |
| Any key | Select it. Hold it and tap another key to copy that key's default-layer keycode | Copy its default-layer keycode | – |

Exit with **Raise + `CONFIG`**, holding both knob buttons for 2 s, holding
**Esc** for 2 s, or 5 minutes without input. Unsaved LED/trackpad changes are
dropped on exit. Tip: when holding both knobs, press the right one first —
the left knob is Mute.

Keymap changes are saved like Vial's (EEPROM; a new firmware build resets them
to the defaults in `keymap.c`). v1 covers keymap, knobs, underglow and
trackpad; use Vial for combos, tap dance and macros.

After changing `config_page/gen_config.py` or `config_page/page.html`,
regenerate `config_picker.h` and the page:
`python keyboards/sofle/keyhive/keymaps/brettssofle/config_page/gen_config.py`

## Building

The Sea-Picro EXT uses the Elite-C pinout with VBUS detect on GPIO19, so build
with the `rp2040_ce` converter (`promicro_rp2040` is deprecated):

```sh
make sofle/keyhive:brettssofle CONVERT_TO=rp2040_ce
```

Without a local QMK environment, use Docker from the repository root (PowerShell):

```powershell
docker run --rm -v "${PWD}:/qmk_firmware" -w /qmk_firmware ghcr.io/qmk/qmk_cli `
  make sofle/keyhive:brettssofle CONVERT_TO=rp2040_ce
```

Output: `sofle_keyhive_brettssofle_rp2040_ce.uf2` in the repository root.

For trackpad debug output (`qmk console` / hid_listen), add `TRACKPAD_DEBUG=yes`.
To build without the trackpad (for troubleshooting), add `POINTING_DEVICE_ENABLE=no`.

## Flashing

The **same UF2 goes on both halves** (`MASTER_LEFT` decides which half is which).

1. Unplug the TRRS cable and plug USB into one half.
2. Hold the Sea-Picro reset button for **more than 1 second** (a short tap only resets).
   A drive called `RPI-RP2` appears.
3. Copy the `.uf2` onto the drive. It disconnects and reboots.
4. Repeat for the other half, then reconnect TRRS and plug USB into the **left** half.

Every new build resets the saved Vial keymap to the defaults in `keymap.c` (Vial
stamps each build with a random ID). Before reflashing, save your layout in Vial
(*File → Save current layout*) and load it again afterwards.
