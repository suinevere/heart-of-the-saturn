#!/usr/bin/env python3
import os
import struct
import sys

CMP = ["==", "!=", ">", ">=", "<", "<=", "?6", "?7"]

ALPHABET = "BCDFGHJKLRTX"

MANIFEST = {
    "ROOMS1.BIN": (4501, 370688),
    "ROOMS2.BIN": (1282, 370688),
    "ROOMS3.BIN": (1463, 370688),
    "ROOMS4.BIN": (1644, 370688),
    "ROOMS5.BIN": (1825, 370688),
    "ROOMS6.BIN": (4139, 370688),
    "ROOMS7.BIN": (2006, 160826),
    "ROOMS8.BIN": (4320, 370688),
}

PASSWORD_TABLE_PC = 0x0367
PASSWORD_SWITCH_PC = 0x01B9
PASSWORD_REFUSE_PC = 0x016D


class Script:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        self.sp = struct.unpack_from(">I", self.d, 0)[0]
        self.base = self.sp - 0xF900

    def b(self, pc):
        return self.d[self.base + pc]

    def w(self, pc):
        return struct.unpack_from(">H", self.d, self.base + pc)[0]

    def sw(self, pc):
        return struct.unpack_from(">h", self.d, self.base + pc)[0]


def sext8(v):
    return v - 256 if v > 127 else v


