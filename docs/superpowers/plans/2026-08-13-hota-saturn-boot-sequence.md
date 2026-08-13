# Heart of The Alien → Sega Saturn — Boot Sequence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot the disc into four still opening screens with music, then a two-entry game-select menu that starts Heart of the Alien and replays the opening when left alone.

**Architecture:** A pure C state machine (`bootmenu.c`) owns all timing and is host-tested; a thin SRL wrapper (`saturn_bootart.cxx`) draws six TGAs as VDP1 sprites with NBG0 switched off; `boot_sequence()` in `main.c` joins them and drives CD-DA through the existing disc seam. The art is produced offline by `tools/mkbootart.py` from the two supplied AVI captures and committed.

**Tech Stack:** C99, C++ (SaturnRingLib / SGL), Python 3 + ffmpeg for the asset tool, host `gcc` for unit tests.

**Design spec:** `docs/superpowers/specs/2026-08-13-hota-saturn-boot-sequence-design.md`

## Global Constraints

- **New C files take `.c`, new C++ files take `.cxx`.** `shared.mk` defines pattern rules for only `%.o : %.c` and `%.o : %.cxx`; a `.cpp` is silently dropped from the link with no error.
- **Every file, function and constant gets a banner comment** in the project's form (`| name`, `| Description:`, `| Author: suinevere`, `| Dependencies:`, `| Globals:`, `| Params:`, `| Returns:`, `N/A` where inapplicable). **No comments inside function bodies.**
- **Author of record is `suinevere`.** Commit messages are **one sentence**, no body, no bullets, no trailers, and never mention Claude, AI or the session.
- **Never run `compile.bat`, `make`, or Mednafen from a tool call.** It opens with `rm -f` on the ISO, so an overlapping run hands the emulator a half-written disc. Syntax-check only; the human builds and runs.
- **Syntax-check** with `-fsyntax-only` using the flags from `make -n src/<file>.o`. The SH-2 compiler is at `SaturnRingLib/Compiler/sh2eb-elf/bin/`, not on PATH.
- **Host tests** build with `-std=c99 -Wall -Wextra -Werror -O1 -g -I../src`. Warnings are errors; unused parameters must be cast to void.
- **CD filenames are 8.3 uppercase.**
- **ffmpeg is not on PATH.** Fallback: `%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-9.0-full_build\bin\ffmpeg.exe`.
- **Music index is 2**, which `discfmt_cue_track_for_music` maps to cue track 3 = `track03.wav` = 2:46.17. Task 2 asserts this.

## Refinements to the spec made during planning

Two numbers in the spec are tightened here, and the spec has been updated to match:

1. **Crop heights are padded to even as well as widths.** Band 1 measured 33 rows, which puts a sprite's centre on a half-pixel. `pad_even` grows an odd span towards the low end, so it becomes `y45–78`, 34 rows. Row 45 is not a differing row, so drawing it from the lit frame is a no-op over the background. Band 1's texture is 4,658 bytes rather than 4,521, and the VDP1 total is 188,958.

3. **The two Virgin holds are not a 3 px shift.** The first draft asserted they were, inferred from ink bounding boxes that share a bottom edge. Measurement disproves it: they differ in 570 pixels across four rows (`y26–29`, `x76–243`) and are identical everywhere else — the first hold carries three rows of white along the top of the logo's frame and the second does not. Both slots draw one texture, cut from the first hold. `BOOT_SCREEN_VIRGIN_LOW` is renamed `BOOT_SCREEN_VIRGIN_2`, and `BOOT_ART_VIRGIN_DROP` is gone.
2. **The TGA flavour is pinned to `ELF_S.TGA`'s**, a file this SRL ships and loads: `imagetype=1`, `cmaporigin=0`, `cmapdepth=24`, `bpp=8`, `descriptor=0x20`. ffmpeg's targa encoder cannot be used — it always writes `cmaplength=256`, which makes `BitmapInfo` choose `Paletted256` and doubles VRAM.

## File Structure

| File | Responsibility |
|---|---|
| `tools/mkbootart.py` | **Create.** AVI → six TGAs. Measures geometry; never hardcodes it. Self-verifies. |
| `saturn/cd/data/BOOTLEGL.TGA` | **Create (generated, committed).** Legal screen, 320×224, 2 colours. |
| `saturn/cd/data/BOOTVIRG.TGA` | **Create (generated, committed).** Virgin logo, 320×224, 8 colours. |
| `saturn/cd/data/BOOTTITL.TGA` | **Create (generated, committed).** Title card, 320×224, 21 colours. |
| `saturn/cd/data/MENUBG.TGA` | **Create (generated, committed).** Menu, both logos dim, 320×224. |
| `saturn/cd/data/MENUOOTW.TGA` | **Create (generated, committed).** Lit *OUT OF THIS WORLD* band, 274×34. |
| `saturn/cd/data/MENUHOTA.TGA` | **Create (generated, committed).** Lit *HEART OF THE ALIEN* band, 300×34. |
| `saturn/src/bootmenu.h` | **Create.** Screen/entry enums, key bits, timing constants, state and frame structs. |
| `saturn/src/bootmenu.c` | **Create.** The whole state machine. No SRL, no stdio, no engine headers. |
| `saturn/tests/test_bootmenu.c` | **Create.** Host unit tests for the above. |
| `saturn/tests/run_tests.sh` | **Modify.** Add the eighth suite. |
| `saturn/src/disc.h` | **Modify.** Declare `disc_set_music_volume`. |
| `saturn/src/system/disc_srl.cxx` | **Modify.** Saturn implementation over `SRL::Sound::Cdda::SetVolume`. |
| `saturn/host/disc_cue.c` | **Modify.** Host no-op so the SDL build links. |
| `saturn/src/system/saturn_bootart.h` | **Create.** Four-call C seam. |
| `saturn/src/system/saturn_bootart.cxx` | **Create.** The only new file including `<srl.hpp>`. |
| `saturn/src/main.c` | **Modify.** `boot_sequence()` and its call between `initialize()` and `run()`. |

---

### Task 1: The asset tool and the six TGAs

**Files:**
- Create: `tools/mkbootart.py`
- Create (generated): `saturn/cd/data/BOOTLEGL.TGA`, `BOOTVIRG.TGA`, `BOOTTITL.TGA`, `MENUBG.TGA`, `MENUOOTW.TGA`, `MENUHOTA.TGA`

**Interfaces:**
- Consumes: `tools/assets/avi/Heart of the Saturn.avi`, `tools/assets/avi/Heart of the Alien - Main Menu.avi`
- Produces: the six TGA files, and the geometry constants Task 5 hardcodes as sprite offsets — `MENUOOTW` at crop `x19,y45,274×34` and `MENUHOTA` at crop `x9,y86,300×34`. If the tool prints different numbers, Task 5's offsets must be recomputed from its output, not from this document.

- [ ] **Step 1: Write the tool**

Create `tools/mkbootart.py`:

