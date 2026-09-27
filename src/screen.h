/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.h -- the board picture: composition in the framebuffer and uploads.
 *
 * Grids up to 9x9 use 40x24-dot cells, the 10x10 grids 32x20-dot cells
 * (dots have a 3:5 pitch on the CRT, so both are about square). The board
 * is centred in the 368 dots left of the panel. */
#ifndef SCREEN_H
#define SCREEN_H

#define BOARD_BYTES 46                      /* the panel starts at text column 46 */

/* Selects the cell size and the board's place for the loaded puzzle. */
extern void screen_setup(void);

/* Composes the whole board in RAM. */
extern void screen_compose(void);

/* Sends the composed picture after ESC 3: the grid as lines, everything
 * else as uploads of what the lines lack. */
extern void screen_flush(void);

/* Redraws the cells whose appearance changed and sends the bytes that differ. */
extern void screen_sync(void);

#endif
