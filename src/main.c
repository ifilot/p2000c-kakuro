/* SPDX-License-Identifier: GPL-3.0-only */
/* Kakuro voor de Philips P2000C -- the number crossword, in Dutch.
 *
 * Start screen (choose one of the puzzles, help, quit), then puzzles until
 * the player leaves. Modules:
 *   game.c    state, keys, course of a puzzle    screen.c  board picture and uploads
 *   panel.c   text panel with the combinations   screens.c start and help screens
 *   puzzle.c  puzzles, sums, rules               scores.c  best times (KAKURO.DAT)
 *   saver.c   key waits with the CRT screen saver   clock.c   game clock (BIOS ticks)
 *   video.asm framebuffer primitives, ESC r uploads, BIOS console I/O, BDOS
 *
 * The board uses the terminal's 512x252 high-resolution mode with the 64x21
 * text plane for the panel; the other screens use the 80x24 text mode.
 */
#include "video.h"
#include "game.h"
#include "screens.h"
#include "scores.h"
#include "clock.h"

#define ESC 27

int main(void)
{
    conout(ESC); conout('c');                /* no blinking text cursor */
    clock_probe();
    scores_load();
    while (start_screen()) {
        if (play() == PLAY_QUIT)
            break;
        video_text();
    }
    video_text();
    text_clear();
    conout(ESC); conout('C');                /* CP/M gets its cursor back */
    return 0;
}
