/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Polycom Trio C60 (kepler_proto1) LPDDR4 timing - declarations.
 *
 * Tables live in lpddr4_timing.c. Most callers should `extern struct
 * dram_timing_info dram_timing;` directly; this header just gives callers
 * a single include point if they want it.
 */

#ifndef _BOARD_KEPLER_PROTO1_LPDDR4_TIMING_H
#define _BOARD_KEPLER_PROTO1_LPDDR4_TIMING_H

#include <asm/arch/ddr.h>

extern struct dram_timing_info dram_timing;

#endif /* _BOARD_KEPLER_PROTO1_LPDDR4_TIMING_H */
