/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.c -- the panel: title, puzzle, clock, record, empty cells, the
 * combinations for the cursor's row and column, status, note and keys.
 *
 * The panel takes text columns 46-63 (the board ends before dot 368).
 * Every field is padded to its width so a shorter text overwrites a longer
 * one. Nothing is ever written to the last column of the last text row,
 * which would scroll the text plane.
 */
#include <string.h>
#include "video.h"
#include "puzzle.h"
#include "puzzles.h"
#include "game.h"
#include "panel.h"
#include "clock.h"
#include "scores.h"

#define COL        46
#define WIDTH      18
#define CLOCK_MAX  35999u                   /* 9:59:59 */

#define ROW_TITLE  0
#define ROW_PUZZLE 1
#define ROW_SIZE   2
#define ROW_CLOCK  3
#define ROW_RECORD 4
#define ROW_EMPTY  5
#define ROW_ACROSS 7                        /* header, then COMBO_LINES lines */
#define ROW_DOWN   12
#define COMBO_LINES 4
#if ROW_DOWN != ROW_ACROSS + COMBO_LINES + 1
#error "shadow[] expects the down section right below the across one"
#endif
#define ROW_KEYS1  17
#define ROW_STATUS 18
#define ROW_NOTE   19
#define ROW_KEYS2  20
#define VALUE_COL  (COL + 10)

/* shown_a/shown_d besides a cell index or NO_RUN */
#define HIDDEN     0xFD                     /* solved: nothing shown */
#define UNKNOWN    0xFE                     /* whatever: redraw */

static const char *note_text = "";
static unsigned char shown_a, shown_d;      /* heads whose combinations are on screen */

static void at(unsigned char r, unsigned char c)
{
    con_at(ROWCOL(r, c));
}

/* Writes s and pads with spaces to width. */
static void put_padded(const char *s, unsigned char width)
{
    while (*s && width) {
        conout(*s++);
        width--;
    }
    while (width--)
        conout(' ');
}

/* Unsigned number, right-aligned in `width` columns. */
static void put_number(unsigned int u, unsigned char width)
{
    char buf[6];
    unsigned char i = sizeof buf - 1;
    buf[i] = '\0';
    do {
        buf[--i] = '0' + u % 10;
        u /= 10;
    } while (u);
    while (sizeof buf - 1 - i < width)
        buf[--i] = ' ';
    con_puts(buf + i);
}

/* h:mm:ss, capped at 9:59:59. */
static void put_clock(unsigned int s)
{
    unsigned int h, m;
    if (s > CLOCK_MAX)
        s = CLOCK_MAX;
    h = s / 3600;
    m = (s / 60) % 60;
    s %= 60;
    conout('0' + h); conout(':');
    conout('0' + m / 10); conout('0' + m % 10); conout(':');
    conout('0' + s / 10); conout('0' + s % 10);
}

void put_time(unsigned int s)
{
    if (s)
        put_clock(s);
    else
        con_puts("-:--:--");
}

void show_clock(void)
{
    if (!clock_available)
        return;
    at(ROW_CLOCK, VALUE_COL + 1);
    put_clock(clock_seconds());
}

void show_record(void)
{
    at(ROW_RECORD, VALUE_COL + 1);
    put_time(best_time[puzzle]);
}

void show_empty(void)
{
    at(ROW_EMPTY, VALUE_COL + 5);
    put_number(empty, 3);
}

/* The combination rows are built as whole lines and compared with what
 * the panel shows, so only the characters that change are sent. */
static char shadow[2 * (COMBO_LINES + 1)][WIDTH];
static char line[WIDTH];
static unsigned char pos;

static void line_start(void)
{
    memset(line, ' ', WIDTH);
    pos = 0;
}

static void line_add(const char *s)
{
    while (*s && pos < WIDTH)
        line[pos++] = *s++;
}

