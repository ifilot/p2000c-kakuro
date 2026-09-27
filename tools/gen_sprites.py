#!/usr/bin/env python3
"""Generate src/sprites.h: the cell pictures for the 512x252 high-res mode.

Dots have a 3:5 horizontal-to-vertical pitch on the CRT (p2000c-emulator,
docs/hardware.md), so a 40x24-dot cell is square. Grids up to 9x9 (the sum
row and column included) use such cells; the 10x10 grids use 32x20 cells,
which are nearly square, so that the board leaves room for the panel.

Every cell starts as a whole tile, byte aligned, row-major, MSB = leftmost
dot, that carries the board's grid on its top row and left column (the
terminal draws the grid itself as long lines; screen.c only uploads what
differs from them). Tiles:

  white   an empty white cell: just the grid, dark inside
  block   a black cell without sums: a 50% checkerboard dither
  clue_a  a black cell with an across sum: a diagonal from the top left to
          the bottom right, the upper right triangle dark (the sum goes
          there) and the lower left one dithered
  clue_d  the same with a down sum: the lower left triangle dark
  clue_ad both sums: both triangles dark

On top of a white tile the program ORs a digit (DIGIT_W bytes wide from
byte DIGIT_X, DIGIT_H lines from line DIGIT_Y). A given digit has a whole
tile of its own: the cell inside the grid lines in a sparse 25% dither
(darker than the 50% of the black cells), the digit left dark inside a lit
contour. Sums are 2-byte-wide, 7-line numbers put at SUM_A_* (across) or
SUM_D_* (down). The cursor is OR-ed over the whole cell.

Glyphs are the terminal's own 8x12 character-ROM digits (font sheet from
the sibling p2000c-emulator checkout): scaled 3x2 in the large cells, 2x2
in the small ones, and unscaled for the sums.

  python3 tools/gen_sprites.py [--preview]    (preview: build/sprites_preview.png)
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"


class Size:
    """One cell size and where everything goes inside a cell."""
    def __init__(self, name, w, h, scale, digit_x, digit_y, digit_w, sum_a, sum_d, bracket):
        self.name, self.w, self.h = name, w, h
        self.scale = scale                    # digit scale (x, y)
        self.digit_x, self.digit_y = digit_x, digit_y   # digit's first byte, first line
        self.digit_w = digit_w                # bytes the digit spans
        self.sum_a, self.sum_d = sum_a, sum_d  # (first byte, first line) of the sum numbers
        self.bracket = bracket                # cursor corner brackets (length x, length y, thickness x, thickness y)


SIZES = [
    Size("large", 40, 24, (3, 2), 1, 5, 3, (3, 2), (0, 15), (9, 5, 3, 2)),
    Size("small", 32, 20, (2, 2), 1, 3, 2, (2, 2), (0, 12), (7, 4, 2, 1)),
]

SUM_W, SUM_H = 16, 7                          # sum numbers: 2 bytes x 7 lines
SUM_FIELD = (2, 14)                           # dots of the 16 the number may use


class Bitmap:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0] * w for _ in range(h)]

    def copy(self):
        b = Bitmap(self.w, self.h)
        b.px = [row[:] for row in self.px]
        return b

    def set(self, x, y, v=1):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = v

    def rect(self, x0, y0, x1, y1, v=1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, v)

    def paste(self, pattern, x0, y0, v=1):
        for y, row in enumerate(pattern):
            for x, on in enumerate(row):
                if on:
                    self.set(x0 + x, y0 + y, v)

    def to_bytes(self):
        out = bytearray()
        for row in self.px:
            for b in range(0, self.w, 8):
                bits = 0
                for x in range(b, b + 8):
                    bits = (bits << 1) | (row[x] if x < self.w else 0)
                out.append(bits)
        return bytes(out)


# --- glyphs ----------------------------------------------------------------------

def rom_glyph(sheet, ch):
    code = ord(ch)
    return [[int(sheet.getpixel(((code & 15) * 12 + x, (code >> 4) * 12 + y))[1] != 0)
             for x in range(8)] for y in range(12)]


def scale(pattern, sx, sy):
    return [[v for v in row for _ in range(sx)] for row in pattern for _ in range(sy)]


def digit_glyph(sheet, d):
    """A digit's ROM ink, cropped to the box every digit shares (6x7; the 1 is narrower)."""
    return [row[1:7] for row in rom_glyph(sheet, str(d))[1:8]]


