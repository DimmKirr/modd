/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * Modifications Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
 * Origin: lwip-tcpip/lwip src/include/lwip/arch.h @ STABLE-2_2_1_RELEASE (77dcd25a)
 *         https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/include/lwip/arch.h
 * BSD-3-Clause notice: LICENSES/BSD-3-Clause-lwIP.txt */
/* lwIP architecture port for AC791N (pi32v2, 32-bit bare-metal).
 * Defines compiler/platform abstractions required by lwip/arch.h. */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

/* --- standard integer types -------------------------------------------- */
/* The JieLi toolchain provides <stdint.h>. */
#include <stdint.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

/* --- byte order -------------------------------------------------------- */
#define BYTE_ORDER LITTLE_ENDIAN

/* --- compiler hints ---------------------------------------------------- */
#define LWIP_PLATFORM_DIAG(x)   do { (void)0; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { (void)0; } while (0)

/* Pack structs using GCC/Clang attribute */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

/* --- type limits (replaces <limits.h> when LWIP_NO_LIMITS_H=1) ---------- */
#ifndef INT_MAX
#define INT_MAX  0x7FFFFFFF
#endif

/* --- printf format strings (unused: LWIP_STATS=0, diagnostics are no-ops) */
#define X8_F   "02x"
#define U16_F  "u"
#define S16_F  "d"
#define X16_F  "x"
#define U32_F  "u"
#define S32_F  "d"
#define X32_F  "x"
#define SZT_F  "u"

/* --- static pools --------------------------------------------------------- */
/* lwIP's memp pools, pbuf pool and heap are ~20 KB of zero-initialised memory: section
 * .pool_net. app.ld keeps it with the other big buffers in POOL; app_dock.ld (CONFIG_DOCK)
 * places it in the dock module's RAM region instead, which POOL cannot spare: a network
 * link and a running module are exclusive (dock.c refuses one while the other is up). */
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
    u8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)] \
        __attribute__((aligned(4), section(".pool_net")))

/* --- sys_now: millisecond tick for lwIP timers ------------------------- */
/* Implemented in net/net.c; returns the global fm1_ms counter. */
u32_t sys_now(void);

#endif /* LWIP_ARCH_CC_H */