/* Sends the part of the line that differs from the panel's row. */
static void line_put(unsigned char row)
{
    char *s = shadow[row - ROW_ACROSS];
    unsigned char first = 0, last = WIDTH;
    while (first < WIDTH && line[first] == s[first])
        first++;
    if (first == WIDTH)
        return;
    while (line[last - 1] == s[last - 1])
        last--;
    at(row, COL + first);
    for (; first < last; first++)
        conout(s[first] = line[first]);
}

/* The combinations of one run: a header ("Rij:   17 in 3:") and
 * COMBO_LINES lines, words wrapped at the panel's width (the longest list,
 * twelve combinations of four or five digits, takes four lines). head is
 * the black cell with the sum. */
static void show_run(unsigned char row, const char *label, unsigned char head, unsigned char across)
{
    static char text[80];
    unsigned char sum, len, k, r;
    const char *p, *q;
    text[0] = '\0';
    line_start();
    if (state == PLAYING)                       /* solved: the section is cleared */
        line_add(label);
    if (state == PLAYING && head == NO_RUN)
        line_add("-");                          /* a lone cell has no sum this way */
    else if (state == PLAYING) {
        sum = across ? sum_a[head] : sum_d[head];
        len = across ? len_a[head] : len_d[head];
        line[pos++] = sum >= 10 ? '0' + sum / 10 : ' ';
        line[pos++] = '0' + sum % 10;
        line_add(" in ");
        line[pos++] = '0' + len;
        line_add(":");
        combinations(sum, len, text);
    }
    line_put(row);
    p = text;
    for (r = 1; r <= COMBO_LINES; r++) {
        line_start();
        while (*p) {                            /* whole words while they fit */
            for (q = p; *q != ' '; q++)
                ;
            k = q - p;
            if (pos + k > WIDTH)
                break;
            while (p < q)
                line[pos++] = *p++;
            p++;                                /* the space after the word */
            if (pos < WIDTH)
                pos++;
        }
        line_put(row + r);
    }
}

void show_combinations(void)
{
    unsigned char a = state == PLAYING ? head_a[cur] : HIDDEN;
    unsigned char d = state == PLAYING ? head_d[cur] : HIDDEN;
    if (a != shown_a) {
        show_run(ROW_ACROSS, "Rij:   ", a, 1);
        shown_a = a;
    }
    if (d != shown_d) {
        show_run(ROW_DOWN, "Kolom: ", d, 0);
        shown_d = d;
    }
}

void show_note(const char *note)
{
    note_text = note;
    at(ROW_NOTE, COL);
    put_padded(note, WIDTH);
}

void restore_note(void)
{
    show_note(note_text);
}

void show_status(const char *status)
{
    at(ROW_STATUS, COL);
    put_padded(status, WIDTH);
}

void draw_panel(void)
{
    at(ROW_TITLE, COL);   con_puts("K A K U R O");
    at(ROW_PUZZLE, COL);  con_puts("Puzzel ");
    put_number(puzzle + 1, 1);
    con_puts(" van ");
    put_number(PUZZLE_COUNT, 1);
    at(ROW_SIZE, COL);
    put_number(n, 1);
    con_puts(" x ");
    put_number(n, 1);
    con_puts(stars > 2 ? "   ***" : "   **");
    if (clock_available) {
        at(ROW_CLOCK, COL);
        con_puts("Tijd");
    }
    at(ROW_RECORD, COL);  con_puts("Record");
    at(ROW_EMPTY, COL);   con_puts("Lege vakjes");
    at(ROW_KEYS1, COL);   con_puts("1-9 cijfer, 0 wis");
    at(ROW_KEYS2, COL);   con_puts("H hulp  ESC menu");
    show_clock();
    show_record();
    show_empty();
    memset(shadow, ' ', sizeof shadow);     /* ESC 3 cleared the text plane */
    shown_a = shown_d = UNKNOWN;            /* redraw both */
    show_combinations();
}
