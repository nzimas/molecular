# Sonicake Pocket Master (QME-10): first exploration

Date: 2026-10-30. Nothing has been written to the device. Everything below comes from
reading the USB descriptors, the four public stock firmware images and the official Mac Manager app.

## 1. The connected unit (read from USB)

| Field | Value |
|---|---|
| VID:PID | `0x84EF:0x0284` (Sonicake) |
| bcdDevice | `0x0101` |
| Serial string | `20240710` (looks like a build date, not a per-unit serial) |
| USB | 1.1, **Full Speed (12 Mbit/s)**, composite device (class EF/02/01, uses an IAD) |
| Interfaces | 0: Audio Control · 1–2: Audio Streaming (stereo in and out) · 3: **USB-MIDI** (2 endpoints) |
| Vendor-specific interface | **None** |

What this means: the only control channel is **USB-MIDI**. Firmware updates, preset editing and NAM/IR
uploads all travel as MIDI messages, and the Manager's `HTDevice::startUpdateFirmware` confirms it
sends them through CoreMIDI. Because the link is full-speed, USB audio bandwidth is limited, which fits
the community CFW listing "48 kHz USB audio" as a roadmap item.

## 2. The SoC: MVsilicon "B1" family (BP10xx), Andes NDS32 core

Evidence inside the firmware:
- A banner string: `MVsilicon B1 SDK` / `SDK Version: %d.%d.%d`
- A Bluetooth library version string: `2.8.15_v0.1.12P05_v761_BLE_20241016_9`. The `v0.1.12` part matches the public SDK
- The source path `../bt_audio_app_src/main.c`, which matches the folder `BT_Audio_APP/bt_audio_app_src/` in the public SDK
- Section `b`, the main code, is **not** ARM code: it has 3 `BX LR` instructions in 1.4 MB. That fits NDS32

The public SDK mirror is <https://github.com/leadercxn/bp1048_sdk_v0.1.12>, the exact SDK version this firmware uses:
- Chip ID `0xB1`, SDK 0.1.12, toolchain **Andes `nds32le-elf`** (AndeSight IDE), platform `nds32-ae210p`
- FreeRTOS, a BT stack, USB audio and MIDI, an audio-effects framework, `upgrade.c`, `flash_boot.c`, `bt_obex_upgrade.c`
- Flash map in `flash_config.h`: code at `0x0`, constant data at `0x198000`. The `BP1048P4` variant moves the
  data regions up by `+0x200000`, which is a 4 MB flash layout. The Pocket Master writes to `0x200000`–`0x288000`,
  so it has at least **4 MB of SPI NOR flash**

Published BP1048B2 specs (B1 specs not confirmed, so treat these as an approximation): 32-bit RISC with DSP instructions
and an FPU, up to 288 MHz, ~320 KB SRAM, 32 KB I-cache and 32 KB D-cache, an FFT accelerator, BT 5.0 dual mode,
multiple audio ADCs and DACs.

**Still unconfirmed:** the exact part number (for example BP1048B1 or BP1064). Reading the chip marking needs the case opened.

## 3. Firmware container: "HTFW" (fully parsed, see `tools/htfw.py`)

```
0x00 "HTFW" | 0x04 u16 image CRC | 0x06 u16 =1 | 0x08 u32 file size
0x0C char[16] "Pocket Master" | 0x1C 'V' maj min patch | 0x20 u16 ? | 0x22 u16 nsections
0x24 u32 payload size | 0x28 16x reserved | 0x38 section table (16 B each) | payloads
entry: u16 CRC-16/MODBUS (big-endian) | u8 0 | u8 tag | u32 flash addr | u32 offset | u32 length
```

Both checksums are solved and verified on every stock image: the section CRCs (19 of 19) and the image CRC at
0x04 (CRC-16/MODBUS over bytes 6..EOF). `htfw.rebuild()` reproduces all four images byte for byte. The update protocol is
documented in [PROTOCOL.md](PROTOCOL.md).

| Tag | Flash addr | Size (v1.3.3) | Contents |
|---|---|---|---|
| `b` | `0x000000` | 1.36 MB | Main NDS32 application: BT, USB, UI, DSP chain |
| `e` | `0x190000` | 244 KB (fixed) | Factory presets (`PoketMastr` magic, names like "Sweet OD" and "Neo Soul") |
| `c` | `0x200000` | 50 KB | Begins `FF×8 "V132"`; has error strings and "User IR". Exact role still open |
| `g` | `0x240000` | 292 KB | Resource blob with a self-describing table (464 entries), probably models or coefficients |
| `f` | `0x280000` | 32 KB | Cabinet name database ("TWD 1x8", "Viblux 1x12" and so on). Unchanged since v1.1.0 |

Open questions:
- `g` (`0x240000` + `0x49050`) overlaps `f` (`0x280000`) by `0x9050` bytes, so the address field may not be a plain flat offset for every section.
- Byte statistics in `c` and `g` look like ARM Thumb code. On a single NDS32 SoC that is more likely coincidence in data, but it still needs checking.

## 4. Manager app (Mac, v1.5.2)

A Flutter UI with native `5868USB.dylib`, built on JUCE. Notable exports: `deviceStartUpdate`, `deviceStartBoot`,
`checkCrc`, `isRealFirmware`, `sendMidiMessage`, `convertNamToNamb`, `namConvertClo`, along with WaveNet and
ContainerModel strings. NAM models are **converted on the computer** into a reduced format ("namb"/"clo") before
upload, so the device runs a slimmed-down model, not full NAM.

## 5. Existing alternative firmwares

- **HAGIWO XOX groovebox** (303/909): a working CFW flashed over USB, so the update path accepts non-stock images. Not distributed.
- **Community CFW project** (pocketmaster-cfw.guitar-training-generator.com): more NAM and IR slots, persistence of user data. Beta.
- Restoring stock by flashing the original `.bin` is reported to work.

## 6. Feasibility, and what to expect

Good:
- A public SDK at the matching version, with a real toolchain, drivers, effects framework and upgrade code.
- An open container format, with every checksum solved and reproducible.
- MIDI-based update path that others have already used for CFW.

Hard:
- NDS32 tooling is niche. You need the Andes GCC toolchain, and Ghidra supports NDS32 only through community plugins.
- No board schematic. The pins for the display, encoder, footswitches and codec have to be recovered from the stock `b` image.
- The SDK may not include the Sonicake-specific drivers (display, UI) or the NAM inference code.
- No JTAG/SWD access confirmed. Recovery depends on the bootloader surviving a bad flash. **Keep the v1.3.3 image and confirm a recovery path before the first custom flash.**

## Suggested next steps

1. ~~Reverse the image checksum and the MIDI update protocol~~: done, see [PROTOCOL.md](PROTOCOL.md). Still to do: verify on the device.
2. Clone the SDK and build its demo for the matching chip so the toolchain is proven.
3. Load section `b` into Ghidra (NDS32 plugin) at base `0x0`, and use the SDK's driver code to label peripherals and GPIO.
4. Open the unit and photograph the board to get the chip marking, flash part, codec, and any debug pads.
