// pmmidi: talks to the Pocket Master over CoreMIDI.
//   pmmidi list                     the MIDI sources and destinations
//   pmmidi listen SECONDS           prints every SysEx the Pocket Master sends
//   pmmidi send HEX [SECONDS]       sends one SysEx (F0 ... F7, hex) and prints the replies for SECONDS (default 2)
//   pmmidi log SECONDS              prints Molecular's log lines (SysEx F0 7D 4D text F7) as text, with the time,
//                                   reconnecting whenever the pedal leaves USB and comes back
//   pmmidi update FILE              a firmware update to flash_boot: FILE has one SysEx a line (hex), as
//                                   analysis/sonicake_proto.py writes them; each must be acknowledged (ACK group 0x13 or
//                                   0x14, status 0) before the next is sent; a NACK or 3 s of silence resends it, at most
//                                   6 times (as the Sonicake Manager's HTFirmware); then it stops and sends nothing more.
// Nothing is sent unless "send" or "update" is given.
import CoreMIDI
import Foundation

func name(_ obj: MIDIObjectRef) -> String {
    var s: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(obj, kMIDIPropertyDisplayName, &s)
    return (s?.takeRetainedValue() as String?) ?? "?"
}
func find(_ count: Int, _ get: (Int) -> MIDIEndpointRef) -> MIDIEndpointRef? {
    for i in 0..<count where name(get(i)).contains("Pocket Master") { return get(i) }
    return nil
}

let args = CommandLine.arguments
var quiet = false
var logmode = false
let logStart = Date()
guard args.count >= 2 else { print("usage: pmmidi list | listen SECONDS | send HEX [SECONDS]"); exit(2) }
var client = MIDIClientRef(), inPort = MIDIPortRef(), outPort = MIDIPortRef()
MIDIClientCreateWithBlock("pmmidi" as CFString, &client) { _ in }

if args[1] == "list" {
    for i in 0..<MIDIGetNumberOfSources() { print("source:      \(name(MIDIGetSource(i)))") }
    for i in 0..<MIDIGetNumberOfDestinations() { print("destination: \(name(MIDIGetDestination(i)))") }
    exit(0)
}

var sysex = [UInt8]()
let lock = NSLock()
var replies = [[UInt8]]()

func crc8(_ d: [UInt8]) -> UInt8 {
    var c: UInt8 = 0
    for b in d {
        c ^= b
        for _ in 0..<8 { c = (c & 0x80) != 0 ? (c << 1) ^ 0x07 : c << 1 }
    }
    return c
}
/* a reply's payload, or nil if it is not a valid nibble-encoded frame */
func payload(_ m: [UInt8]) -> [UInt8]? {
    guard m.count >= 10, m.first == 0xF0, m.last == 0xF7, (m.count - 2) % 2 == 0 else { return nil }
    var body = [UInt8]()
    var i = 1
    while i + 1 < m.count - 1 { body.append((m[i] << 4) | m[i + 1]); i += 2 }
    guard body.count >= 4 else { return nil }
    var chk = body; chk[0] = 0
    guard crc8(chk) == body[0], Int(body[3]) == body.count - 4 else { return nil }
    return Array(body[4...])
}
MIDIInputPortCreateWithProtocol(client, "in" as CFString, ._1_0, &inPort) { list, _ in
    for packet in list.unsafeSequence() {
        let words = Array(packet.words())
        for w in words {                         // UMP: type 3 = 7-bit SysEx, up to 6 bytes a packet
            guard (w >> 28) == 3 else { continue }
        }
        var i = 0
        while i + 1 < words.count {
            let w0 = words[i], w1 = words[i + 1]
            if (w0 >> 28) == 3 {
                let status = (w0 >> 20) & 0xF, n = Int((w0 >> 16) & 0xF)
                let bytes = [UInt8((w0 >> 8) & 0xFF), UInt8(w0 & 0xFF), UInt8((w1 >> 24) & 0xFF),
                             UInt8((w1 >> 16) & 0xFF), UInt8((w1 >> 8) & 0xFF), UInt8(w1 & 0xFF)]
                if status == 0 || status == 1 { sysex = [0xF0] }
                sysex += bytes.prefix(n)
                if status == 0 || status == 3 {
                    sysex.append(0xF7)
                    if logmode, sysex.count >= 4, sysex[1] == 0x7D, sysex[2] == 0x4D {
                        let text = String(bytes: sysex[3..<(sysex.count - 1)], encoding: .ascii) ?? "?"
                        print(String(format: "%7.2f  ", Date().timeIntervalSince(logStart)) + text)
                        fflush(stdout)
                    } else if quiet == false {
                        print("<- " + sysex.map { String(format: "%02x", $0) }.joined(separator: " "))
                        fflush(stdout)
                    }
                    lock.lock(); replies.append(sysex); lock.unlock()
                }
            }
            i += 2
        }
    }
}
if args[1] == "log" {
    logmode = true
    let end = Date(timeIntervalSinceNow: Double(args.count > 2 ? args[2] : "60") ?? 60)
    var current: MIDIEndpointRef = 0
    print("listening for Molecular's log (the time is seconds from now)"); fflush(stdout)
    while Date() < end {
        let src = find(MIDIGetNumberOfSources(), MIDIGetSource) ?? 0
        if src != current {
            if current != 0 { MIDIPortDisconnectSource(inPort, current) }
            if src != 0 { MIDIPortConnectSource(inPort, src, nil) }
            print(String(format: "%7.2f  ", Date().timeIntervalSince(logStart)) + (src != 0 ? "[Pocket Master on MIDI]" : "[Pocket Master gone]"))
            fflush(stdout)
            current = src
        }
        RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.25))
    }
    exit(0)
}
guard let src = find(MIDIGetNumberOfSources(), MIDIGetSource) else { print("no Pocket Master source"); exit(1) }
MIDIPortConnectSource(inPort, src, nil)

