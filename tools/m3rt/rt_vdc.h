/* rt_vdc.h - M3: the PC Engine's video chips for the 32X runtime (rt_vdc.c, docs/17).
 *
 * The registers, VRAM, the sprite table and the colours as tools/m2host/vdc_model.c has them (that model is checked
 * against Mednafen, docs/11-13), with the timing at the level the game needs: the VCE's line and frame, and in each
 * line the two points where the VDC does something the CPU sees - the vertical blank test at the display's start and
 * the raster counter's step at its end - plus the sprite-table copy and the VRAM DMA. Lines where nothing happens
 * are skipped in one step, so a frame costs a few dozen steps, not the VDC's 90,000 clocks. Accesses are never held
 * (no busy bit). Times are master clocks (21.48 MHz). */
#pragma once
#include <stdint.h>
#include "huc_ops.h"                         /* (VPort: the VRAM data port's write address and latch live there) */

typedef struct VTime {                       /* the timing state and the registers it reads: copied to look ahead */
    /* the times are master clocks after base, in 32 bits (the SH2 compares those in one instruction); far ahead:
       VT_NEVER. The runtime's times are after the same base (rt.c): it moves base up now and then (v32_rebase) */
    int64_t base;
    int32_t now;                             /* the time it has run to */
    int32_t t_hb;                            /* the next VCE horizontal sync end (a line every 1,365) */
    int32_t t_vb;                            /* the next VCE vertical blank toggle */
    int32_t t_hdw, t_hdwf;                   /* this line's display start (the VD test) and end (the raster step) */
    int32_t t_sat_test;                      /* the sprite-table copy's test (the VDC clock after the display ends) */
    int32_t t_sat_end, t_dma_end;            /* the sprite-table copy's and the VRAM DMA's end */
    int32_t t_next;                          /* the first of the times above: before it, running is only now = t */
    int vblank, lc263, ratio;
    int32_t scanline; int need_sl_reset;     /* the VCE's line count (the frame ends where line 257 starts: docs/14) */
    int vphase; int32_t vphase_ctr; uint32_t rcr_count;
    int need_rcr_inc, need_vb_test, need_sat_test, burst_mode;
    uint32_t hds_c, hdw_c, vds_c, vsw_c, vdw_c, vcr_c;
    uint16_t CR, RCR, HSR, HDR, VSR, VDR, VCR, DCR, LENR;
    uint8_t status, satb_pending, dma_pending, dma_running;
    int frame_end;                           /* line 257 has started (the runtime clears it) */
    /* inert lines are skipped past the time asked for (rt_vdc.c): what they were before, while the skip is ahead of
       now, so a timing register written inside them first brings them to now one by one */
    int snap_on; int32_t snap_scanline, snap_vphase_ctr; uint32_t snap_rcr_count; int32_t snap_t_hb;
    long steps;                              /* events processed (what the timing costs) */
    int32_t raised_to;                       /* the raises up to this time are in status already: the runtime took
                                                them from its schedule (rt.c raises_seen, docs/31); not again
                                                (INT32_MIN: none) */
} VTime;

#define VS_MAX 16
/* and, for a change of RCR alone (rt.c, docs/24): each raise's bit (rk), and the raster counter's steps as runs - g_n
   steps a line apart from time g_t, the counter g_c, g_c + 1 ... after them - whole up to g_end */
#define VS_SEG 24
typedef struct VSched {
    int32_t r[VS_MAX], w[VS_MAX], end; int nr, nw;   /* (times after V32's base, as all the runtime's) */
    uint8_t rk[VS_MAX], wk[VS_MAX];                /* (wk: 3 the sprite-table copy's test, 4 its end; HUC_MARKS: 5 the
                                                      display's start, where the picture is drawn - docs/56) */
    int32_t g_t[VS_SEG], g_end; uint32_t g_c[VS_SEG]; int32_t g_n[VS_SEG]; int ng;
} VSched;
/* the schedules made last from a line's start (rt_vdc.c v32_schedule, docs/25): a look ahead is a function of the
   timing state alone, so one made again from the same state there, a frame later, is the same shifted in time. key:
   the state, its times from that line's start; the schedule from there and its result kept from there too */
#define VK_N 36
#define VS_CACHE 4
typedef struct VSCache { int32_t key[VK_N]; int32_t hrel, ret; int frame, valid; VSched o; } VSCache;
typedef struct V32 {                         /* (the registers and the timing first: a few cache lines on the SH2) */
    VTime t;
    VPort *vp;                               /* the ports' registers: the Cpu's (docs/19) */
    uint8_t vce_cr;                          /* (select, MARR, the read buffer, the colour address: vp's) */
    uint16_t BXR, BYR, MWR, SOUR, DESR, DVSSR;
    /* (Sapphire docs/16) the raster splits: at each raster compare match the line after it and the BXR / BYR / CR the
       game writes then (its interrupt handler); sp_* this frame's so far, spl_* the last whole frame's (the picture's) */
    struct { uint16_t line, bxr, byr, cr; uint8_t wrote; } sp[4], spl[4];
    int8_t sp_n, sp_cur, spl_n;
    uint16_t sat[256], color[512], vram[32768];
    int sc_next; long sc_hits, sc_made;
    VSCache sc[VS_CACHE];
    /* (Sapphire docs/59) every raster match of the frame, for the 32X-layer background (rt_bg32.c): sp / spl hold four
       (the last match in the last place). The game scrolls line by line in 662 frames of the recording - up to 178
       matches a frame. wrote: bit 1 the handler wrote BYR (the lines then count on from BYR + 1; not written, they
       count on from the band before). xs_* this frame's so far, xsl_* the picture's. At the struct's end: the
       players that know nothing of them (rt_vdc.c's own) leave xsl_n 0 and the layer takes spl */
#define XS_N 226
    struct { uint16_t line, bxr, byr, cr; uint8_t wrote; } xs[XS_N], xsl[XS_N];
    int16_t xs_n, xs_cur, xsl_n;
} V32;

