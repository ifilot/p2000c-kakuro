#!/usr/bin/env python3
"""Bytes over the terminal link per action, for a puzzle of each cell size.

The emulator models neither the 19200-baud link (about 1920 bytes/s) nor
the terminal board drawing lines dot by dot, so both are estimated here:
the bytes sent (from the emulator's terminal trace) and the dots of the
grid lines, which the terminal draws for a new board.

  python3 tools/bench.py        (after make build)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import COM, ROOT, START, act, choose, make_image, puzzles, run

LINK = 1920                                  # bytes per second


def sent(actions, trace):
    run(actions, trace=trace)
    return len(trace.read_bytes())


def main():
    make_image(COM, ROOT / "build")
    trace = ROOT / "build/trace.bin"
    menu = sent(["--wait-for", START], trace)
    for k in (0, 12, 45, 75, 92):
        p = puzzles()[k]
        todo = [i for i in p.white() if i not in p.given]
        base = choose(k)
        board = sent(base, trace)
        cw, ch = (40, 24) if p.n < 10 else (32, 20)
        dots = (p.n + 1) * p.n * (cw + ch)
        step = sent(base + act("d"), trace) - board
        down = sent(base + act("s"), trace) - board
        digit = sent(base + act(str(p.solution[todo[0]])), trace) - board
        print(f"puzzle {k + 1:2} ({p.n}x{p.n}): board {board - menu:5} bytes ({(board - menu) / LINK:.1f} s) "
              f"and {dots} dots of lines; step right {step} B, down {down} B, digit {digit} B")


if __name__ == "__main__":
    main()
