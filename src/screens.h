/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.h -- the text-mode screens: start (choosing a puzzle) and help. */
#ifndef SCREENS_H
#define SCREENS_H

extern void text_clear(void);               /* clear the text screen, hide the cursor */
extern unsigned char start_screen(void);    /* 1: play `puzzle`, 0: quit */
extern void help_screen(void);              /* rules page from the game; restores the board */

#endif