```python
#!/usr/bin/env python3
# ----------------------
# | mkbootart.py
# | Description: Turns the two Kega captures in tools/assets/avi into the six
# |   TGAs the Saturn boot sequence draws. Every geometry decision is measured
# |   from the captures rather than written down here: the still screens are
# |   found by grouping consecutive identical frames into runs, and the menu's
# |   two logo bands by diffing its two states and splitting at the largest
# |   interior gap.
# |
# |   Writes TGA directly rather than shelling out to ffmpeg's targa encoder,
# |   which always emits a 256-entry colour map. SRL's BitmapInfo picks its
# |   texture colour mode from the map length, so a 256-entry map on a
# |   16-colour image doubles VRAM and burns a 256-colour CRAM bank. The
# |   output matches ELF_S.TGA, a file SaturnRingLib ships and loads.
# | Author: suinevere
# | Dependencies: python3, ffmpeg
# ----------------------
import os
import shutil
import subprocess
import sys

# ----------------------
# | SRC_W / SRC_H / ACTIVE_Y / ACTIVE_H
# | Description: The capture is 640x480, a 2x pixel-double of 320x240. The
# |   Sega CD's 224-line display sits centred in those 240 lines, so the
# |   active area starts 8 rows down. Every ink bounding box measured falls
# |   inside it.
# | Author: suinevere
# ----------------------
SRC_W = 320
SRC_H = 240
ACTIVE_Y = 8
ACTIVE_H = 224

# ----------------------
# | STILL_MIN_FRAMES
# | Description: A held opening screen runs ~306 frames at 60 fps. Anything
# |   this long is a screen; the black lead-in and the menu tail are single
# |   digits, so the threshold separates them with two orders of margin.
# | Author: suinevere
# ----------------------
STILL_MIN_FRAMES = 100

# ----------------------
# | FFMPEG_FALLBACKS
# | Description: Where ffmpeg lands when installed by winget, which does not
# |   put it on PATH. Checked only after shutil.which fails.
# | Author: suinevere
# ----------------------
FFMPEG_FALLBACKS = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages",
                 "Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe",
                 "ffmpeg-9.0-full_build", "bin", "ffmpeg.exe"),
]

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AVI_DIR = os.path.join(REPO, "tools", "assets", "avi")
OUT_DIR = os.path.join(REPO, "saturn", "cd", "data")
OPENING_AVI = os.path.join(AVI_DIR, "Heart of the Saturn.avi")
MENU_AVI = os.path.join(AVI_DIR, "Heart of the Alien - Main Menu.avi")


# ----------------------
# | find_ffmpeg
# | Description: Locates ffmpeg on PATH or at the winget fallback.
# | Author: suinevere
# | Params: N/A
# | Returns: Absolute path to ffmpeg
# ----------------------
def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    for path in FFMPEG_FALLBACKS:
        if os.path.isfile(path):
            return path
    sys.exit("mkbootart: ffmpeg not found on PATH or at the winget fallback")


# ----------------------
# | decode
# | Description: Decodes an AVI to a list of 320x240 rgb24 frames. The 2:1
# |   reduction from 640x480 is exact -- every colour in the result is a
# |   multiple of 0x21, the Mega Drive's 3-bit channel ladder -- so no
# |   scaling noise reaches the assets.
# | Author: suinevere
# | Params: path -- AVI file
# | Returns: List of bytes objects, one per frame
# ----------------------
def decode(path):
    cmd = [find_ffmpeg(), "-v", "error", "-i", path,
           "-vf", "scale=%d:%d" % (SRC_W, SRC_H),
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    size = SRC_W * SRC_H * 3
    if len(raw) % size != 0:
        sys.exit("mkbootart: %s decoded to a partial frame" % path)
    return [raw[i:i + size] for i in range(0, len(raw), size)]


# ----------------------
# | find_runs
# | Description: Groups consecutive identical frames into runs.
# | Author: suinevere
# | Params: frames -- list of frame bytes
# | Returns: List of (start, end, frame) with inclusive 0-based bounds
# ----------------------
def find_runs(frames):
    out = []
    start = 0
    for i in range(1, len(frames) + 1):
        if i == len(frames) or frames[i] != frames[start]:
            out.append((start, i - 1, frames[start]))
            start = i
    return out


# ----------------------
# | active
# | Description: Crops a captured frame to the 320x224 Sega CD active area.
# | Author: suinevere
# | Params: frame -- 320x240 rgb24 bytes
# | Returns: 320x224 rgb24 bytes
# ----------------------
def active(frame):
    return crop(frame, 0, ACTIVE_Y, SRC_W, ACTIVE_H, SRC_W)


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
# | count_differing
# | Description: Number of pixels at which two same-sized images disagree.
# | Author: suinevere
# | Params: a, b -- rgb24 images; w, h -- their size
# | Returns: Count of differing pixels
# ----------------------
def count_differing(a, b, w, h):
    total = 0
    for i in range(w * h):
        if a[i * 3:i * 3 + 3] != b[i * 3:i * 3 + 3]:
            total += 1
    return total


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
# | pad_even
# | Description: Widens an inclusive span by one if it is odd, preferring to
# |   grow towards the low end and falling back to the high end at the edge.
# |   Both dimensions must be even: SRL's 4bpp TGA packer walks two pixels per
# |   byte and terminates on an exact match against the row end, so an odd
# |   width never terminates, and an odd height puts the sprite's centre on a
# |   half pixel.
# | Author: suinevere
# | Params: low, high -- inclusive span; limit -- exclusive upper bound
# | Returns: (start, length)
# ----------------------
def pad_even(low, high, limit):
    length = high - low + 1
    if length % 2:
        if low > 0:
            low -= 1
        elif high + 1 < limit:
            high += 1
        else:
            sys.exit("mkbootart: cannot pad span %d-%d to an even length" % (low, high))
        length += 1
    return low, length


# ----------------------
# | write_tga
# | Description: Writes an uncompressed colour-mapped TGA in the exact shape
# |   SaturnRingLib's own ELF_S.TGA uses: image type 1, colour map origin 0,
# |   24-bit BGR map entries, 8-bit indices, descriptor 0x20 for a top-left
# |   origin. Never RLE -- Tga::DecodeRlePaletted allocates one byte per pixel
# |   unconditionally and skips the 4bpp packing entirely.
# | Author: suinevere
# | Params: path, pixels, w, h -- output; max_colors -- palette ceiling
# | Returns: Number of palette entries used
# ----------------------
def write_tga(path, pixels, w, h, max_colors):
    lookup = {}
    palette = []
    data = bytearray(w * h)
    for i in range(w * h):
        colour = pixels[i * 3:i * 3 + 3]
        if colour not in lookup:
            if len(palette) >= max_colors:
                sys.exit("mkbootart: %s needs more than %d colours" %
                         (os.path.basename(path), max_colors))
            lookup[colour] = len(palette)
            palette.append(colour)
        data[i] = lookup[colour]

    header = bytearray(18)
    header[1] = 1
    header[2] = 1
    header[5:7] = len(palette).to_bytes(2, "little")
    header[7] = 24
    header[12:14] = w.to_bytes(2, "little")
    header[14:16] = h.to_bytes(2, "little")
    header[16] = 8
    header[17] = 0x20

    cmap = bytearray()
    for colour in palette:
        cmap += bytes((colour[2], colour[1], colour[0]))

    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(cmap)
        handle.write(data)
    return len(palette)


# ----------------------
# | verify
# | Description: Re-reads every emitted file and fails the build on anything
# |   that would load wrong rather than not at all: an RLE type, an odd
# |   dimension, a colour map SRL would round up to a larger texture mode, a
# |   colour map origin that IsFormatValid rejects, or an origin bit that
# |   would draw every sprite flipped.
# |
# |   Two of these cannot currently fire, and are kept deliberately. write_tga
# |   hardcodes the colour map origin to 0 and no image has zero colours, so
# |   the origin check is a regression guard on write_tga's header
# |   construction rather than a test of the captures -- it mirrors SRL's own
# |   IsFormatValid rule (srl_tga.hpp), which is the thing that would actually
# |   reject the file, and that rule is worth stating where it can be seen.
# | Author: suinevere
# | Params: expected -- list of (filename, w, h, max_colors)
# | Returns: N/A
# ----------------------
def verify(expected):
    for name, w, h, max_colors in expected:
        path = os.path.join(OUT_DIR, name)
        with open(path, "rb") as handle:
            header = handle.read(18)
        image_type = header[2]
        cmap_origin = int.from_bytes(header[3:5], "little")
        cmap_length = int.from_bytes(header[5:7], "little")
        cmap_depth = header[7]
        width = int.from_bytes(header[12:14], "little")
        height = int.from_bytes(header[14:16], "little")
        bpp = header[16]
        descriptor = header[17]

        if image_type != 1:
            sys.exit("%s: image type %d, want 1 (uncompressed colour-mapped)" % (name, image_type))
        if header[1] != 1 or cmap_depth != 24 or bpp != 8:
            sys.exit("%s: not an 8bpp 24-bit-map paletted TGA" % name)
        if descriptor != 0x20:
            sys.exit("%s: image descriptor 0x%02x, want 0x20; any other value moves the "
                     "origin or claims attribute bits, and the sprite draws flipped rather "
                     "than failing to load" % (name, descriptor))
        if cmap_origin >= cmap_length:
            sys.exit("%s: colour map origin %d not below length %d, IsFormatValid rejects this"
                     % (name, cmap_origin, cmap_length))
        if width % 2 or height % 2:
            sys.exit("%s: %dx%d has an odd dimension" % (name, width, height))
        if (width, height) != (w, h):
            sys.exit("%s: %dx%d, want %dx%d" % (name, width, height, w, h))
        if cmap_length > max_colors:
            sys.exit("%s: %d colours, want at most %d" % (name, cmap_length, max_colors))
        print("  ok  %-13s %3dx%-3d %2d colours" % (name, width, height, cmap_length))


# ----------------------
# | main
# | Description: Measures both captures, writes the six TGAs and verifies
# |   them.
# |
# |   BOOTVIRG is cut from the FIRST Virgin hold, which carries the complete
# |   white frame. The second hold is the same logo missing the top three rows
# |   of that frame -- 570 pixels, 0.74% -- and both holds draw from this one
# |   texture, so the logo sits unbroken for 10.2 s. The drift check below
# |   fails the build if a re-capture ever makes them genuinely different
# |   screens, which would need a second texture and a second enum value.
# | Author: suinevere
# | Params: N/A
# | Returns: N/A
# ----------------------
def main():
    opening = decode(OPENING_AVI)
    stills = [run for run in find_runs(opening) if run[1] - run[0] + 1 >= STILL_MIN_FRAMES]
    print("opening: %d frames, %d runs, %d held screens"
          % (len(opening), len(find_runs(opening)), len(stills)))
    for start, end, _ in stills:
        print("  frames %4d-%-4d  %5.2f s" % (start + 1, end + 1, (end - start + 1) / 60.0))
    if len(stills) != 4:
        sys.exit("mkbootart: expected 4 held opening screens, found %d" % len(stills))

    legal = active(stills[0][2])
    virgin = active(stills[1][2])
    virgin_second = active(stills[2][2])
    title = active(stills[3][2])

    drift = count_differing(virgin, virgin_second, SRC_W, ACTIVE_H)
    if drift > (SRC_W * ACTIVE_H) // 50:
        sys.exit("mkbootart: the two Virgin holds differ in %d pixels; they are separate "
                 "screens now and each needs its own texture" % drift)
    print("virgin holds differ in %d pixels (%.2f%%), one texture serves both"
          % (drift, 100.0 * drift / (SRC_W * ACTIVE_H)))

    menu = decode(MENU_AVI)
    states = [run for run in find_runs(menu) if run[1] - run[0] + 1 >= 2]
    if len(states) != 2:
        sys.exit("mkbootart: expected 2 menu states, found %d" % len(states))
    state_a = active(states[0][2])
    state_b = active(states[1][2])

    rows = changed_rows(state_a, state_b, SRC_W, ACTIVE_H)
    band_a, band_b = split_at_largest_gap(rows)
    rects = []
    for low, high in (band_a, band_b):
        x_low, x_high = changed_columns(state_a, state_b, SRC_W, low, high)
        x, w = pad_even(x_low, x_high, SRC_W)
        y, h = pad_even(low, high, ACTIVE_H)
        rects.append((x, y, w, h))
        print("band: x%d y%d %dx%d" % (x, y, w, h))

    ootw_rect, hota_rect = rects
    background = paste(state_a, state_b, ootw_rect[0], ootw_rect[1],
                       ootw_rect[2], ootw_rect[3], SRC_W)

    os.makedirs(OUT_DIR, exist_ok=True)
    write_tga(os.path.join(OUT_DIR, "BOOTLEGL.TGA"), legal, SRC_W, ACTIVE_H, 16)
    write_tga(os.path.join(OUT_DIR, "BOOTVIRG.TGA"), virgin, SRC_W, ACTIVE_H, 16)
    write_tga(os.path.join(OUT_DIR, "BOOTTITL.TGA"), title, SRC_W, ACTIVE_H, 64)
    write_tga(os.path.join(OUT_DIR, "MENUBG.TGA"), background, SRC_W, ACTIVE_H, 16)
    write_tga(os.path.join(OUT_DIR, "MENUOOTW.TGA"),
              crop(state_a, ootw_rect[0], ootw_rect[1], ootw_rect[2], ootw_rect[3], SRC_W),
              ootw_rect[2], ootw_rect[3], 16)
    write_tga(os.path.join(OUT_DIR, "MENUHOTA.TGA"),
              crop(state_b, hota_rect[0], hota_rect[1], hota_rect[2], hota_rect[3], SRC_W),
              hota_rect[2], hota_rect[3], 16)

    print("verify:")
    verify([
        ("BOOTLEGL.TGA", SRC_W, ACTIVE_H, 16),
        ("BOOTVIRG.TGA", SRC_W, ACTIVE_H, 16),
        ("BOOTTITL.TGA", SRC_W, ACTIVE_H, 64),
        ("MENUBG.TGA", SRC_W, ACTIVE_H, 16),
        ("MENUOOTW.TGA", ootw_rect[2], ootw_rect[3], 16),
        ("MENUHOTA.TGA", hota_rect[2], hota_rect[3], 16),
    ])
    print("saturn_bootart offsets: OOTW crop x%d y%d %dx%d, HOTA crop x%d y%d %dx%d"
          % (ootw_rect + hota_rect))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

Run: `python tools/mkbootart.py`

Expected output — four held screens near 5.1 s each, a small Virgin drift, two bands, and six `ok` lines:

```
opening: 1237 frames, 6 runs, 4 held screens
  frames    7-312   5.10 s
  frames  313-618   5.10 s
  frames  619-925   5.12 s
  frames  926-1233  5.13 s
