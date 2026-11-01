/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR on the Pocket Master: its USB-MIDI device (usbmidi.c). */
#pragma once
#include <stdint.h>
void usb_start(void);
void usb_poll(void);
int usb_configured(void);
int usb_host_open(void);
void usb_log(const char *s);
void usb_logx(const char *label, uint32_t v);
void usb_logn(const char *label, uint32_t v);
int usb_midi_rx(void);
