/* SPDX-License-Identifier: GPL-3.0-only */
/* puzzle.c -- loading a puzzle, its sums and runs, and the rules.
 *
 * The puzzles come from puzzles.h (tools/gen_puzzles.py): per puzzle the
 * grid size, the difficulty, and the solution with the given digits
 * marked. The sums are worked out from the solution when a puzzle is
 * loaded; after that the solution is not needed, because the puzzle is
 * solved as soon as every run obeys the rules (the generator made sure that
 * only one filling does).
 */
#include "puzzle.h"
#include "puzzles.h"

unsigned char n, ncells, stars;
unsigned char cell[MAX_CELLS];
unsigned char sum_a[MAX_CELLS], sum_d[MAX_CELLS];
unsigned char len_a[MAX_CELLS], len_d[MAX_CELLS];
unsigned char head_a[MAX_CELLS], head_d[MAX_CELLS];
unsigned char empty;

/* Walks the run that starts after black cell `head` in steps of `step`
 * (1 across, n down): gives every cell its head and returns the sum and,
 * in *length, the number of cells. */
static unsigned char walk(const unsigned char *solution, unsigned char head, unsigned char step,
                          unsigned char *heads, unsigned char *length)
{
    unsigned char i = head + step, k = 0, total = 0, last = step == 1 ? head - head % n + n : ncells;
    while (i < last && solution[i]) {
        total += solution[i] & CELL_DIGIT;
        heads[i] = head;
        i += step;
        k++;
    }
    *length = k;
    return total;
}

void puzzle_load(unsigned char k)
{
    const unsigned char *p = puzzle_data + puzzle_offset[k];
    const unsigned char *solution;
    unsigned char i, v;

    n = p[0];
    stars = p[1];
    solution = p + 2;
    ncells = n * n;
    empty = 0;
    for (i = 0; i < ncells; i++) {
        v = solution[i];
        cell[i] = 0;
        sum_a[i] = sum_d[i] = len_a[i] = len_d[i] = 0;
        head_a[i] = head_d[i] = NO_RUN;
        if (!v)
            continue;
        if (v & CELL_GIVEN)
            cell[i] = CELL_WHITE | CELL_FIXED | (v & CELL_DIGIT);
        else {
            cell[i] = CELL_WHITE;
            empty++;
        }
    }
    for (i = 0; i < ncells; i++) {
        if (solution[i])
            continue;
        v = walk(solution, i, 1, head_a, &len_a[i]);
        if (len_a[i] >= 2)
            sum_a[i] = v;
        else if (len_a[i])
            head_a[i + 1] = NO_RUN;          /* a lone cell: no sum across */
        v = walk(solution, i, n, head_d, &len_d[i]);
        if (len_d[i] >= 2)
            sum_d[i] = v;
        else if (len_d[i])
            head_d[i + n] = NO_RUN;
    }
}

/* Does the run after `head` hold different digits adding up to `sum`? */
static unsigned char run_ok(unsigned char head, unsigned char step, unsigned char length, unsigned char sum)
{
    unsigned int seen = 0, bit;
    unsigned char total = 0, i = head;
    while (length--) {
        i += step;
        bit = 1u << (cell[i] & CELL_DIGIT);
        if (seen & bit)
            return 0;
        seen |= bit;
        total += cell[i] & CELL_DIGIT;
    }
    return total == sum;
}

unsigned char puzzle_solved(void)
{
    unsigned char i;
    if (empty)
        return 0;
    for (i = 0; i < ncells; i++) {
        if (sum_a[i] && !run_ok(i, 1, len_a[i], sum_a[i]))
            return 0;
        if (sum_d[i] && !run_ok(i, n, len_d[i], sum_d[i]))
            return 0;
    }
    return 1;
}

/* Depth-first in increasing order: digits from `from` on, `len` still to
 * choose, `sum` still to reach; prefix holds the digits chosen so far. */
static char prefix[10];
static unsigned char depth, found;
static char *write;

static void choose(unsigned char from, unsigned char len, unsigned char sum)
{
    unsigned char d, i;
    if (len == 0) {
        if (sum)
            return;
        for (i = 0; i < depth; i++)
            *write++ = prefix[i];
        *write++ = ' ';
        found++;
        return;
    }
    for (d = from; d <= 9 && d <= sum; d++) {
        prefix[depth++] = '0' + d;
        choose(d + 1, len - 1, sum - d);
        depth--;
    }
}

unsigned char combinations(unsigned char sum, unsigned char len, char *out)
{
    write = out;
    depth = found = 0;
    choose(1, len, sum);
    *write = '\0';
    return found;
}
