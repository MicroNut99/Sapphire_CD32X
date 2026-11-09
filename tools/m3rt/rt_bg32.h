/* rt_bg32.h - Sapphire docs/42: a background beyond the Genesis plane, on the 32X layer (rt_bg32.c) */
#pragma once
#include <stdint.h>
#include "rt_vdc.h"

int bg32_wanted(const V32 *v);
int bg32_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256]);
extern int bg32_black;                                  /* (the entry bg32_draw gave black) */
