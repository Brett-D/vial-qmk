// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// JSON API of the network config mode (http://192.168.7.1/api/...).
//
// The page served by httpd.c is the same sofle-config.html as the WebHID
// version; over the network it sends the same 32-byte VIA / Vial / 0xF5 /
// 0xF6 commands, tunnelled through HTTP. They run through raw_hid_receive()
// exactly as USB raw HID reports do, so both ways share one implementation
// and the same write gate: writes need the web session confirmed on the
// keyboard (CONF? on the OLED, config_mode.c).
//
//   POST /api/session  -> {"token":"<16 hex>","fw":"..."}
//   POST /api/hid      body: hex of 1-8 32-byte commands, header X-Sofle-Token
//                      -> {"r":"<hex of the 32-byte replies>"}
//
// Against other web sites (CSRF / DNS rebinding): httpd.c accepts only
// Host 192.168.7.1 or sofle.local; here Origin must be absent or this
// keyboard's, and every command needs the token, which only a page from
// this origin can read (no CORS headers are ever sent, so a cross-site
// request with a custom header fails its preflight).

#include <stdio.h>
#include <string.h>
#include "httpd.h"
#include "stack.h"
#include "raw_hid.h"

#ifndef API_FW_NAME
#    define API_FW_NAME "brettssofle"
#endif
#define API_MAX_CMDS 8

static bool     capturing;
static char     token[17];
static char     out[16 + API_MAX_CMDS * 64];
static uint32_t calls;

// The reply of a command run here stays in its buffer, not sent over USB.
bool raw_hid_send_user(uint8_t *data, uint8_t length) {
    return capturing;
}

uint32_t api_calls(void) {
    return calls;
}

#if defined(__arm__)
// RP2040 ring oscillator random bit, mixed with the time of the request.
static uint32_t entropy(void) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < 64; i++) {
        v = (v << 1 | v >> 31) ^ (*(volatile uint32_t *)0x4006001C & 1u);
        for (volatile uint8_t d = 0; d < 8; d++) {
        }
    }
    return v;
}
#else
#    include <stdlib.h>
static uint32_t entropy(void) {
    return (uint32_t)rand();
}
#endif

void httpd_api_reset(void) {
    token[0] = 0; // a new token for each time network mode starts
}

static void make_token(void) {
    uint32_t a = entropy() ^ (net_millis() * 2654435761u), b = entropy() ^ (a * 2246822519u) ^ net_millis();
    snprintf(token, sizeof(token), "%08lx%08lx", (unsigned long)a, (unsigned long)b);
}

static bool origin_ok(const char *origin) {
    return !origin[0] || strcmp(origin, "http://192.168.7.1") == 0 || strcmp(origin, "http://sofle.local") == 0;
}

static int8_t hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool reply(http_response_t *r, uint16_t status, const char *json) {
    r->status       = status;
    r->content_type = "application/json";
    r->body         = (const uint8_t *)json;
    r->body_len     = strlen(json);
    return true;
}

static bool error(http_response_t *r, uint16_t status, const char *msg) {
    snprintf(out, sizeof(out), "{\"error\":\"%s\"}", msg);
    return reply(r, status, out);
}

static bool run_commands(const http_request_t *req, http_response_t *r) {
    if (!token[0] || strcmp(req->token, token) != 0) {
        return error(r, 403, "token");
    }
    uint16_t n = req->body_len;
    while (n && (req->body[n - 1] == '\n' || req->body[n - 1] == '\r' || req->body[n - 1] == ' ')) {
        n--;
    }
    if (!n || n % 64 || n / 64 > API_MAX_CMDS) {
        return error(r, n / 64 > API_MAX_CMDS ? 413 : 400, "body: hex of 1-8 32-byte commands");
    }
    char *o = out + sprintf(out, "{\"r\":\"");
    for (uint16_t at = 0; at < n; at += 64) {
        uint8_t buf[32];
        for (uint8_t i = 0; i < 32; i++) {
            int8_t hi = hexval(req->body[at + 2 * i]), lo = hexval(req->body[at + 2 * i + 1]);
            if (hi < 0 || lo < 0) {
                return error(r, 400, "hex");
            }
            buf[i] = (uint8_t)(hi << 4 | lo);
        }
        capturing = true;
        raw_hid_receive(buf, sizeof(buf));
        capturing = false;
        calls++;
        for (uint8_t i = 0; i < 32; i++) {
            o += sprintf(o, "%02x", buf[i]);
        }
    }
    strcpy(o, "\"}");
    return reply(r, 200, out);
}

bool httpd_route(const http_request_t *req, http_response_t *r) {
    if (strncmp(req->path, "/api/", 5) != 0) {
        return false;
    }
    if (!origin_ok(req->origin)) {
        return error(r, 403, "origin");
    }
    bool post = strcmp(req->method, "POST") == 0;
    if (strcmp(req->path, "/api/session") == 0) {
        if (!post) {
            return error(r, 405, "POST");
        }
        if (!token[0]) {
            make_token();
        }
        snprintf(out, sizeof(out), "{\"token\":\"%s\",\"fw\":\"%s\"}", token, API_FW_NAME);
        return reply(r, 200, out);
    }
    if (strcmp(req->path, "/api/hid") == 0) {
        return post ? run_commands(req, r) : error(r, 405, "POST");
    }
    return error(r, 404, "unknown API");
}
