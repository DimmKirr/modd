// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)

/* The lwIP sources included below are BSD-3-Clause (see firmware/vendor/lwip/COPYING). */
/* Unity-build include for lwIP 2.2.0 core sources.
 * This file #includes the minimal set of lwIP .c files required for
 * TCP, UDP, DNS, DHCP, ARP, and ICMP on a bare-metal NO_SYS=1 target.
 *
 * Include paths required by the build:
 *   firmware/src/wifi/          (lwipopts.h, arch/cc.h)
 *   firmware/vendor/lwip/src/include/  (lwIP headers)
 *
 * lwIP functions are NOT static -- this is intentional. lwIP is designed
 * as a separately-linked library; its symbols are extern by default. */

/* ---- core ------------------------------------------------------------- */
#include "vendor/lwip/src/core/init.c"
#include "vendor/lwip/src/core/def.c"
#include "vendor/lwip/src/core/mem.c"
#include "vendor/lwip/src/core/memp.c"
#include "vendor/lwip/src/core/pbuf.c"
#include "vendor/lwip/src/core/netif.c"
#include "vendor/lwip/src/core/ip.c"
#include "vendor/lwip/src/core/tcp.c"
#include "vendor/lwip/src/core/tcp_in.c"
#include "vendor/lwip/src/core/tcp_out.c"
#include "vendor/lwip/src/core/udp.c"
#include "vendor/lwip/src/core/dns.c"
#include "vendor/lwip/src/core/timeouts.c"
#include "vendor/lwip/src/core/inet_chksum.c"

/* ---- IPv4 ------------------------------------------------------------- */
#include "vendor/lwip/src/core/ipv4/etharp.c"
#include "vendor/lwip/src/core/ipv4/ip4.c"
#include "vendor/lwip/src/core/ipv4/ip4_addr.c"
#include "vendor/lwip/src/core/ipv4/ip4_frag.c"
#include "vendor/lwip/src/core/ipv4/dhcp.c"
#include "vendor/lwip/src/core/ipv4/icmp.c"

/* ---- netif ------------------------------------------------------------ */
#include "vendor/lwip/src/netif/ethernet.c"