# --- tiles -------------------------------------------------------------------------

def grid_tile(s):
    t = Bitmap(s.w, s.h)
    t.rect(0, 0, s.w - 1, 0)
    t.rect(0, 0, 0, s.h - 1)
    return t


def side(s, x, y):
    """Where a dot lies against the diagonal from (0,0) to (w,h): -1 in the
    upper right triangle, +1 in the lower left one, 0 on the diagonal. The
    diagonal is about three dots wide on every line, so it looks solid."""
    d = x * s.h - y * s.w                     # > 0 right of the diagonal
    if 2 * abs(d) < 3 * s.h:                  # within one and a half dots of it
        return 0
    return -1 if d > 0 else 1


def dithered(x, y):
    return (x + y) % 2 == 0                  # cells are even-sized: the dither runs on across cells


def sparse(x, y):
    """The given digits' background: every fourth dot on every line, shifted by
    two each line (25%), which the CRT shows as an even dim grey."""
    return (x + 2 * y) % 4 == 0


def block_tile(s):
    t = grid_tile(s)
    for y in range(1, s.h):
        for x in range(1, s.w):
            if dithered(x, y):
                t.set(x, y)
    return t


def clue_tile(s, across, down):
    """Diagonal, and dither in the triangle(s) without a sum."""
    t = grid_tile(s)
    for y in range(1, s.h):
        for x in range(1, s.w):
            where = side(s, x, y)
            if where == 0:
                t.set(x, y)
            elif (where < 0 and not across) or (where > 0 and not down):
                if dithered(x, y):
                    t.set(x, y)
    return t


def digit_sprite(sheet, s, d):
    """The digit, DIGIT_W bytes wide, centred on the cell's inside."""
    g = scale(digit_glyph(sheet, d), *s.scale)
    x0 = 1 + (s.w - 1 - len(g[0])) // 2 - s.digit_x * 8
    b = Bitmap(digit_width(s) * 8, len(g))
    b.paste(g, x0, 0)
    return b


def digit_width(s):
    return s.digit_w


def given_tile(sheet, s, d):
    """A given digit: the whole cell inside the grid lines in the sparse
    dither, the digit left dark with a lit contour of one dot around its
    strokes, which makes it readable on so faint a background."""
    left = s.digit_x * 8
    digit = digit_sprite(sheet, s, d)

    def ink(x, y):
        dx, dy = x - left, y - s.digit_y
        return 0 <= dy < digit.h and 0 <= dx < digit.w and digit.px[dy][dx]

    t = grid_tile(s)
    for y in range(1, s.h):
        for x in range(1, s.w):
            if ink(x, y):
                continue
            edge = any(ink(x + i, y + j) for i in (-1, 0, 1) for j in (-1, 0, 1))
            if edge or sparse(x, y):
                t.set(x, y)
    return t


def cursor_overlay(s):
    """Thick corner brackets inside the grid lines."""
    t = Bitmap(s.w, s.h)
    lx, ly, tx, ty = s.bracket
    for cx, sx in ((1, 1), (s.w - 1, -1)):
        for cy, sy in ((1, 1), (s.h - 1, -1)):
            for i in range(lx):
                for d in range(ty):
                    t.set(cx + sx * i, cy + sy * d)
            for i in range(ly + 1):
                for d in range(tx):
                    t.set(cx + sx * d, cy + sy * i)
    return t


def sum_sprite(sheet, n):
    b = Bitmap(SUM_W, SUM_H)
    parts = [digit_glyph(sheet, int(c)) for c in str(n)]
    width = sum(len(p[0]) for p in parts) + (len(parts) - 1)
    x = SUM_FIELD[0] + (SUM_FIELD[1] - SUM_FIELD[0] + 1 - width + 1) // 2
    for p in parts:
        b.paste(p, x, 0)
        x += len(p[0]) + 1
    return b


