#!/usr/bin/env python3
# ----------------------
# | mkmenuart.py
# | Description: Builds the menu's four .ART files. The font is a fixed glyph
# |   table drawn with a one-pixel outline; the panels are solid rectangles.
# |   Every colour comes out of the screengrabs, so a re-grab re-keys the menus
# |   instead of leaving them keyed to a palette that no longer exists: the
# |   panels from HEART OF THE ALIEN003.tga, the same grab BOOTTITL.ART is cut
# |   from, and the text from the lit entry of the game-select menu, which is
# |   the one place in the original that styles a selectable line of text.
# |   Run from anywhere; paths resolve against the repository.
# | Author: suinevere
# | Dependencies: python3, mkbootart.py, font8x8.py
# ----------------------
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkbootart import (read_tga, active, crop, high_color, reduce_palette,
                       changed_rows, split_at_largest_gap, changed_columns,
                       pad_to, SRC_W, ACTIVE_H, TGA_DIR, OUT_DIR)
from font8x8 import FONT8X8, FONT8X8_FIRST, FONT8X8_LAST

TITLE_SOURCE = "HEART OF THE ALIEN003.tga"
MENU_STATE_A = "HEART OF THE ALIEN004.tga"
MENU_STATE_B = "HEART OF THE ALIEN005.tga"

# ----------------------
# | GLYPH_W / GLYPH_H / GLYPH_INK_X / GLYPH_INK_Y
# | Description: The cell, and where the 8x8 source bitmap sits inside it.
# |
# |   font8x8.py's glyphs use columns 0-4 and rows 0-6, so a one-pixel outline
# |   around them needs columns -1..5 and rows -1..7 -- two of which are outside
# |   an 8x8 cell. Offsetting the ink by one puts the outline at columns 0-6 and
# |   rows 0-8, which fits an 8-wide cell exactly and needs two more rows of
# |   height. Width has to stay a multiple of 8 because VDP1 counts sprite width
# |   in 8-dot units; height has no such rule and only has to stay even, since
# |   4bpp rows pack two pixels to the byte.
# |
# |   The cost is letter spacing: outline to outline is now one pixel where ink
# |   to ink used to be three. Anything wider would need a 16-wide cell and
# |   double the font's VDP1 footprint for the sake of a gap.
# | Author: suinevere
# ----------------------
GLYPH_W = 8
GLYPH_H = 10
GLYPH_INK_X = 1
GLYPH_INK_Y = 1
GLYPH_COUNT = FONT8X8_LAST - FONT8X8_FIRST + 1

PANELS = [
    ("MENUPANP.ART", 168, 96),
    ("MENUPANS.ART", 272, 168),
    ("MENUPANC.ART", 240, 72),
]

PANEL_BORDER_PX = 2

# ----------------------
# | IDX_TRANSPARENT .. PALETTE_ENTRIES
# | Description: The shared palette layout for all four files, so they can
# |   share one CRAM bank. Index 0 is transparent -- VDP1 treats palette
# |   index 0 in a Paletted16 sprite as transparent, which is what lets a
# |   glyph sit over the title card with no box around it.
# |
# |   Only indices 0, 1, 2, 3 and 5 are ever a pixel: transparent, the glyph
# |   fill, the panel's two colours and the glyph outline. Everything else is a
# |   colour saturn_menuart.cxx substitutes into 1 or 5 when it builds a bank,
# |   which is how one font texture draws in four different styles without a
# |   second copy of the glyphs -- 64 more VDP1 textures would not fit the
# |   budget. Indices 8-15 stay black padding.
# |
# |   The two title entries exist because the sub-title card is the only screen
# |   whose text sits on the artwork rather than on a panel of its own. It gets
# |   the game-select menu's white-outlined red; every screen with a panel keeps
# |   the blue ramp it always had, and has its outline written in the panel's
# |   own fill colour so the halo lands invisibly on the panel behind it.
# | Author: suinevere
# ----------------------
IDX_TRANSPARENT = 0
IDX_TEXT_DIM = 1
IDX_PANEL_FILL = 2
IDX_PANEL_BORDER = 3
IDX_TEXT_SEL = 4
IDX_TEXT_OUTLINE = 5
IDX_TITLE_DIM = 6
IDX_TITLE_SEL = 7
PALETTE_ENTRIES = 16

