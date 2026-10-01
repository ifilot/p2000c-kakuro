/* SPDX-License-Identifier: GPL-3.0-only */
/* game.c -- game state, keys, and the course of a puzzle.
 *
 * After every change the board is brought up to date through the screen
 * module and the panel through the panel module; the status line is always
 * written last, so it doubles as a display-complete marker for the tests.
 * Doubled keys (the terminal board taking a held key for a new press
 * while it is busy with picture data) are filtered in saver.c, where every
 * key is read.
 */
#include "video.h"
#include "puzzle.h"
#include "puzzles.h"
#include "game.h"
#include "screen.h"
#include "panel.h"
#include "screens.h"
#include "saver.h"
#include "clock.h"
#include "scores.h"

unsigned char puzzle;
unsigned char state;
unsigned char cur;

static unsigned char touched;               /* the player typed or wiped a digit */

/* Cursor keys. The P2000C keyboard's cursor quadrant emits the WordStar
 * diamond (^S ^D ^E ^X, as P2EDIT and SuperCalc expect); the graphical
 * emulator sends the terminal's own cursor-control bytes instead, so both
 * sets are accepted. */
#define KEY_LEFT   0x13                     /* ^S */
#define KEY_RIGHT  0x04                     /* ^D */
#define KEY_UP     0x05                     /* ^E */
#define KEY_DOWN   0x18                     /* ^X */
#define KEY_LEFT2  0x15
#define KEY_RIGHT2 0x06
#define KEY_UP2    0x1A
#define KEY_DOWN2  0x0A
#define KEY_TAB    0x09
#define KEY_BS     0x08
#define KEY_DEL    0x7F
#define KEY_CR     0x0D
#define KEY_ESC    0x1B
#define BEL        0x07

#define UP    0
#define DOWN  1
#define LEFT  2
#define RIGHT 3

/* Idle hook while waiting for a key: keeps the clock display current. */
static void tick_clock(void)
{
    if (state == PLAYING && clock_update())
        show_clock();
}

/* The status line from the game state; written last. */
static void announce(void)
{
    show_status(state == SOLVED ? "Opgelost!" : "Vul alle vakjes in");
}

/* Rebuilds the whole game screen from the framebuffer and the game state,
 * after the help page or the screen saver (both leave graphics mode). */
void redraw_game_screen(void)
{
    video_graphics();
    draw_panel();
    screen_flush();
    restore_note();
    announce();
}

/* The first white cell without a given digit, in reading order. */
static unsigned char first_open(void)
{
    unsigned char i;
    for (i = 0; i < ncells; i++)
        if ((cell[i] & (CELL_WHITE | CELL_FIXED)) == CELL_WHITE)
            return i;
    return 0;
}

static void start_puzzle(void)
{
    puzzle_load(puzzle);
    screen_setup();
    state = PLAYING;
    touched = 0;
    cur = first_open();
    screen_compose();
    video_graphics();
    draw_panel();
    screen_flush();
    show_note("");
    clock_reset();                          /* the drawing is not counted */
    announce();
}

/* Board and panel after a change, the status line last. */
static void refresh(void)
{
    screen_sync();
    show_empty();
    show_combinations();
    announce();
}

/* After the last empty cell was filled: solved, or not yet. */
static void check_solved(void)
{
    unsigned int seconds;
    if (empty)
        return;
    if (!puzzle_solved()) {
        show_note("Nog niet goed...");
        return;
    }
    state = SOLVED;
    clock_freeze();
    show_note("N: volgende puzzel");
    if (!clock_available)
        return;
    seconds = clock_seconds();
    if (seconds == 0)
        seconds = 1;
    if (best_time[puzzle] == 0 || seconds < best_time[puzzle]) {
        best_time[puzzle] = seconds;
        scores_save();
        show_record();
        show_note("Nieuw record!");
    }
}

/* A digit 1-9, or 0 to wipe the cell. */
static void put_digit(unsigned char d)
{
    unsigned char old = cell[cur] & CELL_DIGIT;
    if (cell[cur] & CELL_FIXED) {
        conout(BEL);
        return;
    }
    if (d == old)
        return;
    show_status("Bezig...");                /* replaces the marker before any flushing */
    touched = 1;
    if (!old)
        empty--;
    if (!d)
        empty++;
    cell[cur] = (cell[cur] & ~CELL_DIGIT) | d;
    show_note("");
    check_solved();
    refresh();
}

