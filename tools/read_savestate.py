#!/usr/bin/env python3
import gzip
import os
import struct
import sys
import zlib

HWRAM_BASE = 0x06000000
LWRAM_BASE = 0x00200000
BANK_SIZE = 0x00100000

PREVIEW_W = 302
PREVIEW_H = 240

WANTED = [
    ("current_room", "int", 1),
    ("next_script", "int", 1),
    ("death_played", "int", 1),
    ("ending_played", "int", 1),
    ("current_task", "int", 1),
    ("task_pc", "shorts", 64),
    ("new_task_pc", "shorts", 64),
    ("enabled_tasks", "shorts", 64),
    ("variables", "shorts", 256),
]

NOTABLE_VARS = {
    220: "room entry index -- what vm_enter_code sets and menu_note_checkpoint reads",
    227: "set to 1 by every path that starts or jumps a game (the password stub's own flag)",
    250: "fire button, read live every frame",
}


def inflate(raw):
    if raw[:2] == b"\x1f\x8b":
        return gzip.decompress(raw)
    try:
        return zlib.decompress(raw)
    except zlib.error:
        return raw


def sections(blob):
    if blob[:8] != b"MDFNSVST":
        raise SystemExit("not a Mednafen save state (no MDFNSVST magic)")

    pos = 32 + PREVIEW_W * PREVIEW_H * 3

    while pos + 36 <= len(blob):
        name = blob[pos:pos + 32].split(b"\x00")[0].decode("latin-1")
        size = struct.unpack_from("<I", blob, pos + 32)[0]
        pos += 36

        if size == 0 or pos + size > len(blob):
            break

        end = pos + size
        inner = pos

        while inner + 1 <= end:
            namelen = blob[inner]
            if namelen == 0 or inner + 1 + namelen + 4 > end:
                break
            entry = blob[inner + 1:inner + 1 + namelen].decode("latin-1")
            inner += 1 + namelen
            esize = struct.unpack_from("<I", blob, inner)[0]
            inner += 4
            if inner + esize > end:
                break
            yield name, entry, blob[inner:inner + esize]
            inner += esize

        pos = end


def unswap(data):
    out = bytearray(len(data))
    out[0::2] = data[1::2]
    out[1::2] = data[0::2]
    return bytes(out)