/* the state Mednafen had (tools/m2_trace.py vdc_snapshot: 64 K words of VRAM, the sprite table, the colours, 96
   registers), the chips' clocks taken at time t0 (from the CPU's 0, which is base then: the times after it are the
   runtime's own until it first moves base) */
void v32_load(V32 *v, VPort *vp, const uint16_t *vram, const uint16_t *sat, const uint16_t *color, const uint32_t *r, int64_t t0);
#define VT_NEVER INT32_MAX
/* (every time below is after base) */
void v32_run_events(V32 *v, int32_t t);
void v32_rebase(V32 *v, int32_t d);                  /* base moved up by d: the times after it down by d */
static inline int32_t v32_next(const V32 *v) { return v->t.t_next; }   /* the next event's time */
static inline void v32_touch(V32 *v, int32_t t)      /* time t, before the next event: only the time moves */
{
    if (t > v->t.now) v->t.now = t;
}
static inline void v32_run(V32 *v, int32_t t)        /* run the chips to time t (at every hardware access: cheap) */
{
    if (t >= v32_next(v)) v32_run_events(v, t); else v32_touch(v, t);
}
int32_t v32_next_irq(const V32 *v, int32_t horizon); /* when a status bit next rises, as things stand (or horizon) */
int32_t v32_next_frame_end(const V32 *v, int32_t horizon);   /* when line 257 next starts (or horizon) */
/* the schedule, as things stand, in one look ahead (docs/21): the times a status bit rises and the times of the events
   that read or write VRAM (the sprite-table copy's test and end: the copy, a DMA's start), from now to horizon; with
   frame, only to where line 257 next starts (its time returned, else horizon). end: the lists are whole up to it */
int32_t v32_schedule(V32 *v, int32_t horizon, int frame, VSched *o);
uint8_t v32_read(V32 *v, unsigned a);
void v32_write(V32 *v, unsigned a, uint8_t val);
uint8_t v32_vce_read(V32 *v, unsigned a);
void v32_vce_write(V32 *v, unsigned a, uint8_t val);
#ifdef HUC_MARKS
/* the display's start (the raster counter's run from 0: Mednafen's RCRCount 0, where its picture is drawn from VRAM,
   the sprite table and the colours): the VDC calls it there, in the order of the VRAM port's accesses (it is a VRAM
   event of the schedule, so a write after it waits for it) - docs/56 */
extern void (*v32_line0)(V32 *v);
#ifdef RT_LINES
/* (RT_LINES, PC checks only, docs/63: BXR's (7) or BYR's (8) every write; the display start's time, the access's,
   the last interrupt's lateness and when it was taken: rt.c) */
extern void (*v32_scroll)(V32 *v, int reg); extern int32_t v32_line0_t, v32_access_t, v32_irq_late, v32_irq_take;
#endif
#ifdef VLOG
/* VLOG=1 (docs/18): while the secondary SH2 makes a picture's background list from VRAM, the game's next frame does
   not write VRAM: its writes go to a log (address << 16 | word), applied with their marks once the job is done - at
   the next picture, at a read of VRAM, when the log is full, or at any write after the job's end. While the log is on
   every VDC access takes the runtime's way (left_min: the inline paths' test fails) */
#ifndef VLOG_N
#define VLOG_N 1024
#endif
extern int vlog_on;
extern uint32_t vlog_buf[VLOG_N], vlog_n;
extern int (*vlog_done)(void);           /* (the job has ended: the log can go; the PC's: never) */
extern void (*vlog_wait)(void);          /* (vlog_sync: until the job has ended) */
void vlog_sync(void);                    /* the job waited for, the log applied, off */
void vlog_start(VPort *p);
void vlog_sat(uint16_t *sat, unsigned src, unsigned len);   /* (the log's words over a sprite table's copy) */
#define VLOG_FORCE(p) do { if (vlog_on && vlog_done()) vlog_sync(); if (vlog_on) { (p)->left_min = 0x7FFFFFFF; (p)->left_min1 = VP_NEVER; } } while (0)
#define VWRITE(p, a, w) do { if (vlog_on && (vlog_n == VLOG_N || vlog_done())) vlog_sync(); \
    VW_SRC0(a); if (vlog_on) vlog_buf[vlog_n++] = (uint32_t)(a) << 16 | (uint16_t)(w); \
    else { (p)->vram[a] = (uint16_t)(w); VMARK(p, a); } } while (0)
#define VREAD_SYNC() do { if (vlog_on) vlog_sync(); } while (0)
#define VLOG_ON_() (vlog_on && (!vlog_done() || (vlog_sync(), 0)))
#else
#define VLOG_ON_() 0
#define VLOG_FORCE(p) ((void)0)
#define VWRITE(p, a, w) do { VW_SRC0(a); (p)->vram[a] = (uint16_t)(w); VMARK(p, a); } while (0)
#define VREAD_SYNC() ((void)0)
#endif
#ifdef VW_COUNT                          /* (docs/18: VRAM traffic measured on the PC) */
extern unsigned long vw_n[6];            /* port writes, TIA from the card, block-move other, VRAM-VRAM DMA, port reads, SAT DMA */
#define VWC(k, n) (vw_n[k] += (n))
extern uint32_t vw_src[32768]; extern int vw_card;   /* (each word's card byte + 1, 0: not the card's) */
#define VW_SRC0(a) (vw_card ? (void)0 : (void)(vw_src[a] = 0))
#else
#define VWC(k, n) ((void)0)
#define VW_SRC0(a) ((void)0)
#endif
#endif

