#!/usr/bin/env python3
# Copyright 2026 Brett-D
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the config-mode keycode picker (C) and the configurator page (HTML).

Both come from the same picker list, so the firmware and the page always agree
on picker indexes. Run by tools/gen.py (every build).

Outputs (generated/):
    config_picker.h           compiled into the firmware
    sofle-config.html         the page (open it from disk in any browser)
    sofle-light.html          the light page
"""

import json
import re
from pathlib import Path

from paths import write, GEN, KEYMAP as KEYMAP_DIR, LEDS, PAGE, QMK_ROOT

# ---------------------------------------------------------------------------
# Picker: categories of keycodes the right knob scrolls through.
# Entries are C expressions; the generator evaluates them for the page.
# ---------------------------------------------------------------------------
LETTERS = [f"KC_{c}" for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"]
DIGITS = [f"KC_{c}" for c in "1234567890"]

PICKER = [
    ("Basic", ["KC_NO", "KC_TRNS"] + LETTERS + DIGITS + [
        "KC_ENTER", "KC_ESCAPE", "KC_BACKSPACE", "KC_TAB", "KC_SPACE", "KC_MINUS", "KC_EQUAL",
        "KC_LEFT_BRACKET", "KC_RIGHT_BRACKET", "KC_BACKSLASH", "KC_SEMICOLON", "KC_QUOTE", "KC_GRAVE",
        "KC_COMMA", "KC_DOT", "KC_SLASH", "KC_CAPS_LOCK"]),
    ("Symbols", [f"S({k})" for k in [
        "KC_GRAVE", "KC_1", "KC_2", "KC_3", "KC_4", "KC_5", "KC_6", "KC_7", "KC_8", "KC_9", "KC_0",
        "KC_MINUS", "KC_EQUAL", "KC_LEFT_BRACKET", "KC_RIGHT_BRACKET", "KC_BACKSLASH", "KC_SEMICOLON",
        "KC_QUOTE", "KC_COMMA", "KC_DOT", "KC_SLASH"]]),
    ("Navigation", ["KC_LEFT", "KC_DOWN", "KC_UP", "KC_RIGHT", "KC_HOME", "KC_END", "KC_PAGE_UP",
                    "KC_PAGE_DOWN", "KC_INSERT", "KC_DELETE", "KC_PRINT_SCREEN", "KC_SCROLL_LOCK",
                    "KC_PAUSE", "KC_APPLICATION"]),
    ("Modifiers", ["KC_LEFT_CTRL", "KC_LEFT_SHIFT", "KC_LEFT_ALT", "KC_LEFT_GUI",
                   "KC_RIGHT_CTRL", "KC_RIGHT_SHIFT", "KC_RIGHT_ALT", "KC_RIGHT_GUI"]),
    ("Function", [f"KC_F{i}" for i in range(1, 25)]),
    ("Media", ["KC_AUDIO_MUTE", "KC_AUDIO_VOL_UP", "KC_AUDIO_VOL_DOWN", "KC_MEDIA_PLAY_PAUSE",
               "KC_MEDIA_NEXT_TRACK", "KC_MEDIA_PREV_TRACK", "KC_MEDIA_STOP", "KC_BRIGHTNESS_UP",
               "KC_BRIGHTNESS_DOWN", "KC_CALCULATOR", "KC_MY_COMPUTER", "KC_WWW_SEARCH",
               "KC_WWW_BACK", "KC_WWW_FORWARD", "KC_WWW_REFRESH"]),
    ("Mouse", ["MS_BTN1", "MS_BTN2", "MS_BTN3", "MS_BTN4", "MS_BTN5", "MS_UP", "MS_DOWN", "MS_LEFT",
               "MS_RGHT", "MS_WHLU", "MS_WHLD", "MS_WHLL", "MS_WHLR", "MS_ACL0", "MS_ACL1", "MS_ACL2"]),
    ("Layers", [f"MO({i})" for i in range(1, 6)] + [f"TG({i})" for i in range(1, 6)]
               + [f"TO({i})" for i in range(0, 6)] + [f"OSL({i})" for i in range(1, 6)]
               + [f"TT({i})" for i in range(1, 6)] + ["PDF(0)", "PDF(1)"]),
    ("Numpad", ["KC_NUM_LOCK", "KC_KP_SLASH", "KC_KP_ASTERISK", "KC_KP_MINUS", "KC_KP_PLUS",
                "KC_KP_ENTER"] + [f"KC_KP_{i}" for i in range(1, 10)] + ["KC_KP_0", "KC_KP_DOT", "KC_KP_EQUAL"]),
    ("Lighting", ["RM_TOGG", "RM_NEXT", "RM_PREV", "RM_HUEU", "RM_HUED", "RM_SATU", "RM_SATD",
                  "RM_VALU", "RM_VALD", "RM_SPDU", "RM_SPDD"]),
    ("Special", ["CONFIG_MODE", "OLED_PAGE", "WIGGLE", "CYCLE", "DRAG_SCROLL", "CW_TOGG", "QK_BOOT"]),
]

# Short labels for the page (anything missing falls back to the QMK name).
LABELS = {
    "KC_NO": "", "KC_TRNS": "▽", "KC_ENTER": "Enter", "KC_ESCAPE": "Esc", "KC_BACKSPACE": "Bksp",
    "KC_TAB": "Tab", "KC_SPACE": "Space", "KC_MINUS": "-", "KC_EQUAL": "=", "KC_LEFT_BRACKET": "[",
    "KC_RIGHT_BRACKET": "]", "KC_BACKSLASH": "\\", "KC_SEMICOLON": ";", "KC_QUOTE": "'",
    "KC_GRAVE": "`", "KC_COMMA": ",", "KC_DOT": ".", "KC_SLASH": "/", "KC_CAPS_LOCK": "Caps",
    "KC_LEFT": "←", "KC_DOWN": "↓", "KC_UP": "↑", "KC_RIGHT": "→", "KC_HOME": "Home", "KC_END": "End",
    "KC_PAGE_UP": "PgUp", "KC_PAGE_DOWN": "PgDn", "KC_INSERT": "Ins", "KC_DELETE": "Del",
    "KC_PRINT_SCREEN": "PrtSc", "KC_SCROLL_LOCK": "ScrLk", "KC_PAUSE": "Pause", "KC_APPLICATION": "Menu",
    "KC_LEFT_CTRL": "LCtrl", "KC_LEFT_SHIFT": "LShift", "KC_LEFT_ALT": "LAlt", "KC_LEFT_GUI": "LGui",
    "KC_RIGHT_CTRL": "RCtrl", "KC_RIGHT_SHIFT": "RShift", "KC_RIGHT_ALT": "RAlt", "KC_RIGHT_GUI": "RGui",
    "KC_AUDIO_MUTE": "Mute", "KC_AUDIO_VOL_UP": "Vol+", "KC_AUDIO_VOL_DOWN": "Vol-",
    "KC_MEDIA_PLAY_PAUSE": "Play", "KC_MEDIA_NEXT_TRACK": "Next", "KC_MEDIA_PREV_TRACK": "Prev",
    "KC_MEDIA_STOP": "Stop", "KC_BRIGHTNESS_UP": "Bri+", "KC_BRIGHTNESS_DOWN": "Bri-",
    "KC_CALCULATOR": "Calc", "KC_MY_COMPUTER": "MyPC", "KC_WWW_SEARCH": "Search", "KC_WWW_BACK": "Back",
    "KC_WWW_FORWARD": "Fwd", "KC_WWW_REFRESH": "Refresh",
    "MS_BTN1": "Click L", "MS_BTN2": "Click R", "MS_BTN3": "Click M", "MS_BTN4": "Btn4", "MS_BTN5": "Btn5",
    "MS_UP": "Ms↑", "MS_DOWN": "Ms↓", "MS_LEFT": "Ms←", "MS_RGHT": "Ms→", "MS_WHLU": "Whl↑",
    "MS_WHLD": "Whl↓", "MS_WHLL": "Whl←", "MS_WHLR": "Whl→", "MS_ACL0": "Accel0", "MS_ACL1": "Accel1",
    "MS_ACL2": "Accel2", "KC_NUM_LOCK": "NumLk", "KC_KP_SLASH": "P/", "KC_KP_ASTERISK": "P*",
    "KC_KP_MINUS": "P-", "KC_KP_PLUS": "P+", "KC_KP_ENTER": "PEnt", "KC_KP_DOT": "P.", "KC_KP_EQUAL": "P=",
    "RM_TOGG": "RGB on/off", "RM_NEXT": "RGB mode+", "RM_PREV": "RGB mode-", "RM_HUEU": "Hue+",
    "RM_HUED": "Hue-", "RM_SATU": "Sat+", "RM_SATD": "Sat-", "RM_VALU": "Bright+", "RM_VALD": "Bright-",
    "RM_SPDU": "Speed+", "RM_SPDD": "Speed-", "CW_TOGG": "CapsWord", "QK_BOOT": "Bootloader",
    "CONFIG_MODE": "Config mode", "OLED_PAGE": "OLED page", "WIGGLE": "Mouse wiggle", "CYCLE": "Cycle base", "DRAG_SCROLL": "Drag scroll",
}
for i in range(10):
    LABELS[f"KC_KP_{i}"] = f"P{i}"

LAYER_NAMES = ["QWERTY", "Colemak", "Lower", "Raise", "Mouse", "Adjust"]

# Friendlier names for some VialRGB effects (the rest are derived from the enum).
EFFECT_NAMES = {
    "OFF": "Off", "SOLID_COLOR": "Solid colour", "ALPHAS_MODS": "Letters / mods two-tone",
    "CYCLE_LEFT_RIGHT": "Rainbow sweep (left-right)", "CYCLE_ALL": "Rainbow (all keys)",
    "CYCLE_OUT_IN": "Rainbow (outside in)", "RAINBOW_MOVING_CHEVRON": "Rainbow chevron",
    "TYPING_HEATMAP": "Typing heatmap", "DIGITAL_RAIN": "Digital rain",
    "SOLID_REACTIVE_SIMPLE": "Reactive: pressed key lights",
    "SOLID_REACTIVE": "Reactive: pressed key hue shift",
    "SOLID_REACTIVE_WIDE": "Reactive: glow around key", "SOLID_REACTIVE_MULTIWIDE": "Reactive: glow around keys (multi)",
    "SOLID_REACTIVE_CROSS": "Reactive: cross from key", "SOLID_REACTIVE_MULTICROSS": "Reactive: cross from keys (multi)",
    "SOLID_REACTIVE_NEXUS": "Reactive: nexus lines", "SOLID_REACTIVE_MULTINEXUS": "Reactive: nexus lines (multi)",
    "SPLASH": "Splash: rainbow burst from key", "MULTISPLASH": "Splash: rainbow bursts (multi)",
    "SOLID_SPLASH": "Splash: colour burst from key", "SOLID_MULTISPLASH": "Splash: colour bursts (multi)",
}


# Key-press effects (led_fx.c): REACT ones can go over a whole-keyboard
# effect, SOLO ones paint every key themselves. Everything else is a
# whole-keyboard effect.
REACT_EFFECTS = ["SOLID_REACTIVE_SIMPLE", "SOLID_REACTIVE_WIDE", "SOLID_REACTIVE_MULTIWIDE", "SOLID_REACTIVE_CROSS",
                 "SOLID_REACTIVE_MULTICROSS", "SOLID_REACTIVE_NEXUS", "SOLID_REACTIVE_MULTINEXUS", "SPLASH",
                 "MULTISPLASH", "SOLID_SPLASH", "SOLID_MULTISPLASH"]
SOLO_EFFECTS = ["SOLID_REACTIVE", "TYPING_HEATMAP"]
LAYERED_ID = 0xFE  # LED_LAYERS_VIALRGB_ID (led_fx.h)


def vialrgb_effect_enum():
    text = (QMK_ROOT / "quantum" / "vialrgb_effects.inc").read_text(encoding="utf-8")
    body = text[text.index("enum {"):text.index("};")]
    return re.findall(r"VIALRGB_EFFECT_([A-Z0-9_]+)", body)


def parse_vialrgb_effects():
    """VialRGB effect id -> name, from quantum/vialrgb_effects.inc."""
    names = {i: EFFECT_NAMES.get(n, n.replace("_", " ").capitalize()) for i, n in enumerate(vialrgb_effect_enum())}
    names[LAYERED_ID] = "Whole keyboard + key presses"
    return names

def parse_fx():
    """The lighting effects (led_fx_table.h), the sine table (led_fx_core.h)
    and the LED layout (rgb_layout.c): the page's preview uses the same
    tables as the firmware (FX core in fx_core.js)."""
    table = (LEDS / "led_fx_table.h").read_text(encoding="utf-8")
    kinds = {k: int(v) for k, v in re.findall(r"#define FXK_(\w+) (\d+)", table)}

    def rows(macro):
        body = table[table.index(f"#define {macro}(X)"):]
        body = body[:body.index("\n\n") if "\n\n" in body else len(body)]
        out = []
        for i, name_c, label, kind, hue in re.findall(r'X\((\d+), (\w+), "([^"]+)", ([A-Z_ |]+), (\d+)\)', body):
            out.append([int(i), name_c, label, sum(kinds[k.strip()[4:]] for k in kind.split("|")), int(hue)])
        return out

    core = (LEDS / "led_fx_core.h").read_text(encoding="utf-8")
    sin = [int(v) for v in re.search(r"fx_sin_q\[65\] = \{([^}]*)\}", core).group(1).replace("\n", " ").split(",")]
    gamma = [int(v) for v in re.search(r"fx_gamma_q\[65\] = \{([^}]*)\}", core).group(1).replace("\n", " ").split(",")]
    assert len(gamma) == 65
    lay = (LEDS / "rgb_layout.c").read_text(encoding="utf-8")
    blocks = lay[lay.index("#define LED_CONFIG_DEFAULT"):].split("}, { \\")
    matrix = [[int(v) if v.strip() != "NO_LED" else None for v in row.split(",") if v.strip()] for row in re.findall(r"\{([^{}]*NO_LED[^{}]*)\}", blocks[0])]
    points = [[int(a), int(b)] for a, b in re.findall(r"\{\s*(\d+),\s*(\d+)\}", blocks[1])]
    flags = [1 if f == "KEYLIGHT" else 2 for f in re.findall(r"LED_FLAG_(\w+)", blocks[2])]
    key = {led: [r, cc] for r, row in enumerate(matrix) for cc, led in enumerate(row) if led is not None}
    leds = [[x, y, flags[i]] + key.get(i, []) for i, (x, y) in enumerate(points)]
    assert len(sin) == 65 and len(leds) == 74, (len(sin), len(leds))
    fx = {"kinds": kinds, "ownBg": int(re.search(r"#define FX_OWN_BG (0x\w+)", table).group(1), 16),
          "ownSolo": int(re.search(r"#define FX_OWN_SOLO (0x\w+)", table).group(1), 16),
          "backgrounds": rows("FX_OWN_BACKGROUNDS"), "solos": rows("FX_OWN_SOLOS"), "overlays": rows("FX_OVERLAYS"), "qmk": rows("FX_QMK_FLAGS")}
    fx["gamma"] = gamma
    return fx, sin, leds


LED_IDLE_LABELS = ["Never", "1 min", "2 min", "5 min", "10 min", "30 min"]


def parse_keycodes():
    """Name -> value from quantum/keycodes.h (enum entries and aliases)."""
    text = (QMK_ROOT / "quantum" / "keycodes.h").read_text(encoding="utf-8")
    values = {}
    for name, val in re.findall(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*([A-Za-z0-9_]+)\s*,", text, re.M):
        if val.startswith("0x"):
            values[name] = int(val, 16)
        elif val in values:
            values[name] = values[val]
    return values


def parse_us_shifted():
    """Value of S(KC_x) -> symbol, from keymap_us.h comments."""
    text = (QMK_ROOT / "quantum" / "keymap_extras" / "keymap_us.h").read_text(encoding="utf-8")
    return re.findall(r"#define\s+KC_[A-Z]+\s+S\((KC_[A-Z0-9_]+)\)\s*//\s*(\S+)", text)


def main():
    kc = parse_keycodes()
    vial = json.loads((KEYMAP_DIR / "vial.json").read_text(encoding="utf-8"))
    custom = vial.get("customKeycodes", [])
    c_custom_names = ["CYCLE", "DRAG_SCROLL", "CONFIG_MODE", "OLED_PAGE", "WIGGLE"]
    for i, name in enumerate(c_custom_names):
        kc[name] = kc["QK_KB_0"] + i if "QK_KB_0" in kc else 0x7E00 + i

    fn = {
        "S": lambda v: 0x0200 | v, "MO": lambda n: kc["QK_MOMENTARY"] | n, "TG": lambda n: kc["QK_TOGGLE_LAYER"] | n,
        "TO": lambda n: kc["QK_TO"] | n, "OSL": lambda n: kc["QK_ONE_SHOT_LAYER"] | n,
        "TT": lambda n: kc["QK_LAYER_TAP_TOGGLE"] | n, "PDF": lambda n: kc["QK_PERSISTENT_DEF_LAYER"] | n,
    }

    def evaluate(expr):
        m = re.fullmatch(r"(\w+)\((\w+)\)", expr)
        if m:
            arg = m.group(2)
            return fn[m.group(1)](int(arg) if arg.isdigit() else kc[arg])
        return kc[expr]

    shifted = {kc[base] | 0x0200: sym for base, sym in parse_us_shifted() if base in kc}

    picker, cats, cat_starts = [], [], []
    for cat, exprs in PICKER:
        cat_starts.append(len(picker))
        cats.append(cat)
        for e in exprs:
            v = evaluate(e)
            if e.startswith("S("):
                label = shifted.get(v, e)
            elif e in LABELS:
                label = LABELS[e]
            else:
                label = re.sub(r"^KC_", "", e)
            picker.append((e, v, label))

    # Names for every basic/quantum keycode the page may need to display.
    names = {}
    for name, v in sorted(kc.items(), key=lambda x: (len(x[0]), x[0])):
        if v not in names and not name.endswith("_MAX") and not re.match(r"QK_(BASIC|MODS|KB_|USER_)", name):
            names[v] = LABELS.get(name, re.sub(r"^(KC_|QK_)", "", name))
    for e, v, label in picker:
        names[v] = label
    for i, ck in enumerate(custom):
        names[kc["QK_KB_0"] + i] = ck.get("shortName", f"KB{i}")
    names.update(shifted)

    def crc16(data):
        crc = 0xFFFF
        for b in data:
            crc ^= b << 8
            for _ in range(8):
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        return crc

    raw = b"".join(v.to_bytes(2, "little") for _, v, _ in picker) + b"".join(s.to_bytes(2, "little") for s in cat_starts)
    phash = crc16(raw)

    # --- C header ---------------------------------------------------------
    lines = [
        "// Generated by tools/gen_config.py - do not edit.",
        "#pragma once",
        "",
        f"#define CFG_PICKER_HASH 0x{phash:04X}",
        f"#define CFG_PICKER_COUNT {len(picker)}",
        f"#define CFG_PICKER_CATS {len(cats)}",
        "",
        "static const uint16_t PROGMEM cfg_picker[CFG_PICKER_COUNT] = {",
    ]
    for n, (cat, start) in enumerate(zip(cats, cat_starts)):
        end = cat_starts[n + 1] if n + 1 < len(cat_starts) else len(picker)
        lines.append(f"    // {cat} ({start})")
        group = [e for e, _, _ in picker[start:end]]
        for i in range(0, len(group), 8):
            lines.append("    " + ", ".join(group[i:i + 8]) + ",")
    lines += ["};", "", "static const uint16_t PROGMEM cfg_picker_cat_start[CFG_PICKER_CATS] = {" + ", ".join(map(str, cat_starts)) + "};", ""]
    write(GEN / "config_picker.h", "\n".join(lines))

    # --- Layout from keyboard.json ---------------------------------------
    kb = json.loads((QMK_ROOT / "keyboards" / "sofle" / "keyhive" / "keyboard.json").read_text(encoding="utf-8"))
    layout = [{"r": k["matrix"][0], "c": k["matrix"][1], "x": k["x"], "y": k["y"], "h": k.get("h", 1)}
              for k in kb["layouts"]["LAYOUT"]["layout"]]

    fx_table, fx_sin, fx_leds = parse_fx()
    led_effects = parse_vialrgb_effects()
    for rows_ in (fx_table["backgrounds"], fx_table["solos"]):
        for idx, _, label, _, _ in rows_:
            led_effects[(fx_table["ownBg"] if rows_ is fx_table["backgrounds"] else fx_table["ownSolo"]) + idx] = label
    data = {
        "pickerHash": phash,
        "rows": vial["matrix"]["rows"],
        "cols": vial["matrix"]["cols"],
        "usb": {"vid": int(vial["vendorId"], 16), "pid": int(vial["productId"], 16)},
        "picker": [[v, label] for _, v, label in picker],
        "cats": cats,
        "catStarts": cat_starts,
        "names": {str(k): v for k, v in sorted(names.items())},
        "layout": layout,
        "layerNames": LAYER_NAMES,
        "ledEffects": led_effects,
        "fxTable": fx_table, "fxSin": fx_sin, "fxLeds": fx_leds, "fxGamma": fx_table.pop("gamma"),
        "ledReact": [vialrgb_effect_enum().index(n) for n in REACT_EFFECTS],
        "ledSolo": [vialrgb_effect_enum().index(n) for n in SOLO_EFFECTS],
        "ledLayeredId": LAYERED_ID,
        "ledDirectId": vialrgb_effect_enum().index("DIRECT"),
        "ledZones": ["Effect on keys + underglow", "Effect on keys, solid underglow", "Keys only (underglow off)"],
        "petArt": json.loads((GEN / "pet_art.json").read_text(encoding="utf-8")),  # gen_pet_art.py
        "ledIdle": LED_IDLE_LABELS,
        "ranges": {k: kc[k] for k in ["QK_MODS", "QK_MOD_TAP", "QK_LAYER_TAP", "QK_LAYER_MOD", "QK_TO",
                                      "QK_MOMENTARY", "QK_DEF_LAYER", "QK_TOGGLE_LAYER", "QK_ONE_SHOT_LAYER",
                                      "QK_ONE_SHOT_MOD", "QK_LAYER_TAP_TOGGLE", "QK_PERSISTENT_DEF_LAYER",
                                      "QK_TAP_DANCE", "QK_MACRO", "QK_KB", "QK_USER"]},
    }
    # page.html is the markup and styles; its script is page/js/*.js, in order.
    script = "".join(f.read_text(encoding="utf-8") for f in sorted((PAGE / "js").glob("*.js")))
    template = (PAGE / "page.html").read_text(encoding="utf-8").replace("/*__PAGE_JS__*/", script)
    page = template.replace("/*__DATA__*/null", json.dumps(data, ensure_ascii=False, separators=(",", ":")))
    page = page.replace("/*__FX_CORE__*/", (PAGE / "fx_core.js").read_text(encoding="utf-8"))
    page = page.replace("/*__PET_CORE__*/", (PAGE / "pet_core.js").read_text(encoding="utf-8"))  # pet motion (pet_motion.h)
    write(GEN / "sofle-config.html", page)
    print(f"picker: {len(picker)} keycodes, {len(cats)} categories, hash 0x{phash:04X}; page {len(page.encode())} bytes")

    # The light page (the one the keyboard types): only what it uses.
    effects = data["ledEffects"]
    skip = set(data["ledReact"]) | set(data["ledSolo"]) | {data["ledDirectId"], data["ledLayeredId"], 0}
    light = {
        "pickerHash": phash, "rows": data["rows"], "cols": data["cols"], "usb": data["usb"],
        "picker": data["picker"], "cats": cats, "catStarts": cat_starts, "layout": layout, "layerNames": LAYER_NAMES,
        "ledLayeredId": data["ledLayeredId"], "ledKeyFx": data["ledReact"] + data["ledSolo"],
        "ledZones": data["ledZones"], "ledIdle": data["ledIdle"],
        # Names of the other effects, for keyboard-only mode (its Effect field can be any of them).
        "ledOther": [[int(i), n] for i, n in sorted(((int(i), n) for i, n in effects.items()), key=lambda x: x[0]) if int(i) in skip and int(i)],
        "ledBase": [[int(i), n] for i, n in sorted(((int(i), n) for i, n in effects.items()), key=lambda x: x[0]) if int(i) not in skip and int(i) < data["fxTable"]["ownSolo"]],
        "ranges": {k: data["ranges"][k] for k in ["QK_MOD_TAP", "QK_LAYER_TAP", "QK_LAYER_MOD", "QK_TO", "QK_MOMENTARY",
                                                  "QK_TOGGLE_LAYER", "QK_ONE_SHOT_LAYER", "QK_LAYER_TAP_TOGGLE"]},
    }
    template = (PAGE / "light.html").read_text(encoding="utf-8")
    lpage = template.replace("/*__DATA__*/null", json.dumps(light, ensure_ascii=False, separators=(",", ":")))
    write(GEN / "sofle-light.html", lpage)
    print(f"light page {len(lpage.encode())} bytes")
    import gen_page_blob  # the copy the keyboard types (config_page_blob.h)
    gen_page_blob.main()


if __name__ == "__main__":
    main()
