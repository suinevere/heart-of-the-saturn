#!/usr/bin/env python3
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG_DIR = os.path.join(ROOT, "tools", "assets", "png")
OUT_DIR = os.path.join(ROOT, "saturn", "cd", "data")

ART_MAGIC = 0x4241

INK_FLOOR = 16

GAIN = {"OOTW.png": 1.0, "HOTA.png": 1.6}

DIM_SCALE = 0.5
DIM_GREY = 0.6

SELECT_SIZE = (152, 96)
TITLE_BOX = (300, 128)

TITLE_TOP = 6

ROW_LABELS = ("START GAME", "LOAD GAME", "OPTIONS")

ROW_DIM = ((0, 0, 8), (5, 32, 93), (67, 131, 208))
ROW_LIT = ((0, 0, 8), (67, 131, 208), (140, 190, 245))

ROW_W, ROW_H = 200, 18
COLS = (4, 3, 3, 3, 4)
ROWS = (4, 3, 3, 3, 4)
GAP = 3
SPACE = 6

GLYPHS = {
    "A": ["o###o", "#...#", "#####", "#...#", "#...#"],
    "D": ["####o", "#...#", "#...#", "#...#", "####o"],
    "E": ["#####", "#....", "####.", "#....", "#####"],
    "G": ["#####", "#....", "#.###", "#...#", "#####"],
    "I": ["#", "#", "#", "#", "#"],
    "L": ["#....", "#....", "#....", "#....", "#####"],
    "M": ["#####", "#.#.#", "#.#.#", "#...#", "#...#"],
    "N": ["####o", "#...#", "#...#", "#...#", "#...#"],
    "O": ["#####", "#...#", "#...#", "#...#", "#####"],
    "P": ["#####", "#...#", "#####", "#....", "#...."],
    "R": ["####o", "#...#", "####o", "#....", "#...."],
    "S": ["#####", "#....", "#####", "....#", "#####"],
    "T": ["#####", "..#..", "..#..", "..#..", "..#.."],
}

ROUND = 3

GLYPH_COLS = {"T": (4, 3, 4, 3, 4)}

DIAGONALS = {"R": (10, 4)}


def load_logo(name):
    im = Image.open(os.path.join(PNG_DIR, name)).convert("RGBA")
    back = Image.new("RGBA", im.size, (0, 0, 0, 255))
    im = Image.alpha_composite(back, im).convert("RGB")
    box = im.convert("L").point(lambda v: 255 if v >= INK_FLOOR else 0).getbbox()
    gain = GAIN.get(name, 1.0)
    return im.crop(box).point(lambda v: min(255, int(v * gain)))


