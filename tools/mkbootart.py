#!/usr/bin/env python3
# ----------------------
# | mkbootart.py
# | Description: Turns the six Genesis screengrabs in tools/assets/TGA into the
# |   seven .ART files the Saturn boot sequence draws. The menu's two logo
# |   bands are still measured rather than written down here, by diffing the
# |   two menu states and splitting at the largest interior gap.
# |
# |   The sources are ordered screengrabs, not a video capture, and that is the
# |   whole point. The previous version decoded two Kega AVIs and grouped
# |   identical frames into runs; the kgv1 stream held the Virgin logo through
# |   the slot where Interplay belongs and then wrote the title screen over
# |   only part of it, so the extracted title carried the Virgin logo's red
# |   square baked in and the sequence appeared to hold Virgin for 10.2 s.
# |   ffmpeg reported no decode errors either time. Ordered stills cannot fail
# |   that way, and this file no longer depends on ffmpeg at all.
# |
# |   .ART is a pre-packed format of our own -- magic, dimensions, a 4bpp
# |   HighColor palette, then 4bpp packed pixels -- because SRL::Bitmap::TGA
# |   allocates the whole file against a 16 KB heap before decoding it and
# |   hangs the console; see saturn_bootart.cxx. Writing it here means the
# |   Saturn side never runs a decoder, only a disc read and a DMA.
# | Author: suinevere
# | Dependencies: python3
# ----------------------
import os
import struct
import sys

# ----------------------
# | SRC_W / SRC_H / ACTIVE_Y / ACTIVE_H
# | Description: Each screengrab is 640x480, an exact 2x pixel-double of
# |   320x240, which read_tga halves losslessly. The Sega CD's 224-line
# |   display sits centred in those 240 lines, so the active area starts 8
# |   rows down. Every source's ink falls inside rows 24..215, well within it.
# | Author: suinevere
# ----------------------
SRC_W = 320
SRC_H = 240
ACTIVE_Y = 8
ACTIVE_H = 224

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TGA_DIR = os.path.join(REPO, "tools", "assets", "TGA")
OUT_DIR = os.path.join(REPO, "saturn", "cd", "data")

# ----------------------
# | SOURCES
# | Description: The six screengrabs in the order the game shows them. Held by
# |   index, not matched by name, because the names carry only a counter --
# |   so a re-grab must keep this order. The four opening stills each hold
# |   BOOT_STILL_MS; the last two are the menu's two highlight states.
# | Author: suinevere
# ----------------------
SOURCES = [
    "HEART OF THE ALIEN000.tga",   # legal text
    "HEART OF THE ALIEN001.tga",   # Virgin Interactive Entertainment
    "HEART OF THE ALIEN002.tga",   # Interplay
    "HEART OF THE ALIEN003.tga",   # title card
    "HEART OF THE ALIEN004.tga",   # menu, OUT OF THIS WORLD lit
    "HEART OF THE ALIEN005.tga",   # menu, HEART OF THE ALIEN lit
]


