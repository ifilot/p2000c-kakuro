#!/usr/bin/env python3
"""Run build/KAKURO.COM in the headless P2000C emulator and save PNGs.

Usage: python3 tools/render.py                       (the default screenshots)
       python3 tools/render.py --wait-for TEXT [--out FILE.png] [-- ACTIONS]

The PNG is a plain dump of the terminal's dot raster: graphics RAM merged
with the text plane (rendered with the character-ROM glyphs from the sibling
p2000c-emulator font sheet), one dot per 3x5 block, green on black, no CRT
effects. Both rasters are placed on the 640x288-dot text canvas the way the
terminal does it, so text-mode and graphics-mode screenshots have the same
size. Requires the sibling p2000c-zulublaster-sasi-drive checkout (headless
emulator, disk tool, dist/pro/ images; or the former p2000c-cpm-disk-tool)
and the p2000c-emulator checkout (font sheet).

The helpers below also serve tools/test_game.py: the puzzles as the game
has them (parsed from src/puzzles.c), and emulator actions that choose a
puzzle on the start screen and fill in cells.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent


def find_disk_tool() -> Path:
    """The disk-image/emulator checkout (as p2000c-chess finds it)."""
    override = os.environ.get("P2000C_DISK_TOOL")
    candidates = ([Path(override)] if override else []) + [
        ROOT.parent / "p2000c-cpm-disk-tool",
        ROOT.parent / "p2000c-zulublaster-sasi-drive",
    ]
    for candidate in candidates:
        if (candidate / "src/p2000c_disk/cli.py").is_file() and \
           (candidate / "dist/pro/HD0_256.hda").is_file() and \
           (candidate / "dist/pro/HD1_256.hda").is_file():
            return candidate
    raise FileNotFoundError("P2000C disk tools/images not found; looked in "
                            + ", ".join(str(c) for c in candidates))


TOOL = find_disk_tool()
EMULATOR = TOOL / "build/emulator/p2000c-mini"
IPL = TOOL / "tools/emulator/firmware/IPLDUMP.BIN"
HD0 = TOOL / "dist/pro/HD0_256.hda"
HD1 = TOOL / "dist/pro/HD1_256.hda"
COM = ROOT / "build/KAKURO.COM"

WIDTH, HEIGHT, BYTES_PER_LINE = 512, 252, 64
TEXT_RASTER = (640, 288)            # 80x24 by 8x12 dots; sets the dot pitch
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"

DOT_PITCH = (3, 5)                  # horizontal:vertical dot pitch on the 4:3 CRT
FOREGROUND = (51, 255, 51)          # plain phosphor green
BACKGROUND = (0, 0, 0)

START = "terug naar CP/M"           # the start screen's last line
PLAYING = "Vul alle vakjes in"      # the status line while a puzzle is open
SOLVED = "Opgelost!"
CELL_WHITE, CELL_FIXED, CELL_DIGIT = 0x80, 0x10, 0x0F


def make_image(com: Path, out_dir: Path) -> Path:
    """Copy the F: image and drop KAKURO.COM in its high partition (F:)."""
    image = out_dir / "hd1.hda"
    shutil.copy(HD1, image)
    cli = ["python3", "-m", "p2000c_disk.cli", "put", str(image), str(com),
           "--partition", "high", "--replace"]
    subprocess.run(cli, cwd=TOOL, env={"PYTHONPATH": "src", "PATH": "/usr/bin:/bin"},
                   check=True, capture_output=True)
    return image


def symbol(name: str) -> int:
    text = (ROOT / "build/kakuro.map").read_text()
    m = re.search(rf"^{name}\s+= \$([0-9A-F]+)", text, re.M)
    return int(m.group(1), 16)


def run(actions: list[str], dump: dict[str, int] | None = None, dump_fb: bool = False,
        trace: Path | None = None) -> tuple[dict, bytes, dict[str, bytes]]:
    """One emulator session; returns the state, graphics RAM and the memory
    asked for in dump (symbol -> length), plus '_framebuffer' with dump_fb."""
    out_dir = ROOT / "build"
    graphics = out_dir / "graphics.bin"
    wanted = dict(dump or {})
    if dump_fb:
        wanted["_framebuffer"] = 16128
    extra = []
    for name, length in wanted.items():
        extra += ["--dump-memory", f"{symbol(name)}:{length}"]
    if trace:
        trace.unlink(missing_ok=True)
        extra += ["--trace-terminal", str(trace)]
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0),
           "--hard-disk-1", str(out_dir / "hd1.hda"), "--fast-storage", "--wait-cycles", "150000000",
           "--chunk-cycles", "5000",
           "--wait-for", "A>", "--send", "F:KAKURO\\r", *actions, "--dump-graphics", str(graphics),
           *extra, "--output", "json"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    state = json.loads(result.stdout)
    if result.returncode != 0:
        print(f"emulator exit {result.returncode}: {state.get('message')}", file=sys.stderr)
    memory = [bytes.fromhex(m["bytes"].replace(" ", "")) for m in state.get("memory", [])]
    return state, graphics.read_bytes(), dict(zip(wanted, memory))


# --- the puzzles -------------------------------------------------------------------

class Puzzle:
    """A puzzle as the game has it: grid size, stars, solution, givens, sums."""
    def __init__(self, n: int, stars: int, cells: list[int]):
        self.n, self.stars = n, stars
        self.solution = [c & CELL_DIGIT for c in cells]
        self.given = {i for i, c in enumerate(cells) if c & 0x10}
        self.across: dict[int, tuple[int, int]] = {}     # black cell -> (sum, length)
        self.down: dict[int, tuple[int, int]] = {}
        for i, v in enumerate(self.solution):
            if v:
                continue
            for step, sums in ((1, self.across), (n, self.down)):
                run, j = [], i + step
                while j < n * n and self.solution[j] and (step == n or j // n == i // n):
                    run.append(j)
                    j += step
                if len(run) >= 2:
                    sums[i] = (sum(self.solution[k] for k in run), len(run))

    def white(self) -> list[int]:
        return [i for i, v in enumerate(self.solution) if v]

    def head(self, i: int, step: int) -> int | None:
        """The black cell holding cell i's sum across (step 1) or down (step n)."""
        while self.solution[i]:
            i -= step
        return i if i in (self.across if step == 1 else self.down) else None


