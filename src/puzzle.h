/* SPDX-License-Identifier: GPL-3.0-only */
/* puzzle.h -- the puzzle: cells, sums, runs, the solved test and the
 * combinations of a sum.
 *
 * The grid is n x n (6..10) with the sums in row 0 and column 0; cells are
 * row-major (index = row * n + column). A black cell holds the sum of the
 * run of white cells to its right (across) and/or below it (down); a run of
 * a single white cell has no sum in that direction. */
#ifndef PUZZLE_H
#define PUZZLE_H

#define MAX_N     10
#define MAX_CELLS (MAX_N * MAX_N)
#define NO_RUN    0xFF

/* cell[] */
#define CELL_WHITE 0x80
#define CELL_FIXED 0x10                     /* a given digit */
#define CELL_DIGIT 0x0F                     /* the digit shown, 0 = empty */

extern unsigned char n, ncells, stars;
extern unsigned char cell[MAX_CELLS];
extern unsigned char sum_a[MAX_CELLS], sum_d[MAX_CELLS];    /* black cells: the sums (0: none) */
extern unsigned char len_a[MAX_CELLS], len_d[MAX_CELLS];    /* ... and their runs' lengths */
extern unsigned char head_a[MAX_CELLS], head_d[MAX_CELLS];  /* white cells: the black cell holding their sum, or NO_RUN */
extern unsigned char empty;                                 /* white cells without a digit */

/* Loads puzzle k (0-based) with only the given digits filled in. */
extern void puzzle_load(unsigned char k);

/* Every white cell filled, every run's digits different and adding up to its sum? */
extern unsigned char puzzle_solved(void);

/* Writes the combinations of len different digits 1-9 adding up to sum
 * into out as digit strings, each followed by a space, in increasing
 * order, and terminates the text; returns how many there are. */
extern unsigned char combinations(unsigned char sum, unsigned char len, char *out);

#endif
