// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime LED map: which physical LED (chain index 0-36 left, 37-73 right) is
// under which key, as measured with the config page's calibration wizard.
// Saved in the EEPROM keyboard datablock on the master, applied to g_led_config
// on both halves (the right half gets it over split RPC), so a measured map
// needs no rebuild. Without a saved map, rgb_layout.c's built-in one is used.
//
// Map entry per LED: row * MATRIX_COLS + col for a key, LED_MAP_UNDERGLOW, or
// LED_MAP_NONE (nothing lit / unused: never lit by effects).

#include "brettssofle.h"
#include "led_map.h"
#include "transactions.h"

#ifdef RGB_MATRIX_ENABLE

#    define LEDMAP_MAGIC 0x4D
#    define LEDMAP_RESEND_MS 5000
#    define LEDMAP_CHUNK 26
#    define LEDMAP_CHUNKS ((LED_MAP_COUNT + LEDMAP_CHUNK - 1) / LEDMAP_CHUNK)

typedef struct {
    uint8_t magic;
    uint8_t map[LED_MAP_COUNT];
    uint8_t sum;
} ledmap_store_t;
_Static_assert(sizeof(ledmap_store_t) <= EECONFIG_KB_DATA_SIZE, "raise EECONFIG_KB_DATA_SIZE");

extern const led_config_t led_config_default;
extern const uint8_t      key_led_pos[MATRIX_ROWS][MATRIX_COLS][2];

static uint8_t active[LED_MAP_COUNT];
static uint8_t pending[LED_MAP_COUNT];
static bool    measured;

// Master -> right half sync.
static uint8_t  sync_version = 1;
static uint8_t  sync_pending = (1 << LEDMAP_CHUNKS) - 1;
static uint16_t sync_time;
static uint8_t  rx_version, rx_mask;
static uint8_t  rx_buf[LED_MAP_COUNT];
static uint8_t  right_sum;
static bool     right_known;

static uint8_t checksum(const uint8_t *map) {
    uint8_t sum = LEDMAP_MAGIC;
    for (uint8_t i = 0; i < LED_MAP_COUNT; i++) {
        sum += map[i];
    }
    return sum;
}

// Order-sensitive (unlike checksum()), for the right-half sync check.
static uint8_t map_crc(const uint8_t *map) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < LED_MAP_COUNT; i++) {
        crc ^= map[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = crc & 0x80 ? (uint8_t)(crc << 1) ^ 0x07 : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool led_map_valid(const uint8_t *map) {
    bool seen[MATRIX_ROWS * MATRIX_COLS] = {false};
    for (uint8_t i = 0; i < LED_MAP_COUNT; i++) {
        uint8_t c = map[i];
        if (c < MATRIX_ROWS * MATRIX_COLS) {
            if (seen[c] || key_led_pos[c / MATRIX_COLS][c % MATRIX_COLS][0] == 0) {
                return false; // duplicate key, or not a key position
            }
            seen[c] = true;
        } else if (c != LED_MAP_UNDERGLOW && c != LED_MAP_NONE) {
            return false;
        }
    }
    return true;
}

static void map_from_default(uint8_t *map) {
    for (uint8_t i = 0; i < LED_MAP_COUNT; i++) {
        map[i] = (led_config_default.flags[i] & LED_FLAG_UNDERGLOW) ? LED_MAP_UNDERGLOW : LED_MAP_NONE;
    }
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            uint8_t led = led_config_default.matrix_co[r][c];
            if (led < LED_MAP_COUNT) {
                map[led] = r * MATRIX_COLS + c;
            }
        }
    }
}

static void map_apply(const uint8_t *map) {
    memcpy(active, map, LED_MAP_COUNT);
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            g_led_config.matrix_co[r][c] = NO_LED;
        }
    }
    for (uint8_t i = 0; i < LED_MAP_COUNT; i++) {
        uint8_t code = map[i];
        if (code < MATRIX_ROWS * MATRIX_COLS) {
            uint8_t r = code / MATRIX_COLS, c = code % MATRIX_COLS;
            g_led_config.matrix_co[r][c] = i;
            g_led_config.point[i].x      = key_led_pos[r][c][0];
            g_led_config.point[i].y      = key_led_pos[r][c][1];
            g_led_config.flags[i]        = LED_FLAG_KEYLIGHT;
        } else {
            g_led_config.flags[i] = code == LED_MAP_UNDERGLOW ? LED_FLAG_UNDERGLOW : LED_FLAG_NONE;
        }
    }
    // Non-key LEDs sit between their nearest key LEDs on the same half's chain.
    for (uint8_t i = 0; i < LED_MAP_COUNT; i++) {
        if (map[i] < MATRIX_ROWS * MATRIX_COLS) {
            continue;
        }
        uint8_t  lo = i < LED_MAP_COUNT / 2 ? 0 : LED_MAP_COUNT / 2, hi = lo + LED_MAP_COUNT / 2;
        uint16_t x = 0, y = 0, n = 0;
        for (uint8_t d = 1; d < LED_MAP_COUNT / 2 && n == 0; d++) {
            int16_t js[2] = {(int16_t)i - d, (int16_t)i + d};
            for (uint8_t k = 0; k < 2; k++) {
                int16_t j = js[k];
                if (j >= lo && j < hi && map[j] < MATRIX_ROWS * MATRIX_COLS) {
                    x += g_led_config.point[j].x;
                    y += g_led_config.point[j].y;
                    n++;
                }
            }
        }
        g_led_config.point[i].x = n ? x / n : (lo ? 168 : 56);
        g_led_config.point[i].y = n ? y / n : 32;
    }
}

