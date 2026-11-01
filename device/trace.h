/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR on the Pocket Master: a start-up trace in RAM that a restart through flash_boot keeps (0x2003F000:
 * above Molecular's RAM, below the stock firmware's highest use). The next start reads the last checkpoint of the
 * one before and reports it as the USB serial number. */
#ifndef TRACE_H
#define TRACE_H
#include <stdint.h>
#define TRACE ((volatile uint32_t *)0x2003F000u)
#define TRACE_MAGIC 0x4D4F4C54u                     /* "MOLT" */
/* [0] magic, [1] starts counted, [2] last checkpoint, [3] the last setup request's first 4 bytes */
static inline void trace_mark(uint32_t n) { TRACE[2] = n; }
#endif
