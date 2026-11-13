/* rt_bg32.h - Sapphire docs/42: a background beyond the Genesis plane, on the 32X layer (rt_bg32.c) */
#pragma once
#include <stdint.h>
#include "rt_vdc.h"

int bg32_wanted(const V32 *v);
int bg32_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256]);
/* (docs/59) the picture's bands, one after the other: lines l0..l1-1 with the scroll bx and, at l0, the row by; fill 0 its
   tiles, 1 colour 256 (the background off, or a burst frame), 2 colour 0 (the background and the sprites off) */
typedef struct { const V32 *v; int i, n; uint16_t l0, l1, bx, by; uint8_t fill, burst; } B32Band;
void b32_first(B32Band *b, const V32 *v);
int b32_next(B32Band *b);                               /* (0: that was the last) */
int bg32_bands(const V32 *v, uint16_t bl[6], uint16_t bx[5], uint16_t by[5]);   /* (the first 5 in arrays; bl[nb] = 224) */
int bg32_must(const V32 *v);                            /* (the plane cannot show this picture: lines scrolled one by one, a band with the background off) */
void bg32_masks_by(const V32 *v, uint16_t used[16], uint16_t (*mask)(unsigned tile));   /* (bg32_masks from the tiles' kept masks) */
void bg32_masks_rng(const V32 *v, uint16_t used[16], uint16_t (*mask)(unsigned tile), int y0, int y1);   /* (docs/60: ... of lines y0..y1-1) */
void bg32_masks(const V32 *v, uint16_t used[16]);       /* (docs/58: bg32_draw in its parts, for two SH2s) */
int bg32_prep(const V32 *v, uint16_t cram[256], uint8_t own[256], const uint16_t used[16]);
void bg32_rows(const V32 *v, uint8_t *fb, int pitch, int y0, int y1);
extern uint16_t (*bg32_tile_key)(unsigned tile);        /* (a tile's words' hash from elsewhere - the cart's FPGA; 0: made from VRAM) */
extern int bg32_black;                                  /* (the entry bg32_draw gave black) */