def decode_one(s, pc):
    op = s.b(pc)
    p = pc + 1

    def T(txt, n, succ=(), ft=True):
        return (n + 1, txt, list(succ), ft)

    if op == 0x00: return T("var[%d] = %d" % (s.b(p), s.sw(p + 1)), 3)
    if op == 0x01: return T("var[%d] = var[%d]" % (s.b(p), s.b(p + 1)), 2)
    if op == 0x02: return T("var[%d] += var[%d]" % (s.b(p), s.b(p + 1)), 2)
    if op == 0x03: return T("var[%d] += %d" % (s.b(p), s.sw(p + 1)), 3)
    if op == 0x04: return T("call 0x%04x" % s.w(p), 2, [s.w(p)])
    if op == 0x05: return T("ret", 0, [], False)
    if op == 0x06: return T("yield", 0)
    if op == 0x07: return T("jmp 0x%04x" % s.w(p), 2, [s.w(p)], False)
    if op == 0x08:
        q = p + 4
        d2 = s.b(p + 3)
        if d2 != 0:
            d2 -= 1
            while True:
                d0 = s.b(q)
                q += 1
                if d0 & 0x40:
                    d0 &= ~0x40
                    q += 2 * (d0 + 1)
                elif d0 & 0x80:
                    d0 &= 0x7F
                    q += 2 * (d0 + 1)
                else:
                    q += d0 + 1
                d2 -= d0 + 1
                if d2 < 0:
                    break
        return (q - pc, "newtask %d at 0x%04x" % (s.b(p), s.w(p + 1)), [s.w(p + 1)], True)
    if op == 0x09: return T("dbf var[%d], 0x%04x" % (s.b(p), s.w(p + 1)), 3, [s.w(p + 1)])
    if op == 0x0A:
        c, v = s.b(p), s.b(p + 1)
        if c & 0x80:
            return T("if var[%d] %s var[%d] goto 0x%04x" % (v, CMP[c & 7], s.b(p + 2), s.w(p + 3)), 5, [s.w(p + 3)])
        if c & 0x40:
            return T("if var[%d] %s %d goto 0x%04x" % (v, CMP[c & 7], s.sw(p + 2), s.w(p + 4)), 6, [s.w(p + 4)])
        return T("if var[%d] %s %d goto 0x%04x" % (v, CMP[c & 7], s.b(p + 2), s.w(p + 3)), 5, [s.w(p + 3)])
    if op == 0x0B: return T("setpalette %d" % s.b(p), 1)
    if op == 0x0C: return T("destroy_tasks %d..%d" % (s.b(p), s.b(p + 1) & 0x3F), 2)
    if op == 0x0D: return T("select_screen %d" % s.b(p), 1)
    if op == 0x0E: return T("fill_screen %d col %d" % (s.b(p), s.b(p + 1)), 2)
    if op == 0x0F: return T("copy_screen %d <- %d" % (s.b(p + 1), s.b(p)), 2)
    if op == 0x10: return T("update_screen %d" % s.b(p), 1)
    if op == 0x11: return T("terminate", 0, [], False)
    if op == 0x13: return T("var[%d] -= var[%d]" % (s.b(p), s.b(p + 1)), 2)
    if op == 0x14: return T("var[%d] &= 0x%x" % (s.b(p), s.w(p + 1)), 3)
    if op == 0x15: return T("var[%d] |= 0x%x" % (s.b(p), s.w(p + 1)), 3)
    if op == 0x17: return T("var[%d] >>= %d" % (s.b(p), s.b(p + 1)), 2)
    if op == 0x18: return T("play_sample %d vol %d ch %d" % (s.b(p), s.b(p + 1), s.b(p + 2)), 3)
    if op == 0x19:
        n = s.w(p)
        return T("load_screen %d%s" % (n, "   <== %s" % describe_code(n) if 17000 <= n <= 17100 else ""), 2)
    if op == 0x1A: return T("play_track %d" % s.b(p), 1)
    if op == 0x1C:
        n = s.b(p + 1)
        cases = [(sext8(s.b(p + 2 + 3 * i)), s.w(p + 3 + 3 * i)) for i in range(n)]
        return T("switch8 var[%d]: %s" % (s.b(p), ", ".join("%d->0x%04x" % c for c in cases)),
                 2 + 3 * n, [j for _, j in cases])
    if op == 0x1D:
        n = s.b(p + 1)
        cases = [(s.sw(p + 2 + 4 * i), s.w(p + 4 + 4 * i)) for i in range(n)]
        return T("switch16 var[%d]: %s" % (s.b(p), ", ".join("%d->0x%04x" % c for c in cases)),
                 2 + 4 * n, [j for _, j in cases])
    if op == 0x1E: return T("if var[%d]==0 goto 0x%04x" % (s.b(p), s.w(p + 1)), 3, [s.w(p + 1)])
    if op == 0x1F: return T("if var[%d]!=0 goto 0x%04x" % (s.b(p), s.w(p + 1)), 3, [s.w(p + 1)])
    if op == 0x21: return T("death_animation %d" % (s.b(p) % 32), 1)
    if op == 0x24: return T("op24 sprite var[%d]" % s.b(p), 1)
    if op == 0x25:
        return T("add_sprite z=%d idx=%d frame=%d flip=%d at (%d,%d)"
                 % (s.b(p), s.b(p + 1), s.b(p + 2), s.b(p + 3),
                    s.sw(p + 4), s.sw(p + 6)), 9)
    if op == 0x26: return T("draw_sprites", 0)
    if op == 0x27: return T("op27 var[%d] %d" % (s.b(p), s.b(p + 1)), 2)
    if op == 0x28: return T("collision var[%d]" % s.b(p), 1)
    if op == 0x29: return T("remove_sprite %d" % s.b(p), 1)
    if op == 0x2A: return T("next_frame var[%d]" % s.b(p), 1)
    if op == 0x2B: return T("op2b var[%d] f=0x%x" % (s.b(p), s.b(p + 1)), 3 if s.b(p + 1) & 0x80 else 2)
    if op == 0x2C: return T("move_sprite var[%d] by (%d,%d)" % (s.b(p), sext8(s.b(p + 1)), sext8(s.b(p + 2))), 3)
    if 0x2D <= op <= 0x30: return T("nop", 0)
    if op in (0x31, 0x32, 0x33): return T("flip/mirror var[%d]" % s.b(p), 1)
    if op in (0x34, 0x35): return T("d%d = 0" % (4 if op == 0x34 else 5), 0)
    if op in (0x36, 0x37): return T("d%d = 0x%x" % (4 if op == 0x36 else 5, s.w(p)), 2)
    if op in (0x38, 0x39): return T("d%d += %d" % (4 if op == 0x38 else 5, s.sw(p)), 2)
    if op in (0x3A, 0x3B): return T("d%d &= 0x%x" % (4 if op == 0x3A else 5, s.w(p)), 2)
    if op in (0x3E, 0x3F): return T("d%d <<= %d" % (4 if op == 0x3E else 5, s.b(p)), 1)
    if op in (0x40, 0x41): return T("d%d >>= %d" % (4 if op == 0x40 else 5, s.b(p)), 1)
    if op == 0x42: return T("d4 = d5", 0)
    if op == 0x43: return T("d5 = d4", 0)
    if op in (0x4A, 0x4B): return T("d%d = var[%d]" % (4 if op == 0x4A else 5, s.b(p)), 1)
    if op in (0x4C, 0x4D): return T("var[%d] = d%d" % (s.b(p), 4 if op == 0x4C else 5), 1)
    if op == 0x54: return T("if d4 %s %d goto 0x%04x" % (CMP[s.b(p) & 7], s.sw(p + 1), s.w(p + 3)), 5, [s.w(p + 3)])
    if op == 0x56: return T("var[%d] = 0" % s.b(p), 1)
    if op in (0x5C, 0x5D): return T("var[%d] += d%d" % (s.b(p), 4 if op == 0x5C else 5), 1)
    if op in (0x5E, 0x5F): return T("d%d -= var[%d]" % (4 if op == 0x5E else 5, s.b(p)), 1)
    if op == 0x60: return T("var[%d] -= d4" % s.b(p), 1)
    if op in (0x66, 0x67):
        return T("if d%d %s var[%d] goto 0x%04x" % (4 if op == 0x66 else 5, CMP[s.b(p) & 7], s.b(p + 1), s.w(p + 2)),
                 4, [s.w(p + 2)])
    if op == 0x68:
        n = s.b(p)
        cases = [(s.sw(p + 1 + 4 * i), s.w(p + 3 + 4 * i)) for i in range(n)]
        return T("switch d4: %s" % ", ".join("%d->0x%04x" % c for c in cases), 1 + 4 * n, [j for _, j in cases])
    if op == 0x69: return T("d5 = %d" % sext8(s.b(p)), 1)
    if op in (0x6A, 0x6B): return T("if d%d==0 goto 0x%04x" % (4 if op == 0x6A else 5, s.w(p)), 2, [s.w(p)])
    if op in (0x6C, 0x6D): return T("if d4!=0 goto 0x%04x" % s.w(p), 2, [s.w(p)])
    if op in (0x6E, 0x6F): return T("d%d = %d" % (4 if op == 0x6E else 5, sext8(s.b(p))), 1)
    if op == 0x70: return T("op70 var[%d] 0x%x var[%d]" % (s.b(p), s.b(p + 1), s.b(p + 2)), 3)
    if op in (0x71, 0x72): return T("var[%d]%s" % (s.b(p), "++" if op == 0x71 else "--"), 1)
    if 0x73 <= op <= 0x76: return T("d" + ["4++", "5++", "4--", "5--"][op - 0x73], 0)
    if op in (0x77, 0x78): return T("var[%d] %s= %d" % (s.b(p), "<<" if op == 0x77 else ">>", s.b(p + 1)), 2)
    if op == 0x7D: return T("var[2] = last_frame(var[%d])" % s.b(p), 1)
    if op in (0x7E, 0x7F): return T("shift sprite var[%d] by var[%d]" % (s.b(p), s.b(p + 1)), 2)
    if op in (0x80, 0x81): return T("use_aux %d" % (op - 0x80), 0)
    if op == 0x82: return T("breakpoint %d" % s.b(p), 1)
    if op in (0x83, 0x84): return T("copyvars dst=%d src=%d n=%d" % (s.b(p), s.b(p + 1), s.b(p + 2)), 3)
    if op == 0x87: return T("var[%d] = %d" % (s.b(p), s.sw(p + 1)), 3)
    if op == 0x88: return T("indirect jmp var[%d]+var[%d]*2" % (s.b(p), s.b(p + 1)), 2, [], False)
    if op == 0x89:
        return T("set_sprite var[%d] idx=%d frame=%d flip=%d at (%d,%d)"
                 % (s.b(p), s.b(p + 1), s.b(p + 2), s.b(p + 3),
                    s.sw(p + 4), s.sw(p + 6)), 8)
    if op == 0x8A: return T("var[%d] = byte[0x%04x + var[%d]]" % (s.b(p), s.w(p + 1), s.b(p + 3)), 4)
    if op == 0x8B: return T("setpalette var[%d]" % s.b(p), 1)
    if op == 0x8C: return T("scroll %d" % sext8(s.b(p)), 1)
    return (1, "??? op 0x%02x" % op, [], False)


