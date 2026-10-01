# Changelog

All notable changes to Kakuro for the Philips P2000C are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [semantic versioning](https://semver.org/).

## [1.0.1] - 2026-10-01

### Fixed

- A cursor key moved the cursor two cells (on the board and on the start
  screen). The terminal board, busy with picture data, takes a key that is
  still held down for a new press, and the filter for doubled keys let
  quick keys such as cursor steps through. As in p2000c-chess, the same
  key arriving within a third of a second of the previous one is now
  ignored, whatever key it is; arrival times are noted while the board is
  drawn. A key held longer repeats about three times a second.

### Changed

- `make test` also checks that `dd` sent at once on the start screen, and
  `ss` in a puzzle, count as one step, and that the same key a moment
  later counts again.

## [1.0.0] - 2026-09-27

### Added

- Kakuro for the Philips P2000C under CP/M, in Dutch, in the 512x252
  high-resolution graphics mode, with the panel on the text plane.
- 95 puzzles from cx16-kakuro, from 6x6 to 10x10 in seven groups of size
  and difficulty; puzzle 031 is left out (a digit twice in a run), and
  given digits were added to 31 puzzles so that each has exactly one
  solution.
- A start screen listing all puzzles, chosen with the cursor keys, with the
  size, difficulty and best time of the chosen one; puzzles with a best
  time are underlined.
- Hatched sum cells with a diagonal, large digits, given digits dark with a
  lit contour in a cell with a 25% dither (sparser than the 50% of the
  hatched cells); 40x24-dot cells up to 9x9 and 32x20-dot cells for 10x10.
- The cursor moves to the nearest white cell in a direction; `TAB` jumps to
  the next empty cell.
- The combinations of digits for the sums of the cursor's row and column in
  the panel.
- The best time per puzzle, kept in `KAKURO.DAT`; game clock, help screen,
  confirmation when leaving a puzzle in progress, and a screen saver after
  five minutes whose waking key does nothing else.
- Headless-emulator tests that solve a puzzle of every size and difficulty
  and check the picture dot for dot, screenshot and benchmark tools, and
  `make deploy` for a ZuluBlaster/SASI disk image.

[1.0.1]: https://github.com/ifilot/p2000c-kakuro/releases/tag/v1.0.1
[1.0.0]: https://github.com/ifilot/p2000c-kakuro/releases/tag/v1.0.0
