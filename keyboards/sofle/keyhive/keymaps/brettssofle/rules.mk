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

# Layout (readme.md, "Code layout"): one folder per area; generated/ holds
# everything tools/gen.py writes (not in git).
EXTRAINCDIRS += $(addprefix $(BRETTS_DIR)/, common config leds oled pet trackpad generated)

SRC += common/diag.c common/eeprom_layout.c common/brett_util.c common/user_config.c common/kb_config.c common/perf.c common/macros.c
SRC += trackpad/trackpad.c trackpad/trackpad_z.c trackpad/wiggler.c
SRC += oled/oled.c oled/oled_pages.c oled/page_key.c oled/display.c
SRC += pet/pet.c pet/pet_draw.c
OPT_DEFS += -DPET_ENABLE
# Config mode: keyboard-only mode (keystroke bridge), the web session over raw
# HID, the right-OLED screens, typed page delivery.
SRC += config/config_mode.c config/config_hid.c config/config_oled.c config/bridge.c config/deliver.c config/config_page_blob.c
# Generated files (pet art, the config pages, the page blob deliver.c types and
# net/ serves): rebuilt from page/ and tools/ at every build so they never
# drift. Stops the build if a generator fails (old outputs must not be used).
BRETTS_GEN := $(shell (python3 $(BRETTS_DIR)/tools/gen.py || python $(BRETTS_DIR)/tools/gen.py) 2>&1; echo "gen_rc=$$?")
$(info $(filter-out gen_rc=%,$(BRETTS_GEN)))
ifneq ($(lastword $(BRETTS_GEN)),gen_rc=0)
    $(error generated/ could not be made (needs Python 3): $(BRETTS_GEN))
endif
ifeq ($(strip $(RGB_MATRIX_ENABLE)), yes)
    SRC += leds/rgb_driver.c leds/rgb_layout.c leds/led_map.c leds/led_cal.c leds/led_fx.c leds/led_fx_draw.c leds/screensaver.c
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