# ----------------------
# | read_tga
# | Description: Reads one 640x480 24-bit run-length TGA and returns it halved
# |   to 320x240 rgb24.
# |
# |   Handles image type 10 (RLE true-colour) only, which is what these grabs
# |   are, and refuses anything else rather than producing a plausible wrong
# |   picture. TGA stores BGR and, with bit 5 of the descriptor clear, bottom
# |   row first; both are undone here. The 2:1 reduction takes every second
# |   pixel rather than averaging, which is exact because the source is a
# |   verified pixel-double -- averaging would invent colours off the Mega
# |   Drive's 3-bit channel ladder and cost palette entries later.
# | Author: suinevere
# | Params: path -- TGA file
# | Returns: 320x240 rgb24 bytes
# ----------------------
def read_tga(path):
    data = open(path, "rb").read()
    id_len = data[0]
    image_type = data[2]
    width, height, depth, descriptor = struct.unpack("<HHBB", data[12:18])

    if image_type != 10 or depth != 24:
        sys.exit("%s: image type %d at %d bpp, want type 10 at 24 bpp"
                 % (os.path.basename(path), image_type, depth))
    if (width, height) != (SRC_W * 2, SRC_H * 2):
        sys.exit("%s: %dx%d, want %dx%d" % (os.path.basename(path), width, height,
                                            SRC_W * 2, SRC_H * 2))

    pos = 18 + id_len
    want = width * height * 3
    px = bytearray(want)
    out = 0
    while out < want:
        packet = data[pos]
        pos += 1
        count = (packet & 0x7F) + 1
        if packet & 0x80:
            px[out:out + count * 3] = data[pos:pos + 3] * count
            pos += 3
        else:
            px[out:out + count * 3] = data[pos:pos + count * 3]
            pos += count * 3
        out += count * 3
    if out != want:
        sys.exit("%s: run-length data overruns the image by %d bytes"
                 % (os.path.basename(path), out - want))

    rows = []
    for y in range(0, height, 2):
        row = bytearray()
        for x in range(0, width, 2):
            off = (y * width + x) * 3
            row += bytes((px[off + 2], px[off + 1], px[off]))
        rows.append(bytes(row))
    if not (descriptor >> 5) & 1:
        rows.reverse()
    return b"".join(rows)


# ----------------------
# | active
# | Description: Crops a source to the 320x224 Sega CD active area.
# | Author: suinevere
# | Params: image -- 320x240 rgb24 bytes
# | Returns: 320x224 rgb24 bytes
# ----------------------
def active(image):
    return crop(image, 0, ACTIVE_Y, SRC_W, ACTIVE_H, SRC_W)


# ----------------------
# | crop
# | Description: Extracts a rectangle from an rgb24 image.
# | Author: suinevere
# | Params: image, x, y, w, h -- source and rectangle; src_w -- source width
# | Returns: w*h rgb24 bytes
# ----------------------
def crop(image, x, y, w, h, src_w):
    rows = []
    for row in range(y, y + h):
        off = (row * src_w + x) * 3
        rows.append(image[off:off + w * 3])
    return b"".join(rows)


# ----------------------
# | paste
# | Description: Returns image with a rectangle replaced by the same
# |   rectangle taken from other. Used to synthesise the both-logos-dim menu
# |   background, which no captured frame contains.
# | Author: suinevere
# | Params: image, other -- rgb24 images of width src_w; x, y, w, h -- rect
# | Returns: rgb24 bytes
# ----------------------
def paste(image, other, x, y, w, h, src_w):
    out = bytearray(image)
    for row in range(y, y + h):
        off = (row * src_w + x) * 3
        out[off:off + w * 3] = other[off:off + w * 3]
    return bytes(out)


# ----------------------
# | changed_rows
# | Description: Row indices where two same-sized images differ.
# | Author: suinevere
# | Params: a, b -- rgb24 images; w, h -- their size
# | Returns: Sorted list of row indices
# ----------------------
def changed_rows(a, b, w, h):
    out = []
    for y in range(h):
        off = y * w * 3
        if a[off:off + w * 3] != b[off:off + w * 3]:
            out.append(y)
    return out


# ----------------------
# | split_at_largest_gap
# | Description: Splits changed rows into two bands at the largest interior
# |   gap. A fixed merge tolerance cannot do this job: the measured gaps are
# |   6 and 8 rows, so any tolerance of 8 or more fuses both logos into one
# |   band and makes independent highlighting impossible, and the isolated
# |   changed pixel between them has to land in one band or the other.
# | Author: suinevere
# | Params: rows -- sorted row indices
# | Returns: ((y0, y1), (y2, y3)) inclusive
# ----------------------
def split_at_largest_gap(rows):
    if len(rows) < 2:
        sys.exit("mkbootart: menu states differ on fewer than two rows")
    gaps = [(rows[i + 1] - rows[i], i) for i in range(len(rows) - 1)]
    at = max(gaps)[1]
    return (rows[0], rows[at]), (rows[at + 1], rows[-1])