virgin holds differ in 570 pixels (0.79%), one texture serves both
band: x19 y45 274x34
band: x9 y86 300x34
verify:
  ok  BOOTLEGL.TGA  320x224   2 colours
  ok  BOOTVIRG.TGA  320x224   8 colours
  ok  BOOTTITL.TGA  320x224  21 colours
  ok  MENUBG.TGA    320x224  15 colours
  ok  MENUOOTW.TGA  274x34   ?? colours
  ok  MENUHOTA.TGA  300x34   ?? colours
saturn_bootart offsets: OOTW crop x19 y45 274x34, HOTA crop x9 y86 300x34
```

**If the band rectangles differ from `x19 y45 274x34` and `x9 y86 300x34`, Task 5's sprite offsets must be recomputed from this output** using `offset_x = crop_x + width/2 - 160` and `offset_y = crop_y + height/2 - 112`.

Band 1's raw changed rows are `y46–78`, an odd 33; `pad_even` grows an odd span towards the low end, so the crop starts at `y45`. That row is identical in both menu states, so including it is a no-op over the background.

- [ ] **Step 3: Prove the TGAs are conformant by round-tripping through an independent decoder**

This is the test for the hand-written TGA writer. ffmpeg is a reference TGA decoder; if it reads our file back to pixels identical to the source, the header layout, the BGR colour map order and the top-left row order are all correct.

Run, with `FF` set to the ffmpeg path:

```bash
FF="$LOCALAPPDATA/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-9.0-full_build/bin/ffmpeg.exe"
"$FF" -v error -y -i "tools/assets/avi/Heart of the Alien - Main Menu.avi" \
      -vf "scale=320:240,crop=320:224:0:8" -frames:v 1 /tmp/ref.png
"$FF" -i saturn/cd/data/MENUOOTW.TGA -i /tmp/ref.png \
      -lavfi "[1:v]crop=274:34:19:45[r];[0:v][r]psnr" -f null - 2>&1 | grep -i psnr
```

Expected: `average:inf` — every pixel identical. A finite PSNR means the writer is wrong; `average` around 10–15 dB with the R and B planes swapped specifically means the colour map was written RGB instead of BGR.

- [ ] **Step 4: Commit**

```bash
git add tools/mkbootart.py saturn/cd/data/BOOTLEGL.TGA saturn/cd/data/BOOTVIRG.TGA \
        saturn/cd/data/BOOTTITL.TGA saturn/cd/data/MENUBG.TGA \
        saturn/cd/data/MENUOOTW.TGA saturn/cd/data/MENUHOTA.TGA
git commit -m "Cut the boot sequence's six TGAs from the two captures, measuring the held screens and the menu's two logo bands rather than hardcoding them, and writing the colour map at its used length so SRL packs the 16-colour images to 4bpp."
```

---

### Task 2: The opening half of the state machine

**Files:**
- Create: `saturn/src/bootmenu.h`, `saturn/src/bootmenu.c`
- Create: `saturn/tests/test_bootmenu.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `boot_screen`, `boot_entry`, `boot_frame`, `bootmenu_state`, the `BOOT_KEY_*` bits, `BOOT_MUSIC_INDEX`, `BOOT_VOLUME_MAX`, `bootmenu_init(bootmenu_state *, uint32_t)`, `bootmenu_step(bootmenu_state *, uint32_t, uint32_t, boot_frame *)`. Tasks 3, 5 and 6 all use these exact names.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_bootmenu.c`:

```c
/*----------------------
 | test_bootmenu.c
 | Description: Host unit tests for bootmenu.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically. discfmt.c is linked
 |   in only so the music index can be checked against the cue track it
 |   resolves to.
 | Author: suinevere
 | Dependencies: bootmenu.h, discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "bootmenu.h"
#include "discfmt.h"

static int g_fail = 0;

static const char *screen_name(boot_screen s)
{
    switch (s) {
    case BOOT_SCREEN_LEGAL:      return "LEGAL";
    case BOOT_SCREEN_VIRGIN:     return "VIRGIN";
    case BOOT_SCREEN_VIRGIN_2:   return "VIRGIN_2";
    case BOOT_SCREEN_TITLE:      return "TITLE";
    case BOOT_SCREEN_MENU:       return "MENU";
    default:                     return "?";
    }
}

static void expect_screen(const char *what, boot_screen got, boot_screen want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %s\n  expected = %s\n",
               what, screen_name(got), screen_name(want));
    }
}

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_music_index_resolves_to_track_three(void)
{
    expect_int("BOOT_MUSIC_INDEX maps to cue track 3 (track03.wav, 2:46)",
               discfmt_cue_track_for_music(BOOT_MUSIC_INDEX), 3);
}

static void test_screen_boundaries(void)
{
    bootmenu_state st;
    boot_frame f;
    bootmenu_init(&st, 1000u);

    bootmenu_step(&st, 1000u, 0u, &f);
    expect_screen("t=0 is LEGAL", f.screen, BOOT_SCREEN_LEGAL);
    expect_int("the first step starts the music", f.music_restart, 1);

    bootmenu_step(&st, 1000u + 5099u, 0u, &f);
    expect_screen("t=5099 is still LEGAL", f.screen, BOOT_SCREEN_LEGAL);
    bootmenu_step(&st, 1000u + 5100u, 0u, &f);
    expect_screen("t=5100 is VIRGIN", f.screen, BOOT_SCREEN_VIRGIN);
    bootmenu_step(&st, 1000u + 10200u, 0u, &f);
    expect_screen("t=10200 is VIRGIN_2", f.screen, BOOT_SCREEN_VIRGIN_2);
    bootmenu_step(&st, 1000u + 15300u, 0u, &f);
    expect_screen("t=15300 is TITLE", f.screen, BOOT_SCREEN_TITLE);
    bootmenu_step(&st, 1000u + 20399u, 0u, &f);
    expect_screen("t=20399 is still TITLE", f.screen, BOOT_SCREEN_TITLE);
    bootmenu_step(&st, 1000u + 20400u, 0u, &f);
    expect_screen("t=20400 is MENU", f.screen, BOOT_SCREEN_MENU);

    expect_int("music is not restarted by reaching the menu", f.music_restart, 0);
    expect_int("volume is full through the opening", (int)f.music_volume,
               (int)BOOT_VOLUME_MAX);
}

static void test_skip_from_each_still(void)
{
    static const uint32_t AT[] = { 0u, 5200u, 10300u, 15400u };
    unsigned i;

    for (i = 0; i < sizeof(AT) / sizeof(AT[0]); i++) {
        bootmenu_state st;
        boot_frame f;
        bootmenu_init(&st, 0u);
        bootmenu_step(&st, 0u, 0u, &f);
        bootmenu_step(&st, AT[i], BOOT_KEY_A, &f);
        expect_screen("a button skips to the menu", f.screen, BOOT_SCREEN_MENU);
        expect_int("skipping does not restart the music", f.music_restart, 0);
        expect_int("skipping does not start the game", f.start_game, 0);
    }
}

/* Mirrors the edge computation boot_sequence() performs in main.c. bootmenu_step
   is handed edges rather than levels, so a test that passes 0 for a held button
   is asserting the caller's arithmetic by hand and proving nothing; this drives
   the same formula the caller uses so that "held" really means held. */
static uint32_t held_edges(uint32_t current, uint32_t *previous)
{
    uint32_t pressed = current & ~*previous;
    *previous = current;
    return pressed;
}

static void test_held_button_skips_once(void)
{
    bootmenu_state st;
    boot_frame f;
    uint32_t previous = 0u;
    int frame;

    bootmenu_init(&st, 0u);
    bootmenu_step(&st, 0u, held_edges(0u, &previous), &f);

    bootmenu_step(&st, 100u, held_edges(BOOT_KEY_C, &previous), &f);
    expect_screen("the press edge skips", f.screen, BOOT_SCREEN_MENU);

    for (frame = 0; frame < 5; frame++)
    {
        bootmenu_step(&st, (uint32_t)(200 + frame * 100),
                      held_edges(BOOT_KEY_C, &previous), &f);
        expect_screen("holding it yields no further edge", f.screen, BOOT_SCREEN_MENU);
        expect_int("and never starts the game", f.start_game, 0);
    }
}

