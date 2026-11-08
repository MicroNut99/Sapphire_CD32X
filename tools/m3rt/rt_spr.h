/* rt_spr.h - M3, the picture: the PC Engine's sprites drawn into the 32X frame buffer (docs/56) */
#ifndef RT_SPR_H
#define RT_SPR_H
#include <stdint.h>
#include "rt.h"

#define SPR_LINE 320                /* bytes a frame buffer line */
#define SPR_LINES 224
#ifndef SPR_SC
#define SPR_SC 32                   /* the converted cells' cache (docs/55; Sapphire docs/16: SPR_SC=n) */
#endif
#define SPR_CELL 288                /* a cell's bytes (tools/m3_rip.py nibbles) */

/* one frame buffer as the drawing sees it: the overwrite area's and the normal area's byte for the picture's
   (0, 0) (the PC Engine's 256 columns start at the 32X's x 32), and the cache's place in the buffer's spare part */
typedef struct SprBuf { uint8_t *ovl, *nrm, *tail; } SprBuf;
extern SprBuf spr_buf[2];

typedef struct SprStat { uint32_t frames, sprites, cells, cart, hits, conv, conv_max, under_way, skipped; } SprStat;
extern SprStat spr_stat;

void spr_init(const uint8_t *cells);           /* m3_rip.py cells' file (in the cart), or 0: every cell converted */
void spr_frame(const V32 *v, int buf);         /* clear what buf showed last, draw v's sprite table into it */
void spr_last_clear(int buf);
#ifdef SPR_SPLIT
/* (docs/67) the cells to draw, in order: their source, place, flips (bit 0 H, bit 1 V) and palette */
typedef struct SprCmd { const uint8_t *src; int16_t px, py; uint8_t hv, pal; } SprCmd;
#ifndef SPR_CMDS
#define SPR_CMDS 80
#endif
/* (SPR_CMDS: a full list is drawn whole and started again; docs/17: 64 with LATE=1) */
#ifdef __sh__
#define spr_cmd ((SprCmd *)0x0603F440)  /* (the 960 bytes between the master's stack top and 0x0603F800, which docs/35
                                           left unused: SDRAM has no other room on stage 4) */
#else
extern SprCmd spr_cmd[SPR_CMDS];
#endif
extern int spr_ncmd;
void spr_frame_list(const V32 *v, int buf);    /* steps 1-2 of spr_frame, the cells listed in spr_cmd, not drawn */
void spr_draw_cmds(const SprCmd *k, int n, int buf, int ya, int yb);   /* a list's cells inside lines ya-yb */
#endif                  /* (BG=1: buffer buf's skip state, in its spare part, cleared once) */
#endif
