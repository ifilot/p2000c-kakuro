#!/usr/bin/env python3
"""Regression tests: puzzles solved in the headless emulator.

For a puzzle of every size and difficulty it checks:

- the puzzle in memory: size, cells, givens and every sum against the
  model built from src/puzzles.c;
- the panel: number, empty cells, and the combinations listed for the
  cursor's row and column;
- navigation: every cursor step goes where the model says (the nearest
  white cell straight ahead, else in the quarter plane before it), and a
  digit typed on a given cell changes nothing;
- a whole puzzle filled in with TAB and the digits: "Opgelost!", a new
  record, the record on the start screen;
- the picture: at the start, midway, after the help page and when solved,
  the terminal's graphics RAM equals the program's framebuffer dot for dot.

And once: a wrong last digit gives "Nog niet goed...", leaving a puzzle in
progress asks for confirmation, N moves on to the next puzzle, the start
screen marks the chosen puzzle in inverse video, the best time survives a
restart (KAKURO.DAT), a key that arrives twice at once counts once, on
the start screen and in the game, the screen saver starts after five minutes, the key
that ends it changes nothing, and the game clock keeps counting through a
screen saver of more than 18 minutes.

Run after `make build`.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import (COM, PLAYING, ROOT, SOLVED, START, act, choose, combinations, fill, make_image,
                    puzzles, run)

TESTED = [0, 12, 33, 45, 62, 79, 92]        # 6x6, 7x7, 7x7***, 8x8, 8x8***, 9x9, 10x10
SETTLE = "20000000"                         # cycles for the terminal to finish a screen after a match
PUZZLES = puzzles()


def screen_text(state):
    """The text screen as lines: 80 columns, or the 64 of the text plane in graphics mode."""
    flat = "".join(state["screen"])
    width = 80 if state["graphics_mode"] == "character" else 64
    return "\n".join(flat[i:i + width] for i in range(0, len(flat), width))


def panel(state):
    """The panel's 18 columns on each text row."""
    return [row[46:64].rstrip() for row in screen_text(state).split("\n")[:21]]


def same_picture(what, actions, errors):
    """Graphics RAM of the terminal against the framebuffer in RAM."""
    state, graphics, memory = run(actions, dump_fb=True)
    if state["status"] != "ok":
        errors.append(f"{what}: {state['status']} {state.get('message')}")
        return
    fb = memory["_framebuffer"]
    diff = [i for i in range(len(fb)) if fb[i] != graphics[i]]
    if diff:
        line, col = divmod(diff[0], 64)
        errors.append(f"{what}: {len(diff)} bytes differ on screen, first at line {line} byte {col}")


def step(p, cur, key):
    """Where the cursor goes with a direction key (game.c move_dir), or None."""
    n = p.n
    r, c = divmod(cur, n)
    for j in range(2 * n + 1):
        o = (j + 1) // 2 if j & 1 else -(j // 2)
        for k in range(1, n):
            if abs(o) > k:
                continue
            rr, cc = {"w": (r - k, c + o), "s": (r + k, c + o), "a": (r + o, c - k), "d": (r + o, c + k)}[key]
            if 0 <= rr < n and 0 <= cc < n and p.solution[rr * n + cc]:
                return rr * n + cc
    return None


def expected_combinations(p, cur):
    """The panel's lines for the cursor's row and column, as the game wraps them."""
    lines = []
    for label, step_, sums in (("Rij:   ", 1, p.across), ("Kolom: ", p.n, p.down)):
        head = p.head(cur, step_)
        if head is None:
            lines += [label + "-"] + [""] * 4
            continue
        total, length = sums[head]
        lines.append(f"{label}{total:2} in {length}:")
        words, rows, line = combinations(total, length), [], ""
        for w in words:
            if line and len(line) + 1 + len(w) > 18:
                rows.append(line)
                line = w
            else:
                line = f"{line} {w}" if line else w
        rows.append(line)
        lines += (rows + [""] * 4)[:4]
    return lines