def puzzles() -> list[Puzzle]:
    text = (ROOT / "src/puzzles.c").read_text()
    offsets = [int(x) for x in re.search(r"puzzle_offset\[PUZZLE_COUNT\] = \{(.*?)\};", text, re.S)
               .group(1).replace("\n", "").split(",")]
    data = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", text.split("puzzle_data[")[1])]
    result = []
    for o in offsets:
        n = data[o]
        result.append(Puzzle(n, data[o + 1], data[o + 2:o + 2 + n * n]))
    return result


def groups() -> list[tuple[int, int]]:
    """(first puzzle, count) per row of the start screen."""
    text = (ROOT / "src/puzzles.c").read_text()
    first = [int(x) for x in re.search(r"group_first\[GROUP_COUNT\] = \{(.*?)\}", text).group(1).split(",")]
    count = [int(x) for x in re.search(r"group_count\[GROUP_COUNT\] = \{(.*?)\}", text).group(1).split(",")]
    return list(zip(first, count))


def combinations(total: int, length: int) -> list[str]:
    from itertools import combinations as choose
    return ["".join(map(str, c)) for c in choose(range(1, 10), length) if sum(c) == total]


# --- actions ---------------------------------------------------------------------

def act(keys: str, until: str = PLAYING) -> list[str]:
    """A board action: wait until it has been processed (the status line
    reads 'Bezig...' while the board is updated, then the result). A pause
    comes first, as the game drops a key that repeats the previous one
    shortly after a long update."""
    return ["--run", "1000000", "--send", keys, "--wait-for", "Bezig", "--wait-for", until]


def choose(k: int) -> list[str]:
    """From the start screen (first puzzle chosen) into puzzle k (0-based):
    down to its row with 's', along it with 'd', then RETURN."""
    actions = ["--wait-for", START]
    row = next(g for g, (first, count) in enumerate(groups()) if first <= k < first + count)
    first = groups()[row][0]
    keys = "s" * row + "d" * (k - first)
    for key in keys:
        actions += ["--run", "300000", "--send", key]
    return actions + ["--wait-for", f"Puzzel {k + 1:2}:", "--send", "\\r", "--wait-for", PLAYING]


def fill(p: Puzzle, digits: dict[int, int] | None = None, until_last: str = SOLVED) -> list[str]:
    """Types the digits (default: the solution) into every empty cell,
    moving with TAB from the first one in reading order, where the cursor
    starts."""
    todo = [i for i in p.white() if i not in p.given]
    digits = digits or {i: p.solution[i] for i in todo}
    actions = []
    for k, i in enumerate(todo):
        last = k == len(todo) - 1
        actions += act(str(digits[i]), until_last if last else PLAYING)
        if not last:
            actions += act("\\t")
    return actions


# --- raster: what the terminal board puts on the tube ------------------------

def raster_dots(state: dict, graphics: bytes) -> tuple[list[list[int]], int, int]:
    """Per-dot level (0 off, 2 on) for the active raster.

    Mirrors DisplayWidget::rebuild_raster: in high-res mode the 64x21 text
    plane is merged into the 512x252 raster; a character dot on a lit pixel
    goes dark.
    """
    mode = state["graphics_mode"]
    sheet = Image.open(FONT_SHEET).convert("RGB")
    flat = "".join(state["screen"])
    if mode == "character":
        width, height, columns = TEXT_RASTER[0], TEXT_RASTER[1], 80
    else:
        width, height, columns = WIDTH, HEIGHT, 64
    rows = []
    for y in range(height):
        row = [0] * width
        gline = graphics[y * BYTES_PER_LINE:(y + 1) * BYTES_PER_LINE]
        for x in range(width):
            code = ord(flat[(y // 12) * columns + x // 8]) & 0xFF
            char_dot = sheet.getpixel(((code & 15) * 12 + x % 8, (code >> 4) * 12 + y % 12))[1] != 0
            if mode == "character":
                level = 2 if char_dot else 0
            else:
                lit = gline[x // 8] & (0x80 >> (x & 7))
                level = 0 if (lit and char_dot) else 2 if (lit or char_dot) else 0
            row[x] = level
        rows.append(row)
    return rows, width, height


def compose(state: dict, graphics: bytes) -> Image.Image:
    """Plain render: one raster dot -> a 3x5 green block (the CRT dot pitch)."""
    dots, rw, rh = raster_dots(state, graphics)
    raster = Image.new("L", (rw, rh), 0)
    raster.putdata([255 if level else 0 for row in dots for level in row])
    canvas = Image.new("L", TEXT_RASTER, 0)
    canvas.paste(raster, ((TEXT_RASTER[0] - rw) // 2, (TEXT_RASTER[1] - rh) // 2))
    canvas = canvas.resize((TEXT_RASTER[0] * DOT_PITCH[0], TEXT_RASTER[1] * DOT_PITCH[1]), Image.NEAREST)
    return Image.merge("RGB", [canvas.point(lambda v, c=c: c if v else b) for c, b in zip(FOREGROUND, BACKGROUND)])


# --- default screenshots --------------------------------------------------------

def partly(k: int, cells: int, moves: str = "") -> list[str]:
    """Puzzle k with its first few empty cells filled in, then some cursor keys."""
    p = puzzles()[k]
    todo = [i for i in p.white() if i not in p.given][:cells]
    actions = choose(k)
    for i in todo:
        actions += act(str(p.solution[i])) + act("\\t")
    for key in moves:
        actions += act(key)
    return actions


def defaults() -> list[tuple[str, list[str]]]:
    solved = puzzles()[0]
    return [
        ("start.png", ["--wait-for", START, "--run", "300000", "--send", "s", "--run", "300000",
                       "--send", "d", "--wait-for", "Puzzel 12:"]),
        ("help.png", ["--wait-for", START, "--send", "h", "--wait-for", "terug te keren"]),
        ("puzzle7x7.png", partly(12, 6, "s")),
        ("puzzle9x9.png", partly(75, 10, "d")),
        ("puzzle10x10.png", partly(92, 8)),
        ("solved.png", choose(0) + fill(solved)),
    ]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wait-for", help="screen text to wait for")
    parser.add_argument("--out", type=Path, default=ROOT / "build/screen.png")
    parser.add_argument("--only", help="one of the default screenshots, e.g. start.png")
    parser.add_argument("extra", nargs="*", help="additional emulator actions")
    args = parser.parse_args()
    out_dir = ROOT / "build"
    out_dir.mkdir(exist_ok=True)
    make_image(COM, out_dir)
    if args.wait_for:
        jobs = [(args.out, ["--wait-for", args.wait_for, *args.extra])]
    else:
        jobs = [(out_dir / name, actions) for name, actions in defaults() if not args.only or name == args.only]
    for out, actions in jobs:
        state, graphics, _ = run(actions)
        print(f"{out.name}: status={state['status']} mode={state['graphics_mode']} cycles={state['cycles']:,}")
        compose(state, graphics).save(out)


if __name__ == "__main__":
    main()
