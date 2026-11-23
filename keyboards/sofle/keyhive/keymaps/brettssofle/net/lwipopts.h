// Copyright 2026 Brett-D
// SPDX-License-Identifier: GPL-2.0-or-later
//
// lwIP options for the USB network config mode (net/): bare metal (NO_SYS),
// polled from the main loop, IPv4 only, one small netif.

#pragma once

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_TIMERS 1

#define MEM_ALIGNMENT 4
#define MEM_SIZE (12 * 1024)
#define MEMP_NUM_PBUF 16
#define PBUF_POOL_SIZE 8
#define PBUF_POOL_BUFSIZE 1536
#define MEMP_NUM_UDP_PCB 3
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_SYS_TIMEOUT 8

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DHCP 0 // we are the DHCP server (net/dhcpd.c)
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_DNS 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define ARP_TABLE_SIZE 4
#define ARP_QUEUEING 1
// DHCP clients send from 0.0.0.0: accept that source for the DHCP server port
// (ip4.c turns on IP_ACCEPT_LINK_LAYER_ADDRESSING from this).
#define LWIP_IP_ACCEPT_UDP_PORT(port) ((port) == PP_NTOHS(67))

#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_QUEUE_OOSEQ 0
#define LWIP_TCP_KEEPALIVE 0
#define LWIP_LISTEN_BACKLOG 0

#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK 0
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_SINGLE_NETIF 1
#define LWIP_HAVE_LOOPIF 0
#define LWIP_NETIF_LOOPBACK 0

#define LWIP_STATS 0
#define LWIP_DEBUG 0
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_PROVIDE_ERRNO 1
