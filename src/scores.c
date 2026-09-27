/* SPDX-License-Identifier: GPL-3.0-only */
/* scores.c -- the best time per puzzle, kept in KAKURO.DAT.
 *
 * Two 128-byte CP/M records on the current drive: the magic "KK", a format
 * byte, the number of puzzles, then a little-endian word of seconds per
 * puzzle. Plain BDOS sequential file calls; a file that is missing, does
 * not start with the magic or was written for another set of puzzles means
 * no records yet. A failing write (a write-protected disk) is ignored.
 */
#include "video.h"
#include "scores.h"

#define F_OPEN   15
#define F_CLOSE  16
#define F_READ   20
#define F_WRITE  21
#define F_MAKE   22
#define F_DMA    26
#define FAILED   0xFF
#define FORMAT   1
#define HEADER   4
#define RECORDS  2

#if HEADER + 2 * PUZZLE_COUNT > RECORDS * 128
#error "KAKURO.DAT needs more records"
#endif

unsigned int best_time[PUZZLE_COUNT];

static unsigned char fcb[36];
static unsigned char data[RECORDS * 128];
static const char NAME[11] = { 'K', 'A', 'K', 'U', 'R', 'O', ' ', ' ', 'D', 'A', 'T' };

static void fcb_init(void)
{
    unsigned char i;
    for (i = 0; i < sizeof fcb; i++)
        fcb[i] = 0;                         /* drive 0: the current drive */
    for (i = 0; i < sizeof NAME; i++)
        fcb[1 + i] = NAME[i];
}

void scores_load(void)
{
    unsigned char i, ok = 1;
    for (i = 0; i < PUZZLE_COUNT; i++)
        best_time[i] = 0;
    fcb_init();
    if (bdos((unsigned int)fcb, F_OPEN) == FAILED)
        return;
    for (i = 0; i < RECORDS && ok; i++) {
        bdos((unsigned int)(data + i * 128), F_DMA);
        ok = bdos((unsigned int)fcb, F_READ) == 0;
    }
    if (ok && data[0] == 'K' && data[1] == 'K' && data[2] == FORMAT && data[3] == PUZZLE_COUNT)
        for (i = 0; i < PUZZLE_COUNT; i++)
            best_time[i] = data[HEADER + 2 * i] | (data[HEADER + 1 + 2 * i] << 8);
    bdos((unsigned int)fcb, F_CLOSE);
}

void scores_save(void)
{
    unsigned int k;
    unsigned char i;
    for (k = 0; k < sizeof data; k++)
        data[k] = 0x1A;                     /* CP/M end-of-file filler */
    data[0] = 'K';
    data[1] = 'K';
    data[2] = FORMAT;
    data[3] = PUZZLE_COUNT;
    for (i = 0; i < PUZZLE_COUNT; i++) {
        data[HEADER + 2 * i] = best_time[i] & 0xFF;
        data[HEADER + 1 + 2 * i] = best_time[i] >> 8;
    }
    fcb_init();
    if (bdos((unsigned int)fcb, F_OPEN) == FAILED && bdos((unsigned int)fcb, F_MAKE) == FAILED)
        return;
    fcb[32] = 0;                            /* from record 0 */
    for (i = 0; i < RECORDS; i++) {
        bdos((unsigned int)(data + i * 128), F_DMA);
        if (bdos((unsigned int)fcb, F_WRITE))
            break;
    }
    bdos((unsigned int)fcb, F_CLOSE);
}
