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
| Encoders | Left: volume, press = mute. Right: page up/down |
| Underglow | Turns off after 1 minute without key, knob or trackpad use; comes back on the next input |
| Right OLED | Default layer, active layer, Caps/Num lock, active mods (S C A G). Sleeps after 1 minute of no input on **either** half |

### Trackpad

| Gesture | Result |
| --- | --- |
| Move a finger | Move the cursor (with glide/momentum on release) |
| Slide around the outer edge | Circular scroll |
| Tap | Left click |
| Double tap | Double click |
| Tap, then touch and hold | Left button held while your finger is down: **drag windows**, select text. Lift and touch again within 0.3 s to keep dragging |
| Touch and hold still (~0.5 s) | **Right click** |

While (and shortly after) you touch the trackpad, the **Mouse layer** turns on
automatically. On the right-hand home row:

| H | J | K | L |
| --- | --- | --- | --- |
| Drag scroll (hold) | Left click | Right click | Middle click |

All other keys pass through, and typing any other key turns the Mouse layer off
straight away. The Mouse-layer keys (and `Drag scroll`, under *User* keycodes) can be
remapped in Vial.

Two-finger taps aren't possible: in the absolute mode needed for glide and
circular scroll, the Pinnacle reports only one finger.

#### Tuning

Change these in `config.h` and rebuild:

| Define | Default | Meaning |
| --- | --- | --- |
| `TRACKPAD_TAP_TERM` | 200 ms | Longest touch that counts as a tap |
| `TRACKPAD_TAP_DRAG_TERM` | 200 ms | Time after a tap to start a drag / double tap (single clicks are sent after this) |
| `TRACKPAD_LONG_PRESS_TERM` | 500 ms | Hold time for right click |
| `TRACKPAD_DRAG_RELEASE_DELAY` | 300 ms | Grace period to re-touch and continue a drag (0 = off) |
| `TRACKPAD_MOVE_THRESHOLD` | 6 | Movement that cancels a tap / long press |
| `TRACKPAD_DRAG_SCROLL_DIVISOR` | 8 | Higher = slower drag scroll |
| `AUTO_MOUSE_TIME` | 650 ms | How long the Mouse layer stays on after you stop touching the trackpad |
| `I2C1_CLOCK_SPEED` | 400000 | Use 100000 if the trackpad is unreliable |

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
