# Build: make sofle/keyhive:brettssofle -e CONVERT_TO=rp2040_ce
# Hardware: Keyhive Sofle v2, Sea-Picro EXT (RP2040) on both halves,
# Cirque Pinnacle trackpad on the left (USB) half, OLED on the right half.

VIA_ENABLE = yes
VIAL_ENABLE = yes
QMK_SETTINGS = yes

RGBLIGHT_ENABLE = yes
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

SRC += trackpad.c oled.c
