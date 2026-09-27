/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.c -- the text-mode screens: start and help.
 *
 * Both use the plain 80x24 text mode, so they appear instantly. The start
 * screen lists the puzzles in rows of one size and difficulty; the chosen
 * one is shown in inverse video, and those with a best time underlined
 * (character attributes, ESC 0 b).
 */
#include "video.h"
#include "puzzles.h"
#include "game.h"
#include "panel.h"
#include "screens.h"
#include "saver.h"
#include "scores.h"
#include "version.h"

/* Character-ROM glyphs used by the text-mode screens. */
#define CH_BLOCK 0x9F                       /* full 8x12 block */
#define CH_H     0xD0                       /* box drawing: single lines */
#define CH_V     0xFA
#define CH_TL    0xA9
#define CH_TR    0xB9
#define CH_BL    0xAA
#define CH_BR    0xBA
#define ESC      27

/* ESC 0 b attributes: normal intensity (40h) plus underline and/or inverse. */
#define ATTR_NORMAL    0x40
#define ATTR_UNDERLINE 0x20
#define ATTR_INVERSE   0x10

#define KEY_LEFT   0x13                     /* see game.c */
#define KEY_RIGHT  0x04
#define KEY_UP     0x05
#define KEY_DOWN   0x18
#define KEY_LEFT2  0x15
#define KEY_RIGHT2 0x06
#define KEY_UP2    0x1A
#define KEY_DOWN2  0x0A
#define KEY_CR     0x0D

/* --- help ------------------------------------------------------------------------ */

static const char *const HELP[] = {
    "KAKURO v" VERSION " voor de Philips P2000C" "                    gecompileerd " BUILD_DATE,
    REPO_URL,
    "",
    "SPELREGELS",
    "  Kakuro is een kruiswoordpuzzel met cijfers. Vul elk wit vakje met een cijfer",
    "  van 1 tot en met 9. De cijfers in een reeks witte vakjes naast of onder",
    "  elkaar tellen op tot de som in het gearceerde vakje ervoor: rechtsboven de",
    "  schuine streep staat de som van de rij rechts ervan, linksonder die van de",
    "  kolom eronder. Binnen een reeks komt elk cijfer hooguit een keer voor.",
    "  Donkere cijfers in een gerasterd vakje liggen al vast. Elke puzzel heeft",
    "  precies een oplossing.",
    "",
    "HULP",
    "  Het paneel toont voor de rij en de kolom van de cursor alle combinaties van",
    "  cijfers die de som kunnen vormen (\"17 in 3\": som 17 in drie vakjes). Zo is",
    "  4 in 2 altijd 1 en 3.",
    "",
    "TOETSEN",
    "  Pijltjes of W A S D  cursor                 1 tot en met 9    cijfer invullen",
    "  TAB                  volgend leeg vakje     0, spatie of BS   cijfer wissen",
    "  H  hulp    N  volgende puzzel    ESC  andere puzzel kiezen    Q  stoppen",
    "",
    "  De beste tijd per puzzel wordt bewaard in KAKURO.DAT.",
    "Druk op een toets om terug te keren.",
};

/* Clears the 80x24 text screen and hides the blinking cursor. */
void text_clear(void)
{
    con_at(ROWCOL(0, 0));
    conout(ESC); conout('k');
    conout(ESC); conout('c');
}

static void draw_help_page(void)
{
    unsigned char row;
    text_clear();
    for (row = 0; row < sizeof HELP / sizeof HELP[0]; row++) {
        con_at(ROWCOL(row, 0));
        con_puts(HELP[row]);
    }
}

static void help_page(void)
{
    draw_help_page();
    wait_key(draw_help_page);
}

/* Shows the rules from the game, then restores the board. Leaving graphics
 * mode clears the terminal's picture, but the framebuffer in RAM is intact. */
void help_screen(void)
{
    video_text();
    help_page();
    redraw_game_screen();
}

/* --- start screen -------------------------------------------------------------- */

/* KAKURO in a five-row block font; every pixel becomes two block
 * characters, which is close to square on the CRT. */
static const char *const TITLE[5] = {
    "#  #  ##  #  # #  # ###   ## ",
    "# #  #  # # #  #  # #  # #  #",
    "##   #### ##   #  # ###  #  #",
    "# #  #  # # #  #  # # #  #  #",
    "#  # #  # #  #  ##  #  #  ## ",
};

#define TITLE_ROW   2
#define TITLE_COL   11
#define LIST_ROW    11                      /* the box; its groups start a row lower */
#define LABEL_COL   4
#define NUMBER_COL  17                      /* three columns per puzzle */
#define INFO_ROW    (LIST_ROW + 1 + GROUP_COUNT)

static void put_repeat(unsigned char ch, unsigned char count)
{
    while (count--)
        conout(ch);
}

static void draw_box(unsigned char row, unsigned char col, unsigned char width, unsigned char height)
{
    unsigned char r;
    con_at(ROWCOL(row, col));
    conout(CH_TL); put_repeat(CH_H, width - 2); conout(CH_TR);
    for (r = row + 1; r < row + height - 1; r++) {
        con_at(ROWCOL(r, col)); conout(CH_V);
        con_at(ROWCOL(r, col + width - 1)); conout(CH_V);
    }
    con_at(ROWCOL(row + height - 1, col));
    conout(CH_BL); put_repeat(CH_H, width - 2); conout(CH_BR);
}

