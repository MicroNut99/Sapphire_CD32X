/* vdc_model.h - M2e: the VDC / VCE state (vdc_model.c), and M2 timing: when the chips do things (vdc_timing.c) */
#pragma once
#include <stdint.h>
typedef struct Vdc {
    uint16_t vram[65536], sat[256], color[512];
    uint8_t select, write_latch, status, vce_cr;
    uint16_t MAWR, MARR, CR, RCR, BXR, BYR, MWR, HSR, HDR, VSR, VDR, VCR, DCR, SOUR, DESR, LENR, DVSSR;
    uint16_t read_buffer, ctaddress;
    uint8_t pending_read, pending_write, dma_pending, dma_running, satb_pending, busy;
    uint16_t pending_read_addr, pending_write_addr, pending_write_latch;

    /* --- timing (vdc_timing.c, docs/13): used when timed is set; times are master clocks (21.48 MHz) --- */
    int timed;
    int64_t now;                                    /* the time the chips have run to */
    int64_t tick_now;                               /* the VDC clock being processed */
    /* the VCE: the line (hblank 237 + 1128) and the frame (vblank 4095 + the rest), the VDC's clock divider */
    int hblank, vblank, lc263, dot_clock;
    int32_t hblank_ctr, vblank_ctr, clkdiv, ratio, scanline, need_sl_reset;
    int exit_req; int64_t exit_time;               /* Mednafen ends a run slice: 1 at line 123, 2 at the frame's end */
    /* the VDC: horizontal and vertical phases and their register caches, the busy counters */
    int hphase, vphase;
    int32_t hphase_ctr, vphase_ctr, myst_ctr, sat_dma, spr_fetch;
    int myst_ph, need_rcr_inc, need_vb_test, need_sat_test, burst_mode, in_exhsync, in_exvsync;
    uint32_t rcr_count, hsw_c, hds_c, hdw_c, hde_c, vds_c, vsw_c, vdw_c, vcr_c;
    uint16_t cr_c, mwr_c, dma_buf;
    int32_t vdma_cyc; int dma_rw;
    /* the IRQ line (status & 0x3F): when it last rose, and the interval before the last status read cleared it */
    int64_t irq_rise, irq_prev_rise, irq_prev_clear;
    long dma_cut;                                   /* VRAM DMAs cut short by the display starting (none expected) */
    /* docs/105: the background's offsets each line draws with (Mednafen's BG_YMoo / BG_YOffset / BG_XOffset): Y set by
       a BYR write or at the display's first line, one up a line; X taken from BXR at the display start's third step.
       lines[raster counter]: (X, Y) as the line took them - the picture's line table */
    uint16_t bg_ymoo, bg_yoff; int need_bgy_inc;
    uint16_t lines[263][2];
} Vdc;
void vdc_init(Vdc *v);
uint8_t vdc_read(Vdc *v, unsigned a);
void vdc_write(Vdc *v, unsigned a, uint8_t val);
uint8_t vce_read(Vdc *v, unsigned a);
void vce_write(Vdc *v, unsigned a, uint8_t val);
void vdc_event(Vdc *v, int kind, uint32_t arg);
void vdc_dma_finish(Vdc *v);
void vdc_commit(Vdc *v);                            /* commit a held access if the VDC isn't busy */

/* vdc_timing.c */
void vt_start(Vdc *v, const uint32_t *r, int64_t t0);   /* from the core's dumpvdcstate (96 u32) at time t0 */
void vt_run(Vdc *v, int64_t t);                     /* run the chips to time t */
int32_t vt_next_event(Vdc *v, int64_t cd_next);     /* master clocks to the VCE's next event (Mednafen CalcNextEvent) */
int vt_irq_at(const Vdc *v, int64_t t);             /* was the VDC's IRQ line high at time t */
void vt_dot_clock(Vdc *v, uint8_t vce_cr);          /* a VCE control write */
extern void (*vdc_line0_hook)(struct Vdc *v);              /* Sapphire docs/55: the display's first line (NULL: none) */
extern void (*vdc_split_hook)(struct Vdc *v, int what);    /* docs/55: 1 a raster match, -1 the display's end, 0 a CR write */
extern void (*vdc_scroll_hook)(struct Vdc *v);              /* docs/105: called after each BXR / BYR write (NULL: none) */
extern void (*vdc_vram_hook)(unsigned addr);              /* docs/101: called with each VRAM word written (NULL: none) */