static void schedule_sync(void) {
    sync_version++;
    if (sync_version == 0) {
        sync_version = 1;
    }
    sync_pending = (1 << LEDMAP_CHUNKS) - 1;
    right_known  = false;
}

// Right half: collect a full map, apply it, and reply with the checksum of the
// map it now runs so the master can confirm the sync.
static void slave_handler(uint8_t in_len, const void *in_data, uint8_t out_len, void *out_data) {
    const uint8_t *d = in_data;
    if (in_len >= 3 && d[1] + d[2] <= LED_MAP_COUNT && in_len >= 3 + d[2]) {
        if (d[0] != rx_version) {
            rx_version = d[0];
            rx_mask    = 0;
        }
        memcpy(&rx_buf[d[1]], &d[3], d[2]);
        rx_mask |= 1 << (d[1] / LEDMAP_CHUNK);
        if (rx_mask == (1 << LEDMAP_CHUNKS) - 1) {
            rx_mask = 0;
            if (memcmp(rx_buf, active, LED_MAP_COUNT) != 0 && led_map_valid(rx_buf)) {
                map_apply(rx_buf);
            }
        }
    }
    if (out_len >= 1) {
        ((uint8_t *)out_data)[0] = map_crc(active);
    }
}

void led_map_init(void) {
    transaction_register_rpc(RPC_ID_USER_LEDMAP, slave_handler);
    ledmap_store_t store;
    eeconfig_read_kb_datablock(&store, 0, sizeof(store));
    if (is_keyboard_master() && store.magic == LEDMAP_MAGIC && store.sum == checksum(store.map) && led_map_valid(store.map)) {
        map_apply(store.map);
        measured = true;
    } else {
        map_from_default(active);
    }
}

void led_map_task(void) {
    if (!is_keyboard_master()) {
        return;
    }
    if (timer_elapsed(sync_time) > LEDMAP_RESEND_MS) {
        sync_pending = (1 << LEDMAP_CHUNKS) - 1; // the right half may have restarted
        sync_time    = timer_read();
    }
    for (uint8_t chunk = 0; chunk < LEDMAP_CHUNKS; chunk++) {
        if (sync_pending & (1 << chunk)) {
            uint8_t buf[3 + LEDMAP_CHUNK];
            uint8_t off = chunk * LEDMAP_CHUNK;
            uint8_t len = MIN(LEDMAP_CHUNK, LED_MAP_COUNT - off);
            buf[0]      = sync_version;
            buf[1]      = off;
            buf[2]      = len;
            memcpy(&buf[3], &active[off], len);
            uint8_t reply = 0;
            if (transaction_rpc_exec(RPC_ID_USER_LEDMAP, 3 + len, buf, sizeof(reply), &reply)) {
                sync_pending &= ~(1 << chunk);
                if (chunk == LEDMAP_CHUNKS - 1) {
                    // The last chunk completes the map: the reply is the result.
                    right_sum   = reply;
                    right_known = true;
                }
            }
            return; // one chunk per loop
        }
    }
}

void led_map_read(uint8_t offset, uint8_t *out, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        out[i] = offset + i < LED_MAP_COUNT ? active[offset + i] : LED_MAP_NONE;
    }
}

void led_map_write(uint8_t offset, const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len && offset + i < LED_MAP_COUNT; i++) {
        pending[offset + i] = data[i];
    }
}

bool led_map_commit(void) {
    if (!led_map_valid(pending)) {
        return false;
    }
    ledmap_store_t store = {.magic = LEDMAP_MAGIC};
    memcpy(store.map, pending, LED_MAP_COUNT);
    store.sum = checksum(store.map);
    eeconfig_update_kb_datablock(&store, 0, sizeof(store));
    map_apply(pending);
    measured = true;
    schedule_sync();
    return true;
}

void led_map_reset(void) {
    ledmap_store_t store = {0};
    eeconfig_update_kb_datablock(&store, 0, sizeof(store));
    memcpy(&g_led_config, &led_config_default, sizeof(g_led_config));
    map_from_default(active);
    measured = false;
    schedule_sync();
}

bool led_map_is_measured(void) {
    return measured;
}

uint8_t led_map_right_state(void) {
    return !right_known ? 0 : right_sum == map_crc(active) ? 1 : 2;
}

#endif
