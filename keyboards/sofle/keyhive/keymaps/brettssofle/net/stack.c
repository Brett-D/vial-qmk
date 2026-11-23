// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// lwIP (NO_SYS, raw API) on one Ethernet netif at 192.168.7.1/24, plus a
// minimal DHCP server that gives the computer 192.168.7.2/24 with no router
// and no DNS server, so the computer's internet connection and name lookups
// stay on its other networks. Ping and (later) HTTP run on top.

#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "stack.h"
#include "httpd.h"

// Locally administered addresses ("Sofle"): the computer side is what the
// NCM function reports as its MAC (iMACAddress), the keyboard uses its own.
const uint8_t net_host_mac[6]   = {0x02, 0x53, 0x6F, 0x66, 0x6C, 0x65};
const uint8_t net_device_mac[6] = {0x02, 0x53, 0x6F, 0x66, 0x6C, 0x01};

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_LEASE_S 3600

static struct netif   nif;
static net_tx_fn      tx_fn;
static bool           ready;
static uint8_t        leases;
static uint32_t       last_rx;
static struct udp_pcb *dhcp_pcb;
static uint8_t        frame[1536];

u32_t sys_now(void) {
    return net_millis();
}

uint8_t netstack_leases(void) {
    return leases;
}

uint32_t netstack_last_rx(void) {
    return last_rx;
}

static err_t link_output(struct netif *n, struct pbuf *p) {
    if (!tx_fn) {
        return ERR_IF;
    }
    uint16_t len = pbuf_copy_partial(p, frame, sizeof(frame), 0);
    tx_fn(frame, len);
    return ERR_OK;
}

static err_t netif_setup(struct netif *n) {
    n->name[0]    = 's';
    n->name[1]    = 'f';
    n->output     = etharp_output;
    n->linkoutput = link_output;
    n->mtu        = 1500;
    n->hwaddr_len = 6;
    memcpy(n->hwaddr, net_device_mac, 6);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

/* ---- DHCP server (RFC 2131, one client) ---- */

enum { DHCPDISCOVER = 1, DHCPOFFER, DHCPREQUEST, DHCPDECLINE, DHCPACK, DHCPNAK, DHCPRELEASE, DHCPINFORM };

#define BOOTP_FIXED 236
#define DHCP_MAGIC 0x63825363UL

static const uint8_t *dhcp_option(const uint8_t *msg, uint16_t len, uint8_t code, uint8_t *olen) {
    uint16_t i = BOOTP_FIXED + 4;
    while (i < len) {
        uint8_t c = msg[i];
        if (c == 255) {
            break;
        }
        if (c == 0) {
            i++;
            continue;
        }
        if (i + 1 >= len || i + 2 + msg[i + 1] > len) {
            break;
        }
        if (c == code) {
            *olen = msg[i + 1];
            return &msg[i + 2];
        }
        i += 2 + msg[i + 1];
    }
    return NULL;
}

static uint8_t *put_option(uint8_t *o, uint8_t code, uint8_t len, const void *data) {
    *o++ = code;
    *o++ = len;
    memcpy(o, data, len);
    return o + len;
}

static void dhcp_reply(const uint8_t *req, uint8_t type, bool give_address) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 300, PBUF_RAM);
    if (!p) {
        return;
    }
    uint8_t *m = p->payload;
    memset(m, 0, 300);
    m[0] = 2; // BOOTREPLY
    m[1] = 1; // Ethernet
    m[2] = 6;
    memcpy(&m[4], &req[4], 4);   // xid
    memcpy(&m[10], &req[10], 2); // flags
    memcpy(&m[12], &req[12], 4); // ciaddr
    if (give_address) {
        const uint8_t host[4] = {192, 168, 7, NET_IP_HOST};
        memcpy(&m[16], host, 4); // yiaddr
    }
    memcpy(&m[28], &req[28], 16); // chaddr
    const uint8_t magic[4] = {0x63, 0x82, 0x53, 0x63};
    memcpy(&m[BOOTP_FIXED], magic, 4);
    uint8_t      *o      = &m[BOOTP_FIXED + 4];
    const uint8_t server[4] = {192, 168, 7, NET_IP_DEVICE};
    const uint8_t mask[4]   = {255, 255, 255, 0};
    const uint8_t lease[4]  = {DHCP_LEASE_S >> 24, (DHCP_LEASE_S >> 16) & 0xFF, (DHCP_LEASE_S >> 8) & 0xFF, DHCP_LEASE_S & 0xFF};
    o = put_option(o, 53, 1, &type);
    o = put_option(o, 54, 4, server);
    if (type != DHCPNAK) {
        o = put_option(o, 1, 4, mask);
        if (give_address) {
            o = put_option(o, 51, 4, lease);
        }
        // No router (3) and no DNS server (6): this link is for the keyboard only.
    }
    *o = 255;
    udp_sendto_if(dhcp_pcb, p, IP4_ADDR_BROADCAST, DHCP_CLIENT_PORT, &nif);
    pbuf_free(p);
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    uint8_t  msg[576];
    uint16_t len = pbuf_copy_partial(p, msg, sizeof(msg), 0);
    pbuf_free(p);
    if (len < BOOTP_FIXED + 5 || msg[0] != 1 || msg[1] != 1 || msg[2] != 6) {
        return;
    }
    uint32_t magic = (uint32_t)msg[BOOTP_FIXED] << 24 | (uint32_t)msg[BOOTP_FIXED + 1] << 16 | msg[BOOTP_FIXED + 2] << 8 | msg[BOOTP_FIXED + 3];
    uint8_t  olen;
    const uint8_t *type = dhcp_option(msg, len, 53, &olen);
    if (magic != DHCP_MAGIC || !type || olen != 1) {
        return;
    }
    switch (*type) {
        case DHCPDISCOVER:
            dhcp_reply(msg, DHCPOFFER, true);
            break;
        case DHCPREQUEST: {
            const uint8_t  host[4] = {192, 168, 7, NET_IP_HOST};
            const uint8_t *want    = dhcp_option(msg, len, 50, &olen);
            if (!want || olen != 4) {
                want = &msg[12]; // renewing: ciaddr
            }
            if (memcmp(want, host, 4) == 0) {
                dhcp_reply(msg, DHCPACK, true);
                if (leases < 255) {
                    leases++;
                }
            } else {
                dhcp_reply(msg, DHCPNAK, false);
            }
            break;
        }
        case DHCPINFORM:
            dhcp_reply(msg, DHCPACK, false);
            break;
        default:
            break; // DECLINE / RELEASE: nothing to do for one fixed address
    }
}

