// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// HTTP/1.0-style server on lwIP's raw TCP API: GET / answers with the config
// page straight from flash (gzip, the same bytes the keyboard can also type,
// config_page_blob.h); other routes go to httpd_route() (the JSON API). One
// request per connection, a few connections at once, everything polled from
// the main loop. Requests must name the keyboard in Host (192.168.7.1 or
// sofle.local), so a web page on another site can't reach it through DNS
// rebinding.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "lwip/tcp.h"
#include "httpd.h"
#include "stack.h"
#include "config_page_blob.h"

#define CONNS 3
#define REQ_MAX 3072 // request in, then a copied (API) response out
#define IDLE_POLLS 20 // x 500 ms

typedef struct {
    struct tcp_pcb *pcb;
    uint16_t        req_len;
    char            req[REQ_MAX];
    char            head[320];
    uint16_t        head_len, head_sent;
    const uint8_t  *body;
    uint32_t        body_len, body_sent;
    bool            copy_body, responded;
    uint8_t         idle;
} conn_t;

static conn_t   conns[CONNS];
static uint32_t answered;

uint32_t httpd_requests(void) {
    return answered;
}

__attribute__((weak)) bool httpd_route(const http_request_t *req, http_response_t *resp) {
    return false;
}

__attribute__((weak)) void httpd_api_reset(void) {}

static void conn_free(conn_t *c) {
    if (c->pcb) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        tcp_poll(c->pcb, NULL, 0);
    }
    memset(c, 0, sizeof(*c));
}

static void conn_close(conn_t *c) {
    struct tcp_pcb *pcb = c->pcb;
    conn_free(c);
    if (pcb && tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

static void send_more(conn_t *c) {
    struct tcp_pcb *pcb = c->pcb;
    while (c->head_sent < c->head_len) {
        uint16_t n = LWIP_MIN(c->head_len - c->head_sent, tcp_sndbuf(pcb));
        if (!n || tcp_write(pcb, c->head + c->head_sent, n, TCP_WRITE_FLAG_COPY | (c->body_len ? TCP_WRITE_FLAG_MORE : 0)) != ERR_OK) {
            break;
        }
        c->head_sent += n;
    }
    while (c->head_sent == c->head_len && c->body_sent < c->body_len) {
        uint32_t n = LWIP_MIN(c->body_len - c->body_sent, (uint32_t)tcp_sndbuf(pcb));
        n          = LWIP_MIN(n, 0xFFFFu);
        bool more  = c->body_sent + n < c->body_len;
        if (!n || tcp_write(pcb, c->body + c->body_sent, n, (c->copy_body ? TCP_WRITE_FLAG_COPY : 0) | (more ? TCP_WRITE_FLAG_MORE : 0)) != ERR_OK) {
            break;
        }
        c->body_sent += n;
    }
    tcp_output(pcb);
}

static const char *reason(uint16_t status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 421: return "Misdirected Request";
        default: return "Error";
    }
}

static void respond(conn_t *c, const http_response_t *r) {
    uint32_t len = r->body_len;
    if (r->static_body) {
        c->body      = r->body;
        c->copy_body = false;
    } else {
        // The request is handled: its buffer holds the response until sent.
        len = LWIP_MIN(len, (uint32_t)REQ_MAX);
        memmove(c->req, r->body, len);
        c->body      = (const uint8_t *)c->req;
        c->copy_body = true;
    }
    c->body_len = len;
    c->head_len = snprintf(c->head, sizeof(c->head),
                           "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n%s"
                           "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                           r->status, reason(r->status), r->content_type, (unsigned long)len, r->gzip ? "Content-Encoding: gzip\r\n" : "");
    c->responded = true;
    answered++;
    send_more(c);
}

static void text_response(conn_t *c, uint16_t status, const char *text) {
    http_response_t r = {status, "text/plain; charset=utf-8", (const uint8_t *)text, strlen(text), false, true};
    respond(c, &r);
}

// Copies header `name` (case-insensitive) into out; "" if absent.
static void header(const char *head, const char *name, char *out, size_t size) {
    size_t      n = strlen(name);
    const char *p = head;
    out[0]        = 0;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, name, n) == 0 && p[n] == ':') {
            p += n + 1;
            while (*p == ' ') {
                p++;
            }
            size_t i = 0;
            while (p[i] && p[i] != '\r' && i + 1 < size) {
                out[i] = p[i];
                i++;
            }
            out[i] = 0;
            return;
        }
    }
}

