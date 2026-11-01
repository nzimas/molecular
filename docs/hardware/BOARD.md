# Pocket Master board map (from the stock v1.3.3 image)

All found by disassembling section `b` with the Andes toolchain (846 functions named by matching
the MVsilicon SDK's libraries; the disassembly is not in this repository). Addresses below are file offsets in
section `b`; the code runs at offset + 0x10000.

## Flash and boot

| Region | What |
|---|---|
| 0x000000 - 0x00FFFF | Sonicake's **flash_boot** (MVsilicon's bootloader, customised). Not in any update file: updates never write it. |
| 0x010000 - ... | the application = HTFW section `b` (linked at 0x10000; the ROM header fields at 0x100B0..0x100D7) |
| 0x170000 - 0x17FFFF | Molecular's patches (0.54): a 4 KB sector a slot. Past stock's application (ends ~0x15BB60) and no stock code refers to it; stock erases only 0x180000 - 0x18FFFF, 0x1E1000, 0x1F3000 - 0x1FFFFF (its SpiFlashErase at image 0x1482, sector erase = type 1, with suspend) |
| 0x190000 - | section `e`, the factory presets, then stock's own saves (above) |

- The chip's **mask ROM** boots first, then flash_boot, which runs the application at 0x10000 unless asked to upgrade.
- The application asks for an upgrade by setting bits in `0x4000100C` (`FLASH_BOOT_FLAGE`: bit 1 PC, bit 5 update) and
  jumping to 0 (stock: `0x41FE0`, the SDK's `start_up_grate`). flash_boot then runs its **PC upgrade mode**.
- The stock app does that on MIDI command **`11 6F`** (the command switch at `0x3CE7C`: `11 xx` dispatches on xx - 0x11;
  `11 60` is ignored by the app). So `11 60 / 61 / 6E` and the data packets are handled by **flash_boot**, not the app:
  flash_boot writes the new application. A broken application cannot damage flash_boot.
- The stock app also enters flash_boot from a boot-time check (`0x3FA88`) and from a counter reaching 11 (`0x3FABC`).
- Header the ROM reads (the SDK's `stub`): 0xB0 const-data addr, 0xB4 user-data addr, 0xB8 chip/SDK bytes, 0xBC code
  CRC (stock: 1, SDK example: FFFFFFFF; both boot), 0xC0 magic B0BEBDC9, 0xC4 32 kHz trim 0x706.

## Recovery path: tested 2026-10-30 on stock v1.3.3

`tools/pmmidi/pmmidi send f00f0c0001000000020101060ff7` (the command `11 6F`, framed as in PROTOCOL.md):
- the stock app answered `f0 0b 02 00 01 00 00 00 03 01 04 00 08 00 00 f7`: payload `14 08 00` (ACK group 0x14,
  status 0 = OK), as PROTOCOL.md says;
- about 3 s later the pedal left USB and came back as **flash_boot**: same VID `0x84EF`, PID **`0x0285`** (the app is
  `0x0284`), bcdDevice `0x0100` (app `0x0101`), serial `20240515` (app `20240710`), name "Pocket Master", one USB-MIDI
  interface only (class 1/3, 2 endpoints). Nothing more was sent to it.

A power cycle returned it to the stock app (PID `0x0284`, bcdDevice `0x0101`): flash_boot's update mode does not
survive power-off, and the application was untouched.

So the update receiver is a separate program in flash_boot, reachable by `11 6F`, and never written by updates.

## First install: Molecular 0.1, 2026-10-30

`11 6F` to the stock app, flash_boot up after 2 s, then `tools/pmmidi/pmmidi update` with the 39,610 messages of
`Molecular_PocketMaster_0.1.bin`: every one acknowledged (status 0), no retries, 101 s (about 4 ms a message through
sections c, g, f, then about 1 ms). flash_boot then left USB and nothing came back on it (Molecular 0.1 has no USB):
it ran the new application. The screen stayed dark and the pedal stayed off USB.

The escape then worked: reset, both side keys held while powering on, and flash_boot came up (PID 0x0285). So the
ROM -> flash_boot -> Molecular chain runs, and Molecular reaches main(), reads the keys and hands over correctly.

Diagnosis for 0.2: 0.1 set the backlight with a PWM_IOCTRL_ARG of zeros but the duty (stock passes output type 6,
divider 100: 0x40EF8 / 0x40F54), most likely leaving the backlight off; unseen, POWER ON? then timed out after 10 s
and powered off. 0.2 fixes that, arms the watchdog before touching the clocks or the flash, and counts starts that do
not reach the main loop in NVM byte 15 (0xA0 + n): the third such start goes to flash_boot on its own.

## Molecular 0.2, 2026-10-30

Installed straight to flash_boot (all 39,616 messages acknowledged). About 7 s later the pedal was back in flash_boot
on its own: the watchdog (now armed first) reset a hang and the next start handed over. So the hang is in the early
start-up block, which 0.1 and 0.2 took from the SDK's DAC example, not from the stock firmware. Stock (0x4FB18,
clocks at 0x4FA88) differs: no Chip_Init; Remap_DisableTcm before Remap_InitTcm(0x10000, 12) (flash_boot leaves its
TCM mapping on); Clock_ApbClkDivSet(5); APLL 240 MHz; USB/UART clocks from the APLL; SpiFlashInit at 96 MHz. 0.3
copies it, at 288 MHz (stock raises the core voltage only above 288 MHz).

## Root cause of 0.1 .. 0.8 (found 2026-10-30)

The SDK's startup (init-default.c, __cpu_init) sets the interrupt vector base IVB to 0x0000. On the Pocket Master
0x0 is flash_boot's vector table: the application sits at 0x10000. Stock's __cpu_init (0x526FC) is the SDK's with
one change, `movi $r0,#0x10000; mtsr $r0,$ivb`. So in every Molecular version the first interrupt (the system tick)
ran flash_boot's handlers: no screen, no sound, no USB. The escape worked because it runs before any interrupt.
0.8 (USB-MIDI first) confirmed it: it never came up on USB. 0.9 sets IVB to 0x10000.

## Pins

| Pin | Use | Evidence |
|---|---|---|
| A7 | LCD DC (low = command) | LCD byte routines `0x40ABC` / `0x40B04` |
| A9 | LCD CS (active low) | same |
| A16 | LCD RST (high, low 1 ms, high, 130 ms) | `0x40C7C` |
| SPIM port 0 | LCD SCK / MOSI (`SPIM_IoConfig(0)`, `SPIM_Init(mode 0, div 0)`) | `0x4088C`, `0x40C64` |
| A29, A30 | encoder phases, polled; detent with both low; from (A29 0, A30 1) = +1, from (1, 0) = -1 | `0x59D8`, `0x5A48` |
| B6, A15, A31 | the three keys, active low (key codes 6, 15, 31); which is which: to confirm | `0x6B88` |
| A22 | output, set high at board init; **not** the power hold (0.27 released it: the pedal hung) | `0x40808` |
| A17 | output, low at board init; high (after A24) at start-up, low at power-off | `0x40808`, `0x44348`, `0x43E2C` |
| A24 | output, low at board init; high at start-up, low at power-off (the backlight is TIMER5 PWM, not this) | `0x40944`, `0x44344`, `0x43E34` |
| B5 | output, high at board init; stock drives it **low** with USB unplugged (`0x6008`). High: the whole output faint, guitar too. Molecular holds it low | `0x409A0`, `0x6008` |
| B3 | output, high at board init: **the power hold**. Low at power-off (`0x43E30`): on battery the pedal is off. B2 input, no pulls | `0x409EC` |
| B4, B2 | read as inputs (detects: charge? headphones?) | `0x40FD6`, `0x40FEC` |
| A0, A1 | UART0 RX (mode 2), TX (mode 0xC): the control link to the ARM effects processor, 460800 8N1 | `0x41008` |
| SPIS port 1 (A20 .. A23) | the audio link to the ARM (the MV chip is the slave) | `0x41D20` |
| ADC ch 9 / ch 2 | read as a ratio (battery?) | `0x41BB4` |

## LCD: ST7735S, 160 x 128 landscape

Init (`0x40C7C`): reset; `11`, 120 ms; `B1 05 3A 3A`; `B2 05 3A 3A`; `B3 05 3A 3A 05 3A 3A`; `B4 03`; `C0 62 02 04`;
`C1 C0`; `C2 0D 00`; `C3 8D 6A`; `C4 8D EE`; `C5 12`;
`E0 03 1B 12 11 3F 3A 32 34 2F 2B 30 3A 00 01 02 05`; `E1 03 1B 12 11 32 2F 2A 2F 2E 2C 35 3F 00 00 01 05`;
`36 68` (MX, MV, BGR: landscape); `3A 55` (RGB565); `2C`; clear; `29`. Window: `2A x0 x1`, `2B y0 y1`, `2C`, pixels;
the clear writes 128 rows of 160 pixels.

## Software

FreeRTOS (`vTaskDelay` = `0x587CC`, `xTaskCreate` = `0x581E4`; tasks "Flash Task", "SPIS Audio task", ...).

## The audio path: an ARM effects processor (found 2026-10-31, Molecular 0.36 .. 0.44)

The MV chip does not drive the jack. A second chip, an **ARM Cortex-M4F** with its own flash and bootloader, runs
the effects (amp, cabinet, NAM, looper) and the codec. Its application is HTFW section `c` (file offset 0x400 =
its address 0x4400; a Keil image, scatter-loaded into RAM at 0x20000000). Two links:

- **Audio, SPIS** (the ARM is the master): 512 bytes each way per exchange, 689 a second (64 stereo frames of
  16 bits at 44.1 kHz). The MV's audio goes in **bytes 0..255** of what it sends, zeros after; the ARM decodes them
  from byte 1 (its `0x5A14`: the link delays a byte) and adds them after its effects, at the guitar's scale. Stock's
  done interrupt is `0x23A0` (runs at `0x123A0`).
- **Control, UART0** at 460800 8N1. A frame (stock `0x5600`): `[crc8] [n lo] [n hi] [type] [payload]`, n = payload
  + 4, CRC-8 polynomial 0x07, init 0, over the payload; type 0 sets, 1 asks. The payload: `[id lo] [id hi] [len lo]
  [len hi] [data]`. The ARM reads all it has at each poll and wants **exactly one frame**: send one, wait for its
  answer `01 04 00 14` (accepted; `13` rejected). It dispatches on the id (`0x639E`: the top nibble a group, the low
  byte an entry). Stock's commands are a table at `0x130C90` (26 senders, `0x3F21C` .. `0x3F9A6`).
- At start stock sends `0x1000` (two levels, settings +0x2F0 and +0x2F4, 0.005 .. 1, and trims), `0x1001`, then
  the preset `0x0000` (368 bytes: a level, two 16-bit values, the chain order, two 12-byte blocks, and the 320-byte
  parameter block = the preset record from +0x4E). The ARM's settings start at zero: without `0x1000` it is silent.
  Factory presets: section `e`, 0x400 bytes a slot, a 0x198-byte record (magic A5 5A).
- Message `0x5001` is the looper's (mode, levels), `0x0004` switches one effect module on or off (the mask is the
  preset's second 16-bit value).

## Power

Power-off (stock `0x43E24`): A17, B3, A24 low, in that order, then wait. B3 is the power hold: on battery the pedal
goes off there; the power key (the encoder, A15) powers it on. Stock never calls `BACKUP_SystemPowerDown` (0.26
did: the chip stopped answering). A battery disconnect resets the always-on power state.

## The chip's rules for an application (found 2026-11-01, builds 64 .. 74)
Five faults that only the pedal shows (the simulator and the host tests have none of these limits):
- **PSW.AEN** (bit 13, the audio extension) must be set by the application. The zero-overhead loops (`mtlbi`,
  `mtlei`, `$lc`), which GCC makes of many loops with `-mext-zol`, run their body once without it. flash_boot leaves
  it on after a cold start and off after an install (logged: PSW `0x00070008`, a 16-step loop ran 1 step; 16 with
  AEN set). Neither the SDK's start-up nor the stock firmware sets it. Molecular sets it in `__cpu_init`.
- **The FPU is single precision only** (d1088-spu): a double-precision instruction is a coprocessor exception
  (exception 7, ITYPE `0x200E6`). The device Makefile fails on any.
- **RAM from 0x2003C000 is not the application's**: data placed there (USB buffers, the ARM link's buffers, the
  flash driver's state) were overwritten. `app.ld` asserts `_end <= 0x2003BF00`.
- **The stack is 4 KB** (0x20003000 .. 0x20004000), with the hardware stack bound below it and the TCM's copy of the
  first 12 KB of code below that.
- **The SDK's timeouts count an interrupt's tick** (`gSysTick`): a call with a timeout inside `GIE_DISABLE` waits
  for good if the event never comes (USB sends while the Mac does not read).
