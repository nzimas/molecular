#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pocket Master USB-MIDI SysEx protocol, as used by Sonicake Manager 1.5.2 to update firmware.

Recovered by disassembling 5868USB.dylib (getMidiMessage, HTFirmware::timerCallback,
_deviceStartUpdate, HTDevice::handleIncomingMidiMessage, HTDevice::reciveACKData).
This module only builds and parses bytes. It never talks to a device.

Frame (before encoding):
  [crc8, count, index, len] + payload[len]
  crc8   CRC-8, poly 0x07, init 0, computed over the whole frame with crc8 = 0
  count  1 for a single-frame message, 0xFF for streamed firmware data
  index  0 for a single-frame message, packet number (mod 256) for firmware data

Encoding: when nibble mode is on (the Manager always enables it) every frame byte is
sent as two bytes, high nibble then low nibble, so the SysEx body stays 7-bit clean.
The result is wrapped in F0 ... F7. There is no manufacturer ID.

Messages start with a 2-byte opcode. Host to device uses group 0x11. Device replies
in group 0x13 or 0x14 are ACKs; the first byte after the opcode is the status, 0 = OK.
Any other status is a NACK: the Manager resends the last message, gives up after 6
retries, and also times out after 3000 timer ticks without an ACK.

Firmware update sequence (one ACK expected after every message):
  11 60                                  begin update
  per section, in image order:
    11 61 <tag> 00 00 00 <u32 packets> <u32 bytes>   begin section
    data packets: count=0xFF, index=n, payload = next chunk of (11 <tag> + section data)
    11 6E <tag> 00                       end section
  11 6E 6E 00                            end of update ("all finish")

Chunk size is 19 bytes in nibble mode and 42 bytes in raw mode. Either way a full
data packet is a 48-byte SysEx message. Bytes 4..5 of the begin-section payload are
never written by the Manager (left as allocator contents); zero is used here.
There is no separate bootloader step: the Manager's deviceStartBoot is a stub.
"""
import math, struct, sys

GROUP_HOST = 0x11
ACK_GROUPS = (0x13, 0x14)
OP_BEGIN_UPDATE = 0x60
OP_BEGIN_SECTION = 0x61
OP_END = 0x6E
END_ALL = 0x6E
STREAM = 0xFF


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xff if c & 0x80 else (c << 1) & 0xff
    return c


def chunk_size(nibble=True):
    return 19 if nibble else 42


def frame(count, index, payload, nibble=True):
    """Build one SysEx message (F0 ... F7) around a payload."""
    if len(payload) > 0xff:
        raise ValueError("payload longer than 255 bytes")
    body = bytearray([0, count & 0xff, index & 0xff, len(payload)]) + payload
    body[0] = crc8(body)
    if nibble:
        body = bytearray(x for b in body for x in (b >> 4, b & 0x0f))
    return bytes([0xF0]) + bytes(body) + bytes([0xF7])


def parse_frame(msg, nibble=True):
    """Decode one SysEx message into (count, index, payload); raises on a bad frame."""
    if msg[:1] != b"\xF0" or msg[-1:] != b"\xF7":
        raise ValueError("not a SysEx message")
    body = msg[1:-1]
    if nibble:
        if len(body) % 2:
            raise ValueError("odd nibble count")
        body = bytes((body[i] << 4) | body[i + 1] for i in range(0, len(body), 2))
    if len(body) < 4:
        raise ValueError("frame too short")
    check = bytearray(body)
    check[0] = 0
    if crc8(check) != body[0]:
        raise ValueError("CRC-8 mismatch: got 0x%02x, want 0x%02x" % (body[0], crc8(check)))
    count, index, length = body[1], body[2], body[3]
    payload = body[4:]
    if len(payload) != length:
        raise ValueError("length byte %d but %d payload bytes" % (length, len(payload)))
    return count, index, payload


def command(op, args=b"", nibble=True):
    return frame(1, 0, bytes([GROUP_HOST, op]) + bytes(args), nibble)


def is_ack(payload):
    """Return (is_ack, ok) for the payload of a device reply frame."""
    if len(payload) < 3 or payload[0] not in ACK_GROUPS:
        return False, False
    return True, payload[2] == 0


def section_stream(section):
    return bytes([GROUP_HOST, ord(section.tag)]) + section.data


def update_messages(img, nibble=True):
    """Yield (label, sysex) for every message of a firmware update, in order."""
    size = chunk_size(nibble)
    yield "begin update", command(OP_BEGIN_UPDATE, nibble=nibble)
    for s in img.sections:
        stream = section_stream(s)
        packets = math.ceil(len(stream) / size)
        desc = bytes([ord(s.tag), 0, 0, 0]) + struct.pack("<II", packets, len(stream))
        yield "begin section %s" % s.tag, command(OP_BEGIN_SECTION, desc, nibble)
        for n in range(packets):
            yield ("data %s #%d" % (s.tag, n),
                   frame(STREAM, n, stream[n * size:(n + 1) * size], nibble))
        yield "end section %s" % s.tag, command(OP_END, bytes([ord(s.tag), 0]), nibble)
    yield "end update", command(OP_END, bytes([END_ALL, 0]), nibble)


def main(argv):
    """Usage: sonicake_proto.py <firmware.bin> [out.syx]  (offline: writes a file only)"""
    sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from htfw import Image
    img = Image(open(argv[0], "rb").read())
    problems = img.check()
    if problems:
        sys.exit("refusing to encode an image that fails its checks: %s" % "; ".join(problems))
    msgs = list(update_messages(img))
    total = sum(len(m) for _, m in msgs)
    print("%s v%s: %d SysEx messages, %d bytes on the wire"
          % (img.product, img.version_str, len(msgs), total))
    for label, m in msgs[:3] + msgs[-2:]:
        print("  %-18s %s" % (label, m.hex(" ")))
    if len(argv) > 1:
        with open(argv[1], "wb") as f:
            for _, m in msgs:
                f.write(m)
        print("wrote", argv[1])


if __name__ == "__main__":
    main(sys.argv[1:])
