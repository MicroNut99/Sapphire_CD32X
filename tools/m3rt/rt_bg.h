/* rt_bg.h - M3, the picture: the PC Engine's background as the Genesis' plane A, its colours kept by value on the
   console (D7, docs/59 and docs/63).

   At each display start (where the sprites are drawn, docs/56) bg_frame looks at the 64 x 64 map, the tiles it names
   and the 16 background palettes, and keeps:
   - the Genesis' 4 palettes of 15: each PC Engine palette in use has one Genesis palette and, for each colour index
     its tiles show, a slot in it. Slots are shared by colour value (D7); a shared slot whose members' values part
     (a cycle, a flash) is split, and the palette that moved gets a new map (its version up: its tiles converted
     again); when nothing fits, everything is packed again;
   - a cache of Genesis tiles in VRAM: a PC Engine tile converted for its palette's map, kept by (tile, palette),
     valid while the palette's map and the tile's words are as they were;
   - the Genesis plane's 4,096 entries (tile slot, palette).
   What changes goes out as words for the Genesis' VDP: tiles (16 words), map entries, colours - counted in BgStat,
   and on the console sent by command 79 (docs/61). */
#ifndef RT_BG_H
#define RT_BG_H
#include <stdint.h>
#include "rt.h"

#define BG_SLOTS 1536               /* Genesis VRAM tiles for the background (docs/61: the plane, the tables beside) */
#define BG_NONE 0xFFFF

typedef struct BgStat {
    long frames, skipped_mwr, tiles_conv, map_words, col_words, splits, moves, repacks, misfit, slots_max, evicted;
    long conv_max, map_max, col_max, words_max;      /* (the most in one frame: tiles converted, map, colours, all) */
    long hist[6];                                    /* frames by words sent: 0, < 64, < 256, < 1,024, < 4,096, more */
} BgStat;

typedef struct Bg {
    /* the Genesis' colours: each slot's PC Engine value (9 bits; BG_NONE free) and how many (palette, index) use it */
    uint16_t gcol[4][16]; uint8_t gref[4][16];
    /* the PC Engine palettes: the Genesis palette (-1 none), each index's slot (0 none), the map's version */
    int8_t g[16]; uint8_t m[16][16]; uint16_t ver[16];
    /* the PC Engine tiles: their words when last seen, their version, the colour indices they show */
    uint16_t tw[2048][16]; uint16_t tver[2048]; uint16_t tmask[2048];
    /* the cache: (tile, palette) -> slot, with the versions it was made for; the slot's owner and its plane uses */
    uint16_t cslot[2048][16]; uint16_t cver[2048][16], ctver[2048][16];
    uint16_t sowner[BG_SLOTS]; uint16_t srefs[BG_SLOTS]; uint32_t sused[BG_SLOTS];
    uint16_t free_n, free_list[BG_SLOTS];
    /* the Genesis plane (64 x 64: tile slot | palette << 13), and the slots' tiles (4 bits a pixel, 16 words) */
    uint16_t gmap[4096]; uint16_t gtile[BG_SLOTS][16];
    int have; uint32_t clock;
    BgStat st;
} Bg;

void bg_init(Bg *b);
/* one picture, at the display's start; returns 0 if the map is not 64 x 64 (left as it was), 2 if its colours or
   tiles did not all fit (the entries that did not, left as they were), else 1 */
int bg_frame(Bg *b, const V32 *v);
#ifndef __sh__
long bg_check(const Bg *b, const V32 *v, int whole);   /* (PC) the pixels that differ: the window, or the whole map */
#endif
#endif