static void draw_title(void)
{
    unsigned char r;
    const char *pixel;
    for (r = 0; r < 5; r++) {
        con_at(ROWCOL(TITLE_ROW + r, TITLE_COL));
        for (pixel = TITLE[r]; *pixel; pixel++) {
            conout(*pixel == '#' ? CH_BLOCK : ' ');
            conout(*pixel == '#' ? CH_BLOCK : ' ');
        }
    }
}

static void attribute(unsigned char a)
{
    conout(ESC); conout('0'); conout(a);
}

static void put_small(unsigned char v)      /* 0..99, two columns */
{
    conout(v >= 10 ? '0' + v / 10 : ' ');
    conout('0' + v % 10);
}

/* " 7 x 7 " or "10 x 10": seven columns. */
static void put_size(unsigned char size)
{
    put_small(size);
    con_puts(" x ");
    if (size >= 10)
        conout('1');
    conout('0' + size % 10);
    if (size < 10)
        conout(' ');
}

static unsigned char group_of(unsigned char k)
{
    unsigned char g = 0;
    while (g + 1 < GROUP_COUNT && k >= group_first[g + 1])
        g++;
    return g;
}

/* Puzzle k's number in the list, in inverse video when chosen. */
static void draw_entry(unsigned char k)
{
    unsigned char g = group_of(k), a = ATTR_NORMAL;
    if (k == puzzle)
        a |= ATTR_INVERSE;
    if (best_time[k])
        a |= ATTR_UNDERLINE;
    con_at(ROWCOL(LIST_ROW + 1 + g, NUMBER_COL + 3 * (k - group_first[g])));
    attribute(a);
    put_small(k + 1);
    attribute(ATTR_NORMAL);
}

static void draw_info(void)
{
    unsigned char g = group_of(puzzle);
    con_at(ROWCOL(INFO_ROW, LABEL_COL));
    con_puts("Puzzel ");
    put_small(puzzle + 1);
    con_puts(": ");
    put_size(group_n[g]);
    con_puts(group_stars[g] > 2 ? "   moeilijkheid ***" : "   moeilijkheid ** ");
    con_puts("   beste tijd ");
    put_time(best_time[puzzle]);
}

static void draw_start_screen(void)
{
    unsigned char g, k;

    text_clear();
    draw_box(0, 0, 80, 23);                  /* row 23 stays empty: writing its last cell scrolls */
    draw_title();
    con_at(ROWCOL(8, 29)); con_puts("voor de Philips P2000C");
    con_at(ROWCOL(9, 14)); con_puts("versie " VERSION "   -   " REPO_URL);

    draw_box(LIST_ROW, 2, 76, GROUP_COUNT + 3);
    con_at(ROWCOL(LIST_ROW, 5)); con_puts(" Kies een puzzel ");
    con_at(ROWCOL(LIST_ROW, 51)); con_puts(" onderstreept: met record ");
    for (g = 0; g < GROUP_COUNT; g++) {
        con_at(ROWCOL(LIST_ROW + 1 + g, LABEL_COL));
        put_size(group_n[g]);
        con_puts(group_stars[g] > 2 ? "  ***" : "  **");
        for (k = group_first[g]; k < group_first[g] + group_count[g]; k++)
            draw_entry(k);
    }
    draw_info();
    con_at(ROWCOL(21, 5));
    con_puts("Pijltjes: kiezen    RETURN: spelen    H: spelregels    Q: terug naar CP/M");
}

/* Moves the choice to puzzle k. */
static void choose(unsigned char k)
{
    unsigned char old = puzzle;
    puzzle = k;
    draw_entry(old);
    draw_entry(k);
    draw_info();
}

/* Up or down a row, keeping the place in it where the row is long enough. */
static void choose_row(unsigned char g)
{
    unsigned char place = puzzle - group_first[group_of(puzzle)];
    if (place >= group_count[g])
        place = group_count[g] - 1;
    choose(group_first[g] + place);
}

/* Text-mode start screen; returns 1 to play `puzzle`, 0 to leave the program. */
unsigned char start_screen(void)
{
    unsigned char key, g;
    draw_start_screen();
    for (;;) {
        key = wait_key(draw_start_screen);
        if (key >= 'A' && key <= 'Z')
            key += 'a' - 'A';
        g = group_of(puzzle);
        switch (key) {
        case KEY_CR: case ' ':
            return 1;
        case 'q':
            return 0;
        case 'h':
            help_page();
            draw_start_screen();
            break;
        case KEY_LEFT: case KEY_LEFT2: case 'a':
            choose(puzzle ? puzzle - 1 : PUZZLE_COUNT - 1);
            break;
        case KEY_RIGHT: case KEY_RIGHT2: case 'd':
            choose(puzzle + 1 < PUZZLE_COUNT ? puzzle + 1 : 0);
            break;
        case KEY_UP: case KEY_UP2: case 'w':
            choose_row(g ? g - 1 : GROUP_COUNT - 1);
            break;
        case KEY_DOWN: case KEY_DOWN2: case 's':
            choose_row(g + 1 < GROUP_COUNT ? g + 1 : 0);
            break;
        }
    }
}