static void test_move_and_confirm_on_one_frame(void)
{
    bootmenu_state st;
    boot_frame f;

    bootmenu_init(&st, 0u);
    bootmenu_step(&st, 0u, 0u, &f);
    bootmenu_step(&st, BOOT_OPENING_MS, 0u, &f);

    bootmenu_step(&st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN | BOOT_KEY_A, &f);
    expect_int("a same-frame move and confirm still moves the cursor",
               (int)f.highlight, (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
    expect_int("but confirms the entry that was lit before the move",
               f.start_game, 0);
}

int main(void)
{
    test_music_index_resolves_to_track_three();
    test_screen_boundaries();
    test_skip_from_each_still();
    test_held_button_skips_once();
    test_move_and_confirm_on_one_frame();

    if (g_fail != 0) {
        printf("%d bootmenu check(s) failed\n", g_fail);
        return 1;
    }

    printf("bootmenu: all checks passed\n");
    return 0;
}
```

Add to `saturn/tests/run_tests.sh`, after the `run_tests_fadecalc` block:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_bootmenu test_bootmenu.c ../src/bootmenu.c ../src/discfmt.c
./run_tests_bootmenu
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `fatal error: bootmenu.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/bootmenu.h`:

```c
/*----------------------
 | bootmenu.h
 | Description: The boot sequence's state machine: which screen is showing,
 |   which menu entry is lit, what the CD-DA volume should be, and when the
 |   game takes over. Pure arithmetic over an elapsed millisecond count and an
 |   edge-triggered key mask.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason cdda_classify.h and discfmt.h are: compiled into the engine and by
 |   saturn/tests/run_tests.sh with the host gcc. Nothing here draws anything
 |   or touches the disc; saturn_bootart.h and disc.h do that, driven by what
 |   this returns.
 |
 |   Design: docs/superpowers/specs/2026-08-13-hota-saturn-boot-sequence-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef BOOTMENU_H
#define BOOTMENU_H

#include <stdint.h>

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | boot_screen
 | Description: What should be on screen this frame.
 |
 |   The capture holds the Virgin logo twice, 5.1 s each, and the two holds
 |   differ only in the top three rows of the logo's white frame -- 570 pixels
 |   of 71,680. Both slots therefore draw the same texture and the logo sits
 |   unbroken for 10.2 s. The pair is kept rather than collapsed so the four
 |   stills stay uniform and the screen index remains phase_ms / BOOT_STILL_MS.
 | Author: suinevere
 ----------------------*/
typedef enum
{
    BOOT_SCREEN_LEGAL    = 0,
    BOOT_SCREEN_VIRGIN   = 1,
    BOOT_SCREEN_VIRGIN_2 = 2,
    BOOT_SCREEN_TITLE    = 3,
    BOOT_SCREEN_MENU     = 4
} boot_screen;

/*----------------------
 | boot_entry
 | Description: Which menu entry is lit. OUT OF THIS WORLD can be highlighted
 |   but never confirmed -- this engine carries no Part I data, and the entry
 |   exists because the original's menu does.
 | Author: suinevere
 ----------------------*/
typedef enum
{
    BOOT_ENTRY_OUT_OF_THIS_WORLD  = 0,
    BOOT_ENTRY_HEART_OF_THE_ALIEN = 1
} boot_entry;

/*----------------------
 | BOOT_KEY_*
 | Description: Bits in the pressed mask, one per key input.h exports. The
 |   caller passes edges, not levels, so a button held from a previous screen
 |   cannot skip twice or move the cursor every frame.
 | Author: suinevere
 ----------------------*/
#define BOOT_KEY_UP      0x01u
#define BOOT_KEY_DOWN    0x02u
#define BOOT_KEY_LEFT    0x04u
#define BOOT_KEY_RIGHT   0x08u
#define BOOT_KEY_A       0x10u
#define BOOT_KEY_B       0x20u
#define BOOT_KEY_C       0x40u
#define BOOT_KEY_SELECT  0x80u
#define BOOT_KEY_MOVE    (BOOT_KEY_UP | BOOT_KEY_DOWN)
#define BOOT_KEY_CONFIRM (BOOT_KEY_A | BOOT_KEY_B | BOOT_KEY_C)

/*----------------------
 | BOOT_STILL_MS / BOOT_OPENING_MS
 | Description: How long each opening still holds, and the four together. The
 |   capture measures 5100/5100/5117/5133 ms; the variation is frame
 |   quantisation at 60 fps rather than intent, so all four are 5100 here.
 | Author: suinevere
 ----------------------*/
#define BOOT_STILL_MS    5100u
#define BOOT_OPENING_MS  (BOOT_STILL_MS * 4u)

/*----------------------
 | BOOT_MENU_IDLE_MS / BOOT_FADE_MS / BOOT_MUSIC_CAP_MS
 | Description: How long the menu waits before replaying the opening, how long
 |   the music takes to fade before that replay, and the hard ceiling on how
 |   much of the track ever plays.
 |
 |   The cap is mostly emergent -- 20400 of stills plus 19000 of menu is 39400,
 |   so the fade lands just inside it -- and exists so a later timing change
 |   cannot let a 2:46 track run on into its second minute.
 | Author: suinevere
 ----------------------*/
#define BOOT_MENU_IDLE_MS  19000u
#define BOOT_FADE_MS        1000u
#define BOOT_MUSIC_CAP_MS  40000u

/*----------------------
 | BOOT_VOLUME_MAX
 | Description: Full CD-DA volume. SND_SetCdDaLev takes 0..7, so a fade has
 |   eight steps and is a staircase rather than a ramp; lengthening it adds no
 |   resolution, only time to notice each step.
 | Author: suinevere
 ----------------------*/
#define BOOT_VOLUME_MAX 7u

/*----------------------
 | BOOT_MUSIC_INDEX
 | Description: The engine music index the boot sequence plays.
 |   discfmt_cue_track_for_music maps it to cue track 3, which is track03.wav
 |   at 2:46.17 -- the disc numbers its 41 audio tracks 02..42, so this is the
 |   second audio track despite being disc track three. test_bootmenu.c
 |   asserts the mapping, because the wrong index is inaudible as a bug and
 |   merely sounds like different music.
 | Author: suinevere
 ----------------------*/
#define BOOT_MUSIC_INDEX 2

/*----------------------
 | bootmenu_state
 | Description: Everything the sequence remembers between frames. Opaque to
 |   callers by convention; only bootmenu.c reads the fields.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    uint32_t phase_start_ms;
    uint32_t music_start_ms;
    uint32_t idle_start_ms;
    int      in_menu;
    int      highlight;
    int      music_started;
} bootmenu_state;

/*----------------------
 | boot_frame
 | Description: What the caller should do this frame.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    boot_screen screen;
    boot_entry  highlight;
    uint8_t     music_volume;
    int         music_restart;
    int         start_game;
} boot_frame;

/*----------------------
 | bootmenu_init
 | Description: Starts the sequence at the first opening still, with the
 |   cursor on OUT OF THIS WORLD to match the capture's first menu frame.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise; now_ms -- the clock's current reading,
 |         which need not be zero
 | Returns: N/A
 ----------------------*/
void bootmenu_init(bootmenu_state *st, uint32_t now_ms);

/*----------------------
 | bootmenu_step
 | Description: Advances the sequence one frame and reports what to draw and
 |   play. All arithmetic is on unsigned differences, so it is correct across
 |   the millisecond counter's wrap.
 |
 |   A frame carrying both a move and a confirm confirms the entry that was
 |   lit BEFORE the move. The pad is sampled once per frame, so pressing Down
 |   and A together arrives as one mask; resolving the confirm against the
 |   post-move highlight would let a single simultaneous press start a game the
 |   player never saw selected.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; now_ms -- clock reading; pressed -- edge-triggered
 |         BOOT_KEY_* mask; out -- filled in with this frame's decisions
 | Returns: N/A
 ----------------------*/
void bootmenu_step(bootmenu_state *st, uint32_t now_ms, uint32_t pressed,
                   boot_frame *out);

#ifdef __cplusplus
}
#endif

#endif /* BOOTMENU_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/bootmenu.c`:

```c
/*----------------------
 | bootmenu.c
 | Description: The boot sequence's state machine. See bootmenu.h for the
 |   contract and the design spec for why the timings are what they are.
 | Author: suinevere
 | Dependencies: bootmenu.h
 | Globals: N/A
 ----------------------*/
#include "bootmenu.h"

/*----------------------
 | boot_ramp
 | Description: CD-DA volume approaching a deadline: full until the fade
 |   window opens, then stepping down to silence as the deadline arrives.
 |   Integer division means it reaches 0 shortly before the deadline rather
 |   than exactly on it, which is the right direction to err -- the track is
 |   already inaudible when the screen changes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: elapsed -- ms since the thing being timed started; deadline -- ms
 |         at which it ends
 | Returns: 0..BOOT_VOLUME_MAX
 ----------------------*/
static uint8_t boot_ramp(uint32_t elapsed, uint32_t deadline)
{
    uint32_t remaining;

    if (elapsed >= deadline)
    {
        return 0u;
    }

    remaining = deadline - elapsed;

    if (remaining >= BOOT_FADE_MS)
    {
        return (uint8_t)BOOT_VOLUME_MAX;
    }

    return (uint8_t)((remaining * BOOT_VOLUME_MAX) / BOOT_FADE_MS);
}

void bootmenu_init(bootmenu_state *st, uint32_t now_ms)
{
    st->phase_start_ms = now_ms;
    st->music_start_ms = now_ms;
    st->idle_start_ms = now_ms;
    st->in_menu = 0;
    st->highlight = (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
    st->music_started = 0;
}

void bootmenu_step(bootmenu_state *st, uint32_t now_ms, uint32_t pressed,
                   boot_frame *out)
{
    uint32_t phase_ms;
    uint32_t music_ms;
    uint8_t volume;
    uint8_t capped;

    out->music_restart = 0;
    out->start_game = 0;

    if (!st->music_started)
    {
        st->music_started = 1;
        out->music_restart = 1;
    }

    if (!st->in_menu)
    {
        phase_ms = now_ms - st->phase_start_ms;

        if (pressed != 0u || phase_ms >= BOOT_OPENING_MS)
        {
            st->in_menu = 1;
            st->idle_start_ms = now_ms;
        }
    }
    else if (pressed != 0u)
    {
        int highlight_before_move = st->highlight;

        st->idle_start_ms = now_ms;

        if ((pressed & BOOT_KEY_MOVE) != 0u)
        {
            st->highlight = (st->highlight == (int)BOOT_ENTRY_OUT_OF_THIS_WORLD)
                          ? (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
                          : (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
        }

        if ((pressed & BOOT_KEY_CONFIRM) != 0u
            && highlight_before_move == (int)BOOT_ENTRY_HEART_OF_THE_ALIEN)
        {
            out->start_game = 1;
        }
    }

    if (st->in_menu && now_ms - st->idle_start_ms >= BOOT_MENU_IDLE_MS)
    {
        st->in_menu = 0;
        st->phase_start_ms = now_ms;
        st->music_start_ms = now_ms;
        out->music_restart = 1;
    }

    phase_ms = now_ms - st->phase_start_ms;
    music_ms = now_ms - st->music_start_ms;

    if (st->in_menu)
    {
        out->screen = BOOT_SCREEN_MENU;
        volume = boot_ramp(now_ms - st->idle_start_ms, BOOT_MENU_IDLE_MS);
    }
    else
    {
        out->screen = (boot_screen)(phase_ms / BOOT_STILL_MS);
        volume = (uint8_t)BOOT_VOLUME_MAX;
    }

    capped = boot_ramp(music_ms, BOOT_MUSIC_CAP_MS);

    if (capped < volume)
    {
        volume = capped;
    }

    out->highlight = (boot_entry)st->highlight;
    out->music_volume = volume;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: all eight suites pass, ending `bootmenu: all checks passed`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/bootmenu.h saturn/src/bootmenu.c saturn/tests/test_bootmenu.c saturn/tests/run_tests.sh
git commit -m "Add the boot sequence's state machine and its host tests, covering the four opening stills, the edge-triggered skip and the assertion that the music index resolves to the 2:46 track."
```

---

### Task 3: The menu half of the state machine

**Files:**
- Modify: `saturn/tests/test_bootmenu.c`
- Modify: `saturn/src/bootmenu.c` (only if a test fails)

**Interfaces:**
- Consumes: everything Task 2 produced.
- Produces: no new symbols. This task proves the menu, attract and fade behaviour Task 2's implementation already contains but does not yet test.

- [ ] **Step 1: Write the failing tests**

Add to `saturn/tests/test_bootmenu.c`, before `main`:

```c
static bootmenu_state g_menu_st;

static void at_menu(void)
{
    boot_frame f;
    bootmenu_init(&g_menu_st, 0u);
    bootmenu_step(&g_menu_st, 0u, 0u, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS, 0u, &f);
}

static void test_cursor_starts_on_part_one(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS, 0u, &f);
    expect_int("cursor starts on OUT OF THIS WORLD",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
}

static void test_cursor_toggles(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN, &f);
    expect_int("Down moves to HEART OF THE ALIEN",
               (int)f.highlight, (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 200u, BOOT_KEY_UP, &f);
    expect_int("Up moves back to OUT OF THIS WORLD",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
}

static void test_part_one_cannot_be_confirmed(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_A, &f);
    expect_int("confirming OUT OF THIS WORLD does not start the game",
               f.start_game, 0);
    expect_int("and leaves the cursor where it was",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
    expect_screen("and stays on the menu", f.screen, BOOT_SCREEN_MENU);
}

static void test_part_two_starts_the_game(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 200u, BOOT_KEY_B, &f);
    expect_int("confirming HEART OF THE ALIEN starts the game", f.start_game, 1);
}

static void test_idle_timer_resets_on_ignored_input(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18000u, BOOT_KEY_A, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18000u + 18999u, 0u, &f);
    expect_screen("an ignored confirm still resets the idle timer",
                  f.screen, BOOT_SCREEN_MENU);
    expect_int("and no replay has happened", f.music_restart, 0);
}

static void test_fade_and_attract(void)
{
    boot_frame f;
    at_menu();

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18000u, 0u, &f);
    expect_int("volume is full on the last frame before the fade window opens",
               (int)f.music_volume, (int)BOOT_VOLUME_MAX);

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18500u, 0u, &f);
    if (f.music_volume == 0u || f.music_volume >= BOOT_VOLUME_MAX) {
        g_fail++;
        printf("FAIL mid-fade volume is between 0 and 7\n  actual = %d\n",
               (int)f.music_volume);
    }

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18999u, 0u, &f);
    expect_int("volume is silent as the replay arrives", (int)f.music_volume, 0);

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 19000u, 0u, &f);
    expect_screen("the attract loop returns to LEGAL", f.screen, BOOT_SCREEN_LEGAL);
    expect_int("and restarts the music", f.music_restart, 1);
    expect_int("at full volume", (int)f.music_volume, (int)BOOT_VOLUME_MAX);
}

static void test_music_cap(void)
{
    boot_frame f;

    at_menu();

    bootmenu_step(&g_menu_st, BOOT_MUSIC_CAP_MS, BOOT_KEY_SELECT, &f);
    expect_int("the 40 s cap silences the track even though the idle timer just reset",
               (int)f.music_volume, 0);
    expect_screen("without ending the menu", f.screen, BOOT_SCREEN_MENU);
    expect_int("or starting the game", f.start_game, 0);
}
```

Register them in `main`, before the failure check:

```c
    test_cursor_starts_on_part_one();
    test_cursor_toggles();
    test_part_one_cannot_be_confirmed();
    test_part_two_starts_the_game();
    test_idle_timer_resets_on_ignored_input();
    test_fade_and_attract();
    test_music_cap();
```

- [ ] **Step 2: Run the tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS. Task 2's implementation already covers this behaviour; these tests exist to hold it still.

If any fail, fix `saturn/src/bootmenu.c` — do not weaken the test to match the code.

- [ ] **Step 3: Commit**

```bash
git add saturn/tests/test_bootmenu.c
git commit -m "Test the boot menu's cursor, its refusal to start Part I, the idle timer's reset on ignored input, the eight-step fade into the attract replay, and the hard forty-second music cap."
```

---

### Task 4: CD-DA volume on the disc seam

**Files:**
- Modify: `saturn/src/disc.h`
- Modify: `saturn/src/system/disc_srl.cxx`
- Modify: `saturn/host/disc_cue.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void disc_set_music_volume(uint8_t level);` — Task 6 calls it every frame.

- [ ] **Step 1: Declare it on the seam**

In `saturn/src/disc.h`, immediately after the `disc_play_track` / `disc_stop_track` declarations:

```c
/*----------------------
 | disc_set_music_volume
 | Description: Sets CD-DA output level, 0 for silence up to 7 for full.
 |   Exists so the boot sequence can fade the title music out before the
 |   attract loop replays the opening, which is the only fade the game has --
 |   the engine's own music starts and stops at full level.
 |
 |   Eight steps is the hardware's whole range, not a simplification:
 |   SND_SetCdDaLev takes 0..7, so any fade built on this is a staircase.
 |   Levels above 7 are clamped rather than refused. Safe to call at any time,
 |   including before disc_open and with nothing playing, and the host backend
 |   ignores it entirely -- SDL_mixer owns its own volume and the boot
 |   sequence does not run there.
 | Author: suinevere
 ----------------------*/
void disc_set_music_volume(uint8_t level);
```

**`disc.h` currently has no `#include` at all**, and is self-contained: every declaration in it uses only `int`, `char` and `void`, so it parses standalone under `-Werror` today. Introducing a `uint8_t` parameter ends that unless the header brings its own type. Add, above the `extern "C"` block:

```c
#include <stdint.h>
```

This matches `cdda_classify.h` and `bootmenu.h`, which include it for the same reason. Do not rely on callers having pulled in `<stdint.h>` first — that would make the header compile only by accident of include order.

- [ ] **Step 2: Implement it on Saturn**

In `saturn/src/system/disc_srl.cxx`, after `disc_stop_track`:

```cpp
/*----------------------
 | disc_set_music_volume
 | Description: Forwards to the SCSP's CD-DA level register through SRL.
 |   Clamped rather than asserted: a caller computing a fade has no business
 |   knowing the hardware's ceiling, and a level above it is a rounding
 |   mistake rather than a bug worth a panic.
 | Author: suinevere
 | Dependencies: srl_sound.hpp
 | Globals: N/A
 | Params: level -- 0 (silent) to 7 (full)
 | Returns: N/A
 ----------------------*/
void disc_set_music_volume(uint8_t level)
{
	if (level > 7)
	{
		level = 7;
	}

	SRL::Sound::Cdda::SetVolume(level);
}
```

- [ ] **Step 3: Implement the host no-op**

In `saturn/host/disc_cue.c`, beside `disc_stop_track`:

```c
/*----------------------
 | disc_set_music_volume
 | Description: Ignored on the host. SDL_mixer owns music volume here, and the
 |   boot sequence this exists for is Saturn-only. Present so the seam has one
 |   shape on both backends rather than a caller-side ifdef.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: level -- ignored
 | Returns: N/A
 ----------------------*/
void disc_set_music_volume(uint8_t level)
{
    (void)level;
}
```

- [ ] **Step 4: Verify both backends still compile**

First, prove `disc.h` is still self-contained. **No host test includes `disc.h`**, so running the suite proves nothing about it — this check is what does:

Run: `gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only -x c saturn/src/disc.h`
Expected: no output. Any error means the header now depends on something a caller happened to include first, which is the failure mode `#include <stdint.h>` exists to prevent.

Then run the suite as a regression check on everything else:

Run: `sh saturn/tests/run_tests.sh`
Expected: all 8 suites pass. This does not exercise `disc.h`; it confirms this task broke nothing that was already covered.

Then syntax-check the Saturn backend. First get the flags:

Run: `cd saturn && make -n src/system/disc_srl.o`

Take the compiler invocation it prints, replace `-c -o ...` with `-fsyntax-only`, and run it.
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/disc.h saturn/src/system/disc_srl.cxx saturn/host/disc_cue.c
git commit -m "Add a CD-DA level control to the disc seam so the boot sequence can fade its music, clamping to the SCSP's eight steps and ignoring the call on the host where SDL_mixer owns volume."
```

---

### Task 5: The VDP1 side

**Files:**
- Create: `saturn/src/system/saturn_bootart.h`
- Create: `saturn/src/system/saturn_bootart.cxx`

**Interfaces:**
- Consumes: the six TGAs from Task 1, and `boot_screen` / `boot_entry` values from Task 2 as plain ints.
- Produces: `int boot_art_load(void);`, `void boot_art_draw(int screen, int highlight);`, `void boot_art_present(void);`, `void boot_art_release(void);` — Task 6 calls all four.

- [ ] **Step 1: Write the header**

Create `saturn/src/system/saturn_bootart.h`:

```c
/*----------------------
 | saturn_bootart.h
 | Description: The C face of the boot sequence's artwork. saturn_bootart.cxx
 |   implements it against SRL's TGA loader and VDP1, so callers never include
 |   <srl.hpp> -- the engine's headers wrap SGL's C headers in extern "C" and
 |   mixing that with SRL's C++ headers in one translation unit is fragile.
 |   This is the same seam saturn_platform.h draws for video, input and time.
 |
 |   The boot art owns the screen while it is up: NBG0 is switched off by
 |   boot_art_load and back on by boot_art_release, so the engine's bitmap
 |   layer cannot sit in front of the VDP1 sprites the screens are drawn as.
 |   Present nothing through video_present between the two.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SATURN_BOOTART_H
#define SATURN_BOOTART_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | boot_art_load
 | Description: Loads all six TGAs into VDP1 textures and hides NBG0. Loads
 |   everything up front rather than on demand because VDP1's texture
 |   allocator is a bump allocator with no free: an attract loop that
 |   reloaded on each replay would exhaust 512 KB of sprite VRAM in a few
 |   passes.
 | Author: suinevere
 | Params: N/A
 | Returns: 1 if every texture is resident, 0 if any file or allocation
 |          failed -- in which case NBG0 is left alone and boot_art_release
 |          need not be called
 ----------------------*/
int boot_art_load(void);

/*----------------------
 | boot_art_draw
 | Description: Queues this frame's sprites. Draws one for an opening still,
 |   two for the menu: the both-dim background and the lit logo band over it.
 | Author: suinevere
 | Params: screen -- a boot_screen value; highlight -- a boot_entry value,
 |         read only when screen is the menu
 | Returns: N/A
 ----------------------*/
void boot_art_draw(int screen, int highlight);

/*----------------------
 | boot_art_present
 | Description: Puts the queued sprites on screen and waits one vblank.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void boot_art_present(void);

/*----------------------
 | boot_art_release
 | Description: Shows NBG0 again. The textures are deliberately kept -- see
 |   the note in saturn_bootart.cxx. Safe to call without a successful load.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void boot_art_release(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BOOTART_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/system/saturn_bootart.cxx`. **Check the offsets against Task 1 Step 2's printed rectangles before writing them**; the values below are what that step is expected to print.

```cpp
/*----------------------
 | saturn_bootart.cxx
 | Description: The SRL side of the boot sequence's artwork: six TGAs loaded
 |   into VDP1 textures, drawn as sprites, and the NBG0 hand-off around them.
 |
 |   This and saturn_movie are the only things in the port that use VDP1. The
 |   engine rasterizes in software to an NBG0 bitmap (see video_srl.cxx) and
 |   never draws a sprite, so the two never contend -- but a VDP2 layer can sit
 |   in front of a sprite depending on priority, so NBG0 is switched off for
 |   the duration rather than trusting an ordering nobody has verified on
 |   hardware.
 |
 |   The TGAs must be uncompressed and even-sided. SRL packs a 16-colour map to
 |   4bpp only on the non-RLE path, and its packer walks two pixels per byte
 |   against an exact row-end match, which an odd width never reaches.
 |   tools/mkbootart.py enforces both and says why at length.
 | Author: suinevere
 | Dependencies: saturn_bootart.h, SRL (Bitmap::TGA, VDP1, VDP2, Scene2D, Core)
 | Globals: g_texture, g_loaded
 ----------------------*/
#include <srl.hpp>
#include <stdio.h>
#include "saturn_bootart.h"

using namespace SRL::Math::Types;

/*----------------------
 | BOOT_ART_Z_BACK / BOOT_ART_Z_FRONT
 | Description: Sprite sort values. slSetSprite orders far to near, so the
 |   smaller value draws on top -- the lit logo band must be BOOT_ART_Z_FRONT
 |   or the background hides it. The absolute values are arbitrary; only their
 |   order matters, because these are the only sprites drawn.
 |
 |   Integers, not floats, for the reason given on bootArtSprite: every
 |   coordinate in this file reaches Fxp through a function parameter, which
 |   cannot carry a fraction.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_Z_BACK  500
#define BOOT_ART_Z_FRONT 490

/*----------------------
 | BOOT_ART_LEGAL .. BOOT_ART_COUNT
 | Description: Indices into g_texture, matching BOOT_ART_FILES.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_LEGAL   0
#define BOOT_ART_VIRGIN  1
#define BOOT_ART_TITLE   2
#define BOOT_ART_MENUBG  3
#define BOOT_ART_OOTW    4
#define BOOT_ART_HOTA    5
#define BOOT_ART_COUNT   6

/*----------------------
 | BOOT_ART_FILES
 | Description: The six files on the disc, 8.3 and upper case, in g_texture
 |   order.
 | Author: suinevere
 ----------------------*/
static const char *BOOT_ART_FILES[BOOT_ART_COUNT] =
{
    "BOOTLEGL.TGA",
    "BOOTVIRG.TGA",
    "BOOTTITL.TGA",
    "MENUBG.TGA",
    "MENUOOTW.TGA",
    "MENUHOTA.TGA"
};

/*----------------------
 | BOOT_ART_OOTW_X .. BOOT_ART_HOTA_Y
 | Description: Where the lit logo bands sit, as offsets from screen centre.
 |   Derived from tools/mkbootart.py's printed crop rectangles by
 |   offset_x = crop_x + width / 2 - 160 and offset_y = crop_y + height / 2 -
 |   112, against a 320x224 display. OOTW crops at x19 y45 274x34; HOTA at
 |   x9 y86 300x34. Regenerate these if the tool ever prints different
 |   rectangles -- it measures them rather than storing them.
 |
 |   Whole numbers, and they must stay whole. mkbootart.py pads both crop
 |   dimensions to even precisely so these land on integers; a half-pixel
 |   offset would be truncated silently by bootArtSprite's int16_t parameters
 |   rather than refused. If a future band produces a fractional centre, fix
 |   the padding, do not write a fraction here.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_OOTW_X (-4)
#define BOOT_ART_OOTW_Y (-50)
#define BOOT_ART_HOTA_X (-1)
#define BOOT_ART_HOTA_Y (-9)

/*----------------------
 | g_texture
 | Description: The VDP1 textures, or -1 for one that never loaded.
 |
 |   Allocated on the first load and never released, because
 |   VDP1::TryAllocateTexture is a bump allocator with no free: every attract
 |   replay would take another 184 KB of the 512 KB sprite VRAM and the loop
 |   would run it dry in two passes. boot_art_load returns early once they are
 |   resident.
 | Author: suinevere
 ----------------------*/
static int32_t g_texture[BOOT_ART_COUNT] = { -1, -1, -1, -1, -1, -1 };

/*----------------------
 | g_loaded
 | Description: True once every texture is resident, so a second load is a
 |   no-op and a failed one cannot be drawn from.
 | Author: suinevere
 ----------------------*/
static bool g_loaded = false;

/*----------------------
 | bootArtSprite
 | Description: Queues one texture at an offset from screen centre.
 |
 |   Takes int16_t rather than a floating type because SRL's Fxp constructor
 |   for floating-point values is consteval, and a function parameter is never
 |   a constant expression -- a double here does not compile at all. Every
 |   coordinate this file passes is a whole number, so nothing is lost, but
 |   note the consequence: a fractional offset would be truncated at the call
 |   site instead of rejected. See BOOT_ART_OOTW_X.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_texture
 | Params: index -- into g_texture; x, y -- offset from centre; z -- sort value
 | Returns: N/A
 ----------------------*/
static void bootArtSprite(int index, int16_t x, int16_t y, int16_t z)
{
    if (g_texture[index] < 0)
    {
        return;
    }

    SRL::Scene2D::DrawSprite((uint16_t)g_texture[index], Vector3D(x, y, z));
}

/*----------------------
 | BOOT_ART_TGA_USABLE
 | Description: Whether SRL's TGA reader can be used to load these assets. It
 |   cannot, and this stays 0 until the assets are re-cut into a pre-packed
 |   form that loads through the port's own disc layer.
 |
 |   Bitmap::TGA::LoadData allocates the whole file before decoding it
 |   (srl_tga.hpp:687), and the High Work RAM heap is 16,064 bytes --
 |   __heap_start 0x060bc140 to __heap_end 0x060c0000 in the build map.
 |   BOOTLEGL.TGA alone is 71,704. The allocation returns NULL, and
 |   Cd::File::LoadBytes does not check its destination (srl_cd.hpp:399), so
 |   GFS_Load DMAs 36 sectors to address 0. That hangs the SH-2 in the BIOS
 |   exception handler with interrupts masked, which shows as a black screen at
 |   boot. Observed on hardware, not predicted.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_TGA_USABLE 0

/*----------------------
 | bootArtLoadPalette
 | Description: Gives one paletted texture a CRAM bank and fills it.
 |
 |   Every boot screen is paletted, and VDP1::TryLoadTexture refuses a
 |   paletted bitmap outright when no handler is supplied -- it returns -1
 |   rather than choosing a bank itself (srl_vdp1.hpp:246). Without this the
 |   very first file fails, boot_art_load returns 0, and the disc boots
 |   straight into the game with no opening and no menu. That failure compiles
 |   cleanly and no host test can reach it, so it is worth stating plainly
 |   here.
 |
 |   One bank per texture. Paletted16 has 128 banks and the six screens need at
 |   most six, so exhaustion is not a practical concern -- it is still reported
 |   rather than assumed away.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: info -- the bitmap's description, carrying its colour mode and its
 |         colours
 | Returns: The CRAM bank number, or -1 if the bitmap has no palette or no bank
 |          of that size is free
 ----------------------*/
static int16_t bootArtLoadPalette(SRL::Bitmap::BitmapInfo *info)
{
    int32_t bank;

    if (info->Palette == nullptr)
    {
        return -1;
    }

    bank = SRL::CRAM::GetFreeBank(info->ColorMode);

    if (bank < 0)
    {
        return -1;
    }

    SRL::CRAM::SetBankUsedState((uint16_t)bank, info->ColorMode, true);

    SRL::CRAM::Palette destination(info->ColorMode, (uint16_t)bank);
    destination.Load(info->Palette->Colors, (int16_t)info->Palette->Count);

    return (int16_t)bank;
}

/*----------------------
 | boot_art_load
 | Description: Loads all six TGAs into VDP1 textures and hides NBG0.
 |
 |   The GetData() check is not defensive padding. SRL's TGA constructor does
 |   not fail when the file is absent: it calls Debug::Assert, which in a build
 |   without DEBUG expands to an empty function (srl_debug.hpp:278), then
 |   returns a fully constructed object whose width and height were never
 |   assigned -- they are not in its initialiser list (srl_tga.hpp:810). Those
 |   garbage dimensions reach slDMACopy with a null source. GetData() is the
 |   reliable signal, because imageData starts null and only a successful load
 |   sets it.
 |
 |   Textures already loaded are skipped, so a retry after a partial failure
 |   continues rather than bump-allocating a second slot for every screen that
 |   already had one. VDP1's allocator cannot free, so a retry that restarted
 |   from zero would strand the first attempt's slots permanently.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_texture, g_loaded
 | Params: N/A
 | Returns: 1 if every texture is resident, 0 if any file or allocation failed
 ----------------------*/
extern "C" int boot_art_load(void)
{
    int i;

    if (!BOOT_ART_TGA_USABLE)
    {
        return 0;
    }

    if (g_loaded)
    {
        SRL::VDP2::NBG0::ScrollDisable();
        return 1;
    }

    for (i = 0; i < BOOT_ART_COUNT; i++)
    {
        SRL::Bitmap::TGA *tga;

        if (g_texture[i] >= 0)
        {
            continue;
        }

        tga = new SRL::Bitmap::TGA(BOOT_ART_FILES[i]);

        if (tga == nullptr)
        {
            return 0;
        }

        if (tga->GetData() == nullptr)
        {
            printf("boot_art_load: %s is not on the disc\n", BOOT_ART_FILES[i]);
            delete tga;
            return 0;
        }

        g_texture[i] = SRL::VDP1::TryLoadTexture(tga, bootArtLoadPalette);
        delete tga;

        if (g_texture[i] < 0)
        {
            printf("boot_art_load: %s got no texture or palette\n", BOOT_ART_FILES[i]);
            return 0;
        }
    }

    g_loaded = true;
    SRL::VDP2::NBG0::ScrollDisable();
    return 1;
}

/*----------------------
 | boot_art_draw
 | Description: Queues this frame's sprites. One for an opening still, two for
 |   the menu: the both-dim background, then the lit logo band nearer the
 |   viewer. Cases 1 and 2 draw the same texture at the same place on purpose --
 |   the capture holds the Virgin logo twice and the two holds differ by 570
 |   pixels in the top rows of its frame, which is not worth a second texture.
 |   Silently does nothing when no art is resident, so a caller that ignored
 |   boot_art_load's return draws nothing rather than reading a dead texture.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_loaded
 | Params: screen -- a boot_screen value; highlight -- a boot_entry value, read
 |         only for the menu
 | Returns: N/A
 ----------------------*/
extern "C" void boot_art_draw(int screen, int highlight)
{
    if (!g_loaded)
    {
        return;
    }

    switch (screen)
    {
    case 0:
        bootArtSprite(BOOT_ART_LEGAL, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 1:
        bootArtSprite(BOOT_ART_VIRGIN, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 2:
        bootArtSprite(BOOT_ART_VIRGIN, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 3:
        bootArtSprite(BOOT_ART_TITLE, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 4:
        bootArtSprite(BOOT_ART_MENUBG, 0, 0, BOOT_ART_Z_BACK);

        if (highlight == 0)
        {
            bootArtSprite(BOOT_ART_OOTW, BOOT_ART_OOTW_X, BOOT_ART_OOTW_Y,
                          BOOT_ART_Z_FRONT);
        }
        else
        {
            bootArtSprite(BOOT_ART_HOTA, BOOT_ART_HOTA_X, BOOT_ART_HOTA_Y,
                          BOOT_ART_Z_FRONT);
        }
        break;

    default:
        break;
    }
}

/*----------------------
 | boot_art_present
 | Description: Puts the queued sprites on screen and waits one vblank, which
 |   is what paces the whole boot sequence -- bootmenu.c reads elapsed
 |   milliseconds and never counts frames, so this is the only thing keeping
 |   the loop from spinning.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_art_present(void)
{
    SRL::Core::Synchronize();
}

/*----------------------
 | boot_art_release
 | Description: Shows NBG0 again so the engine's own bitmap layer is visible.
 |   The textures are deliberately kept -- see g_texture. Unconditional and
 |   idempotent, so it is safe after a load that failed before ever hiding
 |   NBG0.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_art_release(void)
{
    SRL::VDP2::NBG0::ScrollEnable();
}
```

- [ ] **Step 3: Syntax-check it**

Run: `cd saturn && make -n src/system/saturn_bootart.o`

Take the printed compiler invocation, replace the `-c -o <path>` portion with `-fsyntax-only`, and run it.
Expected: no output. If `printf` is undeclared, add `#include <stdio.h>` after `<srl.hpp>` — the other `system/*.cxx` files show the pattern.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/system/saturn_bootart.h saturn/src/system/saturn_bootart.cxx
git commit -m "Draw the boot sequence's six screens as VDP1 sprites behind a C seam, allocating every texture once because the sprite allocator cannot free and the attract loop would otherwise exhaust it."
```

---

### Task 6: Wire it into main and hand over for hardware

**Files:**
- Modify: `saturn/src/main.c`

**Interfaces:**
- Consumes: `bootmenu.h`, `saturn_bootart.h`, `disc.h` (`disc_play_track`, `disc_stop_track`, `disc_set_music_volume`), `input.h`, `platform.h`.
- Produces: nothing. This is the top of the call graph.

- [ ] **Step 1: Add the includes**

In `saturn/src/main.c`, beside the existing includes:

```c
#include "bootmenu.h"
#ifdef HOTA_SATURN
#include "system/saturn_bootart.h"
#endif
```

- [ ] **Step 2: Write the glue**

Add above `int main(...)`:

```c
#ifdef HOTA_SATURN
/*----------------------
 | boot_key_mask
 | Description: Folds input.h's eight key globals into a BOOT_KEY_* mask.
 | Author: suinevere
 | Dependencies: input.h, bootmenu.h
 | Globals: key_up, key_down, key_left, key_right, key_a, key_b, key_c,
 |          key_select
 | Params: N/A
 | Returns: The mask of keys currently held
 ----------------------*/
static uint32_t boot_key_mask(void)
{
	uint32_t mask = 0u;

	if (key_up)     mask |= BOOT_KEY_UP;
	if (key_down)   mask |= BOOT_KEY_DOWN;
	if (key_left)   mask |= BOOT_KEY_LEFT;
	if (key_right)  mask |= BOOT_KEY_RIGHT;
	if (key_a)      mask |= BOOT_KEY_A;
	if (key_b)      mask |= BOOT_KEY_B;
	if (key_c)      mask |= BOOT_KEY_C;
	if (key_select) mask |= BOOT_KEY_SELECT;

	return mask;
}

/*----------------------
 | boot_sequence
 | Description: Runs the opening stills and the game-select menu, returning
 |   when the player starts Heart of the Alien. Sits between initialize() and
 |   run() so the disc reads it needs are already done and the drive is free
 |   for CD-DA.
 |
 |   Returns immediately if the artwork will not load: a missing decoration
 |   must never brick the disc, so a build without the TGAs boots into the
 |   game instead of hanging on a black screen.
 |
 |   One frame is drawn and presented before the pad is ever sampled, and this
 |   ordering is the whole defence against a button held at power-on skipping
 |   the opening. check_events() only reads SRL's peripheral array; the thing
 |   that refreshes it is Core::Synchronize, reached solely through
 |   boot_art_present (srl_core.hpp:125). Until that first refresh, port 0
 |   holds its static initialiser 0xff, which reads as not-connected, and
 |   check_events zeroes every key. Priming from that would capture a
 |   synthetic zero rather than the pad, so the first genuine sample would
 |   arrive with a stale zero behind it and report every held button as newly
 |   pressed. Sampling after a present means previous holds what the player is
 |   actually holding.
 |
 |   The final present, after the loop, submits an empty VDP1 command list so
 |   the last menu frame's sprites are not left composited over the game.
 |   boot_art_release only toggles NBG0; nothing else in the port draws
 |   sprites, so nothing would otherwise replace them.
 | Author: suinevere
 | Dependencies: bootmenu.h, saturn_bootart.h, disc.h, input.h, platform.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void boot_sequence(void)
{
	bootmenu_state state;
	boot_frame frame;
	uint32_t previous;
	uint32_t current;
	uint32_t pressed;

	if (!boot_art_load())
	{
		return;
	}

	boot_art_draw((int)BOOT_SCREEN_LEGAL, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
	boot_art_present();

	check_events();
	previous = boot_key_mask();

	bootmenu_init(&state, (uint32_t)platform_ticks());

	for (;;)
	{
		check_events();
		current = boot_key_mask();
		pressed = current & ~previous;
		previous = current;

		bootmenu_step(&state, (uint32_t)platform_ticks(), pressed, &frame);

		if (frame.music_restart)
		{
			disc_play_track(BOOT_MUSIC_INDEX, 0);
		}

		disc_set_music_volume(frame.music_volume);

		if (frame.start_game)
		{
			break;
		}

		boot_art_draw((int)frame.screen, (int)frame.highlight);
		boot_art_present();
	}

	disc_stop_track();
	disc_set_music_volume((uint8_t)BOOT_VOLUME_MAX);
	boot_art_release();
	boot_art_present();
}
#endif /* HOTA_SATURN */
```

- [ ] **Step 3: Call it**

In `main()`, change:

```c
	initialize();

	switch(test_flag)
```

to:

```c
	initialize();

#ifdef HOTA_SATURN
	boot_sequence();
#endif

	switch(test_flag)
```

- [ ] **Step 4: Syntax-check**

Run: `cd saturn && make -n src/main.o`

Take the printed invocation, swap `-c -o <path>` for `-fsyntax-only`, and run it.
Expected: no output.

Then confirm the host build is untouched:

Run: `sh saturn/tests/run_tests.sh`
Expected: all eight suites pass.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.c
git commit -m "Run the boot sequence between initialize and the game loop, discarding the first pad sample so a held button and SRL's all-buttons-down peripheral array cannot skip the opening."
```

- [ ] **Step 6: Hand over for the hardware run**

**Do not build or launch the emulator from a tool call.** Ask the human to build and run, and give them this list to check, drawn from the spec's acceptance criteria:

1. Legal text, Virgin logo, Virgin logo slightly lower, title card — 5.1 s each — with music from the first screen.
2. Any button during the opening jumps to the menu and the music keeps playing unbroken.
3. The menu matches the original in both highlight states.
4. Up and Down move the cursor; A, B or C on *HEART OF THE ALIEN* starts the game; the same on *OUT OF THIS WORLD* does nothing.
5. Leaving the menu alone fades the music out and replays the opening.
6. **The sign to check first if something looks wrong**, a one-line fix: if the lit logo never appears on the menu, swap `BOOT_ART_Z_BACK` and `BOOT_ART_Z_FRONT`. `slSetSprite` sorts far to near, so the smaller value should draw on top; that ordering has not been verified on hardware.

Note that the Virgin logo is expected to sit still for 10.2 s across two state-machine slots. That is correct, not a stuck frame — the capture's two holds differ by 570 pixels in the top rows of the logo's frame and both draw one texture.

---

## Self-Review

**Spec coverage.** Every section of the design spec maps to a task: the asset table and both synthesis rules to Task 1; the state machine, timeline and audio behaviour to Tasks 2 and 3; the volume seam to Task 4; the VDP1 architecture, NBG0 hand-off and allocate-once rule to Task 5; the `main.c` hook and error handling to Task 6. The spec's testing section is Tasks 2 and 3; its acceptance list is Task 6 Step 6.

**Two spec numbers were corrected during planning** and the spec has been updated to match: crop heights are padded to even (band 1 becomes 34 rows, 4,658 bytes, VDP1 total 188,958), and the TGA flavour is pinned to `ELF_S.TGA`'s rather than left to ffmpeg, which always writes a 256-entry colour map.

**Placeholders.** None — every code step carries the code, and the two values that genuinely cannot be known until Task 1 runs (the band rectangles) are handled by having Task 1 print them and Task 5 state the formula that converts them.

**Type consistency.** `boot_screen`, `boot_entry`, `boot_frame`, `bootmenu_state`, `BOOT_KEY_*`, `BOOT_VOLUME_MAX`, `BOOT_MUSIC_INDEX`, `bootmenu_init`, `bootmenu_step`, `boot_art_load`, `boot_art_draw`, `boot_art_present`, `boot_art_release` and `disc_set_music_volume` are spelled identically in every task that declares or calls them. `boot_art_draw` takes plain `int`s so `saturn_bootart.h` need not include `bootmenu.h`.

---

### Task 7: Replace the TGA load path with pre-packed assets

**Why:** `SRL::Bitmap::TGA` cannot be used in this port. `LoadData` allocates the whole file (`srl_tga.hpp:687`) against a 16,064-byte HWRAM heap, the allocation returns NULL, and `Cd::File::LoadBytes` hands that NULL to `GFS_Load` unchecked (`srl_cd.hpp:399`), DMAing 36 sectors to address 0. Observed on hardware as a black screen. `BOOT_ART_TGA_USABLE 0` currently disables the whole feature; this task removes the need for it.

**Files:**
- Modify: `tools/mkbootart.py` — emit `.ART` instead of `.TGA`
- Delete: `saturn/cd/data/*.TGA` (six files)
- Create: `saturn/cd/data/BOOTLEGL.ART`, `BOOTVIRG.ART`, `BOOTTITL.ART`, `MENUBG.ART`, `MENUOOTW.ART`, `MENUHOTA.ART`
- Modify: `saturn/src/system/saturn_bootart.cxx`

**Interfaces:** unchanged. `boot_art_load/draw/present/release` keep their signatures; only the loading mechanism changes.

#### The `.ART` format

Big-endian throughout, since the SH-2 is. One file per screen:

| offset | size | meaning |
|---|---|---|
| 0 | 2 | magic `0x4241` (`"BA"`) |
| 2 | 2 | width in pixels |
| 4 | 2 | height in pixels |
| 6 | 2 | palette entry count, 1..16 |
| 8 | count × 2 | palette, one `HighColor` each |
| 8 + count×2 | width × height / 2 | 4bpp packed pixels, two per byte, high nibble first |

`HighColor` is a big-endian bitfield (`srl_color.hpp:12-28`): bit 15 opaque, bits 14–10 Blue, bits 9–5 Green, bits 4–0 Red. So each entry is `0x8000 | (b >> 3) << 10 | (g >> 3) << 5 | (r >> 3)`.

The pixel packing must match `Tga::DecodePaletted`'s: `(left & 0x0f) << 4 | (right & 0x0f)`.

**`.ART`, not `.BIN`** — `.gitignore` carries `saturn/cd/data/*.bin`, and git on Windows matches that case-insensitively, so `.BIN` assets would be silently uncommitted.

#### Every screen becomes Paletted16

The title card drops from 21 colours to 16, reversing the earlier decision to keep all 21. That choice was made when VDP1 VRAM was the only cost; it is now load-bearing for a different reason. The loader needs one work-RAM staging buffer, and it must come from LWRAM, of which roughly 114 KB remains once `vm_alloc_memory` (524,288) and `game2bin_alloc` (409,600) are placed — less the disc bounce buffer. A `Paletted64` title card needs 71,680 bytes of staging against a `Paletted16` screen's 35,840. Uniform 4bpp keeps the buffer at 35,880 and leaves real headroom.

The 8 colours folded away are anti-aliasing shades on the lettering, 1.5% of pixels. `mkbootart.py` merges each into its nearest surviving neighbour by squared RGB distance.

#### The load path

Read with the port's own `disc_read_file`, which reads whole sectors into a caller-owned destination and handles the partial final sector through its own bounce buffer — the overrun `GFS_Load` commits and `disc_read_file_body` was written to refuse. Then hand the pixels to `VDP1::TryLoadTexture(width, height, colorMode, palette, data)` (`srl_vdp1.hpp:211`), the raw overload, which `slDMACopy`s work RAM into VDP1 VRAM. That is the same transfer SRL performs internally, so it is a proven path.

Staging buffer: one allocation of `BOOT_ART_STAGE_BYTES` (36,864) from `saturn_lwram_alloc`, reused for all six files and freed with `saturn_lwram_free` before `boot_art_load` returns, on every path including failure.

- [ ] **Step 1: Re-cut the assets**

In `tools/mkbootart.py`, replace `write_tga` with `write_art` implementing the format above, add a nearest-neighbour merge so any image over 16 colours is reduced, change all six output names to `.ART`, and update `verify()` to re-read the emitted headers: magic, `1 <= count <= 16`, even width and height, and total file size exactly `8 + count*2 + w*h/2`.

Keep every existing measurement step unchanged — the run detection, the 2% Virgin drift check, the largest-gap band split, the even padding. Only the writer and the colour ceiling change.

Run `python tools/mkbootart.py`. Report the printed output. Confirm the band rectangles are still `x19 y45 274x34` and `x9 y86 300x34`; if not, Task 5's sprite offsets need recomputing and you should stop and say so.

`git rm` the six `.TGA` files.

- [ ] **Step 2: Rewrite the loader**

In `saturn/src/system/saturn_bootart.cxx`: delete `BOOT_ART_TGA_USABLE` and `bootArtLoadPalette`, drop the `SRL::Bitmap::TGA` include path, point `BOOT_ART_FILES` at the `.ART` names, and rewrite `boot_art_load` to, for each screen not already loaded: `disc_read_file(name, stage, BOOT_ART_STAGE_BYTES)`, validate the magic and the count, allocate a CRAM bank with `CRAM::GetFreeBank(TextureColorMode::Paletted16)` and mark it used, fill it via `CRAM::Palette(mode, bank).Load((SRL::Types::HighColor *)(stage + 8), count)`, then `VDP1::TryLoadTexture(width, height, TextureColorMode::Paletted16, bank, stage + 8 + count * 2)`.

`disc_read_file` is declared in `disc.h`, which is C and `extern "C"`-guarded, so it can be included directly.

Every function and constant keeps a banner. Record in `boot_art_load`'s banner why the staging buffer exists and why `disc_read_file` rather than any SRL loader.

- [ ] **Step 3: Verify**

Syntax-check from `saturn/` with the flags from `make -n src/system/saturn_bootart.o`, using `-std=c++23` and `../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe`. Run `sh saturn/tests/run_tests.sh` — 8 suites, unaffected but must stay green. Do not run `make`, `compile.bat` or any emulator.

State plainly in the report that the load path itself remains unverified until the human boots it.

- [ ] **Step 4: Commit**

One sentence, no body, no trailers, no mention of Claude or a session.
