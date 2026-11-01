// SPDX-License-Identifier: GPL-3.0-only
// pmota.js: installs a firmware on the Sonicake Pocket Master over Web MIDI (SysEx), as Sonicake's own Manager
// does, from the protocol notes in Molecular's tree (docs/hardware/PROTOCOL.md, tools/sonicake_proto.py).
//
//   frame    [crc8, count, index, len] + payload, CRC-8 (poly 0x07) over the frame with its crc byte 0; every byte
//            sent as two nibbles (high, low) inside F0 .. F7
//   update   11 60 begin; per section 11 61 <tag> 00 00 00 <u32 packets> <u32 bytes>, the data packets (count FF,
//            index n mod 256, 19 bytes each of 11 <tag> + the section), 11 6E <tag> 00; then 11 6E 6E 00
//   answers  every message waits for its ACK (group 13 or 14, status 0); a NACK or 3 s of silence resends it, at
//            most 6 times, then the update stops (nothing more is sent)
//   entry    the update is received by flash_boot, the pedal's own loader, which updates never write: the running
//            firmware (stock or Molecular) hands over to it on 11 6F; it comes back on USB about 2 s later
//
// Nothing here talks to anything but the MIDI port named "Pocket Master". No network use beyond loading the page.
"use strict";

const PMOTA = (() => {
  const NAME = "Pocket Master";
  const CHUNK = 19;

  function crc8(b) {
    let c = 0;
    for (const x of b) {
      c ^= x;
      for (let i = 0; i < 8; i++) c = c & 0x80 ? ((c << 1) ^ 0x07) & 0xff : (c << 1) & 0xff;
    }
    return c;
  }
  function frame(count, index, payload) {
    const body = new Uint8Array(4 + payload.length);
    body.set([0, count & 0xff, index & 0xff, payload.length], 0);
    body.set(payload, 4);
    body[0] = crc8(body);
    const m = new Uint8Array(2 + 2 * body.length);
    m[0] = 0xf0;
    body.forEach((b, i) => { m[1 + 2 * i] = b >> 4; m[2 + 2 * i] = b & 15; });
    m[m.length - 1] = 0xf7;
    return m;
  }
  const command = (op, args = []) => frame(1, 0, Uint8Array.from([0x11, op, ...args]));
  function u32(n) { return [n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff]; }

  // the messages of an update: sections is [{tag: "b", data: Uint8Array}, ...] in the image's order
  function updateMessages(sections) {
    const out = [command(0x60)];
    for (const s of sections) {
      const t = s.tag.charCodeAt(0);
      const stream = new Uint8Array(2 + s.data.length);
      stream[0] = 0x11; stream[1] = t; stream.set(s.data, 2);
      const packets = Math.ceil(stream.length / CHUNK);
      out.push(command(0x61, [t, 0, 0, 0, ...u32(packets), ...u32(stream.length)]));
      for (let n = 0; n < packets; n++) out.push(frame(0xff, n, stream.subarray(n * CHUNK, (n + 1) * CHUNK)));
      out.push(command(0x6e, [t, 0]));
    }
    out.push(command(0x6e, [0x6e, 0]));
    return out;
  }

  // a reply's payload, or null if it is not a valid frame
  function payload(m) {
    if (m.length < 10 || m[0] !== 0xf0 || m[m.length - 1] !== 0xf7 || (m.length - 2) % 2) return null;
    const body = [];
    for (let i = 1; i + 1 < m.length - 1; i += 2) body.push((m[i] << 4) | m[i + 1]);
    if (body.length < 4) return null;
    const chk = body.slice(); chk[0] = 0;
    if (crc8(chk) !== body[0] || body[3] !== body.length - 4) return null;
    return body.slice(4);
  }

  // Sonicake's HTFW container (the stock firmware file): its product, version and sections
  function parseHTFW(buf) {
    const raw = new Uint8Array(buf), dv = new DataView(buf);
    if (String.fromCharCode(...raw.subarray(0, 4)) !== "HTFW") throw new Error("This is not a Sonicake firmware file.");
    const product = String.fromCharCode(...raw.subarray(0x0c, 0x1c)).replace(/\0.*$/, "");
    const version = raw[0x1d] + "." + raw[0x1e] + "." + raw[0x1f];
    if (raw[0x21]) throw new Error("This firmware file is compressed, which this page cannot send.");
    const count = dv.getUint16(0x22, true), head = 0x38 + 16 * count;
    if (dv.getUint32(8, true) !== raw.length) throw new Error("The firmware file is incomplete.");
    const sections = [];
    for (let i = 0; i < count; i++) {
      const e = 0x38 + 16 * i, off = dv.getUint32(e + 8, true), len = dv.getUint32(e + 12, true);
      if (head + off + len > raw.length) throw new Error("The firmware file is damaged.");
      const data = raw.subarray(head + off, head + off + len);
      if (crc16(data) !== dv.getUint16(e, true)) throw new Error("The firmware file is damaged (a section fails its check).");
      sections.push({ tag: String.fromCharCode(raw[e + 3]), data });
    }
    if (crc16(raw.subarray(6)) !== dv.getUint16(4, true)) throw new Error("The firmware file is damaged (its check fails).");
    return { product, version, sections };
  }
  function crc16(d) {        // CRC-16/MODBUS, its bytes swapped as Sonicake stores it
    let c = 0xffff;
    for (const b of d) {
      c ^= b;
      for (let i = 0; i < 8; i++) c = c & 1 ? (c >>> 1) ^ 0xa001 : c >>> 1;
    }
    return ((c & 0xff) << 8) | (c >>> 8);
  }

  const sleep = ms => new Promise(r => setTimeout(r, ms));

  class Pedal {
    constructor(access) {
      this.access = access;
      this.replies = [];
      this.waiter = null;
      this.input = null;
      this.output = null;
      this.onchange = null;
      access.onstatechange = () => { this.bind(); if (this.onchange) this.onchange(); };
      this.bind();
    }
    static async open() {
      if (!navigator.requestMIDIAccess)
        throw new Error("This browser has no Web MIDI. Use a desktop Chrome, Edge or Opera.");
      return new Pedal(await navigator.requestMIDIAccess({ sysex: true }));
    }
    bind() {           // the Pocket Master's ports, as they are now (they change as it moves to update mode)
      const find = map => { for (const p of map.values()) if (p.name && p.name.includes(NAME) && p.state === "connected") return p; return null; };
      const inp = find(this.access.inputs), out = find(this.access.outputs);
      if (inp !== this.input) {
        if (this.input) this.input.onmidimessage = null;
        this.input = inp;
        if (inp) inp.onmidimessage = e => {
          this.replies.push(e.data);
          if (this.waiter) this.waiter();
        };
      }
      this.output = out;
    }
    get present() { return !!(this.input && this.output); }
    // sends one message and waits for its answer: 1 ACK OK, 0 NACK, -1 nothing within ms
    async exchange(m, ms) {
      this.replies = [];
      if (!this.output) return -1;
      try { this.output.send(m); } catch (e) { return -1; }
      const end = performance.now() + ms;
      for (;;) {
        while (this.replies.length) {
          const p = payload(this.replies.shift());
          if (p && p.length >= 3 && (p[0] === 0x13 || p[0] === 0x14)) return p[2] === 0 ? 1 : 0;
        }
        const left = end - performance.now();
        if (left <= 0) return -1;
        await new Promise(r => { this.waiter = r; setTimeout(r, Math.min(left, 50)); });
        this.waiter = null;
      }
    }
    async waitFor(present, ms) {
      const end = performance.now() + ms;
      while (performance.now() < end) {
        this.bind();
        if (this.present === present) return true;
        await sleep(100);
      }
      return false;
    }
    // into flash_boot's update mode, already there or by 11 6F; true once begin update (11 60) is acknowledged
    async enterUpdate(say) {
      if (!this.present) throw new Error("No Pocket Master found. Is it switched on and connected?");
      say("Asking the pedal for its update mode…");
      if (await this.exchange(command(0x60), 1500) === 1) return true;        // (already in update mode)
      await this.exchange(command(0x6f), 1500);
      say("The pedal is restarting into its update mode…");
      await this.waitFor(false, 6000);
      if (!await this.waitFor(true, 15000)) throw new Error("The pedal did not come back in update mode. Unplug it, plug it in again and retry.");
      await sleep(700);
      for (let t = 0; t < 5; t++) {
        if (await this.exchange(command(0x60), 3000) === 1) return true;
        await sleep(500);
      }
      throw new Error("The pedal's update mode did not answer. Unplug it, plug it in again and retry.");
    }
    // the update: msgs from updateMessages (its first, 11 60, already sent by enterUpdate)
    async send(msgs, progress) {
      for (let n = 1; n < msgs.length; n++) {
        let ok = false;
        for (let tries = 0; tries <= 6 && !ok; tries++) {
          const a = await this.exchange(msgs[n], 3000);
          ok = a === 1;
          if (!ok && !this.present) await this.waitFor(true, 5000);
        }
        if (!ok) throw new Error("The update stopped at message " + (n + 1) + " of " + msgs.length +
          ". Nothing more was sent. Reload this page and install again: the pedal stays in its update mode.");
        if (progress && (n % 64 === 0 || n === msgs.length - 1)) progress(n + 1, msgs.length);
      }
    }
  }

  return { Pedal, updateMessages, parseHTFW, crc16 };
})();
