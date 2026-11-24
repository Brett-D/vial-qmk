# Brett's Sofle (Keyhive) — Vial, Sea-Picro RP2040, Cirque trackpad

Keymap for a Keyhive Sofle v2 with:

* **Sea-Picro EXT V1.0** (RP2040) on both halves
* **23 mm Cirque Pinnacle** trackpad (I2C) on the **left** half — the left half is the USB/master side
* **OLED** on the **right** half only
* Rotary encoders on both halves, per-key RGB + underglow (WS2812, 37 LEDs per half)

## Features

| Feature | Details |
| --- | --- |
| Vial | Configure everything live at [vial.rocks](https://vial.rocks) (Chrome/Edge, WebHID) or the Vial desktop app. Unlock combo: hold the four corner keys (top-left `` ` ``, top-right `` ` ``, Left Shift, Right Shift). |
| Layers | 0 QWERTY, 1 Colemak-DH, 2 Lower, 3 Raise, 4 Mouse (automatic), 5 Adjust (hold Lower + Raise together; empty to start) |
| `CYCLE` (Raise + top-left) | Switches the default layer between QWERTY and Colemak-DH (saved) |
| `CONFIG` (Raise + top-right) | Config mode: edit the keyboard from any browser with nothing installed (see [Config mode](#config-mode)) |
| Encoders | Left: volume, press = mute. Right: page up/down, press = next OLED page (`OLED_PAGE`) |
| LEDs | Per-key RGB Matrix with two layers: a **whole-keyboard** effect (static colour, rainbows, raindrops, …) and a **key-press** effect (reactive, splash, …) drawn on top in its own colour; presses light up and fade back into the whole-keyboard effect. Either can be off, so a single effect works too (`led_layers.c`, `rgb_matrix_user.inc`). **LED zones**: effect on keys + underglow, effect on keys with a solid-colour underglow, or keys only. Turns off after 1 minute (changeable: never / 1 / 2 / 5 / 10 / 30 min) without key, knob or trackpad use; comes back on the next input. Set it all in config mode; Vial (VialRGB) can pick single effects |
| Right OLED | Five pages, cycled with the right knob press (`OLED_PAGE`, remappable; the page is remembered): **1 status** (default layer, active layer, Caps/Num lock, mods S C A G), **2 trackpad** (live Z with bar, peak, last peak, press-to-click on/off, threshold, release Z), **3 LEDs** (effect, zone, brightness), **4 info** (build date/time, uptime, last start), **5 pet** (see *OLED pet*; skipped when the pet is off). A **W** next to the page number means the mouse wiggler is on. Sleeps after 1 minute of no input on **either** half. Shows **CONFG** in config mode |

### Mouse wiggler

Keeps the computer awake: after 30 s / 1 / 2 / 4 min without any key, knob
or trackpad use, the pointer moves 1 pixel and straight back (net zero), and
again every interval while you're away. The nudge doesn't count as input, so
the LED and OLED idle timeouts still apply. Turn it on in the config page
(**Trackpad** tab → Mouse wiggler) or with the `WIGGLE` key (Vial: User /
custom keycodes; not on any layer by default). It is never saved: it's off
after every power-up. (It also stops the screen from locking on its own, so
don't use it where a policy requires auto-lock.)

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

### LED layout and calibration

The Keyhive Sofle v2 has 37 LEDs per half: 30 under the keys and 7 underglow.
Which physical LED sits under which key is board-specific, so **measure it
once** with the config page (mouse mode):

1. Connect, open the **LEDs** tab and click **Calibrate LED positions…**, then
   **Start**.
2. One LED lights up white at a time (all 74, left half first). **Press the
   key on top of it** — the page moves on by itself. If the LED is not under
   a key (it glows from under the board), **hold any key for 1 second** (or
   click **Underglow**). **Nothing lit** and **Back** are page buttons; a key
   you can't press can be clicked on the picture instead. While calibrating,
   key presses go only to the page (`led_cal.c`), nothing is typed.
3. **Save to keyboard**. The page reads the map back, waits for the right
   half to confirm it runs the same map (it reports a CRC of its map), and
   switches to **Test**: each key you press lights the LED the saved map gives
   it, green, on its own half (the same `g_led_config` lookup reactive effects
   use); key LEDs glow dim white, underglow dim blue. **Test LED map** runs the
   test any time.

The map is kept in EEPROM on the left half and sent to the right half
(`led_map.c`). **Use built-in LED map** forgets it. **Copy result** gives a
JSON you can bake in as the built-in map:
`python keyboards/sofle/keyhive/keymaps/brettssofle/config_page/gen_rgb_layout.py --map led.json`

The built-in map in `rgb_layout.c` is derived from keyhive's rgblight
`led_map` + the Sofle RGB chain order and does not match every board.
`config.h` sets the 74-LED count and 37/37 split (`sofle/info.json`'s 72-LED
Sofle RGB values would otherwise win).

### Lighting: background + key-press effect (v20)

The LEDs are drawn in layers, every frame, on each half:

1. **Background** — a whole-keyboard effect: QMK's (solid, gradient,
   rainbow, breathing, raindrops, digital rain, …) or one of ours:
   aurora, fire (rising embers), starfield, ocean waves, comet sweep, lava
   lamp, candle, sunrise, breathing gradient, code rain.
2. **On key press** — each press starts its own event (the last 8 are kept)
   that runs over the background: ring ripple, echo rings, splash (filled),
   key glow, cross (row + column), nexus, shockwave with trail, sparks,
   lightning (to a random key), rain splash, key trail (afterglow), heat
   (spreads and cools down).
   - *Colour*: fixed (picker), opposite of the background, rainbow by
     distance, random per press.
   - *Over the background*: **Replace** (default: always clearly visible),
     **Add** (lightens), **Tint** (keeps the background's brightness).
   - *Key-press brightness* (v21): **same as the background** (default) or
     its own 1–100 % of the maximum brightness, so the animations can be
     brighter or dimmer than the rest. The background's *Brightness* slider
     is the rest of the keyboard; *dim background while active* lowers the
     background only while an animation runs. With Tint the animation takes
     the background's brightness. The LEDs never go over the maximum
     brightness (`fx_cap`, also when Add sums past it).
   - *Speed*, *band width*, *reach*, *fade*, *echo rings* (echo rings only) and
     *dim background while active* (the background dims a little while a
     ripple runs, so it stands out).
3. **Underglow** zone as before (follow / solid / off).

The background keeps animating underneath; the key-press layer is
composited once per frame when the LEDs are sent (`rgb_driver.c` →
`led_fx_pixel`), so effects that repaint everything (digital rain) can't
wipe it. Positions are the LEDs' real x/y (`rgb_layout.c`), so rings are
round and cross between the halves: both halves see every press (the
split mirrors the key events) and run the same maths (`led_fx_core.h`);
the left half sends the settings to the right (`RPC_ID_USER_LEDFX`).

**Combinations** (`led_fx_table.h`, one table for the firmware and the
pages): *standalone* effects react to keys themselves (typing heatmap,
QMK's solid reactive, our typing-speed meter and typing VU meter) and are
used on their own — the page greys out "On key press" and says why, and the
keyboard refuses a key-press effect with them, also over raw HID. *Dark*
backgrounds (digital rain, code rain, starfield, comet, …) get a soft
warning when the key-press colour is close to the background's or Tint is
chosen; nothing is blocked.

**Page**: LEDs tab → *Background* and *On key press* sections, and a
preview of the keyboard with the same maths (`config_page/fx_core.js`, a
line-for-line port, checked value for value against the C by
`harness/fx/fx_match.py`); QMK's own backgrounds are approximated there.
Click a key in the preview for a test ripple — it also runs on the
keyboard. Settings are kept in EEPROM (8 bytes, `led_layers.c`; v6–v19
settings are converted) and are in backups.

Raw HID: `0xF6 0x1C` op 0 get, 1 set (preview), 2 save, 3 revert, 4 fire
(row, col); set/reply bytes 3–14 = background (VialRGB id, or 0xC0+ ours /
0xD0+ standalone), effect, blend, colour mode, hue, sat, speed, width,
reach, fade, echoes, dim; set byte 15 = key-press brightness (0 = the
background's, 1–100 %) with bit 7 set (without it the keyboard keeps its
value: v20 pages); reply byte 15 = accepted, 16 = key-press brightness.
The brightness is kept in EEPROM bytes 76–77 (with a check byte) and sent
to the right half as an 11th RPC byte (a v20 right half reads the first 10). The v6–v19 command
`0xF6 0x14` still works (light page).

Cost (v20): about +5.5 KB of flash for the engine and +8.6 KB for the
bigger full page stored on the keyboard (46.4 → 55.0 KB gzipped): 205.4 →
219.5 KB in all, of 16 MB; about 0.6 KB of RAM. Per frame and half, on the RP2040
(counted in an emulator, `harness/fxbench`): our backgrounds 0.09–0.2 ms;
key-press effects ~0.08 ms per live press, up to 0.56–0.8 ms with 8
presses running at once (fast typing), ~0.95 ms on the first frame of a new
press (distances are then cached per press).

## Config mode

Configure the keymap, knobs, LEDs (effect, colour, zones, idle-off) and trackpad press-to-click from
`config_page/sofle-config.html` — a single file you open from disk (keep a
copy on any computer). Nothing to install, no internet. Two ways to use it:

### USB network mode (keyboard at 192.168.7.1)

*Verified on real hardware with Windows 11 (v11 and later): the NCM adapter
binds by itself, DHCP gives 192.168.7.2, and the page and its API work.*

Press `CONFIG` (or hold both knobs), and at **CONF?** press **N**. The
keyboard reconnects as a composite USB device (keyboard, mouse, raw HID and
a CDC-NCM network adapter, product ID `0x1288`); typing and the trackpad keep
working. The computer gets **192.168.7.2** by DHCP — no gateway and no DNS,
so its internet connection is untouched — and the keyboard is
**192.168.7.1**. The OLED shows **NET** and the address **192.168.7.1**
from the start, with the state under it: `USB conn.` → `Link up DHCP` →
`Ready`, and the seconds so far while it waits. Windows can take up to
~30 s, longer the first time on a new USB port (it sets up the adapter);
the address works as soon as it says `Ready`. After 20 s without an
address the OLED also offers `P` = type the page instead. When network mode ends, the OLED says why for a few
seconds (`NET off`: by key / idle 10min / P). Open **http://192.168.7.1** (or http://sofle.local where
.local names resolve) in any browser (Firefox and Safari too — no WebHID needed): the keyboard
serves the config page itself, gzipped from flash. The page asks for a
session straight away; the OLED shows **CONF?** — press the right knob (or
Enter) and the page reads and saves everything with the mouse, as in the
WebHID mode. The OLED then shows `Page conn.`. `CONFIG` / both knobs again return to the plain keyboard;
it also does so by itself after 10 minutes without network traffic.

- Windows 10 (1903+) / 11: a new "Ethernet" adapter appears (built-in
  UsbNcm driver, selected by the keyboard's Microsoft OS 2.0 descriptors).
  `ping 192.168.7.1`; `ipconfig` shows 192.168.7.2 with no default gateway.
- Linux: a new `enx02536f666c65` / `usbX` interface; NetworkManager takes
  the DHCP address. `ping 192.168.7.1`.
- If nothing gets an address within 20 s the OLED shows `No IP yet`: press
  **P** to go back to the plain keyboard and get the light page typed
  instead (below), or use a saved `sofle-config.html` / `sofle-light.html`.

Milestones: v8 = network link, DHCP and ping; v9 = the page over HTTP; v10 = its API.

The API (`net/api.c`) carries the page's 32-byte VIA / Vial / 0xF5 / 0xF6
commands over HTTP and runs them through `raw_hid_receive()`, so it does
exactly what the WebHID mode does, with the same write gate:

- `POST /api/session` → `{"token":"…"}` (a new token each time network mode
  starts).
- `POST /api/hid` with header `X-Sofle-Token` and a body of 1–8 commands as
  hex → `{"r":"<replies as hex>"}`.

Other web sites can't use it: the server answers only Host `192.168.7.1` /
`sofle.local` (DNS rebinding), Origin must be absent or the keyboard's own,
every command needs the token, and no CORS headers are ever sent. Writes need
the CONF? confirm on the keyboard. Network code: `net/` (lwIP from `lib/chibios/ext`, extracted
once — see rules.mk; build without it: `NETWORK_ENABLE=no`).


### Typed page delivery (nothing to install, no network)

The keyboard carries two copies of the page (`config_page_blob.h`, rebuilt
from the HTML at every build) and can type either into any browser:

| | light `sofle-light.html` (**P**) | full `sofle-config.html` (**F**) |
|---|---|---|
| has | keymap + knobs, LED effect / colour / speed / brightness, trackpad press threshold, Backup / Restore | everything (LED zones and calibration, background + key-press effects with the preview, pet, wiggler, knob control, keyboard-only mode, help, …) |
| gzipped | 12.3 KB | 55.5 KB |
| typing (one clean pass) | ~22 s address + loader, then ~94 s | ~22 s, then ~423 s |
| when done | opens in the same tab in keyboard-only mode; **Save for mouse mode next time** | Save as / Use it here |

The full page is also served by network mode (quicker, and the mouse works
there in any browser). The light page has the same backup format.

1. Open a new browser tab (the address bar is selected).
2. Press **Raise + top-right** (`CONFIG`); the OLED asks **CONF?**. Press
   **P** (light page) or **F** (full page).
3. The keyboard types a short `data:` address (~4 s). Press **Enter**.
4. The page asks for **Enter**: press it. The keyboard types the loader
   into the page (~17 s, OLED `Load nn%`). If the page says **Damaged**,
   press the **right knob** to type it again.
5. The page asks for **Enter** again (only once the computer has processed
   everything): press it. The keyboard types the page; the screen shows
   blocks good/bad, repair blocks, keys received and keys/s, the OLED
   `Send nn%` and the pass number. Don't type meanwhile.
6. If the page then says blocks are missing, press the **right knob**
   within 30 s (the OLED shows `Sent. Knob=fix Esc=done` and counts down;
   after that the keyboard finishes by itself and never sends repairs on
   its own). Repair rounds are short (16, 32, 64 … blocks), and the page
   fills the gaps as soon as it has enough. When it says **Done. Press Esc
   on the keyboard (or wait)**, press Esc; the rest of a round is ignored.
7. The light page then opens in that tab by itself, in **keyboard-only
   mode** (a `data:` address can't use WebHID): CONFIG, right knob, then the
   letter it shows. **Save for mouse mode next time** saves it as
   sofle-light.html; opened from disk in Chrome/Edge it uses the mouse.
   The full page offers **Save as sofle-config.html** and **Use it here**.

Why three steps: address bars get slower with every character typed, and
with the 3 KB v16 address Edge fell far behind the keyboard (whose 60 s
wait then ran out). So the address is now only the ~680-character stage 1
(`bootstrap.html`); it receives the loader (`loader.js`) typed into the
page, checks its hash and runs it. The keyboard never runs ahead of USB
(each report waits until the host has collected the last), and at 4 ms per
key the page keeps up, so the OLED % matches the screen. The page never
waits silently: an Enter it shouldn't get, a loader cut off or damaged, or
3 s without keys in a pass (all end markers lost) each tell you what to
press. If the keyboard gave up waiting (2-5 minutes), it says to start
again. The right knob also starts the loader if the keyboard missed your
Enter.

How the stream survives dropped keys: the page is sent in 60-byte blocks
(`,` + 96 letters/digits `A–Z 2–7`: block number, page length, page kind,
51 page bytes, CRC-32), every key as its own key-down and key-up report
(4 ms per key on the first pass). A block with one key lost or doubled is
mended by the page (it tries every insertion/deletion against the CRC);
worse blocks are dropped and the rest kept. Repair blocks are XORs of
pseudo-random sets of page blocks (`deliver_hash()` in `deliver.c` = `H` in
`bootstrap.html`), so any repair block can stand in for any missing block
(solved by GF(2) elimination); repair rounds are a quarter of the page and
type at 6, then 8 ms per key. A page can't signal back to the keyboard (it
can't set the lock LEDs, and a `data:` page has no WebHID), hence the knob
press. In tests with 0.1–1 % of keys dropped, doubled or swapped the light
page arrives byte for byte in ~96–199 s of typing (the v15 single-CRC stream never
survived those rates).

**Esc** stops at any time. The address part is typed for a **US keyboard
layout** on the computer (the page part works with any layout). Only
printable characters are typed — never Enter or shortcuts; the two Enters
are yours.

### Mouse mode (WebHID: Chrome / Edge)

1. Open the page and click **Connect**, then pick **Sofle** in the browser's list.
2. The right OLED shows **CONF?** — press the **right knob** (or Enter) to
   allow it (left knob / Esc refuses; it gives up after 30 s).
3. Click a key or a knob action, then click a keycode — it's saved
   immediately. LEDs and trackpad have sliders with live preview and **Save**.

**Right knob control:** while the page has focus, the right knob drives it
instead of paging up/down: **turn** moves the orange highlight (layer
buttons, keys, knob actions, LED/trackpad controls), **press** selects it
(on a key or knob action: turn to choose a keycode, press to apply; on a
slider or list: turn to change, press to finish; long press backs out),
**hold ~0.6 s** switches tab. Click elsewhere (another window) and the knob
works normally again; the keyboard also takes it back by itself 1.5 s after
the page stops asking (`CMD_NAV` 0xF6 0x12).

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

### Colours

The LED tab has a colour picker and swatches (red, orange, yellow, green,
cyan, blue, purple, pink, warm white, white) for the main colour, the
key-press colour and the solid-underglow colour, in both pages. They set
hue and saturation (the sliders stay as fine controls); brightness is its
own slider, under the firmware's maximum. The keys show colours a little
differently from the screen. In keyboard-only mode, hold the right knob and
turn it on a hue or saturation setting to step through the same presets.

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

| Control | Layout tab (keys and knobs) | LEDs / Trackpad tabs |
| --- | --- | --- |
| Left knob turn | Change layer | Choose setting |
| Hold left knob + turn | Select a knob action (left/right knob: turn left, turn right, press) | – |
| Left knob press | Next tab (hold ~1 s: resend everything to the page) | ← |
| Hold right knob ~0.6 s (no turn) | Next tab | ← |
| Right knob turn | Choose keycode (turn fast to skip ahead) | Change value (LEDs preview live) |
| Hold right knob + turn | Jump to the next/previous keycode category | – |
| Right knob press | Apply to the selected key or knob action | Save |
| Any key | Select it. Hold it and tap another key to copy that key's default-layer keycode. With a knob action selected, hold the left knob and tap a key to copy its keycode | – |

Exit with **Raise + `CONFIG`**, holding both knob buttons for 2 s, holding
**Esc** for 2 s, or 5 minutes without input. Unsaved LED/trackpad changes are
dropped on exit. Tip: when holding both knobs, press the right one first —
the left knob is Mute.

Keymap changes are saved like Vial's (EEPROM; a new firmware build resets them
to the defaults in `keymap.c`). v1 covers keymap, knobs, LEDs and
trackpad; use Vial for combos, tap dance and macros.

After changing `config_page/gen_config.py`, `config_page/page.html` or
`config_page/light.html`, regenerate `config_picker.h` and both pages:
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

Flash **both** halves every time: the left half sends the right OLED what to
show, and the two must agree on its layout. The right OLED's INFO page shows
both versions (`R v19` / `L v19`); if they differ, the right OLED says
`HALVS diff fw` for 10 s after start. Since v19 either half also works with
an older other half (v13 or later): the right half reads any newer layout as
a prefix, and the left half also sends the older layouts. (v18 on the left
with v17 on the right left the right OLED blank in config mode.)

1. Unplug the TRRS cable and plug USB into one half.
2. Hold the Sea-Picro reset button for **more than 1 second** (a short tap only resets).
   A drive called `RPI-RP2` appears.
3. Copy the `.uf2` onto the drive. It disconnects and reboots.
4. Repeat for the other half, then reconnect TRRS and plug USB into the **left** half.

### Settings survive reflashing

Stock Vial marks the EEPROM with a random per-build ID, so every new firmware
reset the keymap, knobs, LEDs, LED map and trackpad settings. This keymap
marks it with `BRETT_EEPROM_LAYOUT_VERSION` (config.h) instead, mixed with
the sizes that decide where things are stored (layers, matrix, encoders,
Vial tap-dance/combo/override/macro sizes, EECONFIG sizes; `eeprom_layout.c`,
via a weak `via_eeprom_magic()` hook in `quantum/via.c`). Reflashing keeps
everything; only a real layout change resets it, once. If one of those sizes
changes, the build stops with "EEPROM layout changed (...)": bump the version
and record the new values in `eeprom_layout.c`. The Vial keyboard UID is
unchanged. (The first flash of a firmware with this, v11, still resets once.)

### OLED pet

A little pixel pet lives on the right OLED's fifth page: **Pip, cat, dog,
owl, dragon, blob or axolotl** (all original art, drawn by
`config_page/gen_pet_art.py`, which writes the firmware's `pet_art.h` and the
page's copy from the same source). The page is laid out for the tall, narrow
screen: name and mood icon, the pet, four stat bars (food, fun, energy,
clean), then the selected care action as a big icon with its name.

- **Growing up:** egg → baby → child → adult. The egg hatches after about 10
  minutes, it's a child at 1 day and an adult at 3 days (counted while the
  keyboard is on; *slowly* doubles that, *quickly* halves it; poor care
  slows it down, nothing makes it shrink). Each stage has its own size (the
  adult dragon gets wings) and growing up gets a little celebration. An egg
  needs no care.
- **Each species** has its own idle animation (antenna wobble, tail flick,
  wag, head tilt, wing flap, squish, gill wiggle) and its own food and toy:
  apple/ball, fish/yarn, bone/ball, mouse/feather, coal/flame, jelly/bubble,
  worm/bubbles. A happy dragon puffs a little flame.

- **Right knob on the pet page** (`page_key.c`): with the care menu closed the
  bottom shows **▸ Next** and a **tap goes to the next page**, as on every
  page. **Turn** to open the menu: Feed / Play / Sleep (Wake) / Clean / Next
  (it wraps; a ghost has New / Next, an egg only Next); a **tap** does the
  choice, and Next leaves. The menu closes 6 s after the last turn. A
  0.5–2 s **hold** also leaves; longer holds are left to the hold-both-knobs
  config combo. The left knob and typing are never taken.
- **Stats** drift while the keyboard is on: at *normally*, a full stat lasts
  about a day (*slowly* ≈ 2 days, *quickly* ≈ 12 h). Typing is exercise:
  every 40 key presses, fun +1 and energy −1. Sleep refills energy and it
  wakes by itself when rested. It refuses food when full and play when tired
  or asleep (shakes its head).
- **Forgiving:** it gets hungry, sad, tired or dirty (poop appears), but never
  dies unless **Can die** is on; then a whole day with no food and no fun turns
  it into a ghost, and the knob menu offers a new egg.
- **Config page → Pet tab** (WebHID or network): live view, stats, stage and
  growth, the same care buttons, name, on/off, care speed, can die, and **New
  pet…** with a species picker (a new egg). Raw HID 0xF6 0x18 (get), 0x19
  (care; 5 = new egg + species), 0x1A (settings), 0x1B (species + stage, for
  restoring); all but 0x18 need the confirmed session. Backups include the
  pet's settings, name, species and stage.
- **Saved** in the last 32 EEPROM bytes (taken off the end of Vial's macro
  buffer, 1887 → 1855 bytes, so nothing else moves and settings survive):
  every 10 minutes if something changed, and 15 s after care or a settings
  change, so a burst of clicks is one write.

### Backup and restore

On the config page (WebHID file mode or the network page), **Backup**
downloads `sofle-backup-<date>.json` with every layer's keymap, the knobs,
lighting (both effects, colours, brightness, speed, zones, underglow colour,
idle timeout), the measured LED map, trackpad press-to-click settings and the
wiggler. **Restore…** loads such a file and lists what would change per
section (untick what to keep); *Restore selected* writes it through the
confirmed session, reads everything back and reports anything that didn't
take — e.g. a Bootloader key, which Vial only accepts after unlocking. Files
from another matrix size or a newer backup format are refused.

### Crash reports

If the keyboard ever hits a fault, it now reboots instead of freezing and
remembers where: the right OLED shows **CRASH** with the code addresses (pc,
lr) and what it was doing ("in netRx", ...) until a key is pressed; the INFO
page shows "Boot CRASH" and the address; and the config page shows a red line
to copy. The page's log also lists free stack space. Network code runs on its
own 6 KB stack.
