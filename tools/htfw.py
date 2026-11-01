#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parser and builder for the Sonicake "HTFW" firmware container (Pocket Master, QME-10).

Layout (little-endian). Field meanings marked (Manager) were confirmed by
disassembling 5868USB.dylib from Sonicake Manager 1.5.2:
  0x00  char[4]   magic "HTFW"                                  (Manager: isValidFirmwareFile)
  0x04  u16       image CRC: CRC-16/MODBUS over bytes 6..EOF    (Manager: _checkCrc)
  0x06  u16       always 1 in stock images; covered by the image CRC
  0x08  u32       total file size, header included              (Manager: _checkCrc)
  0x0c  char[16]  product name, NUL padded; the connected device's name
                  must contain it                               (Manager: isValidFirmwareFile)
  0x1c  u8        'V'
  0x1d  u8[3]     version major, minor, patch
  0x20  u8        unknown (0, or 3 in v1.2.2)
  0x21  u8        1 = payload is compressed (Manager: _startDeCompress); 0 in all stock images
  0x22  u16       section count (Manager reads only the low byte)
  0x24  u32       total payload size (sum of all section lengths)
  0x28  u8[16]    zero / reserved
  0x38  entry[n]  section table, 16 bytes each
  then            concatenated section payloads, in table order

Section table entry:
  +0x0  u16  CRC-16/MODBUS of the section payload (Manager checks it before sending)
  +0x2  u8   zero / reserved
  +0x3  u8   single-letter section tag ('c' 'g' 'f' 'b' 'e'); also sent to the device
  +0x4  u32  target flash address
  +0x8  u32  offset of the payload, relative to the end of the header
  +0xc  u32  payload length

The first payload section ('c') begins with 8x 0xFF followed by an ASCII build tag
such as "V132", which is why the table looks terminated when read by eye.
"""
import struct, sys, os

MAGIC = b"HTFW"
ENTRY = 16
FIXED = 0x38


def crc16(data):
    """CRC-16/MODBUS in the byte order the Manager stores and compares it.

    The Manager uses the classic auchCRCHi/auchCRCLo table form and compares
    (hi << 8 | lo) against a little-endian u16, which is the bitwise MODBUS
    result with its two bytes swapped.
    """
    c = 0xffff
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xa001 if c & 1 else c >> 1
    return ((c & 0xff) << 8) | (c >> 8)


class Section:
    def __init__(self, tag, addr, data, crc=None, off=None):
        self.tag, self.addr, self.data = tag, addr, data
        self.crc = crc16(data) if crc is None else crc
        self.off, self.length = off, len(data)


class Image:
    def __init__(self, raw):
        if raw[:4] != MAGIC:
            raise ValueError("not an HTFW image")
        self.raw = raw
        self.checksum, self.field06, self.total_size = struct.unpack_from("<HHI", raw, 4)
        self.product = raw[0x0c:0x1c].split(b"\0")[0].decode("ascii", "replace")
        self.version = struct.unpack_from("<3B", raw, 0x1d)
        self.unknown20, self.compressed, self.count = struct.unpack_from("<BBH", raw, 0x20)
        (self.payload_size,) = struct.unpack_from("<I", raw, 0x24)
        self.header_size = FIXED + self.count * ENTRY
        self.sections = []
        for i in range(self.count):
            crc, _, tag, addr, off, length = struct.unpack_from(
                "<HBBIII", raw, FIXED + i * ENTRY)
            base = self.header_size + off
            self.sections.append(
                Section(chr(tag), addr, raw[base:base + length], crc, off))

    @property
    def version_str(self):
        return "%d.%d.%d" % self.version

    def check(self):
        problems = []
        if self.total_size != len(self.raw):
            problems.append("size field 0x%x != file size 0x%x"
                            % (self.total_size, len(self.raw)))
        if self.header_size + self.payload_size != len(self.raw):
            problems.append("header+payload 0x%x != file size 0x%x"
                            % (self.header_size + self.payload_size, len(self.raw)))
        if crc16(self.raw[6:]) != self.checksum:
            problems.append("image CRC 0x%04x != computed 0x%04x"
                            % (self.checksum, crc16(self.raw[6:])))
        run = 0
        for s in self.sections:
            if s.off != run:
                problems.append("section '%s' offset 0x%x breaks the chain at 0x%x"
                                % (s.tag, s.off, run))
            if len(s.data) != s.length:
                problems.append("section '%s' truncated" % s.tag)
            elif crc16(s.data) != s.crc:
                problems.append("section '%s' CRC 0x%04x != computed 0x%04x"
                                % (s.tag, s.crc, crc16(s.data)))
            run += s.length
        if run != self.payload_size:
            problems.append("section lengths sum to 0x%x, payload size says 0x%x"
                            % (run, self.payload_size))
        return problems


def build(product, version, sections, unknown20=0, field06=1):
    """Assemble an HTFW image from Section objects, computing every CRC and size.

    Compressed payloads are not supported; the compressed flag is always written as 0.
    """
    name = product.encode("ascii")
    if len(name) > 15:
        raise ValueError("product name longer than 15 bytes")
    header_size = FIXED + len(sections) * ENTRY
    payload = b"".join(s.data for s in sections)
    hdr = bytearray(header_size)
    struct.pack_into("<4sHHI16sc3sBBHI", hdr, 0, MAGIC, 0, field06,
                     header_size + len(payload), name, b"V", bytes(version),
                     unknown20, 0, len(sections), len(payload))
    off = 0
    for i, s in enumerate(sections):
        struct.pack_into("<HBBIII", hdr, FIXED + i * ENTRY,
                         crc16(s.data), 0, ord(s.tag), s.addr, off, len(s.data))
        off += len(s.data)
    raw = bytearray(hdr + payload)
    struct.pack_into("<H", raw, 4, crc16(raw[6:]))
    return bytes(raw)


def rebuild(img, sections=None):
    """Rebuild an image with the same header fields, optionally with new sections."""
    return build(img.product, img.version, sections or img.sections,
                 img.unknown20, img.field06)


def main(paths):
    for p in paths:
        img = Image(open(p, "rb").read())
        print("== %s" % os.path.basename(p))
        print("   product %r  version %s  sections %d  unknown@0x20 %d  compressed %d"
              % (img.product, img.version_str, img.count, img.unknown20, img.compressed))
        print("   crc 0x%04x  size 0x%x  header 0x%x  payload 0x%x"
              % (img.checksum, img.total_size, img.header_size, img.payload_size))
        print("   tag  target-addr  file-off     length       crc16")
        for s in img.sections:
            print("    %s   0x%08x   0x%08x   0x%08x   0x%04x"
                  % (s.tag, s.addr, img.header_size + s.off, s.length, s.crc))
        problems = img.check()
        for msg in problems:
            print("   !! %s" % msg)
        if not problems:
            print("   all sizes and CRCs check out")
        print()


if __name__ == "__main__":
    main(sys.argv[1:])