# ----------------------
# | BAND_MIN_PIXELS
# | Description: How many pixels a colour must cover inside the lit menu band
# |   before text_colours will take it for one of the three text roles. One
# |   percent of the band, which clears the three real colours by an order of
# |   magnitude and rejects the handful of pixels where the band's rectangle
# |   clips a piece of the background.
# | Author: suinevere
# ----------------------
BAND_MIN_PIXELS_PCT = 1


# ----------------------
# | luminance
# | Description: ITU-R-style weighted brightness, used to order the ramp and
# |   to check it is actually separable rather than four shades of the same
# |   grey.
# | Author: suinevere
# | Params: colour -- 3-byte RGB
# | Returns: Integer luminance, 0..255
# ----------------------
def luminance(colour):
    return (colour[0] * 30 + colour[1] * 59 + colour[2] * 11) // 100


# ----------------------
# | title_ramp
# | Description: Pulls four colours off the title card's own palette, darkest
# |   to brightest, so a re-grab re-keys the menus instead of leaving them keyed
# |   to a palette that no longer exists. Filters to blue-dominant colours
# |   first because the title card's sky and metal are blue and that gives a
# |   coherent ramp; the fallback to the whole palette exists because a re-grab
# |   could land on a frame that is not blue-dominant, and failing to a readable
# |   grey-ish ramp beats failing to a crash.
# |
# |   This is the ramp every screen with a panel uses, and it is the one the
# |   menus shipped with. Only the sub-title card takes its colours from
# |   text_colours instead.
# | Author: suinevere
# | Params: N/A
# | Returns: (fill, border, dim, sel) RGB colours, darkest to brightest
# ----------------------
def title_ramp():
    image = active(read_tga(os.path.join(TGA_DIR, TITLE_SOURCE)))
    lookup = {}
    palette = []
    counts = []
    for i in range(SRC_W * ACTIVE_H):
        colour = image[i * 3:i * 3 + 3]
        if colour not in lookup:
            lookup[colour] = len(palette)
            palette.append(colour)
            counts.append(0)
        counts[lookup[colour]] += 1
    if len(palette) > 16:
        palette, _ = reduce_palette(palette, counts, 16)

    blues = [c for c in palette if c[2] >= c[0] and c[2] >= c[1]]
    if len(blues) < 4:
        blues = list(palette)
    blues.sort(key=luminance)

    fill = blues[0]
    border = blues[len(blues) // 3]
    dim = blues[(2 * len(blues)) // 3]
    sel = blues[-1]
    return fill, border, dim, sel


# ----------------------
# | text_colours
# | Description: Pulls the outline and the two fills off the game-select
# |   screen's lit entry, which is the only text in the original styled the way
# |   a selectable menu line needs to be: a near-white outline around a red
# |   fill, with a pinker shade inside it.
# |
# |   The band is located the way mkbootart locates it -- by diffing the two
# |   menu grabs, since the only thing that changes between them is which entry
# |   is lit -- rather than by a hardcoded rectangle, so a re-grab that moves
# |   the menu by a few rows still finds it. The second band is HEART OF THE
# |   ALIEN, lit in state B, which is the game this port is.
# |
# |   The three roles are picked by property rather than by count, because the
# |   counts swap between the two grabs and would make the result depend on
# |   which one was read. The outline is the brightest colour present; of the
# |   red-dominant ones the most saturated red is the selected fill and the next
# |   is the dim fill, which is what puts the flat red on the lit row and the
# |   pink on the others.
# | Author: suinevere
# | Params: N/A
# | Returns: (outline, sel, dim) RGB colours
# ----------------------
def text_colours():
    state_a = active(read_tga(os.path.join(TGA_DIR, MENU_STATE_A)))
    state_b = active(read_tga(os.path.join(TGA_DIR, MENU_STATE_B)))

    rows = changed_rows(state_a, state_b, SRC_W, ACTIVE_H)
    _, band = split_at_largest_gap(rows)
    x_low, x_high = changed_columns(state_a, state_b, SRC_W, band[0], band[1])
    x, w = pad_to(x_low, x_high, SRC_W, 8)
    y, h = pad_to(band[0], band[1], ACTIVE_H, 2)

    lit = crop(state_b, x, y, w, h, SRC_W)
    counts = collections.Counter()
    for i in range(w * h):
        counts[tuple(lit[i * 3:i * 3 + 3])] += 1

    floor = (w * h * BAND_MIN_PIXELS_PCT) // 100
    strong = [c for c, n in counts.items() if n >= floor]
    if not strong:
        sys.exit("mkmenuart: the lit menu band has no colour covering %d%% of "
                 "it; the band search found the wrong rectangle" % BAND_MIN_PIXELS_PCT)

    outline = max(strong, key=luminance)
    reds = [c for c in strong if c[0] > c[1] and c[0] > c[2]]
    reds.sort(key=lambda c: c[0] - max(c[1], c[2]), reverse=True)
    if len(reds) < 2:
        sys.exit("mkmenuart: the lit menu band holds %d red-dominant colours, "
                 "not the two the fill needs; the band search found the wrong "
                 "rectangle or the grab is not the game-select menu" % len(reds))

    return outline, reds[0], reds[1]


# ----------------------
# | build_palette
# | Description: Lays the seven derived colours into the shared 16-entry
# |   palette at their fixed indices, leaving index 0 and 8-15 black.
# | Author: suinevere
# | Params: fill, border, dim, sel -- the panel screens' ramp from title_ramp;
# |         outline, titleSel, titleDim -- the sub-title card's from
# |         text_colours
# | Returns: List of 16 RGB colours
# ----------------------
def build_palette(fill, border, dim, sel, outline, titleSel, titleDim):
    entries = [(0, 0, 0)] * PALETTE_ENTRIES
    entries[IDX_TEXT_DIM] = dim
    entries[IDX_PANEL_FILL] = fill
    entries[IDX_PANEL_BORDER] = border
    entries[IDX_TEXT_SEL] = sel
    entries[IDX_TEXT_OUTLINE] = outline
    entries[IDX_TITLE_DIM] = titleDim
    entries[IDX_TITLE_SEL] = titleSel
    return entries


# ----------------------
# | write_art_indexed
# | Description: Writes one .ART file from a ready-made index buffer against
# |   a fixed shared palette, matching mkbootart.write_art's header and 4bpp
# |   packing so the same loader reads both. Unlike write_art, the palette
# |   here is supplied rather than derived per-image, because all four files
# |   must agree on one 16-entry palette to share a CRAM bank.
# | Author: suinevere
# | Params: path, indices, w, h -- output and 4bpp palette indices, one byte
# |         each; palette -- 16 RGB colours shared by all four files
# | Returns: N/A
# ----------------------
def write_art_indexed(path, indices, w, h, palette):
    header = bytearray()
    header += (0x4241).to_bytes(2, "big")
    header += w.to_bytes(2, "big")
    header += h.to_bytes(2, "big")
    header += len(palette).to_bytes(2, "big")
    for colour in palette:
        header += high_color(colour).to_bytes(2, "big")

    packed = bytearray((w * h) // 2)
    for i in range(0, w * h, 2):
        packed[i // 2] = ((indices[i] & 0x0F) << 4) | (indices[i + 1] & 0x0F)

    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(packed)


# ----------------------
# | font_indices
# | Description: Every glyph stacked into one GLYPH_W-wide column, glyph i at
# |   rows i*GLYPH_H..i*GLYPH_H+GLYPH_H-1, so the whole font is a single tall
# |   texture the Saturn side indexes by glyph number times the cell height
# |   rather than a 2D sheet needing a second coordinate.
# |
# |   Each glyph is drawn twice: the outline over all eight neighbours of every
# |   ink pixel, then the ink on top of it. Painting the outline first and
# |   letting the ink overwrite is what keeps a thin stroke from being eaten by
# |   its own halo -- an outline written only where no ink will land would need
# |   the ink set computed first anyway, and this way the two passes cannot
# |   disagree about which pixels are which.
# | Author: suinevere
# | Params: N/A
# | Returns: GLYPH_W * GLYPH_H * GLYPH_COUNT bytes, one palette index each
# ----------------------
def font_indices():
    indices = bytearray(GLYPH_W * GLYPH_H * GLYPH_COUNT)
    for g in range(GLYPH_COUNT):
        base = g * GLYPH_H * GLYPH_W
        ink = set()
        for row in range(len(FONT8X8[g])):
            bits = FONT8X8[g][row]
            for col in range(8):
                if bits & (0x80 >> col):
                    ink.add((col + GLYPH_INK_X, row + GLYPH_INK_Y))

        for x, y in ink:
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ox, oy = x + dx, y + dy
                    if 0 <= ox < GLYPH_W and 0 <= oy < GLYPH_H:
                        indices[base + oy * GLYPH_W + ox] = IDX_TEXT_OUTLINE

        for x, y in ink:
            indices[base + y * GLYPH_W + x] = IDX_TEXT_DIM

    return indices


# ----------------------
# | panel_indices
# | Description: A solid rectangle, fill inside a PANEL_BORDER_PX-wide border.
# | Author: suinevere
# | Params: w, h -- panel dimensions
# | Returns: w*h bytes, one palette index each
# ----------------------
def panel_indices(w, h):
    indices = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            edge = (x < PANEL_BORDER_PX or x >= w - PANEL_BORDER_PX
                    or y < PANEL_BORDER_PX or y >= h - PANEL_BORDER_PX)
            indices[y * w + x] = IDX_PANEL_BORDER if edge else IDX_PANEL_FILL
    return indices


# ----------------------
# | verify
# | Description: Re-reads every emitted .ART header and fails the build on
# |   anything a later task's hardcoded geometry would trust blindly, plus a
# |   readability check the boot art's verify() has no reason to carry.
# |
# |   The width check fails on anything not a multiple of 8: nothing in SRL
# |   or SGL rejects a bad width, since VDP1 counts sprite width in 8-dot
# |   units, so a wrong width only ever shows up on screen, sheared two
# |   pixels further right on every line -- never as a load error.
# |
# |   The ramp check fails the build on a collapsed or low-contrast ramp
# |   rather than letting it through, because unreadable menu text is close
# |   to unobservable until it reaches hardware: a script exits with a clear
# |   reason today, a human squints at a CRT tomorrow.
# |
# |   The shared-palette check re-reads the palette bytes out of each written
# |   file and fails on any disagreement. saturn_menuart.cxx builds both CRAM
# |   banks from MENUFONT.ART's palette alone and draws all three panels with
# |   that bank, so the four files agreeing is a hardware contract, not a
# |   tidiness preference. It holds today only because main() happens to pass
# |   one shared palette object to all four write_art_indexed calls; an edit
# |   that derived a second palette would pass every other check here and put
# |   the panels on screen in the font's colours.
# | Author: suinevere
# | Params: expected -- list of (filename, w, h); palette -- 16 RGB colours
# | Returns: N/A
# ----------------------
def verify(expected, palette):
    palettes = {}
    for name, w, h in expected:
        path = os.path.join(OUT_DIR, name)
        data = open(path, "rb").read()
        if int.from_bytes(data[0:2], "big") != 0x4241:
            sys.exit("mkmenuart: %s has the wrong magic" % name)
        if int.from_bytes(data[2:4], "big") != w:
            sys.exit("mkmenuart: %s width is not %d" % (name, w))
        if int.from_bytes(data[4:6], "big") != h:
            sys.exit("mkmenuart: %s height is not %d" % (name, h))
        count = int.from_bytes(data[6:8], "big")
        if count != PALETTE_ENTRIES:
            sys.exit("mkmenuart: %s carries %d palette entries, not %d"
                     % (name, count, PALETTE_ENTRIES))
        if w % 8 != 0:
            sys.exit("mkmenuart: %s is %d wide; VDP1 stores width in 8-dot "
                     "units and a remainder shears every line" % (name, w))
        if h % 2 != 0:
            sys.exit("mkmenuart: %s is %d tall; 4bpp rows pack two pixels a "
                     "byte" % (name, h))
        want = 8 + 2 * count + (w * h) // 2
        if len(data) != want:
            sys.exit("mkmenuart: %s is %d bytes, header claims %d"
                     % (name, len(data), want))
        palettes[name] = data[8:8 + 2 * count]
        print("  %-14s %4dx%-4d %6d bytes" % (name, w, h, len(data)))

    reference = expected[0][0]
    for name, _, _ in expected[1:]:
        if palettes[name] != palettes[reference]:
            sys.exit("mkmenuart: %s's palette differs from %s's; "
                     "saturn_menuart.cxx builds both CRAM banks from "
                     "MENUFONT.ART alone and draws every panel with that one "
                     "bank, so all four files must carry the same 16 entries"
                     % (name, reference))
    print("  palette: all %d files carry identical entries" % len(expected))

    fill = palette[IDX_PANEL_FILL]
    border = palette[IDX_PANEL_BORDER]
    dim = palette[IDX_TEXT_DIM]
    sel = palette[IDX_TEXT_SEL]
    outline = palette[IDX_TEXT_OUTLINE]
    title_dim = palette[IDX_TITLE_DIM]
    title_sel = palette[IDX_TITLE_SEL]

    if not (luminance(sel) > luminance(dim) > luminance(border) >= luminance(fill)):
        sys.exit("mkmenuart: the title card's ramp has collapsed -- fill %d, "
                 "border %d, dim %d, selected %d must be strictly increasing"
                 % (luminance(fill), luminance(border), luminance(dim),
                    luminance(sel)))
    if luminance(dim) - luminance(fill) < 40:
        sys.exit("mkmenuart: dim text is only %d brighter than the panel fill; "
                 "under 40 it is unreadable on a CRT"
                 % (luminance(dim) - luminance(fill)))
    if luminance(outline) - luminance(title_sel) < 80:
        sys.exit("mkmenuart: the sub-title card's outline is only %d brighter "
                 "than the fill it surrounds; under 80 the letters have no edge"
                 % (luminance(outline) - luminance(title_sel)))
    if title_dim == title_sel:
        sys.exit("mkmenuart: the sub-title card's two fills are the same "
                 "colour, so a selected row would be marked by the cursor "
                 "alone")
    print("  panel ramp: fill %d, border %d, dim %d, selected %d"
          % (luminance(fill), luminance(border), luminance(dim),
             luminance(sel)))
    print("  title card: outline %s, selected %s, dim %s"
          % (tuple(outline), tuple(title_sel), tuple(title_dim)))


# ----------------------
# | main
# | Description: Derives the panel pair from the title card and the text trio
# |   from the game-select menu, builds the shared palette, writes the font and
# |   the three panels against it, then verifies every file.
# | Author: suinevere
# | Params: N/A
# | Returns: N/A
# ----------------------
def main():
    fill, border, dim, sel = title_ramp()
    outline, titleSel, titleDim = text_colours()
    palette = build_palette(fill, border, dim, sel, outline, titleSel, titleDim)
    os.makedirs(OUT_DIR, exist_ok=True)

    write_art_indexed(os.path.join(OUT_DIR, "MENUFONT.ART"), font_indices(),
                      GLYPH_W, GLYPH_H * GLYPH_COUNT, palette)
    for name, w, h in PANELS:
        write_art_indexed(os.path.join(OUT_DIR, name), panel_indices(w, h),
                          w, h, palette)

    print("verify:")
    verify([("MENUFONT.ART", GLYPH_W, GLYPH_H * GLYPH_COUNT)] + PANELS, palette)
    print("glyph stride %d bytes, pixel data starts at byte %d"
          % (GLYPH_W * GLYPH_H // 2, 8 + 2 * PALETTE_ENTRIES))


if __name__ == "__main__":
    main()
