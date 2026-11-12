/* rt_bg32.h - Sapphire docs/42: a background beyond the Genesis plane, on the 32X layer (rt_bg32.c) */
#pragma once
#include <stdint.h>
#include "rt_vdc.h"

int bg32_wanted(const V32 *v);
int bg32_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256]);
void bg32_masks(const V32 *v, uint16_t used[16]);       /* (docs/58: bg32_draw in its parts, for two SH2s) */
int bg32_prep(const V32 *v, uint16_t cram[256], uint8_t own[256], const uint16_t used[16]);
void bg32_rows(const V32 *v, uint8_t *fb, int pitch, int y0, int y1);
extern int bg32_black;                                  /* (the entry bg32_draw gave black) */
