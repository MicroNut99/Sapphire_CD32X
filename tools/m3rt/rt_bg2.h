/* rt_bg2.h - M3, the picture: the background for the console (D7, docs/64). rt_bg.c's method (docs/63) in the room
   the SH2 has: a few KB of SDRAM, the rest in the 32X frame buffer's spare part, and what it sends written as a list
   for command 79 (FBVDP, docs/61).

   The frame buffer is two buffers, and the SH2 sees only the one not shown, which changes at every picture. So the
   tables kept there are kept in both: each write goes into the buffer seen and into a journal, played into the other
   buffer at the next picture (a buffer seen for the first time is filled whole, then the journal played into it).
   The work of a picture is bounded (the journal, the list): what is left over waits for the next (an area load takes
   a few pictures, while the screen is dark).

   The Genesis' VRAM (docs/64): slots 0-1,535 are background tiles (Genesis tiles 0-1,535), plane A at 0xC000
   (64 x 64), the H scroll tables at 0xE000 / 0xE400, plane B at 0xE800 (empty), the sprite table at 0xF000. */
#ifndef RT_BG2_H
#define RT_BG2_H
#include <stdint.h>
#include "rt.h"

#define BG2_SLOTS 1536
#define BG2_PLANE_A 0xC000u
#define BG2_HSCROLL 0xE000u
#define BG2_HSCROLL2 0xE400u              /* (Sapphire docs/16: the other per-line table, 224 x 2 words each) */
#define BG2_FAILS 4                        /* (packs failed in a row: then BG2_HOLD pictures let be) */
#define BG2_HOLD 30
#define BG2_JOURNAL 768                    /* the tables' writes a picture (then the rest waits) */
#define BG2_LIST_WORDS 2400                /* the list's words a picture (its journal run after them) */

/* the frame buffer's spare part (byte offsets in a buffer): the picture ends at 0x11A00 (0x200 line table, 224 lines
   of 320), the sprite cache is 0x1D000-0x1F400 (docs/56); the list, then the tables, and the other palettes' table
   after the sprite cache */
#define BG2_LIST 0x11A00                   /* (the list: 2,400 words at most, then its journal run) */
#define BG2_JRN 0x12E00                    /* (the journal's pairs, 768 x 2 words, to 0x13A00) */
#define BG2_T_MASK 0x13A00                 /* each tile: the colour indices it shows (u16) */
#define BG2_T_TAG 0x14A00                  /* each tile: its marks' tag when last looked at (u16; ~tag: a copy under way) */
#define BG2_T_PRIM 0x15A00                 /* each tile: the slot it was last converted into (u16) */
#define BG2_T_GMAP 0x16A00                 /* the plane's 4,096 entries as sent (u16) */
#define BG2_T_SLOT 0x18A00                 /* each slot: (tile << 4 | palette), the map version, the words' hash, uses (4 x u16) */
#define BG2_T_HASH 0x1BA00                 /* each tile: a 16-bit hash of its words when last looked at */
#define BG2_T_FP 0x1CA00                   /* each 32 tiles: a 32-bit hash of their tags when all were looked at */
#define BG2_T_END 0x1CB00
#define BG2_T_OVF 0x1F400                  /* (tile, palette) -> slot for a tile's other palettes: 1,024 (u16), to 0x1FC00 */
#define BG2_OVF_N 1024

typedef struct Bg2Stat {
    long ovf_full, all_used, noslot, underway, unseen, frames, behind, tiles, map, cols, journal_max, list_max, words, misfit, splits, repacks, evicted, recounts, grp_changed, hashed, underway_t, masked, scan_t, scan_c;
} Bg2Stat;

typedef struct Bg2 {
    uint16_t *fb[2];                       /* the two buffers (the PC's; on the SH2 the same window, 0x24000000) */
    int cur;                               /* the buffer seen this picture */
    uint16_t jn;                           /* this picture's table writes (their pairs at BG2_JRN) */
    /* the colours (rt_bg.c's) */
    uint16_t gcol[4][16]; uint8_t gref[4][16]; int8_t g[16]; uint8_t m[16][16]; uint16_t chg[16][16], ver[16];
    uint16_t cram[4][16]; uint16_t used[16];
    uint8_t dirty[512];                    /* the plane's entries to send again */
    int ndirty;
    uint16_t cursor;                       /* the slots' clock hand (a slot used by no entry is taken) */
    /* the list being written: its words, the run open */
    int ln; uint16_t run_hdr; unsigned run_addr; int run_open;
    int kn;                                /* the kept words this picture (the listener keeps 256) */
    uint16_t hs, vs;                       /* the scroll as sent */
    int late;                              /* this picture is not all sent (the rest at the next) */
    int have;
    uint16_t geom;                         /* (Sapphire docs/16) the map's size as last sent: MWR & 0x70, 0 none yet */
    int mw, ms, mn;                        /* its width, log2 width, entries */
    int16_t wx, wy;                        /* the window's first column and row (docs/16) */
    /* (Sapphire docs/16) the picture's bands - the top, then each raster split (rt_vdc's spl): from its line, its X and
       the Y its first line shows; their windows (first column, first row, rows); the per-line H scroll as sent (two
       tables taking turns, register 13 switched in the kept run) and the V splits as sent (command 78: main.c sends
       them when vsp_dirty) */
    uint8_t nb; uint16_t bl[3], bx[3], by[3];
    uint8_t nr; int16_t rx[3], ry[3], rn[3];
    uint16_t hs_line[3], hs_val[3]; uint8_t hs_n, htab;
    uint32_t vsp_e[3]; uint8_t vsp_n, vsp_dirty;
    uint8_t fails, hold;                            /* (packs failed in a row; pictures left to let be) */
    uint32_t colhash; int col_ok;          /* the colours in use and their values, as last seen; whether they fitted */
#ifndef __sh__
    Bg2Stat st;
#endif
} Bg2;

void bg2_init(Bg2 *b, uint16_t *fb0, uint16_t *fb1);
void bg2_fill(Bg2 *b, int cur);            /* each buffer's tables, once, at the start */
/* one picture at the display start, into buffer cur (the one the SH2 sees); returns the list's words (in the buffer
   at BG2_LIST, ended by 0; its kept runs, 0x3nnn, at the end); 0 if the map has more than 4,096 entries (128 x 64:
   more than a Genesis plane; Sapphire docs/16: every other size, 32-128 wide, 32-64 tall) */
int bg2_frame(Bg2 *b, const V32 *v, int cur);
/* (Sapphire docs/16) the map's size from MWR: width 32 / 64 / 128 (bits 4-5), height 32 / 64 (bit 6) */
static inline int bg2_mw(unsigned mwr) { const int w = (mwr >> 4) & 3; return w == 0 ? 32 : w == 1 ? 64 : 128; }
static inline int bg2_mh(unsigned mwr) { return (mwr & 0x40) ? 64 : 32; }
#endif
