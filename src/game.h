/* SPDX-License-Identifier: GPL-3.0-only */
/* game.h -- game state shared by the modules, and the game flow. */
#ifndef GAME_H
#define GAME_H

/* state */
#define PLAYING 0
#define SOLVED  1

/* play() results */
#define PLAY_QUIT 0                          /* back to CP/M */
#define PLAY_MENU 1                          /* back to the start screen */

extern unsigned char puzzle;                 /* 0-based; also the start screen's choice */
extern unsigned char state;
extern unsigned char cur;                    /* cursor: a white cell */

/* Puzzles from `puzzle` on until the player leaves (ESC or Q). */
extern unsigned char play(void);

/* Restores the game screen after a text-mode interlude (help, screen saver). */
extern void redraw_game_screen(void);

#endif
