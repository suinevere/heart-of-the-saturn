"""List the Saturn backup-RAM files inside a Mednafen save state.

Usage: python bup.py <state.mc?>

Decodes the BUP directory so a save record can be read without the emulator:
filename, comment, datasize, and for HOTA records the save header's own
flags/room/entry, which is what says whether a record is a full state save or
a 48-byte checkpoint.
"""
import io, gzip, re, struct, sys

SAVE_FLAG_RLE = 0x01
SAVE_FLAG_CHECKPOINT = 0x02


def entries(blob):
    """Yield (offset, name, comment, lang, date, datasize) for each 0x80 tag."""
    for m in re.finditer(rb"BackUpRam Format", blob):
        pass
    i = 0
    while True:
        i = blob.find(b"\x80\x00\x00\x00", i)
        if i < 0:
            return
        name = blob[i + 4:i + 15]
        if not re.match(rb"^[A-Za-z0-9_\-.]{1,11}\x00*$", name):
            i += 4
            continue
        lang = blob[i + 15]
        comment = blob[i + 16:i + 26]
        date = struct.unpack(">I", blob[i + 26:i + 30])[0]
        size = struct.unpack(">I", blob[i + 30:i + 34])[0]
        yield (i, name.rstrip(b"\x00").decode("latin1"),
               comment.rstrip(b"\x00").decode("latin1"), lang, date, size)
        i += 4


def main():
    d = gzip.decompress(io.open(sys.argv[1], "rb").read())
    print("state: %d bytes decompressed" % len(d))

    regions = [m.start() for m in re.finditer(rb"BackUpRam Format", d)]
    print("BackUpRam signatures at: %s%s" % (regions[:4],
                                             " ..." if len(regions) > 4 else ""))
    print()

    found = 0
    for off, name, comment, lang, date, size in entries(d):
        found += 1
        print("  %-12s comment=%-11s lang=%d date=0x%08x datasize=%d"
              % (name, repr(comment), lang, date, size))

        hit = d.find(b"HOTA", off)
        if name.startswith("HOTASAVE") and 0 <= hit - off < 4096:
            h = d[hit:hit + 16]
            ver = struct.unpack(">H", h[4:6])[0]
            flags = h[6]
            entry = h[7]
            stored = struct.unpack(">H", h[8:10])[0]
            room = struct.unpack(">H", h[10:12])[0]
            kind = ("CHECKPOINT" if flags & SAVE_FLAG_CHECKPOINT
                    else "STATE")
            print("      header @%d: ver=%d flags=0x%02x (%s%s) entry=%d "
                  "stored=%d room=%d"
                  % (hit, ver, flags, kind,
                     "+RLE" if flags & SAVE_FLAG_RLE else "",
                     entry, stored, room))
    if not found:
        print("  (no backup-RAM files found)")


main()
