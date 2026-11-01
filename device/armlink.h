/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ARMLINK_H
#define ARMLINK_H
#include <stdint.h>
void arm_init(void);
void arm_poll(void);
void arm_start(void);
void arm_send(uint8_t type, uint16_t id, const uint8_t *data, uint32_t dlen);
extern volatile uint32_t arm_ok_levels, arm_ok_preset;
#endif
