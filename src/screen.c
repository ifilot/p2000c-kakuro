/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.c -- the board picture.
 *
 * Everything is composed in the 16 KiB framebuffer; every cell starts as a
 * whole tile (sprites.h) that carries its share of the board's grid (its
 * top row and left column), with a digit, a sum or the cursor put on top.
 * Two costs decide how the picture goes to the terminal board: the
 * 19200-baud link (about 1920 bytes/s), and the terminal's own Z80, which
 * draws a line dot by dot but stores uploaded bytes about as fast as they
 * arrive. So (as in p2000c-minesweeper):
 *
 * - Only the grid goes as lines (ESC m / ESC M): one per row and column of
 *   cells, plus the closing ones. Everything else goes as bitmap uploads of
 *   what differs from those lines.
 * - Uploads are grouped per band of consecutive changed lines, at most
 *   PIECE_LINES at a time, and each band goes the cheaper way: as one chunk
 *   of whole 64-byte lines (a single ESC r, sent from its lowest line up,
 *   since the terminal's picture RAM runs bottom-up) or as one ESC r per
 *   run of changed bytes on each line.
 * - Each cell keeps the appearance last sent. After a change, the span of
 *   changed cells in each board row is redrawn and compared with its
 *   previous bytes, and only the lines that differ are sent.
 */
#include <string.h>
#include "video.h"
#include "puzzle.h"
#include "game.h"
#include "screen.h"
#include "sprites.h"

#define ESC          27
#define RUN_GAP      7                      /* an ESC r header costs 7 bytes */
#define PIECE_LINES  15                     /* lines per chunk, as p2000c-chess sends them */

/* Appearance codes: a white cell's digit and CELL_FIXED, plus the cursor;
 * a black cell never changes. */
#define LOOK_CURSOR  0x80
#define LOOK_BLACK   0x40
#define LOOK_NONE    0xFF

struct geometry {
    const unsigned char *tiles, *digits, *givens, *cursor;
    unsigned char cw, ch;                   /* cell: bytes, lines */
    unsigned char digit_x, digit_y, digit_w, digit_h;
    unsigned char sum_a_x, sum_a_y, sum_d_x, sum_d_y;
};

static const struct geometry geometries[2] = {
    { &tiles_large[0][0], &digits_large[0][0], &givens_large[0][0], cursor_large, LARGE_CW, LARGE_CH,
      LARGE_DIGIT_X, LARGE_DIGIT_Y, LARGE_DIGIT_W, LARGE_DIGIT_H,
      LARGE_SUM_A_X, LARGE_SUM_A_Y, LARGE_SUM_D_X, LARGE_SUM_D_Y },
    { &tiles_small[0][0], &digits_small[0][0], &givens_small[0][0], cursor_small, SMALL_CW, SMALL_CH,
      SMALL_DIGIT_X, SMALL_DIGIT_Y, SMALL_DIGIT_W, SMALL_DIGIT_H,
      SMALL_SUM_A_X, SMALL_SUM_A_Y, SMALL_SUM_D_X, SMALL_SUM_D_Y },
};

static const struct geometry *g;
static unsigned int tile_size, digit_size;
static unsigned char left;                  /* the board's first byte */
static unsigned char top;                   /* the board's first line */
static unsigned int grid_x0, grid_x1;       /* the grid's first and last column of dots */
static unsigned char grid_y1;               /* the grid's last line */

/* What the lines put on a line of the picture (64 bytes each). */
static unsigned char pat_across[FB_LINE];   /* a grid line across the board */
static unsigned char pat_down[FB_LINE];     /* the dots of the grid's columns */

static unsigned char shown[MAX_CELLS];      /* appearance last sent per cell */
static unsigned char saved[LARGE_CW * 9 * LARGE_CH];   /* a board row's changed span before redrawing */

static unsigned char piece[PIECE_LINES * FB_LINE];     /* deltas of a band of consecutive lines */
static unsigned int piece_cost[PIECE_LINES];           /* what each line costs as runs */
static unsigned char piece_first, piece_n, piece_col0, piece_width;

#if 9 * LARGE_CW * LARGE_CH < 10 * SMALL_CW * SMALL_CH
#error "saved[] is too small for the small cells"
#endif

/* Sets dots x0..x1 of a 64-byte line: whole bytes where it can. */
static void dots(unsigned char *line, unsigned int x0, unsigned int x1)
{
    unsigned char first = x0 >> 3, last = x1 >> 3;
    unsigned char head = 0xFF >> (x0 & 7), tail = 0xFF << (7 - (x1 & 7));
    if (first == last) {
        line[first] |= head & tail;
        return;
    }
    line[first] |= head;
    if (last > first + 1)
        memset(line + first + 1, 0xFF, last - first - 1);
    line[last] |= tail;
}

void screen_setup(void)
{
    unsigned char c, width;
    g = &geometries[n > 9];
    tile_size = g->cw * g->ch;
    digit_size = g->digit_w * g->digit_h;
    width = n * g->cw + 1;                  /* the closing line takes one more byte */
    left = (BOARD_BYTES - width) / 2;
    top = (FB_LINES - (n * g->ch + 1)) / 2;
    grid_x0 = left * 8;
    grid_x1 = grid_x0 + (unsigned int)n * g->cw * 8;
    grid_y1 = top + n * g->ch;
    memset(pat_across, 0, FB_LINE);
    dots(pat_across, grid_x0, grid_x1);
    memset(pat_down, 0, FB_LINE);
    for (c = 0; c <= n; c++)
        dots(pat_down, grid_x0 + c * g->cw * 8, grid_x0 + c * g->cw * 8);
    memset(shown, LOOK_NONE, sizeof shown);
}

static unsigned int cell_offset(unsigned char i)
{
    unsigned char r = i / n, c = i % n;
    return (unsigned int)(top + r * g->ch) * FB_LINE + left + c * g->cw;
}

/* What the cell should look like right now. */
static unsigned char look(unsigned char i)
{
    unsigned char v = cell[i], lk;
    if (!(v & CELL_WHITE))
        return LOOK_BLACK;
    lk = v & (CELL_DIGIT | CELL_FIXED);
    if (state == PLAYING && i == cur)
        lk |= LOOK_CURSOR;
    return lk;
}

static void put_sum(unsigned char sum, unsigned int at)
{
    video_blit(sums + (sum - 1) * (SUM_BYTES * SUM_LINES), at, WH(SUM_BYTES, SUM_LINES));
}

static void draw_cell(unsigned char i, unsigned char lk)
{
    unsigned int offset = cell_offset(i), wh = WH(g->cw, g->ch);
    unsigned char a, d, t;
    if (lk & LOOK_BLACK) {
        a = sum_a[i];
        d = sum_d[i];
        t = a ? (d ? TILE_CLUE_AD : TILE_CLUE_A) : (d ? TILE_CLUE_D : TILE_BLOCK);
        video_copy(g->tiles + t * tile_size, offset, wh);
        if (a)
            put_sum(a, offset + g->sum_a_y * FB_LINE + g->sum_a_x);
        if (d)
            put_sum(d, offset + g->sum_d_y * FB_LINE + g->sum_d_x);
    } else {
        d = lk & CELL_DIGIT;
        if (lk & CELL_FIXED)                /* a whole tile: a dark digit in a sparse dither */
            video_copy(g->givens + (d - 1) * tile_size, offset, wh);
        else {
            video_copy(g->tiles + TILE_WHITE * tile_size, offset, wh);
            if (d)
                video_blit(g->digits + (d - 1) * digit_size,
                           offset + g->digit_y * FB_LINE + g->digit_x, WH(g->digit_w, g->digit_h));
        }
        if (lk & LOOK_CURSOR)
            video_blit(g->cursor, offset, wh);
    }
    shown[i] = lk;
}

/* --- uploads ---------------------------------------------------------------------- */

/* Uploads the nonzero runs of buf[0..width) (runs closer than a header are
 * joined) as parts of framebuffer line `line` from byte column col0; what
 * that costs is runs_cost() (video.asm). */
static void runs(const unsigned char *buf, unsigned char line, unsigned char col0, unsigned char width)
{
    unsigned char x = 0, first, last;
    while (x < width) {
        while (x < width && buf[x] == 0)
            x++;
        if (x == width)
            break;
        first = last = x;
        while (x < width) {
            if (buf[x] != 0)
                last = x;
            else if (x - last >= RUN_GAP)
                break;
            x++;
        }
        video_flush_rect(COLROW(col0 + first, line), WH(last - first + 1, 1));
    }
}

/* One ESC r with framebuffer lines first..first+n-1 whole: it starts on the
 * lowest line and continues upward, as the terminal's picture RAM runs
 * bottom-up. count must not be a multiple of 4 (a count with a zero low
 * byte fails on the terminal). */
static void send_chunk(unsigned char first, unsigned char count)
{
    unsigned int bytes = (unsigned int)count * FB_LINE;
    unsigned char line = first + count - 1;
    conout(ESC); conout('r'); conout(0); conout(0); conout(251 - line);
    conout(bytes & 0xFF); conout(bytes >> 8);
    for (;;) {
        con_write(framebuffer + line * FB_LINE, FB_LINE);
        if (line-- == first)
            break;
    }
}

/* Sends the band of lines collected in piece[] the cheaper way. */
static void piece_flush(void)
{
    unsigned char i, count = piece_n, k;
    unsigned int by_runs = 0, by_chunk;
    const unsigned char *d;
    if (!count)
        return;
    piece_n = 0;
    for (i = 0; i < count; i++)
        by_runs += piece_cost[i];
    k = (count & 3) ? count : count - 1;    /* the last line of a multiple of 4 goes as runs */
    by_chunk = RUN_GAP + k * FB_LINE + (k < count ? piece_cost[k] : 0);
    if (by_chunk < by_runs) {
        send_chunk(piece_first, k);
        if (k < count)
            runs(piece + k * piece_width, piece_first + k, piece_col0, piece_width);
        return;
    }
    for (i = 0, d = piece; i < count; i++, d += piece_width)
        runs(d, piece_first + i, piece_col0, piece_width);
}

/* Starts collecting lines that differ in bytes col0..col0+width-1. */
static void piece_start(unsigned char col0, unsigned char width)
{
    piece_flush();
    piece_col0 = col0;
    piece_width = width;
}

/* Adds a line whose picture goes from old to now over the band's bytes; a
 * line without changes ends the band. */
static void piece_line(unsigned char line, const unsigned char *now, const unsigned char *old)
{
    unsigned char *d;
    unsigned int cost;
    if (piece_n && line != piece_first + piece_n)
        piece_flush();
    d = piece + piece_n * piece_width;
    cost = xor_cost(d, now, old, piece_width);
    if (!cost) {
        piece_flush();
        return;
    }
    if (!piece_n)
        piece_first = line;
    piece_cost[piece_n] = cost;
    if (++piece_n == PIECE_LINES)
        piece_flush();
}

void screen_sync(void)
{
    unsigned char r, c, i, first, last = 0, y, width, lk;
    unsigned int base;
    unsigned char *s;
    for (r = 0; r < n; r++) {
        first = 0xFF;
        for (c = 0, i = r * n; c < n; c++, i++)
            if (look(i) != shown[i]) {
                if (first == 0xFF)
                    first = c;
                last = c;
            }
        if (first == 0xFF)
            continue;
        base = cell_offset(r * n + first);
        width = (last - first + 1) * g->cw;
        for (y = 0, s = saved; y < g->ch; y++, s += width)
            memcpy(s, framebuffer + base + y * FB_LINE, width);
        for (c = first, i = r * n + first; c <= last; c++, i++) {
            lk = look(i);
            if (lk != shown[i])
                draw_cell(i, lk);
        }
        piece_start(left + first * g->cw, width);
        for (y = 0, s = saved; y < g->ch; y++, s += width)
            piece_line(top + r * g->ch + y, framebuffer + base + y * FB_LINE, s);
        piece_flush();
    }
}

/* --- composition ---------------------------------------------------------------- */

void screen_compose(void)
{
    unsigned char i, line;
    video_clear();
    /* the grid's closing column and row (the tiles carry the rest) */
    for (line = top; line < grid_y1; line++)
        dots(framebuffer + line * FB_LINE, grid_x1, grid_x1);
    memcpy(framebuffer + grid_y1 * FB_LINE, pat_across, FB_LINE);
    for (i = 0; i < ncells; i++)
        draw_cell(i, look(i));
}

/* --- fresh picture --------------------------------------------------------------- */

static void vector(unsigned char cmd, unsigned int x, unsigned char line)
{
    conout(ESC); conout(cmd); conout(x & 0xFF); conout(x >> 8); conout(251 - line);
}

static void stroke(unsigned int x0, unsigned char y0, unsigned int x1, unsigned char y1)
{
    vector('m', x0, y0);
    vector('M', x1, y1);
}

/* The grid, drawn by the terminal: one line per row and column of cells
 * and the closing ones. */
static void send_grid(void)
{
    unsigned char i;
    unsigned int x;
    for (i = 0; i <= n; i++)
        stroke(grid_x0, top + i * g->ch, grid_x1, top + i * g->ch);
    for (i = 0, x = grid_x0; i <= n; i++, x += g->cw * 8)
        stroke(x, top, x, grid_y1);
}

/* After ESC 3 (a blank picture): the lines, then everything that differs from them. */
void screen_flush(void)
{
    unsigned char line, t = 0, width = (grid_x1 >> 3) + 1 - left;
    send_grid();
    piece_start(left, width);
    for (line = top; line <= grid_y1; line++) {
        piece_line(line, framebuffer + line * FB_LINE + left, (t == 0 ? pat_across : pat_down) + left);
        if (++t == g->ch)
            t = 0;
    }
    piece_flush();
}