def check_puzzle(k):
    p = PUZZLES[k]
    n = p.n
    errors = []
    start = choose(k)
    cells = n * n
    dump = {"_n": 1, "_cell": cells, "_sum_a": cells, "_sum_d": cells, "_cur": 1, "_state": 1, "_empty": 1}

    # the puzzle in memory, the panel
    state, _, mem = run(start, dump)
    if mem["_n"][0] != n:
        errors.append(f"n = {mem['_n'][0]}, not {n}")
    for i in range(cells):
        v = mem["_cell"][i]
        want = 0 if not p.solution[i] else 0x80 | ((0x10 | p.solution[i]) if i in p.given else 0)
        if v != want:
            errors.append(f"cell {divmod(i, n)} = {v:02X}, not {want:02X}")
        a = p.across.get(i, (0, 0))[0]
        d = p.down.get(i, (0, 0))[0]
        if mem["_sum_a"][i] != a or mem["_sum_d"][i] != d:
            errors.append(f"sums at {divmod(i, n)}: {mem['_sum_a'][i]}/{mem['_sum_d'][i]}, not {a}/{d}")
    todo = [i for i in p.white() if i not in p.given]
    cur = mem["_cur"][0]
    if cur != todo[0]:
        errors.append(f"cursor starts at {cur}, not {todo[0]}")
    rows = panel(state)
    if rows[1] != f"Puzzel {k + 1} van 95":
        errors.append(f"panel: '{rows[1]}'")
    if rows[5] != f"Lege vakjes{len(todo):7}":
        errors.append(f"panel: '{rows[5]}'")
    if rows[7:17] != expected_combinations(p, cur):
        errors.append(f"combinations {rows[7:17]} != {expected_combinations(p, cur)}")

    # cursor steps, and a digit on a given cell
    moves, where = [], cur
    for key in "ddsdssaawwdsdsas":
        nxt = step(p, where, key)
        if nxt is not None and nxt != where:
            moves.append((key, nxt))
            where = nxt
    actions = start[:]
    for key, _ in moves:
        actions += act(key)
    state, _, mem = run(actions, dump)
    if mem["_cur"][0] != where:
        errors.append(f"after {''.join(m[0] for m in moves)} the cursor is at {mem['_cur'][0]}, not {where}")
    if panel(state)[7:17] != expected_combinations(p, where):
        errors.append(f"combinations after moving: {panel(state)[7:17]}")
    given = sorted(p.given)[0]
    path, where = [], cur
    for _ in range(40):                     # walk towards the given cell
        if where == given:
            break
        r, c = divmod(where, n)
        gr, gc = divmod(given, n)
        key = "s" if gr > r else "w" if gr < r else "d" if gc > c else "a"
        nxt = step(p, where, key)
        if nxt is None:
            break
        path.append(key)
        where = nxt
    if where == given:
        actions = start[:]
        for key in path:
            actions += act(key)
        actions += ["--run", "1000000", "--send", str(p.solution[given] % 9 + 1), "--run", "3000000"]
        _, _, mem = run(actions, dump)
        if mem["_cell"][given] != 0x90 | p.solution[given]:
            errors.append("a digit typed on a given cell changed it")

    # a whole puzzle, and the picture along the way
    half = fill(p)[:len(fill(p)) // 2]
    whole = start + fill(p)
    state, _, mem = run(whole, dump)
    text = screen_text(state)
    if state["status"] != "ok" or mem["_state"][0] != 1:
        errors.append(f"not solved: {state['status']} {state.get('message')}")
    if "Nieuw record!" not in text or "Lege vakjes      0" not in text:
        errors.append("panel after solving lacks the record or the count")
    state, _, _ = run(whole + ["--send", "\\x1b", "--wait-for", START, "--run", SETTLE])
    if f"Puzzel {k + 1:2}:" not in screen_text(state) or "beste tijd 0:" not in screen_text(state):
        errors.append("the start screen does not show the new best time")
    same_picture("fresh board", start, errors)
    same_picture("halfway", start + half, errors)
    same_picture("after the help page", start + half + ["--run", "1000000", "--send", "h", "--wait-for", "SPELREGELS",
                                                         "--send", " ", "--wait-for", PLAYING], errors)
    same_picture("solved", whole, errors)

    print(f"puzzle {k + 1} ({n}x{n}, {'*' * p.stars}): {len(todo)} cells, {len(moves)} steps, "
          f"{'ok' if not errors else 'FAILED'}")
    for e in errors[:20]:
        print("   ", e)
    return not errors


def check_once():
    errors = []
    p = PUZZLES[0]
    todo = [i for i in p.white() if i not in p.given]
    wrong = {i: p.solution[i] for i in todo}
    wrong[todo[-1]] = p.solution[todo[-1]] % 9 + 1
    state, _, _ = run(choose(0) + fill(p, wrong, PLAYING))
    if "Nog niet goed" not in screen_text(state):
        errors.append("a wrong filling was not reported")

    # leaving a puzzle in progress, and N after solving
    base = choose(0) + act(str(p.solution[todo[0]]))
    state, _, _ = run(base + ["--run", "1000000", "--send", "\\x1b", "--wait-for", "Opgeven? J/N",
                              "--send", "n", "--wait-for", PLAYING])
    if state["status"] != "ok" or state["graphics_mode"] != "high-512":
        errors.append("N did not keep the puzzle")
    state, _, _ = run(base + ["--run", "1000000", "--send", "\\x1b", "--wait-for", "Opgeven? J/N",
                              "--send", "j", "--wait-for", START])
    if state["status"] != "ok":
        errors.append("J did not leave the puzzle")
    state, _, _ = run(choose(0) + fill(p) + ["--run", "1000000", "--send", "n", "--wait-for", "Puzzel 2 van 95",
                                             "--wait-for", PLAYING])
    if state["status"] != "ok":
        errors.append("N after solving did not open the next puzzle")

    # the chosen puzzle in inverse video (ESC 0 with bit 4 before its number)
    trace = ROOT / "build/trace.bin"
    run(["--wait-for", START, "--run", "300000", "--send", "d", "--wait-for", "Puzzel  2:"], trace=trace)
    if b"\x1b0\x50 2" not in trace.read_bytes():
        errors.append("the chosen puzzle is not shown in inverse video")

    # doubled keys: "dd" at once is one step, a "d" a moment later another
    state, _, _ = run(["--wait-for", START, "--run", "300000", "--send", "dd", "--run", "1600000",
                       "--send", "d", "--wait-for", "Puzzel  3:", "--run", SETTLE])
    if "Puzzel  3:" not in screen_text(state):
        errors.append("start screen: \"dd\" at once did not count as one step")
    where = step(p, step(p, todo[0], "s"), "s")       # a third step would go further
    _, _, mem = run(choose(0) + ["--run", "1600000", "--send", "ss", "--run", "1600000", "--send", "s",
                                 "--run", "4000000"], {"_cur": 1})
    if mem["_cur"][0] != where:
        errors.append(f"game: after \"ss\" and \"s\" the cursor is at {mem['_cur'][0]}, not {where}")

    # the best time survives a restart
    state, _, _ = run(choose(0) + fill(p) + ["--run", "1000000", "--send", "\\x1b", "--wait-for", START,
                                             "--send", "q", "--wait-for", "A>", "--send", "F:KAKURO\\r",
                                             "--wait-for", START, "--run", SETTLE])
    if "beste tijd 0:" not in screen_text(state):
        errors.append("the best time was not kept in KAKURO.DAT")

    # the screen saver, and the key that ends it
    dump = {"_cell": p.n * p.n}
    saver = choose(0) + ["--wait-cycles", "2000000000", "--wait-for", "Philips P2000C"]
    state, _, _ = run(saver)
    if state["status"] != "ok" or state["graphics_mode"] != "character":
        errors.append(f"no screen saver after five minutes: {state['status']}")
    state, _, mem = run(saver + ["--send", "5", "--wait-for", PLAYING, "--run", "3000000"], dump)
    if state["graphics_mode"] != "high-512" or mem["_cell"][todo[0]] != 0x80:
        errors.append("the key that ended the screen saver reached the game")
    same_picture("after the screen saver", saver + ["--send", " ", "--wait-for", PLAYING], errors)

    # the game clock counts on during a long screen saver (its tick counter wraps after 18 minutes)
    state, _, _ = run(saver + ["--run", str(4_000_000 * 60 * 20), "--send", " ", "--wait-for", PLAYING,
                               "--run", "8000000"])
    clock = next((row[10:].strip() for row in panel(state) if row.startswith("Tijd")), "")
    if not clock.startswith("0:2"):
        errors.append(f"after 25 idle minutes the clock reads {clock!r}")

    print(f"once: {'ok' if not errors else 'FAILED'}")
    for e in errors:
        print("   ", e)
    return not errors


def main():
    make_image(COM, ROOT / "build")
    results = [check_puzzle(k) for k in TESTED] + [check_once()]
    print("PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