def check_layout(s):
    """The sums, with a dot of room around them, stay clear of the diagonal and the grid."""
    for (bx, line), where in ((s.sum_a, -1), (s.sum_d, 1)):
        for y in range(line - 1, line + SUM_H + 1):
            for x in range(bx * 8 + SUM_FIELD[0] - 1, bx * 8 + SUM_FIELD[1] + 2):
                assert 1 <= x < s.w and 1 <= y < s.h, (s.name, x, y)
                assert side(s, x, y) == where, (s.name, "a sum touches the diagonal", x, y)
    assert (s.digit_x + s.digit_w) * 8 <= s.w and s.digit_y > 1


# --- output -------------------------------------------------------------------------

def c_bytes(data, indent="    ", per_line=20):
    items = [f"0x{b:02X}" for b in data]
    return ",\n".join(indent + ", ".join(items[i:i + per_line]) for i in range(0, len(items), per_line))


TILES = ["white", "block", "clue_a", "clue_d", "clue_ad"]


def make(sheet, s):
    check_layout(s)
    tiles = {"white": grid_tile(s), "block": block_tile(s), "clue_a": clue_tile(s, True, False),
             "clue_d": clue_tile(s, False, True), "clue_ad": clue_tile(s, True, True)}
    digits = [digit_sprite(sheet, s, d) for d in range(1, 10)]
    givens = [given_tile(sheet, s, d) for d in range(1, 10)]
    return tiles, digits, givens, cursor_overlay(s)


def generate(sheet):
    out = ["/* Generated by tools/gen_sprites.py -- do not edit. */",
           "#ifndef SPRITES_H", "#define SPRITES_H", "",
           "/* Tile order */"]
    for i, name in enumerate(TILES):
        out.append(f"#define TILE_{name.upper()} {i}")
    out += [f"#define TILE_COUNT {len(TILES)}", "",
            f"#define SUM_BYTES {SUM_W // 8}", f"#define SUM_LINES {SUM_H}", ""]
    made = {}
    for k, s in enumerate(SIZES):
        tiles, digits, givens, cur = make(sheet, s)
        made[s.name] = (tiles, digits, givens, cur)
        wb, P = s.w // 8, s.name.upper()
        dw, dh = digit_width(s), len(digits[0].px)
        out += [f"/* {s.name} cells: {s.w}x{s.h} dots ({wb} bytes x {s.h} lines) */",
                f"#define {P}_CW {wb}", f"#define {P}_CH {s.h}",
                f"#define {P}_DIGIT_X {s.digit_x}", f"#define {P}_DIGIT_Y {s.digit_y}",
                f"#define {P}_DIGIT_W {dw}", f"#define {P}_DIGIT_H {dh}",
                f"#define {P}_SUM_A_X {s.sum_a[0]}", f"#define {P}_SUM_A_Y {s.sum_a[1]}",
                f"#define {P}_SUM_D_X {s.sum_d[0]}", f"#define {P}_SUM_D_Y {s.sum_d[1]}",
                f"static const unsigned char tiles_{s.name}[TILE_COUNT][{wb * s.h}] = {{"]
        for name in TILES:
            out.append(f"  {{ /* {name} */\n{c_bytes(tiles[name].to_bytes())} }},")
        out.append("};")
        out.append(f"static const unsigned char digits_{s.name}[9][{dw * dh}] = {{")
        for d, b in enumerate(digits, 1):
            out.append(f"  {{ /* {d} */\n{c_bytes(b.to_bytes())} }},")
        out.append("};")
        out.append(f"static const unsigned char givens_{s.name}[9][{wb * s.h}] = {{")
        for d, b in enumerate(givens, 1):
            out.append(f"  {{ /* {d} */\n{c_bytes(b.to_bytes())} }},")
        out.append("};")
        out.append(f"static const unsigned char cursor_{s.name}[{wb * s.h}] = {{\n{c_bytes(cur.to_bytes())} }};")
        out.append("")
    sums = b"".join(sum_sprite(sheet, n).to_bytes() for n in range(1, 46))
    out.append("/* sums 1-45: SUM_BYTES x SUM_LINES each, sum n at (n - 1) * 14 */")
    out.append(f"static const unsigned char sums[{len(sums)}] = {{\n{c_bytes(sums)} }};")
    out += ["", "#endif", ""]
    (ROOT / "src/sprites.h").write_text("\n".join(out))
    print("wrote src/sprites.h")
    return made