/* ---- stack ---- */

void netstack_start(net_tx_fn tx) {
    tx_fn   = tx;
    leases  = 0;
    last_rx = 0;
    if (!ready) {
        lwip_init();
        ip4_addr_t ip, mask, gw;
        IP4_ADDR(&ip, 192, 168, 7, NET_IP_DEVICE);
        IP4_ADDR(&mask, 255, 255, 255, 0);
        IP4_ADDR(&gw, 0, 0, 0, 0);
        netif_add(&nif, &ip, &mask, &gw, NULL, netif_setup, ethernet_input);
        netif_set_default(&nif);
        dhcp_pcb = udp_new();
        udp_bind(dhcp_pcb, IP4_ADDR_ANY, DHCP_SERVER_PORT);
        udp_recv(dhcp_pcb, dhcp_recv, NULL);
        httpd_start();
        ready = true;
    }
    netif_set_up(&nif);
    netif_set_link_up(&nif);
}

void netstack_stop(void) {
    if (ready) {
        netif_set_link_down(&nif);
        netif_set_down(&nif);
        etharp_cleanup_netif(&nif);
    }
    tx_fn = NULL;
}

void netstack_input(const uint8_t *data, uint16_t len) {
    if (!ready || !tx_fn || len < 14 || len > 1518) {
        return;
    }
    last_rx        = net_millis() | 1;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (!p) {
        return; // out of buffers: dropped (TCP/ARP retry)
    }
    pbuf_take(p, data, len);
    if (nif.input(p, &nif) != ERR_OK) {
        pbuf_free(p);
    }
}

void netstack_poll(void) {
    if (ready) {
        sys_check_timeouts();
    }
}