# ----------------------
# | changed_columns
# | Description: Leftmost and rightmost differing column within a row band.
# | Author: suinevere
# | Params: a, b -- rgb24 images; w -- width; y0, y1 -- inclusive row band
# | Returns: (low, high) column indices
# ----------------------
def changed_columns(a, b, w, y0, y1):
    low = w
    high = -1
    for y in range(y0, y1 + 1):
        for x in range(w):
            off = (y * w + x) * 3
            if a[off:off + 3] != b[off:off + 3]:
                if x < low:
                    low = x
                if x > high:
                    high = x
    return low, high


# ----------------------
# | pad_to
# | Description: Widens an inclusive span until its length is a multiple of
# |   `multiple`, alternating towards the low end first and falling back to
# |   whichever side still has room.
# |
# |   Width must be a multiple of EIGHT, not two. VDP1 stores a sprite's
# |   horizontal size in CMDSIZE as a count of 8-dot units, so it physically
# |   cannot address a 274-wide sprite: it reads 272 and therefore walks 136
# |   bytes per row through data written 137 bytes per row. Every row then
# |   starts one byte early and the picture shears two pixels further right on
# |   each successive line. That is not a subtle artifact -- it turns the lit
# |   menu band into diagonal stripes -- and nothing in SRL or SGL rejects the
# |   size, so it only shows up on screen.
# |
# |   Height needs only to be even, because CMDSIZE counts vertical size in
# |   single lines; two keeps the sprite's centre off a half pixel so the
# |   offsets in saturn_bootart.cxx stay whole numbers.
# | Author: suinevere
# | Params: low, high -- inclusive span; limit -- exclusive upper bound;
# |         multiple -- required divisor of the resulting length
# | Returns: (start, length)
# ----------------------
def pad_to(low, high, limit, multiple):
    while (high - low + 1) % multiple:
        if low > 0:
            low -= 1
        elif high + 1 < limit:
            high += 1
        else:
            sys.exit("mkbootart: cannot pad span %d-%d to a multiple of %d"
                     % (low, high, multiple))
    return low, high - low + 1


# ----------------------
# | high_color
# | Description: Packs an 8-bit RGB triple into SRL's big-endian HighColor
# |   bitfield: bit 15 opaque, bits 14-10 blue, bits 9-5 green, bits 4-0 red.
# | Author: suinevere
# | Params: colour -- 3-byte RGB
# | Returns: 16-bit HighColor value
# ----------------------
def high_color(colour):
    r, g, b = colour[0], colour[1], colour[2]
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


# ----------------------
# | reduce_palette
# | Description: Merges the least-used colours into their nearest surviving
# |   neighbour (by squared RGB distance) until at most max_colors remain.
# |   Repeatedly picking the rarest colour rather than the closest pair is
# |   what keeps this targeted at anti-aliasing fringe shades instead of
# |   eating into colours real fill area depends on.
# | Author: suinevere
# | Params: palette -- list of 3-byte RGB colours; counts -- pixel count per
# |         entry, same length and order; max_colors -- ceiling to reach
# | Returns: (new_palette, remap) where remap[old_index] is the surviving
# |          index in new_palette
# ----------------------
def reduce_palette(palette, counts, max_colors):
    n = len(palette)
    alive = [True] * n
    redirect = list(range(n))

    def find(i):
        while redirect[i] != i:
            i = redirect[i]
        return i

    while sum(alive) > max_colors:
        living = [i for i in range(n) if alive[i]]
        victim = min(living, key=lambda i: counts[i])
        best = None
        best_dist = None
        for j in living:
            if j == victim:
                continue
            dr = palette[victim][0] - palette[j][0]
            dg = palette[victim][1] - palette[j][1]
            db = palette[victim][2] - palette[j][2]
            dist = dr * dr + dg * dg + db * db
            if best_dist is None or dist < best_dist:
                best_dist = dist
                best = j
        alive[victim] = False
        redirect[victim] = best
        counts[best] += counts[victim]

    survivors = [i for i in range(n) if alive[i]]
    new_index = {orig: pos for pos, orig in enumerate(survivors)}
    remap = [new_index[find(i)] for i in range(n)]
    new_palette = [palette[i] for i in survivors]
    return new_palette, remap


