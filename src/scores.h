/* SPDX-License-Identifier: GPL-3.0-only */
/* scores.h -- the best time per puzzle, kept in KAKURO.DAT on the current drive. */
#ifndef SCORES_H
#define SCORES_H

#include "puzzles.h"

extern unsigned int best_time[PUZZLE_COUNT];    /* seconds; 0 = none yet */

extern void scores_load(void);              /* missing or foreign file: no records */
extern void scores_save(void);

#endif
