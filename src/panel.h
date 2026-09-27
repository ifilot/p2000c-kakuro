/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.h -- the panel on the 64x21 text plane, right of the board. */
#ifndef PANEL_H
#define PANEL_H

extern void draw_panel(void);                      /* static texts and all values */
extern void show_clock(void);                      /* time spent on this puzzle */
extern void show_record(void);                     /* its best time */
extern void show_empty(void);                      /* white cells still empty */
extern void show_combinations(void);               /* for the cursor's row and column (when changed) */
extern void show_note(const char *note);           /* remembered for restore_note() */
extern void restore_note(void);
extern void show_status(const char *status);       /* written last after every update */

/* h:mm:ss (capped at 9:59:59), or -:--:-- for 0 seconds; also for the start screen. */
extern void put_time(unsigned int seconds);

#endif