if args[1] == "listen" {
    RunLoop.current.run(until: Date(timeIntervalSinceNow: Double(args.count > 2 ? args[2] : "5") ?? 5))
    exit(0)
}
if args[1] == "send", args.count >= 3 {
    guard let dst = find(MIDIGetNumberOfDestinations(), MIDIGetDestination) else { print("no Pocket Master destination"); exit(1) }
    let hex = args[2].replacingOccurrences(of: " ", with: "")
    var bytes = [UInt8]()
    var idx = hex.startIndex
    while idx < hex.endIndex {
        let next = hex.index(idx, offsetBy: 2)
        bytes.append(UInt8(hex[idx..<next], radix: 16)!)
        idx = next
    }
    MIDIOutputPortCreate(client, "out" as CFString, &outPort)
    var buf = [UInt8](repeating: 0, count: 1024)
    buf.withUnsafeMutableBytes { raw in
        let pl = raw.baseAddress!.assumingMemoryBound(to: MIDIPacketList.self)
        var pkt = MIDIPacketListInit(pl)
        pkt = MIDIPacketListAdd(pl, 1024, pkt, 0, bytes.count, bytes)
        MIDISend(outPort, dst, pl)
    }
    print("-> " + bytes.map { String(format: "%02x", $0) }.joined(separator: " "))
    RunLoop.current.run(until: Date(timeIntervalSinceNow: Double(args.count > 3 ? args[3] : "2") ?? 2))
    exit(0)
}
if args[1] == "update", args.count >= 3 {
    quiet = true
    guard let dst = find(MIDIGetNumberOfDestinations(), MIDIGetDestination) else { print("no Pocket Master destination"); exit(1) }
    MIDIOutputPortCreate(client, "out" as CFString, &outPort)
    let lines = try! String(contentsOfFile: args[2]).split(separator: "\n").map(String.init).filter { !$0.isEmpty }
    func bytes(_ hex: String) -> [UInt8] {
        var out = [UInt8](); var i = hex.startIndex
        while i < hex.endIndex { let n = hex.index(i, offsetBy: 2); out.append(UInt8(hex[i..<n], radix: 16)!); i = n }
        return out
    }
    func send(_ b: [UInt8]) {
        var buf = [UInt8](repeating: 0, count: 1024)
        buf.withUnsafeMutableBytes { raw in
            let pl = raw.baseAddress!.assumingMemoryBound(to: MIDIPacketList.self)
            var pkt = MIDIPacketListInit(pl)
            pkt = MIDIPacketListAdd(pl, 1024, pkt, 0, b.count, b)
            MIDISend(outPort, dst, pl)
        }
    }
    /* waits for the answer to the message just sent: 1 ACK OK, 0 NACK, -1 nothing within the time */
    func answer(_ timeout: Double) -> Int {
        let end = Date(timeIntervalSinceNow: timeout)
        while Date() < end {
            lock.lock(); let got = replies; replies.removeAll(); lock.unlock()
            for r in got {
                guard let p = payload(r), p.count >= 3, p[0] == 0x13 || p[0] == 0x14 else { continue }
                return p[2] == 0 ? 1 : 0
            }
            RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.0005))
        }
        return -1
    }
    let t0 = Date()
    for (n, line) in lines.enumerated() {
        let b = bytes(line)
        var tries = 0, ok = false
        lock.lock(); replies.removeAll(); lock.unlock()
        while tries <= 6 {
            send(b)
            let a = answer(3.0)
            if a == 1 { ok = true; break }
            print("message \(n + 1)/\(lines.count): \(a == 0 ? "NACK" : "no answer"), \(tries < 6 ? "resending" : "giving up")")
            tries += 1
        }
        if !ok {
            print("STOPPED at message \(n + 1) of \(lines.count): nothing more sent")
            exit(1)
        }
        if n % 2000 == 0 || n == lines.count - 1 {
            print(String(format: "%6d / %d  (%.0f s)", n + 1, lines.count, Date().timeIntervalSince(t0)))
            fflush(stdout)
        }
    }
    print("all \(lines.count) messages acknowledged")
    exit(0)
}
print("unknown command"); exit(2)
