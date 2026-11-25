"""Where the keymap's sources and generated files live (shared by the generators)."""
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
KEYMAP = TOOLS.parent                # keyboards/sofle/keyhive/keymaps/brettssofle
QMK_ROOT = TOOLS.parents[5]
PAGE = KEYMAP / "page"               # config page sources (HTML / JS)
LEDS = KEYMAP / "leds"               # led_fx_table.h, led_fx_core.h, rgb_layout.c
GEN = KEYMAP / "generated"           # everything the generators write (not in git)
GEN.mkdir(exist_ok=True)


def write(path, text):
    """Writes text (LF) only if it changed, so a build doesn't recompile what didn't.
    open() rather than Path.write_text(newline=...), which needs Python 3.10."""
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