# ----------------------
# | write_art
# | Description: Writes one .ART file: magic 0x4241, width, height, palette
# |   count, the palette as big-endian HighColor entries, then the pixels
# |   packed 4bpp two-per-byte high-nibble-first, matching
# |   Tga::DecodePaletted's packing so the same bit layout the SRL path would
# |   have produced reaches VDP1. Colours over max_colors are folded down
# |   with reduce_palette rather than rejected -- BOOTTITL needs this every
# |   run, since the capture always yields 21 colours against a 16-colour
# |   ceiling.
# | Author: suinevere
# | Params: path, pixels, w, h -- output; max_colors -- palette ceiling, 16
# | Returns: Number of palette entries used
# ----------------------
def write_art(path, pixels, w, h, max_colors):
    lookup = {}
    palette = []
    counts = []
    indices = bytearray(w * h)
    for i in range(w * h):
        colour = pixels[i * 3:i * 3 + 3]
        if colour not in lookup:
            lookup[colour] = len(palette)
            palette.append(colour)
            counts.append(0)
        idx = lookup[colour]
        indices[i] = idx
        counts[idx] += 1

    if len(palette) > max_colors:
        palette, remap = reduce_palette(palette, counts, max_colors)
        for i in range(w * h):
            indices[i] = remap[indices[i]]

    header = bytearray()
    header += (0x4241).to_bytes(2, "big")
    header += w.to_bytes(2, "big")
    header += h.to_bytes(2, "big")
    header += len(palette).to_bytes(2, "big")
    for colour in palette:
        header += high_color(colour).to_bytes(2, "big")

    packed = bytearray((w * h) // 2)
    for i in range(0, w * h, 2):
        packed[i // 2] = ((indices[i] & 0x0f) << 4) | (indices[i + 1] & 0x0f)

    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(packed)
    return len(palette)


# ----------------------
# | verify
# | Description: Re-reads every emitted .ART header and fails the build on
# |   anything the Saturn loader would trust blindly: a wrong magic, a
# |   palette count outside 1..16 (CRAM::Palette::Load reads exactly this
# |   many entries), an odd dimension (breaks the 4bpp row packing), or a
# |   file size that does not match the header's own claimed geometry, which
# |   would leave disc_read_file handing boot_art_load a short or overlong
# |   buffer.
# | Author: suinevere
# | Params: expected -- list of (filename, w, h)
# | Returns: N/A
# ----------------------
def verify(expected):
    for name, w, h in expected:
        path = os.path.join(OUT_DIR, name)
        with open(path, "rb") as handle:
            data = handle.read()

        if len(data) < 8:
            sys.exit("%s: %d bytes, too short for a header" % (name, len(data)))

        magic = int.from_bytes(data[0:2], "big")
        width = int.from_bytes(data[2:4], "big")
        height = int.from_bytes(data[4:6], "big")
        count = int.from_bytes(data[6:8], "big")

        if magic != 0x4241:
            sys.exit("%s: magic 0x%04x, want 0x4241" % (name, magic))
        if not (1 <= count <= 16):
            sys.exit("%s: %d palette entries, want 1..16" % (name, count))
        if width % 8:
            sys.exit("%s: width %d is not a multiple of 8; VDP1 counts sprite width in "
                     "8-dot units and would shear this by %d pixels per row"
                     % (name, width, 2 * (width % 8)))
        if height % 2:
            sys.exit("%s: height %d is odd, which puts the sprite centre on a half pixel"
                     % (name, height))
        if (width, height) != (w, h):
            sys.exit("%s: %dx%d, want %dx%d" % (name, width, height, w, h))

        expected_size = 8 + count * 2 + (width * height) // 2
        if len(data) != expected_size:
            sys.exit("%s: %d bytes, want %d (8 + count*2 + w*h/2)" % (name, len(data), expected_size))
        print("  ok  %-13s %3dx%-3d %2d colours" % (name, width, height, count))


# ----------------------
# | main
# | Description: Reads the six screengrabs, writes the seven .ART files and
# |   verifies them.
# |
# |   Every opening still gets its own texture. There is no shared one: the
# |   four screens are legal, Virgin, Interplay and the title card, and the
# |   distinctness check below fails the build if any two of them arrive
# |   identical, which is what a mis-ordered or duplicated re-grab looks like.
# | Author: suinevere
# | Params: N/A
# | Returns: N/A
# ----------------------
def main():
    for name in SOURCES:
        if not os.path.isfile(os.path.join(TGA_DIR, name)):
            sys.exit("mkbootart: %s is missing from %s" % (name, TGA_DIR))
    sources = [active(read_tga(os.path.join(TGA_DIR, name))) for name in SOURCES]
    legal, virgin, interplay, title, state_a, state_b = sources

    for i in range(len(sources)):
        for j in range(i + 1, len(sources)):
            if sources[i] == sources[j]:
                sys.exit("mkbootart: %s and %s are the same picture; the six sources must be "
                         "the four opening stills then the menu's two states, in order"
                         % (SOURCES[i], SOURCES[j]))
    print("sources: %d screengrabs, all distinct" % len(sources))

    rows = changed_rows(state_a, state_b, SRC_W, ACTIVE_H)
    band_a, band_b = split_at_largest_gap(rows)
    rects = []
    for low, high in (band_a, band_b):
        x_low, x_high = changed_columns(state_a, state_b, SRC_W, low, high)
        x, w = pad_to(x_low, x_high, SRC_W, 8)
        y, h = pad_to(low, high, ACTIVE_H, 2)
        rects.append((x, y, w, h))
        print("band: x%d y%d %dx%d" % (x, y, w, h))

    ootw_rect, hota_rect = rects
    background = paste(state_a, state_b, ootw_rect[0], ootw_rect[1],
                       ootw_rect[2], ootw_rect[3], SRC_W)

    os.makedirs(OUT_DIR, exist_ok=True)
    write_art(os.path.join(OUT_DIR, "BOOTLEGL.ART"), legal, SRC_W, ACTIVE_H, 16)
    write_art(os.path.join(OUT_DIR, "BOOTVIRG.ART"), virgin, SRC_W, ACTIVE_H, 16)
    write_art(os.path.join(OUT_DIR, "BOOTPLAY.ART"), interplay, SRC_W, ACTIVE_H, 16)
    write_art(os.path.join(OUT_DIR, "BOOTTITL.ART"), title, SRC_W, ACTIVE_H, 16)
    write_art(os.path.join(OUT_DIR, "MENUBG.ART"), background, SRC_W, ACTIVE_H, 16)
    write_art(os.path.join(OUT_DIR, "MENUOOTW.ART"),
              crop(state_a, ootw_rect[0], ootw_rect[1], ootw_rect[2], ootw_rect[3], SRC_W),
              ootw_rect[2], ootw_rect[3], 16)
    write_art(os.path.join(OUT_DIR, "MENUHOTA.ART"),
              crop(state_b, hota_rect[0], hota_rect[1], hota_rect[2], hota_rect[3], SRC_W),
              hota_rect[2], hota_rect[3], 16)

    print("verify:")
    verify([
        ("BOOTLEGL.ART", SRC_W, ACTIVE_H),
        ("BOOTVIRG.ART", SRC_W, ACTIVE_H),
        ("BOOTPLAY.ART", SRC_W, ACTIVE_H),
        ("BOOTTITL.ART", SRC_W, ACTIVE_H),
        ("MENUBG.ART", SRC_W, ACTIVE_H),
        ("MENUOOTW.ART", ootw_rect[2], ootw_rect[3]),
        ("MENUHOTA.ART", hota_rect[2], hota_rect[3]),
    ])
    print("saturn_bootart offsets: OOTW crop x%d y%d %dx%d, HOTA crop x%d y%d %dx%d"
          % (ootw_rect + hota_rect))


if __name__ == "__main__":
    main()
