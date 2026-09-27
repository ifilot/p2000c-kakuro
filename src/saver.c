/* SPDX-License-Identifier: GPL-3.0-only */
/* saver.c -- CRT screen saver.
 *
 * Idle time is taken from the BIOS 60 Hz tick counter when clock_probe()
 * found one; otherwise it is estimated by counting keyboard polls, with
 * POLLS_PER_SECOND calibrated in the emulator for a bare polling loop (an
 * idle hook that does work per poll slows that estimate down, which is why
 * the tick counter is preferred).
 *
 * The saver itself lives in the 80x24 text mode with the cursor hidden, and
 * prints its caption with the quarter-bright attribute, so hardly any
 * phosphor is driven and never at one place for long.
 *
 * Every key the program takes goes through here, which also filters out
 * doubled keys. While the board is being drawn the terminal board is busy
 * with picture data, and a key pressed meanwhile can arrive twice (its
 * release seen late, the keyboard's auto-repeat fires). So when a key kept
 * the program busy for BUSY_TICKS or more (a shot and the reply, a whole
 * screen), the same key again is dropped if it was already waiting when
 * the program became ready for the next key, or comes within ECHO_TICKS
 * after that: sooner than anyone reacts to the new picture. Quick keys (a
 * cursor step) are never filtered, so a key tapped or held repeats as
 * usual. Without the BIOS clock every waiting copy of the previous key is
 * dropped, and one within ECHO_POLLS polls.
 */
#include "video.h"
#include "saver.h"
#include "clock.h"

#define POLLS_PER_SECOND 20300              /* calibrated in the emulator: wait_key() polls per second at 4 MHz */
#define MOVE_SECONDS     3                  /* caption moves every few seconds */
#define ESC 27

static const char *const CAPTION[2] = { "K A K U R O", "Philips P2000C" };

/* Quarter-bright attribute: intensity bits (6, 0) both zero; 40h is normal. */
static void set_brightness(unsigned char attribute)
{
    conout(ESC); conout('0'); conout(attribute);
}

static void caption_at(unsigned char row, unsigned char col)
{
    conout(12);                              /* form feed: clear screen, cursor home */
    con_at(ROWCOL(row, col));     con_puts(CAPTION[0]);
    con_at(ROWCOL(row + 1, col)); con_puts(CAPTION[1]);
}

/* Runs until a key is pressed; returns that key. */
static unsigned char screen_saver(void)
{
    unsigned char row = 5, col = 10;
    unsigned int polls;
    unsigned char seconds;

    video_text();
    conout(ESC); conout('c');
    set_brightness(0x00);
    for (;;) {
        caption_at(row, col);
        clock_update();                      /* the game clock counts on (it must be read within 18 minutes) */
        for (seconds = 0; seconds < MOVE_SECONDS; seconds++)
            for (polls = 0; polls < POLLS_PER_SECOND; polls++)
                if (conready()) {
                    set_brightness(0x40);
                    conout(12);
                    return conin();
                }
        row = (unsigned char)((row + 7) % 22);       /* a simple walk that covers the screen */
        col = (unsigned char)((col + 23) % 66);
    }
}

#define IDLE_EVERY 512                       /* polls between idle() calls and clock checks, ~25 ms */
#define SAVER_TICKS ((unsigned int)SAVER_SECONDS * CLOCK_TICKS_PER_SECOND)   /* 18000 < 65536 */

#define ECHO_POLLS (POLLS_PER_SECOND / 4)
#define ECHO_TICKS 15                        /* 1/4 s */
#define BUSY_TICKS 20                        /* 1/3 s */

static unsigned char last_key;
static unsigned int key_time;                /* clock tick when last_key was taken */
static unsigned int ready_time;              /* ... when the program was ready for more */
static unsigned char after_busy;             /* last_key kept the program busy */

static unsigned char take(unsigned char key)
{
    last_key = key;
    key_time = clock_ticks();
    return key;
}

static void ready(void)
{
    ready_time = clock_ticks();
    after_busy = !clock_available || (unsigned int)(ready_time - key_time) >= BUSY_TICKS;
}

unsigned char wait_key_idle(void (*redraw)(void), void (*idle)(void))
{
    unsigned int polls = 0, seconds = 0, quiet = 0, start = clock_ticks();
    unsigned char expired, key;
    ready();
    for (;;) {
        if (conready()) {
            key = conin();
            if (key == last_key && after_busy &&
                (clock_available ? (unsigned int)(clock_ticks() - ready_time) < ECHO_TICKS
                                 : quiet < ECHO_POLLS))
                continue;                       /* a doubled key */
            return take(key);
        }
        if (quiet < ECHO_POLLS)
            quiet++;
        expired = 0;
        if ((polls & (IDLE_EVERY - 1)) == 0) {
            if (idle)
                idle();
            if (clock_available && (unsigned int)(clock_ticks() - start) >= SAVER_TICKS)
                expired = 1;
        }
        if (++polls >= POLLS_PER_SECOND) {
            polls = 0;
            if (!clock_available && ++seconds >= SAVER_SECONDS)
                expired = 1;
        }
        if (expired) {
            take(screen_saver());               /* the key only wakes the screen */
            redraw();
            ready();
            start = ready_time;
            seconds = 0;
            quiet = 0;
        }
    }
}

unsigned char wait_key(void (*redraw)(void))
{
    return wait_key_idle(redraw, 0);
}
