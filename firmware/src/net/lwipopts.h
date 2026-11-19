/* SPDX-License-Identifier: BSD-3-Clause
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: lwip-tcpip/lwip contrib/examples/example_app/test_configs/opt_nosys_ipv4.h @ STABLE-2_2_1_RELEASE (77dcd25a)
 *         https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/contrib/examples/example_app/test_configs/opt_nosys_ipv4.h
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * BSD-3-Clause notice: LICENSES/BSD-3-Clause-lwIP.txt */
/* lwIP configuration for modd (AC791N, bare-metal, NO_SYS=1).
 * This header is BSD-licensed: it configures BSD-licensed lwIP code. */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* --- JieLi bare-metal: skip system headers lwIP would normally pull in -- */
#define LWIP_NO_INTTYPES_H      1
#define LWIP_NO_CTYPE_H         1
#define LWIP_NO_LIMITS_H        1

/* --- NO_SYS mode: no OS, raw API only --------------------------------- */
#define NO_SYS                  1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define SYS_LIGHTWEIGHT_PROT    0

/* --- enabled protocols ------------------------------------------------- */
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_DNS                1
#define LWIP_DHCP               1
#define LWIP_ARP                1
#define LWIP_ICMP               1

/* --- disabled protocols ------------------------------------------------ */
#define LWIP_IPV6               0
#define LWIP_IGMP               0
#define LWIP_PPP                0
#define LWIP_SNMP               0
#define LWIP_RAW                0
#define LWIP_AUTOIP             0
#define LWIP_ACD                0
#define LWIP_DHCP_DOES_ACD_CHECK 0

/* --- memory tuning (fits in ~12 KB RAM) -------------------------------- */
#ifdef CONFIG_EMU
#define MEM_SIZE                32768
#else
#define MEM_SIZE                8192
#endif
#define MEM_ALIGNMENT           4
#define PBUF_POOL_SIZE          8
#define PBUF_POOL_BUFSIZE       1536

/* --- TCP tuning -------------------------------------------------------- */
#define MEMP_NUM_TCP_PCB        12
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define SO_REUSE                1
#define TCP_MSS                 1400
#ifdef CONFIG_EMU
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_SND_QUEUELEN        ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define MEMP_NUM_TCP_SEG        TCP_SND_QUEUELEN
#define TCP_WND                 (4 * TCP_MSS)
#else
#define TCP_SND_BUF             (2 * TCP_MSS)
#define TCP_WND                 (2 * TCP_MSS)
#endif
/* TIME_WAIT = 2 * TCP_MSL / TCP_SLOW_INTERVAL = 2 * 100 / 500 = 0, clamped
 * to 1 slow tick = 500 ms. The emulator NAT reuses a fixed 4-tuple for every
 * forwarded connection, so a long TIME_WAIT blocks subsequent requests.
 * On real hardware each client uses a unique source port and this is harmless. */
#define TCP_MSL                 100

/* --- DNS --------------------------------------------------------------- */
#define DNS_TABLE_SIZE          2
#define DNS_MAX_SERVERS         1

/* --- DHCP -------------------------------------------------------------- */
#define DHCP_DOES_ARP_CHECK     0

/* --- statistics (disabled to save RAM) --------------------------------- */
#define LWIP_STATS              0

/* --- checksums: software (no hardware offload on AC791N) --------------- */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_GEN_ICMP       1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1
#define CHECKSUM_CHECK_ICMP     1

/* --- netif callbacks --------------------------------------------------- */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1

/* --- RNG for DNS transaction IDs --------------------------------------- */
static unsigned rng(void);
#define LWIP_RAND()             rng()

#endif /* LWIPOPTS_H */
