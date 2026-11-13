/* fifo_synth.h - Sapphire docs/40: docs/24's FIFO from the runtime's picture state (fifo_synth.c) */
#pragma once
#include <stdint.h>
#include "rt_vdc.h"

typedef struct {
    uint16_t vram[32768], color[512], sat[256];             /* what the player has, as these entries left it */
    unsigned dvssr, cur;                                    /* (its DVSSR; the address its next VRAM word goes to) */
    unsigned bxr, cr;                                       /* (docs/59: its BXR and CR after the last picture's entries) */
    int started;
} FifoShadow;

/* the entries for one picture (rt_draw_hook's V32, at the display's start) into out[max]; the count (past max: lost) */
int fifo_synth(FifoShadow *s, const V32 *v, uint32_t *out, int max);
