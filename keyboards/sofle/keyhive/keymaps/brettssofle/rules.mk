# Build: make sofle/keyhive:brettssofle -e CONVERT_TO=rp2040_ce
BRETTS_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
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
# editing config_page/gen_config.py, page.html or light.html, rerun:
#   python keyboards/sofle/keyhive/keymaps/brettssofle/config_page/gen_config.py
SRC += config_mode.c bridge.c wiggler.c deliver.c config_page_blob.c diag.c eeprom_layout.c pet.c pet_draw.c page_key.c
OPT_DEFS += -DPET_ENABLE
# Typed page delivery: config_page_blob.h is the page as deliver.c types it,
# regenerated from sofle-config.html / sofle-light.html at every build so they never drift.
CONFIG_PAGE_BLOB_GEN := $(shell python3 $(BRETTS_DIR)/config_page/gen_page_blob.py 2>&1 || python $(BRETTS_DIR)/config_page/gen_page_blob.py 2>&1)
$(info $(CONFIG_PAGE_BLOB_GEN))
ifeq ($(strip $(RGB_MATRIX_ENABLE)), yes)
    SRC += rgb_driver.c rgb_layout.c led_map.c led_cal.c led_layers.c
endif

# USB network config mode (net/): the keyboard re-enumerates with a CDC-NCM
# network adapter and serves the config page at http://192.168.7.1 (lwIP).
# lwIP ships inside ChibiOS as an archive; extract it once:
#   7z x lib/chibios/ext/lwip-2.1.2.7z -olib/chibios/ext
# (or: python -m pip install py7zr && python -c "import py7zr; py7zr.SevenZipFile('lib/chibios/ext/lwip-2.1.2.7z').extractall('lib/chibios/ext')")
NETWORK_ENABLE ?= yes
ifeq ($(strip $(NETWORK_ENABLE)), yes)
    LWIP_DIR := lib/chibios/ext/lwip/src
    ifeq ($(wildcard $(LWIP_DIR)/core/init.c),)
        $(error lwIP not extracted: run  7z x lib/chibios/ext/lwip-2.1.2.7z -olib/chibios/ext  (or build with NETWORK_ENABLE=no))
    endif
    OPT_DEFS += -DNETWORK_ENABLE -DNET_DIAG
    EXTRAINCDIRS += $(BRETTS_DIR)/net $(LWIP_DIR)/include
    SRC += net/ncm.c net/ntb.c net/stack.c net/httpd.c net/api.c
    SRC += $(addprefix $(LWIP_DIR)/, core/init.c core/def.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c \
        core/netif.c core/pbuf.c core/sys.c core/tcp.c core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c \
        core/ipv4/etharp.c core/ipv4/icmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c core/ipv4/ip4_frag.c netif/ethernet.c)
endif
