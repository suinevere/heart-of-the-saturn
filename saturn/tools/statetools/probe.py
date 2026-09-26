"""Read a Mednafen Saturn save state and report Part I's pacer / CD-DA state.

Usage: python probe.py <state.mc?>

Symbol addresses come from the live ELF rather than being hardcoded, because
.bss moves whenever the code size changes.
"""
import io, gzip, struct, subprocess, sys, re

ELF = r"C:/Users/saggl/CLionProjects/Another-Saturn/saturn/BuildDrop/Another World (USA).elf"
MAP = r"C:/Users/saggl/CLionProjects/Another-Saturn/saturn/BuildDrop/Another World (USA).map"

WANT = """g_picMs g_picSet g_leadMs g_prevTrackMs g_totSlices g_anchors g_lagMin
g_lagMax g_cddaCut g_playedMs g_cue g_loop g_wantCue g_liveAudible g_observed
g_fadBase g_fadBaseSet g_stallFrames g_paused g_armPending g_idleFrames
g_startAt g_startGap g_wasPlaying g_pauseFad g_restarts g_loopTail g_askedAt
g_fallbackAt g_fallbackPart lastTimeStamp""".split()


def symbols():
    out = subprocess.run(["nm", ELF], capture_output=True, text=True).stdout
    addr, funcs = {}, []
    for line in out.splitlines():
        m = re.match(r"([0-9a-f]{8}) ([A-Za-z]) (.+)", line)
        if not m:
            continue
        a, kind, name = int(m.group(1), 16), m.group(2), m.group(3)
        base = name
        mm = re.match(r"__ZL\d+(.+)$", name)
        if mm:
            base = mm.group(1)
        base = base.lstrip("_")
        if base in WANT and base not in addr:
            addr[base] = a
        if kind in "Tt" and 0x06000000 <= a <= 0x06100000:
            funcs.append((a, base))
    funcs.sort()
    return addr, funcs


def state(path):
    d = gzip.decompress(io.open(path, "rb").read())
    i = d.find(b"SH2-M")
    size = struct.unpack("<I", d[i + 32:i + 36])[0]
    off, end, f = i + 36, i + 36 + size, {}
    while off < end:
        nl = d[off]; off += 1
        nm = d[off:off + nl].decode("latin1"); off += nl
        sz = struct.unpack("<I", d[off:off + 4])[0]; off += 4
        f[nm] = d[off:off + sz]; off += sz
    j = d.find(b"WorkRAMH")
    hsz = struct.unpack("<I", d[j + 8:j + 12])[0]
    return f, d[j + 12:j + 12 + hsz]


def main():
    path = sys.argv[1]
    addr, funcs = symbols()
    f, hw = state(path)

    def rd(a, n):
        o = a - 0x06000000
        b = hw[o:o + n]
        return bytes(b[k ^ 1] for k in range(len(b)))

    pc = struct.unpack("<I", f["PC"])[0]
    sr, gbr, vbr = struct.unpack("<3I", f["CtrlRegs"])
    mach, macl, pr = struct.unpack("<3I", f["SysRegs"])
    regs = struct.unpack("<16I", f["R"])

    def where(a):
        prev = None
        for fa, fn in funcs:
            if fa <= a:
                prev = (fa, fn)
            else:
                break
        return "%s+0x%x" % (prev[1], a - prev[0]) if prev else "?"

    print("PC  %08x  %s" % (pc, where(pc)))
    print("PR  %08x  %s" % (pr, where(pr)))
    print("SR  %08x  (imask %d)" % (sr, (sr >> 4) & 0xF))
    print("R15 %08x" % regs[15])
    print()
    for k in WANT:
        if k not in addr:
            print("  %-14s  <not in elf>" % k)
            continue
        a = addr[k]
        sv = struct.unpack(">i", rd(a, 4))[0]
        uv = struct.unpack(">I", rd(a, 4))[0]
        byte = hw[(a - 0x06000000) ^ 1]
        print("  %-14s @%08x  %12d  0x%08x  byte=%d" % (k, a, sv, uv, byte))


main()
