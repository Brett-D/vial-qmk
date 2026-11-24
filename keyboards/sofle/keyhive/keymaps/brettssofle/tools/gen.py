#!/usr/bin/env python3
"""The keymap's generated files, from one place. They all go to generated/
(not in git); rules.mk runs this at every build.

  python tools/gen.py                              pet art, both config pages
                                                   and the page blob (the build)
  python tools/gen.py rgb-layout [--map led.json]  leds/rgb_layout.c, the built-in
                                                   LED map (a committed source)
  python tools/gen.py pet-sheet out.png            preview sheet of the pet art
                                                   (needs Pillow)

Order for the build: gen_pet_art (pet_art.h / .json) -> gen_config (the two
pages and config_picker.h; it reads pet_art.json) -> gen_page_blob (the gzipped
pages and the typed loader, config_page_blob.h).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


def main(argv):
    if argv[:1] == ["rgb-layout"]:
        import gen_rgb_layout
        sys.argv = ["gen_rgb_layout.py"] + argv[1:]
        gen_rgb_layout.main()
        return
    import gen_pet_art
    if argv[:1] == ["pet-sheet"]:
        sys.argv = ["gen_pet_art.py", "--sheet"] + argv[1:]
        gen_pet_art.main()
        return
    if argv:
        sys.exit(__doc__)
    sys.argv = ["gen_pet_art.py"]
    gen_pet_art.main()
    import gen_config
    gen_config.main()  # ends with gen_page_blob.main()


if __name__ == "__main__":
    main(sys.argv[1:])