def read_map(path):
    syms = {}

    with open(path, "r", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2 and parts[0].startswith("0x") and len(parts[0]) >= 8:
                try:
                    syms.setdefault(parts[1], int(parts[0], 16))
                except ValueError:
                    pass
    return syms


class Ram(object):
    def __init__(self, high, low):
        self.high = high
        self.low = low

    def slice(self, addr, length):
        for base, bank in ((HWRAM_BASE, self.high), (LWRAM_BASE, self.low)):
            if bank is not None and base <= addr < base + BANK_SIZE:
                off = addr - base
                if off + length <= len(bank):
                    return bank[off:off + length]
        return None

    def s16(self, addr):
        raw = self.slice(addr, 2)
        return None if raw is None else struct.unpack(">h", raw)[0]

    def s32(self, addr):
        raw = self.slice(addr, 4)
        return None if raw is None else struct.unpack(">i", raw)[0]


def report(ram, syms):
    values = {}

    for name, kind, count in WANTED:
        addr = syms.get(name)

        if addr is None:
            print("  %-14s not in the map" % name)
            continue

        if kind == "int":
            values[name] = ram.s32(addr)
            print("  %-14s 0x%08x  %s" % (name, addr, values[name]))
        else:
            values[name] = [ram.s16(addr + 2 * i) for i in range(count)]

    print("")
    print("live tasks (pc, enabled -- 0 means running):")
    pcs = values.get("task_pc") or []
    en = values.get("enabled_tasks") or []
    npcs = values.get("new_task_pc") or []
    live = 0

    for i in range(len(pcs)):
        if pcs[i] in (None, -1) and (i >= len(npcs) or npcs[i] in (None, -1)):
            continue
        live += 1
        print("  task %2d  pc=%s  next=%s  enabled=%s"
              % (i,
                 "----" if pcs[i] == -1 else "%04x" % (pcs[i] & 0xFFFF),
                 "----" if npcs[i] == -1 else "%04x" % (npcs[i] & 0xFFFF),
                 en[i] if i < len(en) else "?"))

    if live == 0:
        print("  none")

    print("")
    print("variables the port routes on:")
    vs = values.get("variables") or []

    for index in sorted(NOTABLE_VARS):
        if index < len(vs):
            print("  var[%3d] = %-6s %s" % (index, vs[index], NOTABLE_VARS[index]))

    print("")
    print("what this says about the death path:")
    room = values.get("current_room")
    nxt = values.get("next_script")
    death = values.get("death_played")

    if death:
        print("  death_played is set, so the 0x21 guard fired and the gate is")
        print("  owed a death screen.")
    else:
        print("  death_played is CLEAR. If the prompt is on screen now, this")
        print("  death never reached decode.c's 0x21 guard -- nothing asked for")
        print("  room %s and nothing will open the death screen." % nxt)

    if nxt == 7:
        print("  next_script is 7, so the next frame enters menu_gate.")
    elif nxt == 0:
        print("  next_script is 0, so the room is running and no gate is pending.")
    else:
        print("  next_script is %s, so a room change is pending." % nxt)

    print("  current_room is %s." % room)


def main(argv):
    if len(argv) < 2:
        print("usage: read_savestate.py <state.mc0> [map] [--png out.png]")
        return 2

    path = argv[1]
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    mapPath = None
    png = None
    rest = argv[2:]

    while rest:
        if rest[0] == "--png" and len(rest) > 1:
            png = rest[1]
            rest = rest[2:]
        else:
            mapPath = rest[0]
            rest = rest[1:]

    if mapPath is None:
        mapPath = os.path.join(here, "saturn", "BuildDrop",
                               "Heart of the Alien (USA).map")

    blob = inflate(open(path, "rb").read())

    print("state %s  (%d bytes inflated)" % (os.path.basename(path), len(blob)))
    print("  written %s" % time_of(path))
    print("map   %s" % os.path.basename(mapPath))
    print("  built   %s" % time_of(mapPath))
    print("")

    high = None
    low = None
    slaveOn = None

    for section, entry, data in sections(blob):
        if section == "MAIN" and entry == "WorkRAMH":
            high = unswap(data)
        elif section == "MAIN" and entry == "WorkRAML":
            low = unswap(data)
        elif entry == "SlaveSH2On" and len(data) >= 1:
            slaveOn = data[0]

    if high is None:
        print("no MAIN/WorkRAMH in this state; sections seen:")
        for section, entry, data in sections(blob):
            print("  %-12s %-24s %d" % (section, entry, len(data)))
        return 1

    if png:
        write_png(png, blob[32:32 + PREVIEW_W * PREVIEW_H * 3])
        print("screenshot written to %s" % png)
        print("")

    if slaveOn is not None:
        print("slave SH-2 %s" % ("running" if slaveOn else "halted"))
        print("")

    syms = read_map(mapPath)

    if not syms:
        print("the map produced no symbols; is it the linker map?")
        return 1

    report(Ram(high, low), syms)
    return 0


def time_of(path):
    import datetime

    if not os.path.exists(path):
        return "missing"
    stamp = datetime.datetime.fromtimestamp(os.path.getmtime(path))
    return stamp.strftime("%Y-%m-%d %H:%M:%S")


def write_png(path, rgb):
    raw = bytearray()

    for y in range(PREVIEW_H):
        raw.append(0)
        raw += rgb[y * PREVIEW_W * 3:(y + 1) * PREVIEW_W * 3]

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", PREVIEW_W, PREVIEW_H,
                                           8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
