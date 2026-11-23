// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tiny HTTP server for the network config mode (see httpd.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HTTPD_PORT 80

// A response the API (or a static route) produced.
typedef struct {
    uint16_t       status;       // 200, 400, 403, ...
    const char    *content_type; // e.g. "application/json"
    const uint8_t *body;
    uint32_t       body_len;
    bool           gzip;   // body is gzip-encoded
    bool           static_body; // body stays valid (flash): sent without copying
} http_response_t;

// One request: method, path (with query), headers of interest, body.
typedef struct {
    char        method[8];
    char        path[96];
    char        host[48];
    char        origin[64];
    char        token[40]; // X-Sofle-Token header
    const char *body;
    uint16_t    body_len;
} http_request_t;

void httpd_start(void);
uint32_t httpd_requests(void); // pages / API calls answered since start
// Routes other than GET / : return true and fill resp to answer (net/api.c).
bool httpd_route(const http_request_t *req, http_response_t *resp);
