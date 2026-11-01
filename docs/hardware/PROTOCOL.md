# Pocket Master firmware update protocol (USB-MIDI SysEx)

Recovered on 2026-10-30 by disassembling `5868USB.dylib` (arm64 slice) from Sonicake Manager 1.5.2 for Mac.
Symbol names come from the library itself. Reference code: [tools/sonicake_proto.py](../../tools/sonicake_proto.py)
(encoder/decoder), [tools/htfw.py](../../tools/htfw.py) (image parser/builder). Its tests
(9/9 against four stock images) live in the research tree, not in this repository.

**Status:** recovered from the host side; since exercised on the device by every Molecular install (tools/pmmidi,
the web installer docs/js/pmota.js), including updates of section `b` alone.

## Image integrity (both checks are now solved)

| Field | Algorithm | Source |
|---|---|---|
| Image CRC, u16 at `0x04` | CRC-16/MODBUS over bytes `6..EOF`, stored as `hi<<8 \| lo` (the bitwise result byte-swapped) | `_checkCrc` |
| Section CRC, u16 at entry `+0` | Same CRC over the section payload | `_deviceStartUpdate` |
| File size, u32 at `0x08` | Must equal the real file size | `_checkCrc` |
| Magic and product | `"HTFW"`; the connected device's name must contain the header's product string | `isValidFirmwareFile` |

`htfw.rebuild()` reproduces all four stock images byte for byte, so it can produce images the Manager accepts.
Header byte `0x21` is a compression flag (`_startDeCompress`). It is 0 in every stock image, and the builder always writes 0.

## Framing

```
frame   = [crc8, count, index, len] + payload[len]
crc8    = CRC-8 poly 0x07, init 0, over the whole frame with byte 0 = 0   (table at 0x155f50)
count   = 1 for single-frame commands, 0xFF for streamed firmware data
index   = 0 for commands, packet number mod 256 for data
on wire = F0 + nibbles(frame) + F7      (each byte -> high nibble, low nibble; no manufacturer ID)
```

The Manager hard-sets nibble mode on in `connectDevice`. A raw 8-bit mode also exists (42-byte chunks), but this Manager never uses it.
A full data packet is 19 payload bytes, which comes to **48 bytes of SysEx**.

Device to host uses the same framing. A message with `index` 0 carries a 2-byte opcode at frame bytes 4–5, then data;
continuation frames carry data from byte 4. Opcode groups **`0x13`/`0x14` are ACKs**, and the byte after the opcode is the status (`0` = OK).
Everything else goes to `reciveMidiData`, which handles presets, parameters and so on.

## Update sequence

Stop-and-wait: every message, data packets included, waits for an ACK before the next is sent.

```
11 60                                           begin update
for each section, in image-table order:
  11 61 <tag> 00 00 00 <u32 packets> <u32 bytes> begin section   (bytes 4-5 never written by the Manager)
  [data] count=FF index=n  payload = stream[19n : 19n+19]
         where stream = 11 <tag> + section payload
         packets = ceil(len(stream) / 19)
  11 6E <tag> 00                                end section
11 6E 6E 00                                     end of update ("all finish")
```

- NACK (status ≠ 0): resend the last message, up to 6 retries, then abort (`HTFirmware::reset`).
- Timeout: 3000 timer ticks without an ACK. The tick period is not known yet.
- No bootloader handshake: `deviceStartBoot` is a stub returning −1. The running firmware receives the update itself,
  which fits `upgrade.c` / `flash_boot.c` in the public MVsilicon SDK.
- v1.3.3 comes to 108,262 SysEx messages, about 5.2 MB on the wire.

## What is not verified yet

- How the device actually behaves: the real ACK payload beyond group and status, whether it cares about bytes 4–5 of `11 61`,
  the pacing it needs, and what it does on a bad CRC.
- The section tag → flash address mapping happens on the device. The `addr` field is *not* sent; only the tag is.
  That fits the `g`/`f` address overlap: the device probably decides the placement by tag.
- What the device does after `11 6E 6E 00` (reboot, verify).
- Other opcodes (presets, NAM/IR upload, version query) are built in the Dart UI layer and passed through `_sendMidiMessage`.
  They were not recovered.