static void move_to(unsigned char i)
{
    if (i == cur)
        return;
    show_status("Bezig...");
    cur = i;
    screen_sync();
    show_combinations();
    announce();
}

static unsigned char white_at(int r, int c)
{
    return r >= 0 && r < n && c >= 0 && c < n && (cell[r * n + c] & CELL_WHITE);
}

/* The nearest white cell in a direction: straight ahead first, else the
 * closest one in the quarter plane before the cursor. Plain int
 * arithmetic (a signed-char version of such code was miscompiled in
 * p2000c-minesweeper). */
static void move_dir(unsigned char dir)
{
    int r = cur / n, c = cur % n, k, o, j, rr, cc;
    for (j = 0; j <= 2 * n; j++) {
        o = (j & 1) ? (j + 1) / 2 : -(j / 2);   /* 0, 1, -1, 2, -2, ... */
        for (k = 1; k < n; k++) {
            if (o > k || -o > k)
                continue;
            switch (dir) {
            case UP:    rr = r - k; cc = c + o; break;
            case DOWN:  rr = r + k; cc = c + o; break;
            case LEFT:  rr = r + o; cc = c - k; break;
            default:    rr = r + o; cc = c + k; break;
            }
            if (white_at(rr, cc)) {
                move_to(rr * n + cc);
                return;
            }
        }
    }
    conout(BEL);
}

/* TAB: the next empty cell in reading order, wrapping. */
static void next_empty(void)
{
    unsigned char i = cur, k;
    for (k = 0; k < ncells; k++) {
        if (++i == ncells)
            i = 0;
        if (cell[i] == CELL_WHITE) {
            move_to(i);
            return;
        }
    }
    conout(BEL);
}

static const char *question;

/* After the screen saver during a question: the screen and the question. */
static void redraw_asking(void)
{
    redraw_game_screen();
    show_status(question);
}

/* A yes/no question on the status line; returns 1 for yes. */
static unsigned char confirm(const char *text)
{
    unsigned char key;
    question = text;
    show_status(question);
    key = wait_key_idle(redraw_asking, tick_clock);
    if (key == 'j' || key == 'J' || key == 'y' || key == 'Y')
        return 1;
    announce();
    return 0;
}

/* Giving up a puzzle in progress needs a confirmation; a solved or
 * untouched one does not. */
static unsigned char may_leave(void)
{
    return state != PLAYING || !touched || confirm("Opgeven? J/N");
}

unsigned char play(void)
{
    unsigned char key;

    start_puzzle();
    for (;;) {
        key = wait_key_idle(redraw_game_screen, tick_clock);
        if (key >= 'A' && key <= 'Z')
            key += 'a' - 'A';
        if (key >= '1' && key <= '9') {
            if (state == PLAYING)
                put_digit(key - '0');
            else
                conout(BEL);
            continue;
        }
        switch (key) {
        case KEY_LEFT:  case KEY_LEFT2:  case 'a':
        case KEY_RIGHT: case KEY_RIGHT2: case 'd':
        case KEY_UP:    case KEY_UP2:    case 'w':
        case KEY_DOWN:  case KEY_DOWN2:  case 's':
            if (state != PLAYING) {
                conout(BEL);
                break;
            }
            if (key == KEY_LEFT || key == KEY_LEFT2 || key == 'a')
                move_dir(LEFT);
            else if (key == KEY_RIGHT || key == KEY_RIGHT2 || key == 'd')
                move_dir(RIGHT);
            else if (key == KEY_UP || key == KEY_UP2 || key == 'w')
                move_dir(UP);
            else
                move_dir(DOWN);
            break;
        case '0': case ' ': case KEY_BS: case KEY_DEL:
            if (state == PLAYING)
                put_digit(0);
            else
                conout(BEL);
            break;
        case KEY_TAB:
            if (state == PLAYING)
                next_empty();
            break;
        case 'h':
            help_screen();
            break;
        case 'n':
            if (may_leave()) {
                puzzle = puzzle + 1 < PUZZLE_COUNT ? puzzle + 1 : 0;
                start_puzzle();
            }
            break;
        case KEY_ESC:
            if (may_leave())
                return PLAY_MENU;
            break;
        case 'q':
            if (confirm("Stoppen? J/N"))
                return PLAY_QUIT;
            break;
        }
    }
}