static bool host_ok(const char *host) {
    static const char *const names[] = {"192.168.7.1", "192.168.7.1:80", "sofle.local", "sofle.local:80"};
    for (uint8_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcasecmp(host, names[i]) == 0) {
            return true;
        }
    }
    return false;
}

// "METHOD /path HTTP/1.1": copies method and path (bounded).
static bool request_line(const char *s, http_request_t *req) {
    size_t i = 0, j = 0;
    while (s[i] && s[i] != ' ' && i + 1 < sizeof(req->method)) {
        req->method[i] = s[i];
        i++;
    }
    if (s[i] != ' ' || i == 0) {
        return false;
    }
    i++;
    while (s[i] && s[i] != ' ' && s[i] != '\r' && j + 1 < sizeof(req->path)) {
        req->path[j++] = s[i++];
    }
    return j > 0 && (s[i] == ' ' || s[i] == '\r' || s[i] == 0);
}

static void handle(conn_t *c) {
    NET_CRUMB(CRUMB_HTTP);
    char *end = strstr(c->req, "\r\n\r\n");
    *end      = 0;
    static http_request_t req; // static: keeps the network stack shallow
    memset(&req, 0, sizeof(req));
    if (!request_line(c->req, &req)) {
        text_response(c, 400, "bad request\n");
        return;
    }
    header(c->req, "Host", req.host, sizeof(req.host));
    header(c->req, "Origin", req.origin, sizeof(req.origin));
    header(c->req, "X-Sofle-Token", req.token, sizeof(req.token));
    if (!host_ok(req.host)) {
        text_response(c, 421, "Open http://192.168.7.1/ to use the Sofle config page.\n");
        return;
    }
    char clen[12];
    header(c->req, "Content-Length", clen, sizeof(clen));
    req.body     = end + 4;
    req.body_len = c->req_len - (uint16_t)(end + 4 - c->req);
    if (clen[0] && (uint16_t)atoi(clen) > req.body_len) {
        *end = '\r';
        return; // wait for the rest of the body
    }
    if (clen[0]) {
        req.body_len = (uint16_t)atoi(clen);
    } else if (req.body_len) {
        req.body_len = 0; // no Content-Length: no body
    }

    bool get = strcmp(req.method, "GET") == 0 || strcmp(req.method, "HEAD") == 0;
    if (get && (strcmp(req.path, "/") == 0 || strcmp(req.path, "/index.html") == 0)) {
        http_response_t r = {200, "text/html; charset=utf-8", config_page_gz, CONFIG_PAGE_GZ_LEN, true, true};
        if (req.method[0] == 'H') {
            r.body_len = 0;
        }
        respond(c, &r);
        return;
    }
    http_response_t r;
    memset(&r, 0, sizeof(r));
    if (httpd_route(&req, &r)) {
        respond(c, &r);
        return;
    }
    text_response(c, 404, "not found\n");
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = arg;
    if (!p) {
        conn_close(c); // the computer closed its side
        return ERR_OK;
    }
    if (c->responded) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    uint16_t room = REQ_MAX - 1 - c->req_len;
    uint16_t n    = pbuf_copy_partial(p, c->req + c->req_len, LWIP_MIN(room, p->tot_len), 0);
    tcp_recved(pcb, p->tot_len);
    bool overflow = p->tot_len > room;
    pbuf_free(p);
    c->req_len += n;
    c->req[c->req_len] = 0;
    c->idle            = 0;
    if (overflow) {
        text_response(c, 413, "request too large\n");
    } else if (strstr(c->req, "\r\n\r\n")) {
        handle(c);
    }
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    conn_t *c = arg;
    c->idle   = 0;
    send_more(c);
    if (c->responded && c->head_sent == c->head_len && c->body_sent == c->body_len && !pcb->unsent && !pcb->unacked) {
        conn_close(c);
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    if (arg) {
        memset(arg, 0, sizeof(conn_t)); // lwIP already freed the pcb
    }
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
    conn_t *c = arg;
    if (++c->idle > IDLE_POLLS) {
        conn_free(c);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if (c->responded) {
        send_more(c);
    }
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    conn_t *c = NULL;
    for (uint8_t i = 0; i < CONNS; i++) {
        if (!conns[i].pcb) {
            c = &conns[i];
            break;
        }
    }
    if (!c || err != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    memset(c, 0, sizeof(*c));
    c->pcb = pcb;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    tcp_poll(pcb, on_poll, 1);
    return ERR_OK;
}

void httpd_start(void) {
    static bool started;
    httpd_api_reset();
    if (started) {
        return;
    }
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb || tcp_bind(pcb, IP4_ADDR_ANY, HTTPD_PORT) != ERR_OK) {
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, on_accept);
    started = true;
}
