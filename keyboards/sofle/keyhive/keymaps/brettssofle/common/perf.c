// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Main-loop profiler (perf.h).

#include <string.h>
#include "timer.h"
#include "perf.h"

#if defined(MCU_RP)
#    include "hardware/timer.h"
uint32_t perf_us(void) {
    return time_us_32(); // the RP2040's 1 MHz timer (it runs on while flash writes stop the CPU)
}
#else
__attribute__((weak)) uint32_t perf_us(void) {
    return timer_read32() * 1000; // host tests
}
#endif

#define BUCKETS 10      // x 1 s = the last 10 s
#define BUCKET_US 1000000
#define SPIKE_US 10000

typedef struct {
    uint16_t max[PF_COUNT]; // 10 us units
    uint16_t loop_max;
    uint32_t loop_sum; // us
    uint16_t loops;
} bucket_t;

static uint32_t cur[PF_COUNT]; // this pass, us
static uint32_t mark_t, loop_t, bucket_t0;
static bucket_t b[BUCKETS];
static uint8_t  bi;
static uint32_t worst; // us
static uint8_t  worst_sec, spikes;

static uint16_t u10(uint32_t us) {
    uint32_t v = (us + 5) / 10;
    return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

void perf_mark(uint8_t section) {
    uint32_t now = perf_us();
    if (mark_t && section < PF_COUNT) {
        cur[section] += now - mark_t;
    }
    mark_t = now;
}

void perf_add(uint8_t section, uint32_t us) {
    if (section < PF_COUNT) {
        cur[section] += us;
    }
}

void perf_reset(void) {
    memset(b, 0, sizeof b);
    worst = worst_sec = spikes = 0;
}

// The section that took most of this pass (PF_TASKS without the OLED drawing in it).
static uint8_t biggest(uint32_t loop) {
    if (cur[PF_FLASH] * 2 >= loop) {
        return PF_FLASH;
    }
    uint8_t  s    = PF_SCAN;
    uint32_t best = 0;
    for (uint8_t i = 0; i < PF_FLASH; i++) {
        uint32_t v = i == PF_TASKS && cur[PF_TASKS] > cur[PF_OLED] ? cur[PF_TASKS] - cur[PF_OLED] : cur[i];
        if (v > best) best = v, s = i;
    }
    return s;
}

void perf_loop_end(void) {
    uint32_t now = perf_us();
    if (loop_t) {
        uint32_t loop = now - loop_t;
        if (now - bucket_t0 >= BUCKET_US) {
            bi        = (bi + 1) % BUCKETS;
            memset(&b[bi], 0, sizeof b[bi]);
            bucket_t0 = now;
        }
        bucket_t *k = &b[bi];
        for (uint8_t i = 0; i < PF_COUNT; i++) {
            uint16_t v = u10(cur[i]);
            if (v > k->max[i]) k->max[i] = v;
        }
        if (u10(loop) > k->loop_max) k->loop_max = u10(loop);
        k->loop_sum += loop;
        if (k->loops < 0xFFFF) k->loops++;
        if (loop > worst) {
            worst     = loop;
            worst_sec = biggest(loop);
        }
        if (loop > SPIKE_US && spikes < 255) spikes++;
    } else {
        bucket_t0 = now;
    }
    memset(cur, 0, sizeof cur);
    loop_t = mark_t = now;
}

uint16_t perf_loop_max(void) {
    uint16_t m = 0;
    for (uint8_t i = 0; i < BUCKETS; i++) {
        if (b[i].loop_max > m) m = b[i].loop_max;
    }
    return m;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
}

void perf_report(uint8_t out[PERF_REPORT_LEN]) {
    memset(out, 0, PERF_REPORT_LEN);
    uint32_t sum = 0, n = 0;
    for (uint8_t i = 0; i < BUCKETS; i++) {
        sum += b[i].loop_sum;
        n += b[i].loops;
        for (uint8_t s = 0; s < PF_COUNT; s++) {
            uint16_t v = b[i].max[s];
            uint16_t o = out[PR_SECTIONS + 2 * s] | out[PR_SECTIONS + 2 * s + 1] << 8;
            if (v > o) put16(&out[PR_SECTIONS + 2 * s], v);
        }
    }
    put16(&out[PR_LOOP_MAX], perf_loop_max());
    put16(&out[PR_WORST], u10(worst));
    out[PR_WORST_SEC] = worst_sec;
    out[PR_SPIKES]    = spikes;
    uint32_t avg      = n ? (sum / n + 10) / 20 : 0;
    out[PR_AVG]       = avg > 255 ? 255 : (uint8_t)avg;
}
