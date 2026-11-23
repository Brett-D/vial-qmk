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
| Slide around the outer ring | Circular scroll: start on the **right** side of the ring for vertical scroll, on the **left** side for horizontal |
| Tap | Left click (double tap = double click) |

While (and shortly after) you touch the trackpad, the **Mouse layer** turns on
automatically:

| Key | Mouse layer |
| --- | --- |
| **T** | Left button — hold T and move your finger to **drag** windows / select text |
| **R** | Right button |
| H | Drag scroll (hold, then move your finger to scroll) |
| J / K / L | Left / right / middle button |

The layer stays on while a mouse key is held, turns off ~0.65 s after you lift
your finger, and turns off immediately when you press any other key, so typing
isn't affected. All of these keys (and `Drag scroll`, under *User* keycodes) can
be remapped in Vial.

Two-finger taps aren't possible: in the absolute mode needed for glide and
circular scroll, the Pinnacle reports only one finger.

#### Trackpad Z experiment (`TRACKPAD_Z_DEBUG`, on in this build)

Z is the Cirque's signal strength / contact size (0–63), not true force. To
see whether a light touch and a firm press are distinguishable:

* **Right OLED** shows `Z` (live, plus a bar; `Z` is highlighted while touching),
  `PEAK` (this touch), `LAST` (peak of the previous touch) and a `TOUCH` counter.
* **Browser**: open `trackpad_z.html` straight from disk in Edge/Chrome (close
  the Vial tab first), click *Connect*, then use *Record LIGHT* / *Record FIRM*
  and touch the pad ~10 times each. It graphs Z live and says whether the two
  groups overlap. It uses raw HID command `0xF5`.

Comment out `TRACKPAD_Z_DEBUG` in `config.h` to go back to the normal OLED status.

#### Tuning

Change these in `config.h` and rebuild:

| Define | Default | Meaning |
| --- | --- | --- |
| `TRACKPAD_TAP_TERM` | 200 ms | Longest touch that counts as a tap |
| `TRACKPAD_MOVE_THRESHOLD` | 6 | Movement (or scrolling) that cancels a tap |
| `TRACKPAD_DRAG_SCROLL_DIVISOR` | 8 | Higher = slower drag scroll |
| `TRACKPAD_LONG_PRESS_RIGHT_CLICK` | off | Touch and hold still = right click (can misfire during slow ring scrolls) |
| `TRACKPAD_LONG_PRESS_TERM` | 500 ms | Hold time for the long-press right click |
| `AUTO_MOUSE_TIME` | 650 ms | How long the Mouse layer stays on after you lift your finger |
| `CIRQUE_PINNACLE_TIMEOUT` | 5 ms | Give up on a failed trackpad I2C transfer after this long |

The I2C bus runs at QMK's 100 kHz default, like the original firmware.
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
