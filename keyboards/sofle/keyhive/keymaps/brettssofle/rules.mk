# Build: make sofle/keyhive:brettssofle -e CONVERT_TO=rp2040_ce
# Hardware: Keyhive Sofle v2, Sea-Picro EXT (RP2040) on both halves,
# Cirque Pinnacle trackpad on the left (USB) half, OLED on the right half.

VIA_ENABLE = yes
VIAL_ENABLE = yes
QMK_SETTINGS = yes

# Per-key RGB Matrix (layout in keyhive/keyboard.json) with Vial's VialRGB.
RGB_MATRIX_ENABLE = yes
VIALRGB_ENABLE = yes
# rgb_driver.c wraps the WS2812 driver so each half only drives its own LEDs.
RGB_MATRIX_DRIVER = custom
# rgb_matrix_user.inc: LAYERED (whole-keyboard + key-press effect, led_layers.c).
RGB_MATRIX_CUSTOM_USER = yes
WS2812_DRIVER_REQUIRED = yes
ENCODER_MAP_ENABLE = yes
EXTRAKEY_ENABLE = yes
MOUSEKEY_ENABLE = yes
OLED_ENABLE = yes

POINTING_DEVICE_ENABLE ?= yes
POINTING_DEVICE_DRIVER = cirque_pinnacle_i2c

# Not used on this board.
BOOTMAGIC_ENABLE = no
COMMAND_ENABLE = no
BACKLIGHT_ENABLE = no
AUDIO_ENABLE = no

LTO_ENABLE = yes

# Debug output for the trackpad gestures (view with `qmk console` or hid_listen).
# Build with: make ... -e TRACKPAD_DEBUG=yes
TRACKPAD_DEBUG ?= no
ifeq ($(strip $(TRACKPAD_DEBUG)), yes)
    CONSOLE_ENABLE = yes
    OPT_DEFS += -DTRACKPAD_DEBUG -DPOINTING_DEVICE_DEBUG
else
    CONSOLE_ENABLE = no
endif

SRC += trackpad.c trackpad_z.c oled.c oled_pages.c
# Config mode + keystroke bridge (config_page/sofle-config.html). After
# editing config_page/gen_config.py or page.html, rerun:
#   python keyboards/sofle/keyhive/keymaps/brettssofle/config_page/gen_config.py
SRC += config_mode.c bridge.c wiggler.c deliver.c
# Typed page delivery: config_page_blob.h is the page as deliver.c types it,
# regenerated from sofle-config.html at every build so the two never drift.
CONFIG_PAGE_BLOB_GEN := $(shell python3 $(dir $(lastword $(MAKEFILE_LIST)))config_page/gen_page_blob.py 2>&1 || python $(dir $(lastword $(MAKEFILE_LIST)))config_page/gen_page_blob.py 2>&1)
$(info $(CONFIG_PAGE_BLOB_GEN))
ifeq ($(strip $(RGB_MATRIX_ENABLE)), yes)
    SRC += rgb_driver.c rgb_layout.c led_map.c led_cal.c led_layers.c
endif