def fit(im, box):
    scale = min(box[0] / im.width, box[1] / im.height)
    w = max(1, round(im.width * scale))
    h = max(1, round(im.height * scale))
    im = im.resize((w, h), Image.LANCZOS)
    pw = (w + 7) // 8 * 8
    ph = (h + 1) // 2 * 2
    out = Image.new("RGB", (pw, ph), (0, 0, 0))
    out.paste(im, ((pw - w) // 2, (ph - h) // 2))
    return out


def quantise(im):
    q = im.quantize(colors=15, method=Image.MEDIANCUT, dither=Image.NONE)
    flat = q.getpalette()[:45]
    pal = [(0, 0, 0)] + [tuple(flat[i * 3:i * 3 + 3]) for i in range(15)]
    idx = [i + 1 for i in q.get_flattened_data()]
    src = list(im.get_flattened_data())
    for n, (r, g, b) in enumerate(src):
        if r < 12 and g < 12 and b < 12:
            idx[n] = 0
    return pal, idx


def dim_colour(rgb):
    lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]
    return tuple(DIM_SCALE * (c + (lum - c) * DIM_GREY) for c in rgb)


def highcolor(rgb):
    r, g, b = (min(255, max(0, int(c))) >> 3 for c in rgb)
    return 0x8000 | (b << 10) | (g << 5) | r


def write_art(name, w, h, pal, idx):
    data = struct.pack(">HHHH", ART_MAGIC, w, h, len(pal))
    data += b"".join(struct.pack(">H", highcolor(c)) for c in pal)
    data += bytes((idx[i] << 4) | idx[i + 1] for i in range(0, len(idx), 2))
    with open(os.path.join(OUT_DIR, name), "wb") as f:
        f.write(data)
    print("%-12s %3dx%-3d %6d bytes" % (name, w, h, len(data)))


def build_select(png, stem):
    im = load_logo(png).resize(SELECT_SIZE, Image.LANCZOS)
    pal, idx = quantise(im)
    dim = [dim_colour(rgb) for rgb in pal]
    write_art(stem + "0.ART", im.width, im.height, dim, idx)
    write_art(stem + "1.ART", im.width, im.height, pal, idx)


def build_title(png, name):
    logo = fit(load_logo(png), TITLE_BOX)
    page = Image.new("RGB", (320, 224), (0, 0, 0))
    page.paste(logo, ((320 - logo.width) // 2, TITLE_TOP + (TITLE_BOX[1] - logo.height) // 2))
    pal, idx = quantise(page)
    write_art(name, 320, 224, pal, idx)


def glyph_mask(ch):
    cells = GLYPHS[ch]
    widths = GLYPH_COLS.get(ch, COLS) if len(cells[0]) == 5 else (COLS[0],)
    w, h = sum(widths), sum(ROWS)
    mask = [[False] * w for _ in range(h)]
    xs = [sum(widths[:c]) for c in range(len(widths))]
    ys = [sum(ROWS[:r]) for r in range(len(ROWS))]

    def cell(r, c):
        return 0 <= r < len(ROWS) and 0 <= c < len(widths) and cells[r][c] != "."

    for r in range(len(ROWS)):
        for c in range(len(widths)):
            if cell(r, c):
                for yy in range(ys[r], ys[r] + ROWS[r]):
                    for xx in range(xs[c], xs[c] + widths[c]):
                        mask[yy][xx] = True
    for r in range(len(ROWS)):
        for c in range(len(widths)):
            if cells[r][c] != "o":
                continue
            for dr in (-1, 1):
                for dc in (-1, 1):
                    if cell(r + dr, c) or cell(r, c + dc):
                        continue
                    cx = xs[c] if dc < 0 else xs[c] + widths[c] - 1
                    cy = ys[r] if dr < 0 else ys[r] + ROWS[r] - 1
                    for k in range(ROUND):
                        for m in range(ROUND - k):
                            mask[cy - dr * k][cx - dc * m] = False
                    if cell(r - dr, c - dc):
                        continue
                    iy = ys[r - dr] if dr < 0 else ys[r - dr] + ROWS[r - dr] - 1
                    ix = xs[c - dc] if dc < 0 else xs[c - dc] + widths[c - dc] - 1
                    for k in range(ROUND):
                        for m in range(ROUND - k):
                            mask[iy - dr * k][ix - dc * m] = True

    if ch in DIAGONALS:
        top, left = DIAGONALS[ch]
        for yy in range(top, h):
            for xx in range(left + yy - top, min(w, left + yy - top + 2 * COLS[-1])):
                mask[yy][xx] = True

    def filled(xx, yy):
        return 0 <= xx < w and 0 <= yy < h and mask[yy][xx]

    corners = [(xx, yy) for yy in range(h) for xx in range(w) if mask[yy][xx]
               and any(not filled(xx + dx, yy) and not filled(xx, yy + dy)
                       for dx in (-1, 1) for dy in (-1, 1))]
    for xx, yy in corners:
        mask[yy][xx] = False
    return mask


def render_label(text):
    masks = [None if ch == " " else glyph_mask(ch) for ch in text]
    total = sum(SPACE if m is None else len(m[0]) for m in masks) + GAP * (len(masks) - 1)
    px = [[0] * ROW_W for _ in range(ROW_H)]
    x = (ROW_W - total - 1) // 2
    for m in masks:
        if m is None:
            x += SPACE + GAP
            continue
        for yy, row in enumerate(m):
            for xx, on in enumerate(row):
                if on:
                    px[yy][x + xx] = 2
        x += len(m[0]) + GAP
    face = [(xx, yy) for yy in range(ROW_H) for xx in range(ROW_W) if px[yy][xx] == 2]
    for xx, yy in face:
        if any(not (0 <= xx + dx < ROW_W and 0 <= yy + dy < ROW_H) or px[yy + dy][xx + dx] == 0
               for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            px[yy][xx] = 3
    for xx, yy in face:
        if xx + 1 < ROW_W and yy + 1 < ROW_H and px[yy + 1][xx + 1] == 0:
            px[yy + 1][xx + 1] = 1
    return [v for row in px for v in row]


def build_rows():
    for n, label in enumerate(ROW_LABELS):
        idx = render_label(label)
        for suffix, ramp in (("D", ROW_DIM), ("L", ROW_LIT)):
            pal = [(0, 0, 0)] + list(ramp) + [(0, 0, 0)] * 12
            write_art("TROW%d%s.ART" % (n, suffix), ROW_W, ROW_H, pal, idx)


if __name__ == "__main__":
    build_select("OOTW.png", "SELOOTW")
    build_select("HOTA.png", "SELHOTA")
    build_title("HOTA.png", "HOTATITL.ART")
    build_rows()
    sys.exit(0)