def describe_code(n):
    ns = (n // 10) % 10 + 1
    if ns == 10:
        return "room 8 entry %d" % (n % 10)
    if ns == 7:
        return "MAKE2MB+MID2 then room 6 entry %d" % (n % 10)
    if ns == 6:
        return "END1..4 then the password room" % ()
    return "room %d entry %d" % (ns, n % 10)


def walk(s, entries, limit=0x20000):
    seen = {}
    todo = list(entries)
    while todo:
        pc = todo.pop()
        while pc not in seen and 0 <= pc < limit:
            try:
                n, txt, succ, ft = decode_one(s, pc)
            except Exception as e:
                seen[pc] = (1, "<decode error %s>" % e)
                break
            seen[pc] = (n, txt)
            todo.extend(x for x in succ if x not in seen)
            if not ft:
                break
            pc += n
    return seen


def read_passwords(s):
    n, txt, succ, ft = decode_one(s, PASSWORD_SWITCH_PC)
    if not txt.startswith("switch16 var[2]"):
        raise SystemExit("ROOMS7 does not switch on var[2] at 0x%04x" % PASSWORD_SWITCH_PC)
    targets = {}
    count = s.b(PASSWORD_SWITCH_PC + 2)
    for i in range(count):
        key = s.sw(PASSWORD_SWITCH_PC + 3 + 4 * i)
        targets[key] = s.w(PASSWORD_SWITCH_PC + 5 + 4 * i)

    out = []
    i = 0
    while True:
        row = [s.b(PASSWORD_TABLE_PC + i * 4 + j) for j in range(4)]
        if row[0] >= len(ALPHABET):
            break
        ordinal = i + 1
        target = targets.get(ordinal, PASSWORD_REFUSE_PC)
        code = None
        if target != PASSWORD_REFUSE_PC:
            if s.b(target) != 0x00 or s.b(target + 1) != 0xE3 or s.b(target + 4) != 0x19:
                raise SystemExit("ordinal %d does not land on a checkpoint stub" % ordinal)
            code = s.w(target + 5)
        out.append((ordinal, "".join(ALPHABET[v] for v in row), code))
        i += 1
    return out


def extract(track, name, out):
    lba, size = MANIFEST[name]
    data = bytearray()
    with open(track, "rb") as f:
        sec = lba
        while len(data) < size:
            f.seek(sec * 2352 + 16)
            data += f.read(2048)
            sec += 1
    path = os.path.join(out, name)
    with open(path, "wb") as f:
        f.write(bytes(data[:size]))
    return path


def main(argv):
    if len(argv) < 2:
        print(__doc__ or "usage: hota_script.py {passwords|dis|extract} ...")
        return 2

    if argv[1] == "extract":
        for name in sorted(MANIFEST):
            print(extract(argv[2], name, argv[3]))
        return 0

    if argv[1] == "passwords":
        s = Script(argv[2])
        for ordinal, word, code in read_passwords(s):
            if code is None:
                print("%2d  %s  refused by the switch" % (ordinal, word))
            else:
                print("%2d  %s  %d  %s" % (ordinal, word, code, describe_code(code)))
        return 0

    if argv[1] == "dis":
        s = Script(argv[2])
        lo = int(argv[3], 0) if len(argv) > 3 else 0
        hi = int(argv[4], 0) if len(argv) > 4 else 0x20000
        seen = walk(s, [0])
        for pc in sorted(seen):
            if lo <= pc < hi:
                n, txt = seen[pc]
                raw = " ".join("%02x" % s.b(pc + i) for i in range(min(n, 8)))
                print("  %04x: %-24s %s" % (pc, raw, txt))
        return 0

    print("unknown command %s" % argv[1])
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