# --- preview ---------------------------------------------------------------------------

def put(img, bitmap, x0, y0, mode="or"):
    for y in range(bitmap.h):
        for x in range(bitmap.w):
            if bitmap.px[y][x]:
                p = (x0 + x, y0 + y)
                img[p] = 0 if (mode == "xor" and img.get(p)) else 1


def preview_board(sheet, s, made, grid, entries, cursor):
    """A board as screen.c composes it: grid is a list of rows ('#' black,
    digits the solution, a leading '!' a given); entries maps cells to
    typed digits."""
    tiles, digits, givens, cur = made
    n = len(grid)
    sol = [[int(c.lstrip("!")) if c != "#" else 0 for c in row] for row in grid]
    img = {}

    def run_len(r, c, dr, dc):
        k = 0
        while 0 <= r < n and 0 <= c < n and sol[r][c]:
            k, r, c = k + 1, r + dr, c + dc
        return k

    def run_sum(r, c, dr, dc):
        t = 0
        while 0 <= r < n and 0 <= c < n and sol[r][c]:
            t, r, c = t + sol[r][c], r + dr, c + dc
        return t

    for r in range(n):
        for c in range(n):
            x0, y0 = c * s.w, r * s.h
            if sol[r][c]:
                put(img, tiles["white"], x0, y0)
                given = grid[r][c].startswith("!")
                d = sol[r][c] if given else entries.get((r, c), 0)
                if given:
                    put(img, givens[d - 1], x0, y0)
                elif d:
                    put(img, digits[d - 1], x0 + s.digit_x * 8, y0 + s.digit_y)
                if (r, c) == cursor:
                    put(img, cur, x0, y0)
                continue
            a = run_len(r, c + 1, 0, 1) >= 2 if c + 1 < n else False
            dn = run_len(r + 1, c, 1, 0) >= 2 if r + 1 < n else False
            put(img, tiles["clue_ad" if a and dn else "clue_a" if a else "clue_d" if dn else "block"], x0, y0)
            for flag, dr, dc, (bx, line) in ((a, 0, 1, s.sum_a), (dn, 1, 0, s.sum_d)):
                if not flag:
                    continue
                put(img, sum_sprite(sheet, run_sum(r + dr, c + dc, dr, dc)), x0 + bx * 8, y0 + line)
    w, h = n * s.w + 1, n * s.h + 1
    for x in range(w):                         # the closing lines
        img[(x, n * s.h)] = 1
    for y in range(h):
        img[(n * s.w, y)] = 1
    out = Image.new("L", (w, h), 0)
    for (x, y), v in img.items():
        if v and x < w and y < h:
            out.putpixel((x, y), 255)
    return out.resize((w * 3, h * 5), Image.NEAREST)


SAMPLE = [  # 006.puz with its two givens and a few entries
    "# # # # # #".split(),
    "# 9 1 # 7 9".split(),
    "# 7 2 1 8 #".split(),
    "# # 8 3 9 !5".split(),
    "# 8 3 # 3 1".split(),
    "# 9 !6 # 5 2".split(),
]


def preview(sheet, made):
    boards = []
    entries = {(1, 1): 9, (2, 3): 1, (3, 2): 8, (4, 4): 3}
    for s in SIZES:
        boards.append(preview_board(sheet, s, made[s.name], SAMPLE, entries, (2, 3)))
    total_w = sum(b.width for b in boards) + 60
    sheet_img = Image.new("RGB", (total_w, max(b.height for b in boards) + 40), (30, 30, 30))
    x = 20
    for b in boards:
        green = Image.merge("RGB", [b.point(lambda v, c=c: c if v else 0) for c in (51, 255, 51)])
        sheet_img.paste(green, (x, 20))
        x += b.width + 20
    (ROOT / "build").mkdir(exist_ok=True)
    sheet_img.save(ROOT / "build/sprites_preview.png")
    print("wrote build/sprites_preview.png")


if __name__ == "__main__":
    font = Image.open(FONT_SHEET).convert("RGB")
    result = generate(font)
    if "--preview" in sys.argv:
        preview(font, {s.name: result[s.name] for s in SIZES})
