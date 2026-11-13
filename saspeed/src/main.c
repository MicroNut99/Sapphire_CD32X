/* saspeed - Sapphire CD32X, M3's speed test (docs/09): DraculaX_32X's dxspeed (its docs/17), the translated game on the master SH2.
 *
 * LEVEL00.BIN (file 100, the cart's level slot, SH2 0x02200000): the translated blocks of one trace window's
 * banks, linked there (dxspeed.ld). LEVEL01.BIN (file 201, the resident slot, 0x02080000): the window's starting
 * state and its pad masks ('DXS1', tools/m3_speedtest.py pack). Both are game data: built in the work folder only.
 *
 * The runtime (tools/m3rt) runs the game from that state as fast as it can - no waiting for the 32X's frame - and
 * the SH2's free-running timer measures each PC Engine frame. Then the screen shows: the frames, the SH2 cycles a
 * frame (mean, the busiest, the quietest), the 32X refreshes the whole run took, the state's hashes at the last frame's
 * end (tools/m3rt/rt_pc.c prints the same for the same file: "frame 599 hash <with stack> <without>"), and the
 * runtime's counts. COMM12: 4E20.. the steps, 4B10 done (4EE0 a load failed, 4EE5 a file is wrong). COMM4: the frames
 * run so far; at the end COMM8: the mean, COMM6: the 32X refreshes.
 */
#include <stdint.h>
#include <string.h>
#include "32x.h"
#include "cdapi.h"
#include "font8x8.h"
#include "rt.h"
#ifdef SPR_DRAW
#include "rt_spr.h"
#ifdef VS_SEC
#include "rt_bg2.h"
#endif
/* LATE=1 (docs/17): the secondary draws the sprites the master listed and sends the picture itself, while the master
   runs the next frame. What both SH2s use for the swap is read and written past the cache (SH) */
#if defined(SPR_LATE) || defined(VS_SEC)
#define SH(x) (*(__typeof__(&(x)))((uintptr_t)&(x) | 0x20000000))
#else
#define SH(x) (x)
#endif
#endif

volatile uint32_t gTicks = 0;
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
#ifdef SPR_PACE
static void pace_vbi(void);
void pri_vbi_handler(void) { gTicks++; pace_vbi(); }
#else
void pri_vbi_handler(void) { gTicks++; }
#endif
void sec_dma1_handler(void) { }
/* the secondary SH2 has nothing to do here. It reads a COMM register now and then (one access in about 4,000 clocks):
   ares's bus model (cont=3) takes an SH2's use of the bus from its own accesses, so a loop that never leaves the cache
   would leave its last figures (the boot's) standing for the master's waits; the console has no such memory */
#ifdef SPR_SEC
/* SEC=1 (docs/66): the secondary draws the sprites while the master makes the background's list. A job: its number
   (bit 0 the buffer) and the VDC model, through SDRAM read past the cache (0x20000000 up); the secondary purges its
   cache first (the master changed VRAM, the table and the tags since), the master waits for the job's end before the
   game goes on (nothing the drawing reads may change under it) */
static volatile uint32_t sec_job_, sec_done_; static const V32 *volatile sec_v_;
#ifdef BG32
/* (docs/58) the 32X-layer background's lower half: the secondary draws it while the master draws the upper (it idled
   through bg32_draw, the master in it in 89 of 100 samples at stage 1's start) */
#define BG32_SEC
static volatile uint32_t sec_b32_, sec_b32d_, sec_nobg_; static const V32 *volatile sec_bv_;
#define SEC_NOBG (*(volatile uint32_t *)((uintptr_t)&sec_nobg_ | 0x20000000))
#define SEC_B32  (*(volatile uint32_t *)((uintptr_t)&sec_b32_ | 0x20000000))
#define SEC_B32D (*(volatile uint32_t *)((uintptr_t)&sec_b32d_ | 0x20000000))
#define SEC_BV   (*(const V32 *volatile *)((uintptr_t)&sec_bv_ | 0x20000000))
#endif
#define SEC_JOB  (*(volatile uint32_t *)((uintptr_t)&sec_job_ | 0x20000000))
#define SEC_DONE (*(volatile uint32_t *)((uintptr_t)&sec_done_ | 0x20000000))
#define SEC_V    (*(const V32 *volatile *)((uintptr_t)&sec_v_ | 0x20000000))
#define SH2_CCR  (*(volatile uint8_t *)0xFFFFFE92)
#ifdef SEC_BG
/* SEC=2: the other way round - the secondary makes the background's list (its count back through SEC_N), the master
   draws the sprites */
static int sec_bg_job(const V32 *v, int buf);
static volatile int sec_nw_; static volatile uint32_t sec_pn_;
#define SEC_PN   (*(volatile uint32_t *)((uintptr_t)&sec_pn_ | 0x20000000))
#ifdef SPR_SPLIT
/* SPLIT=1 (docs/67): the sprites' cells listed by the master, then drawn by both: the master lines 0 to SPLIT_Y - 1,
   the secondary the rest once its background is made (SEC_LIST: the list is there, SEC_NC its cells) */
#ifndef SPLIT_Y
#define SPLIT_Y 112
#endif
static volatile uint32_t sec_list_; static volatile int sec_nc_;
#ifdef SPR_LATE
static volatile uint32_t sec_sent_;
#define SEC_SENT (*(volatile uint32_t *)((uintptr_t)&sec_sent_ | 0x20000000))
static void sec_send(int buf, int nn);
#endif
#define SEC_LIST (*(volatile uint32_t *)((uintptr_t)&sec_list_ | 0x20000000))
#define SEC_NC   (*(volatile int *)((uintptr_t)&sec_nc_ | 0x20000000))
#endif
#define SEC_N    (*(volatile int *)((uintptr_t)&sec_nw_ | 0x20000000))
#ifdef VS_SEC
/* VS_SEC=1 (docs/19): command 78 sent by the secondary once the 68000 has run the picture's list (command 79 op 2 /
   3), outside a blank: the V splits' switch is then at the vblank of the 32X's swap, which the list's end decides */
static volatile uint32_t sec_vs_, sec_vsdone_;
#define SEC_VS     (*(volatile uint32_t *)((uintptr_t)&sec_vs_ | 0x20000000))
#define SEC_VSDONE (*(volatile uint32_t *)((uintptr_t)&sec_vsdone_ | 0x20000000))
static void vsplit_send(void);
static Bg2 bg2;
static volatile uint32_t wdt_vbl; static uint32_t lists_now;
uint32_t bg32_direct;                                         /* (DIAG: pictures swapped by the SH2 itself, docs/58) */
/* (DIAG, docs/59) a picture's way in refreshes, counted from the moment it is asked for: [0] to draw_hook's start, [1] to
   its list sent (or queued for the blank, or the SH2's own swap asked), [2] to its showing; pic_gap: the game frames
   since the picture before. Read from an SDRAM dump (work/vars.py, work/pichist.py) */
uint32_t pic_at[3][8], pic_gap[8], bg32_traffic, bg32_short, bg32_lines; static volatile uint32_t pic_rel_tick;
#define PIC_AT(n) do { const uint32_t d_ = gTicks - pic_rel_tick; pic_at[n][d_ > 7 ? 7 : d_]++; } while (0)
static volatile uint32_t vs_job_v; static volatile int lst_ready_v, lst_op_v;
#define vs_job SH(vs_job_v)
#define lst_ready SH(lst_ready_v)
#define lst_op SH(lst_op_v)
#endif
#endif
void secondary(void)
{
    uint32_t seen = 0;
#ifdef PWM_SOUND
    /* (docs/44) the PWM: 22,044 Hz (23.01 MHz / 1,044), both channels, an interrupt every 3 samples to this SH2,
       which fills the FIFO from the sound ring (crt0.s sec_pwm_irq) */
    MARS_PWM_CYCLE = 1045;
    MARS_PWM_CTRL = 0x0305;
    *(volatile uint8_t *)0x20004001 = 0x01;                   /* (this SH2's interrupts: the PWM's) */
    __asm__ volatile("ldc %0,sr" : : "r"(0x50));             /* (levels 6 and up: the PWM's) */
#endif
    for (;;) {
        uint32_t j;
        while ((j = SEC_JOB) == seen) {
#ifdef BG32_SEC
            static uint32_t seen32; const uint32_t q = SEC_B32;
            if (q != seen32) {                                /* (docs/58: lines 112-223 of a 32X-layer background) */
                seen32 = q; SH2_CCR = 0x11;                   /* (the tables and VRAM are the master's: read again) */
                bg32_rows(SEC_BV, (uint8_t *)&MARS_FRAMEBUFFER + 0x200 + 32, 320, 112, 224);
                SEC_B32D = q;
            }
#endif
            for (int i = 0; i < 200; i++) __asm__ volatile("");
        }
        seen = j;
#ifdef DX_PLAY
        if (j == 0xFFFFFFFFu) {                               /* (docs/70: a stage switch - to the blob's park) */
            extern const uint8_t dx_chain_blob[], dx_chain_sec[];
            __asm__ volatile("ldc %0,sr" : : "r"(0xF0));
            ((void (*)(void))(0x2603F440u + (uint32_t)(dx_chain_sec - dx_chain_blob)))();
        }
#endif
        SH2_CCR = 0x11;                                       /* (purge, cache on: measured as fast as reading the VDC
                                                                 model and the tags past the cache) */
#ifdef SEC_BG
        SEC_N = sec_bg_job(SEC_V, (int)(j & 1));
#ifdef SPR_LATE
        SEC_DONE = j;                                         /* (the background's list made: the master runs the game on) */
        while (SEC_LIST != j) { }
        SH2_CCR = 0x11;
        { extern int spr_ylim; const int yl = *(volatile int *)((uintptr_t)&spr_ylim | 0x20000000);
          spr_draw_cmds((const SprCmd *)((uintptr_t)spr_cmd | 0x20000000), SEC_NC, (int)(j & 1), 0, yl); }
#ifdef VS_SEC
        vs_job = j;
#endif
        sec_send((int)(j & 1), SEC_N);
#ifdef VS_SEC
        if (bg2.vsp_dirty) {
            while (SEC_VS != j) { }
            while (MARS_SYS_COMM0 != 0) { }
            while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }
            vsplit_send();
        }
        SEC_VSDONE = j;
#endif
        SEC_SENT = j;
        continue;
#endif
#ifdef SPR_SPLIT
        while (SEC_LIST != j) { }                             /* (the master's list of the sprites' cells) */
        { extern int spr_ylim; const int yl = *(volatile int *)((uintptr_t)&spr_ylim | 0x20000000);   /* (Sapphire docs/16: the
                                                                 master's line limit, past the cache) */
          spr_draw_cmds((const SprCmd *)((uintptr_t)spr_cmd | 0x20000000), SEC_NC, (int)(j & 1), SPLIT_Y, yl); }
#endif
#else
        spr_frame(SEC_V, (int)(j & 1));
#endif
        SEC_DONE = j;
#ifdef VS_SEC
#ifdef VS_SWAP
        if (0) {                                              /* (docs/26: the master sent it, before the list) */
#else
        if (bg2.vsp_dirty) {
#endif
            while (SEC_VS != j) { }                           /* (the master has sent the list) */
            while (MARS_SYS_COMM0 != 0) { }                   /* (the 68000 has run it) */
            while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }        /* (not in a blank: pace_vbi's) */
            vsplit_send();
        }
        SEC_VSDONE = j;
#endif
    }
}
#else
void secondary(void)
{
    for (;;) {
        for (int i = 0; i < 1000; i++) __asm__ volatile("");
        (void)MARS_SYS_COMM0;
    }
}
#endif
extern void CacheControl(int mode);

#define COMM8_32   (*(volatile uint32_t *)0x20004028)
/* (docs/57) PADVB=1: COMM4 and COMM6 are the stack's PADVB (ledger 37) - the 68000's blank handler writes pad 1 into COMM4
   once COMM6 holds 0x5056 - so this program's own figures do not go there */
#ifdef PADVB
#define SET_COMM4(x) ((void)(x))
#define SET_COMM6(x) ((void)(x))
#else
#define SET_COMM4(x) (MARS_SYS_COMM4 = (x))
#define SET_COMM6(x) (MARS_SYS_COMM6 = (x))
#endif
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
static volatile int cdr_busy_v;                               /* (docs/69: a sector read under way) */
#define cdr_busy SH(cdr_busy_v)
#endif
#define COMM12_16  (*(volatile uint16_t *)0x2000402C)
#define FB         ((volatile uint16_t *)&MARS_FRAMEBUFFER)
#define STATE      ((const uint8_t *)0x02080000)          /* LEVEL01.BIN, cached */
#ifndef FRAMES
#define FRAMES     600                                        /* (FRAMES=n: the Makefile, docs/39) */
#endif

/* ---- the screen: 256 colours, text ---- */
enum { C_BLACK, C_WHITE, C_RED, C_GREEN, C_YELLOW, C_GREY };
static void screen(void)
{
    static const uint16_t pal[6] = { 0x0000, 0x7FFF, 0x001F, 0x03E0, 0x03FF, 0x4210 };
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
    for (int i = 0; i < 256; i++) cram[i] = i < 6 ? pal[i] : 0;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 256; i++) FB[i] = (uint16_t)(0x100 + (i < 224 ? i : 223) * 160);
        for (int i = 0; i < 160 * 224; i++) FB[0x100 + i] = 0;
        const uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
        MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
    }
}
static void show(void)                                        /* the buffer drawn into goes on screen */
{
    const uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
    MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
}
static void text(int x, int y, const char *s, uint8_t c)      /* x a multiple of 8 */
{
    for (; *s; s++, x += 8) {
        int ch = (uint8_t)*s; if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b += 2)
                FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] = (uint16_t)((((g[r] & (0x80 >> b)) ? c : 0) << 8) | ((g[r] & (0x40 >> b)) ? c : 0));
    }
}
static char *dec(char *d, uint32_t v)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    *d = 0; return d;
}
static char *hex8(char *d, uint32_t v)
{
    for (int i = 0; i < 8; i++) *d++ = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15];
    *d = 0; return d;
}
static char *str(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static void line(int y, const char *label, uint32_t v, int hex, uint8_t c)
{
    char b[48]; char *p = str(b, label); if (hex) hex8(p, v); else dec(p, v);
    text(16, y, b, c);
}

/* ---- the timer: FRT at the system clock / 32 (one tick 32 cycles, 91 ms around) ---- */
#define SDTEXT __attribute__((section(".sdtext")))   /* (in SDRAM with the runtime, dxspeed.ld: run each frame, docs/38) */
static SDTEXT uint16_t frt(void) { const uint8_t h = SH2_FRT_FRCH; const uint8_t l = SH2_FRT_FRCL; return (uint16_t)(h << 8 | l); }
uint32_t end_hash[2] __attribute__((used));
volatile uint32_t pwm_on, pwm_rd;                             /* (docs/44: crt0.s sec_pwm_irq's - the PWM fed from the sound ring) */
static uint16_t t_last; static uint32_t t_sum, t_max, t_min = 0xFFFFFFFF, nframes, hash, hash_stack;
static long frames_to_run; static uint32_t frt_check;
uint32_t fifo_frt_check(void) { return frt_check; } static volatile uint32_t ticks_end;
#ifdef CONV_TEST
#include "conv.h"
/* DIAG (docs/45): each frame CONV_TEST background tiles and CONV_CELLS sprite cells of the VRAM converted, to
   measure what the console's own conversion would cost (make EXTRA=...) */
#ifndef CONV_CELLS
#define CONV_CELLS 0
#endif
static uint32_t conv_out[64];
static SDTEXT void conv_frame(Rt *r)
{
    const uint16_t *v = r->vdc.vram;
    for (int i = 0; i < CONV_TEST; i++) conv_tile(v + 0x2000 + 16 * ((i * 37 + (int)r->frame) & 511), conv_out);
    for (int i = 0; i < CONV_CELLS; i++) conv_cell(v + 0x4000 + 64 * ((i * 11 + (int)r->frame) & 127), conv_out);
}
#endif
#if defined(PROF_LOG) || defined(SEC_TIMES) || defined(VS_SEC)
/* DIAG (docs/62, make RTX=-DPROF_LOG): each PC Engine frame's time, its drawing's time, the sprite cells drawn and the
   sprites left as they were, 4 words a frame in the frame buffer's spare part (the speed test draws into one buffer
   and never swaps); copied at the end to SDRAM 0x0600B000 (over the hot code, run no more) for ares's --dump-sdram
   (tools/m3prof/frames.py). The clock: the watchdog timer as an interval timer (system clock / 4096, crt0's overflow
   interrupt counting the wraps at level 2: its handler keeps / 4096) - the FRT can't be one, crt0 clears it at each
   compare match for the interrupts' acknowledge */
#define PROF_LOG_N 600
static volatile uint16_t *const prof_log = (volatile uint16_t *)0x24012000;
static uint32_t prof_n, prof_draw, prof_cells0, prof_skip0, prof_last;
extern volatile unsigned mars_pwdt_ovf_count;
static uint32_t __attribute__((noinline)) wdt_now(void)            /* ticks of 4,096 clocks */
{
    for (;;) {
        const uint32_t c = mars_pwdt_ovf_count; const uint8_t w = SH2_WDT_RTCNT; const uint8_t f = SH2_WDT_RTCSR;
        if (c != mars_pwdt_ovf_count) continue;
        return (c + ((f & 0x80) && w < 128)) << 8 | w;   /* (a wrap whose interrupt is not taken yet) */
    }
}
#if defined(PACE_HIST) || defined(BG2_PROF) || defined(SPR_PROF)
/* DIAG areas (docs/36: they were fixed addresses at 0x0603E800-0x0603EF40, inside the master's stack - a slow picture's
   ring entry overwrote it and the game crashed): [0-31] BG2_PROF, [32-47] its step 4, [48-55] SPR_PROF, [56-63]
   PACE_HIST's refreshes a picture, [64-159] its ring of the last 16 slow pictures (6 words each). The scripts find
   it by its symbol in output.map */
uint32_t diag_area[160];
#define DIAGW ((volatile uint32_t *)((uintptr_t)diag_area | 0x20000000))
#endif
#ifdef SPR_PROF
uint32_t (*spr_clk)(void) = wdt_now;                          /* (DIAG, docs/32) */
#endif
static void prof_init(void)
{
    SH2_WDT_VCR = (uint16_t)(65 << 8 | (SH2_WDT_VCR & 0xFF));     /* vector 65 (crt0: pri_irq, levels 2-3) */
    SH2_INT_IPRA = (uint16_t)((SH2_INT_IPRA & 0xFF0F) | 0x0020);  /* level 2 */
    SH2_WDT_WTCSR_TCNT = 0x5A00;                                  /* the count 0 */
    SH2_WDT_WTCSR_TCNT = 0xA53E;                                  /* interval mode, on, / 4096 */
    prof_last = wdt_now();
}
#ifdef PROF_LOG
static void __attribute__((noinline)) prof_frame(void)
{
    const uint32_t t = wdt_now();
    if (prof_n < PROF_LOG_N) {
        volatile uint16_t *e = prof_log + 4 * prof_n;
#ifdef SPR_DRAW
        e[0] = (uint16_t)(t - prof_last); e[1] = (uint16_t)prof_draw;
        e[2] = (uint16_t)(spr_stat.cells - prof_cells0); e[3] = (uint16_t)(spr_stat.skipped - prof_skip0);
        prof_cells0 = spr_stat.cells; prof_skip0 = spr_stat.skipped;
#else
        e[0] = (uint16_t)(t - prof_last); e[1] = 0; e[2] = 0; e[3] = 0;
#endif
        prof_n++;
    }
    prof_draw = 0; prof_last = t;
}
#endif
#endif
#ifdef RT_COMBO
static void combo_check(Rt *r);
#endif
static SDTEXT void on_frame(Rt *r)
{
#if defined(RT_DIGEST) && defined(DIGEST_FSTOP)
    if (r->frame == DIGEST_FSTOP) {                           /* (DIAG, docs/74: rt_sdump at that frame's end, over the hot
                                                                 code, for ares's --dump-sdram; then stop) */
        volatile uint32_t *o = (volatile uint32_t *)0x2600B000; o[0] = 0x44474653; o[1] = (uint32_t)r->frame;
        COMM8_32 = (uint32_t)rt_sdump(r, (uint8_t *)0x2600B010);
#ifdef RT_ELOG
        { extern volatile uint32_t rt_elog_n; volatile uint32_t *e = (volatile uint32_t *)0x2601E000;   /* (the entries after it) */
          const volatile uint32_t *l = (const volatile uint32_t *)0x24000200; e[0] = 0x44474550; e[1] = rt_elog_n;
          for (uint32_t i = 0; i < 4 * rt_elog_n; i++) e[2 + i] = l[i]; }
#endif
        cd_status(0x4B10); while (1) { }
    }
#endif
#if defined(RT_DIGEST) && defined(DIGEST_FLOG)
    if (r->frame < 3000) ((volatile uint32_t *)0x24014000)[r->frame] = rt_hash(r, 0);   /* (DIAG, docs/74: each frame's
                                                                 hash, at the frame's end; to SDRAM after the run) */
#endif
#ifdef CONV_TEST
    conv_frame(r);
#endif
    const uint16_t t = frt(); const uint32_t d = (uint16_t)(t - t_last);
    t_sum += d; if (d > t_max) t_max = d; if (d < t_min) t_min = d; nframes++;
#ifdef PROF_LOG
    prof_frame();
#endif
    SET_COMM4((short)nframes);                          /* the progress (ares prints COMM4 each second) */
#ifdef RT_COMBO
    combo_check(r);                                           /* (docs/39: the game's code changed to another combination?) */
#endif
    if (r->frame == frames_to_run - 1) {                      /* the last frame's end: rt_pc prints the same two */
        ticks_end = gTicks;                                   /* (the run's time: the hashes not counted) */
        hash_stack = rt_hash(r, 1); hash = rt_hash(r, 0);
        end_hash[0] = hash_stack; end_hash[1] = hash;         /* (docs/39: kept for an SDRAM dump - the picture builds show no end screen) */
    }
    t_last = frt();                                           /* (the hash not counted) */
}

#ifdef RT_DIGEST
/* the validation build (make RT_DIGEST=1, docs/20): each wait's state hash, as rt_pc prints them ("idle"), folded
   into one digest (FNV-1a over the hashes) - shown at the end and left in COMM8 instead of the mean */
static uint32_t digest = 2166136261u, idles_hashed;
#ifdef RT_POOL
static uint32_t pool_fail;                                    /* (the write-backs refused: pool_wb) */
#endif
static void on_idle_digest(Rt *r)
{
    const uint32_t h = rt_hash(r, 0);
    for (int k = 0; k < 4; k++) { digest ^= (h >> (8 * k)) & 0xFF; digest *= 16777619u; }
#ifdef DIGEST_STOP
    if (idles_hashed == DIGEST_STOP) {                        /* (DIAG, docs/71: the hashed state at that wait, over the hot
                                                                 code - run no more - for ares's --dump-sdram; then stop) */
        volatile uint8_t *o = (volatile uint8_t *)0x2600B000;
        o[0] = 'D'; o[1] = 'G'; o[2] = 'S'; o[3] = 'T'; o += 16;
        for (int i = 0; i < 8192; i++) *o++ = r->base_ram[i];
        const uint8_t *q = (const uint8_t *)r->vdc.vram; for (int i = 0; i < 65536; i++) *o++ = q[i];
        q = (const uint8_t *)r->vdc.sat; for (int i = 0; i < 512; i++) *o++ = q[i];
        q = (const uint8_t *)r->vdc.color; for (int i = 0; i < 1024; i++) *o++ = q[i];
        for (int b = 0x68; b < 0x88; b++) {                   /* (each RAM bank's FNV-1a, the bank's address, then the
                                                                 bank DIGEST_BANK whole) */
            uint32_t f = 2166136261u; const uint8_t *k = r->bank[b];
            for (int i = 0; i < 8192; i++) { f ^= k[i]; f *= 16777619u; }
            const uint32_t a = (uint32_t)(uintptr_t)k;
            *o++ = (uint8_t)(f >> 24); *o++ = (uint8_t)(f >> 16); *o++ = (uint8_t)(f >> 8); *o++ = (uint8_t)f;
            *o++ = (uint8_t)(a >> 24); *o++ = (uint8_t)(a >> 16); *o++ = (uint8_t)(a >> 8); *o++ = (uint8_t)a;
        }
#ifdef DIGEST_BANK
        { const uint8_t *k = r->bank[DIGEST_BANK]; for (int i = 0; i < 8192; i++) *o++ = k[i]; }
#endif
#ifdef RT_POOL
        { const uint32_t q4[4] = { (uint32_t)rt_pool_takes, (uint32_t)rt_pool_wbs, pool_fail, (uint32_t)(uintptr_t)r->bank[0x6A] };
          for (int i = 0; i < 4; i++) { *o++ = (uint8_t)(q4[i] >> 24); *o++ = (uint8_t)(q4[i] >> 16); *o++ = (uint8_t)(q4[i] >> 8); *o++ = (uint8_t)q4[i]; } }
#endif
        COMM8_32 = h; cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef DIGEST_LOG
    if (idles_hashed < 12000) ((volatile uint32_t *)0x24014000)[idles_hashed] = h;   /* (DIAG, docs/71: each wait's hash, in
                                                                 the frame buffer past the picture; to SDRAM at the end) */
#endif
    idles_hashed++;
}
#endif
#ifdef SPR_DRAW
/* the sprites drawn at each display start (make SPR=1, docs/56): into the buffer not shown - always the same one here,
   as the speed test does not swap (its cost, not its picture: the picture is the paced build's) */
static uint32_t spr_ticks;                                    /* (the drawing's time, FRT ticks of 32 clocks) */
static uint16_t frt_(void) { const uint8_t h = SH2_FRT_FRCH; const uint8_t l = SH2_FRT_FRCL; return (uint16_t)(h << 8 | l); }
#ifndef SPR_PACE
#ifdef PROF_LOG
static void draw_hook(V32 *v) { const uint32_t a = wdt_now(); spr_frame(v, 0); prof_draw += wdt_now() - a; }
#else
static void draw_hook(V32 *v) { const uint16_t a = frt_(); spr_frame(v, 0); spr_ticks += (uint16_t)(frt_() - a); }
#endif
#else
/* PACE=1 (docs/57): one PC Engine picture a 32X refresh, as docs/08's motion test. At each display start: wait for the
   swap asked for last time (the picture before is on screen from that vblank) and write its colours then; take this
   picture's sprite colours; draw it into the buffer not shown, its number (9 bits) in the left border; ask for the
   swap. The colours: entry p * 16 + i is the PC Engine's sprite colour p, i; 16 and 32 (never drawn: pixel 0) the
   border's black and the number's white. ares's screenshots against rt_pc RUN_SPR_DUMP: tools/m3_sprcheck.py */
#ifdef BG2_DRAW
#include "rt_bg2.h"
/* BG=1 (docs/65): the background on the Genesis. rt_bg2 writes each picture's list into the frame buffer's spare
   part; command 79 (FBVDP) runs it on the 68000 while the SH2 waits (the frame buffer is the 68000's then: FM), and
   its kept runs - the scroll, the colours - are committed after the swap, from the vertical blank's interrupt
   (op 1: the 68000's own RAM, no frame buffer). bg2_wait: a commit still under way before the next command */
static Bg2 bg2;
static volatile int bg2_given_v;                                /* (the frame buffer is the 68000s: op 0 or the commit) */
static uint32_t bg2_sent, bg2_words, bg2_fail, bg2_late;
static volatile int bg2_pairs_v;                                /* (the picture's list has pairs: op 1 after its swap) */     /* (late: blanks the list was still running at) */
#define bg2_given SH(bg2_given_v)
#define bg2_pairs SH(bg2_pairs_v)
static void bg2_wait(void) { for (int n = 0; n < 2000000 && MARS_SYS_COMM0 != 0; n++) { }
#ifdef SA_LOG
    { extern uint32_t sa_to[8]; if (MARS_SYS_COMM0 != 0) sa_to[4]++; }
#endif
}
static int bg2_run(uint32_t off)                              /* a list at off in the buffer seen: 1 done */
{
    bg2_wait();
    volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
    *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }          /* (the frame buffer to the 68000) */
    MARS_SYS_COMM2 = 0; COMM8_32 = off; COMM12_16 = 0xFFFF;
    const int ok = cd_cmd(79) && COMM12_16 == 1;
    *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }       /* (and back) */
    return ok;
}
/* op 2 / 3: the picture's list, the 32X's swap to frame buffer op - 2, the kept runs at its vblank; op 1 (the commit,
   from the vertical blank's interrupt once the swap has been): the tables' pairs into the buffer the SH2 draws in
   next. Either way the frame buffer is the 68000's until bg2_take; nothing touches it, the 32X's registers or its
   palette meanwhile (the game's code does not; draw_hook and pace_vbi take it back first) */
/* (Sapphire docs/16) command 78 (VSPLIT): the bands' V splits rt_bg2 made, entry 0 the top's; then shown from the
   picture whose swap is asked for next (the stack's double-buffered list, ledger 21a). Only when they changed */
static void vsplit_send(void)
{
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
    bg2_wait();
    for (int k = 0; k < bg2.vsp_n; k++) { MARS_SYS_COMM2 = (short)k; COMM8_32 = bg2.vsp_e[k]; cd_cmd(78); }
    MARS_SYS_COMM2 = (short)(0x8000 | (bg2.vsp_n > 1 ? bg2.vsp_n : 0)); cd_cmd(78);
    bg2.vsp_dirty = 0;
    cdr_busy = 0;
}
static void bg2_send(uint32_t off, int op)                    /* op 2 / 3, not waited for: bg2_take waits */
{
    bg2_wait();
    *(volatile uint8_t *)0x20004000 = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }
    bg2_given = 1;
    MARS_SYS_COMM2 = (short)op; COMM8_32 = off; COMM12_16 = 0xFFFF; MARS_SYS_COMM0 = 79;
}
static void bg2_commit(void)
{
    if (MARS_SYS_COMM0 != 0) return;
    *(volatile uint8_t *)0x20004000 = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }
    bg2_given = 1;
    MARS_SYS_COMM2 = 1; MARS_SYS_COMM0 = 79;
}
static void bg2_take(void)
{
    bg2_wait();
    if (bg2_given) { *(volatile uint8_t *)0x20004000 = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { } bg2_given = 0; }
}
#ifdef SEC_BG
static void pic_extras(const V32 *v, uint32_t num);
#ifdef BG2_PROF
static uint8_t sec_clk(void) { return SH2_WDT_RTCNT; }       /* (DIAG, docs/30: the secondary's own watchdog, no interrupt) */
#endif
static int sec_bg_job(const V32 *v, int buf) {
#ifdef BG2_PROF
    static int on; if (!on) { on = 1; SH2_WDT_WTCSR_TCNT = 0x5A00; SH2_WDT_WTCSR_TCNT = 0xA53E; extern uint8_t (*bg2_clk)(void); bg2_clk = sec_clk; }
    const uint8_t t0 = sec_clk();
#endif
    pic_extras(v, SEC_PN);                                    /* (measured: the master's is the longer side) */
#ifdef BG2_PROF
    { const uint8_t t1 = sec_clk(); DIAGW[31] += (uint8_t)(t1 - t0); }
#endif
#ifdef BG32_SEC
    if (SEC_NOBG) return 0;                                   /* (docs/58: a 32X-layer background covers the plane and the
                                                                 plane's pack failed - no list, the swap alone; bg2_frame
                                                                 finds what changed by its tags when it next runs) */
#endif
    const int n = bg2_frame(&bg2, v, buf); return bg2.jn ? n | 0x8000 : n; }
#endif
static void bg2_setup(void)
{   /* the Genesis for it (rt_bg2.h's map): display off, plane A and B at 0xC000 (B: the same map under A, the same
       scroll), the sprite table 0xF000 (sprite 0: off), the H scroll table 0xE000 (one value: full-screen), 64 x 64
       cells, no window, the backdrop palette 0 colour 0 (black), H40, increment 2; then the display on. The 32X's
       colour 0 see-through; its borders (x 0-31, 288-319: the PC Engine shows 256) black, in both buffers */
    volatile uint16_t *L = (volatile uint16_t *)(0x24000000 + BG2_LIST); int n = 0;
    static const uint16_t regs[] = { 0x8134, 0x8230, 0x8406, 0x8578, 0x8D38, 0x8B03, 0x9011, 0x9100, 0x9200, 0x8700, 0x8C81, 0x8F02 };   /* (Sapphire docs/16: 0x8B03, the H scroll per line) */
    L[n++] = (uint16_t)(0x1000 | (sizeof regs / 2)); for (unsigned i = 0; i < sizeof regs / 2; i++) L[n++] = regs[i];
    L[n++] = 0x1002; L[n++] = 0x7000; L[n++] = 0x0003; L[n++] = 0x2004; L[n++] = 0; L[n++] = 0; L[n++] = 0; L[n++] = 0;   /* SAT 0xF000 */
    L[n++] = 0x1002; L[n++] = 0x6000; L[n++] = 0x0003; L[n++] = 0x2002; L[n++] = 32; L[n++] = 32;                         /* H scroll */
    L[n++] = 0x1002; L[n++] = 0x4000; L[n++] = 0x0010; L[n++] = 0x2002; L[n++] = 0; L[n++] = 0;                             /* VSRAM */
    L[n++] = 0x1002; L[n++] = 0xC000; L[n++] = 0x0000; L[n++] = 0x2001; L[n++] = 0;                                         /* CRAM 0 */
    L[n++] = 0x1002; L[n++] = 0x4000; L[n++] = 0x0003; L[n++] = 0x2000 | 2048;                                            /* plane A: 0 */
    for (int i = 0; i < 2048; i++) L[n++] = 0;
    L[n++] = 0x1002; L[n++] = 0x5000; L[n++] = 0x0003; L[n++] = 0x2000 | 2048;
    for (int i = 0; i < 2048; i++) L[n++] = 0;
    L[n++] = 0x1001; L[n++] = 0x8174;                                                                                      /* display on */
    L[n++] = 0;
    if (!bg2_run(BG2_LIST)) { bg2_fail++; cd_status(0x4EE8); while (1) { } }
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256 | MARS_VDP_PRIO_32X;   /* (the 32X in front: its colour 0
                                                                                  see-through, the rest over the plane) */
    while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
    cram[0] = 0x8000;
    bg2_init(&bg2, (uint16_t *)0x24000000, (uint16_t *)0x24000000);
    for (int b = 0; b < 2; b++) {
        const int cur = MARS_VDP_FBCTL & MARS_VDP_FS;
        for (int y = 0; y < 224; y++) for (int k = 0; k < 160; k++) FB[0x100 + y * 160 + k] = (k < 16 || k >= 144) ? 0x1010 : 0;
        bg2_fill(&bg2, cur ^ 1); spr_last_clear(cur ^ 1);
        MARS_VDP_FBCTL = (uint16_t)(cur ^ 1); while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
    }
}
#endif
static uint16_t cram_shadow[256], cram_pend[256];
static volatile int pace_req_v = -1;
#define pace_req SH(pace_req_v)
static uint32_t pace_n, pace_last, pace_missed, pace_worst, pace_shown_at;
static volatile int pal_go_v = -1;                            /* (the buffer whose colours are in cram_pend) */
#define pal_go SH(pal_go_v)
#ifdef SPR_LATE
static void sec_send(int buf, int nn)                         /* (docs/17: the secondary sends its picture) */
{
    const int n = nn & 0x7FFF; bg2_pairs = (nn & 0x8000) != 0;
    if (n == 0) FB[BG2_LIST >> 1] = 0;
#ifdef VS_SEC
    (void)0;                                                  /* (docs/19: pace_vbi sends the list at the blank; 78 after) */
    pace_req = buf; pal_go = buf; lst_op = 2 + buf; lst_ready = 1;
#else
    if (bg2.vsp_dirty) vsplit_send();
    bg2_send(BG2_LIST, 2 + buf);
    pace_req = buf; pal_go = buf;
#endif
}
#endif
static int pal_dirty;                                         /* (cram_pend differs from what the 32X has) */
#define PAL_DIRTY (*(volatile int *)((uintptr_t)&pal_dirty | 0x20000000))   /* (SEC=2: the secondary's: past the cache) */
/* the colours at the vblank the picture comes on in, however late the next drawing is */
#ifdef BG2_DRAW
/* BG=1 (docs/65, docs/66): the list goes as command 79 op 2 / 3, so the 68000 itself asks for the 32X's swap when the
   list has run and writes the kept runs (the scroll, the background's colours) from its vblank interrupt at the vblank
   the swap shows at: the same line as the 32X's picture, however late the Sub-CPU is. Here, in the blank that shows
   it: the sprites' colours, then op 1, the tables' pairs into the buffer drawn in next. A list still running, or a
   swap not yet shown: the next blank (the 68000's FBCTL write takes effect there) */
static void pace_vbi(void)
{
#ifdef VS_SEC
    wdt_vbl = wdt_now();                                      /* (docs/25: the line now, from the watchdog's time) */
    if (lst_ready) {                                          /* (docs/19: the picture's list, at the blank's start) */
        if (cdr_busy || MARS_SYS_COMM0 != 0) { bg2_late++; return; }
        bg2_send(BG2_LIST, lst_op); lst_ready = 0; SEC_VS = vs_job;
        return;
    }
#endif
    if (pal_go < 0) return;
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
    if (cdr_busy) { bg2_late++; return; }                     /* (a sector read or command 78 under way: the COMM registers are its) */
#endif
    if (MARS_SYS_COMM0 != 0) {                                /* (the command not answered yet: the frame buffer the 68000's) */
        if (PAL_DIRTY && (uint16_t)MARS_SYS_COMM10 == (uint16_t)(0xFB00 | pal_go))
            for (int n = 0; n < 12000 && MARS_SYS_COMM0 != 0; n++) { }   /* (the swap asked for before this blank: it
                                                                 is at this blank, the sprites' new colours must be too -
                                                                 wait for the command's end, docs/68) */
        if (MARS_SYS_COMM0 != 0) { bg2_late++; return; }      /* (else its swap is at a later blank: a list that ends in
                                                                 a blank waits for the display first) */
    }
    bg2_take();                                               /* (the frame buffer back) */
    if ((MARS_VDP_FBCTL & MARS_VDP_FS) != pal_go) { bg2_late++; return; }
    if (PAL_DIRTY) {
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        const volatile uint16_t *pend = (const volatile uint16_t *)((uintptr_t)cram_pend | 0x20000000);
        for (int i = 1; i < 256; i++) if ((i & 15) && pend[i] != cram_shadow[i]) cram[i] = cram_shadow[i] = pend[i];
        if (pend[0] != cram_shadow[0]) cram[0] = cram_shadow[0] = pend[0];   /* (docs/59: the backdrop) */
        PAL_DIRTY = 0;
    }
    if (bg2_pairs) bg2_commit();                              /* (op 1 only for pairs: the kept runs are the 68000's) */
    pal_go = -1; PIC_AT(2);
}
#else
static void pace_vbi(void)
{
    if (pal_go < 0 || (MARS_VDP_FBCTL & MARS_VDP_FS) != pal_go) return;
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    for (int i = 1; i < 256; i++) if ((i & 15) && cram_pend[i] != cram_shadow[i]) cram[i] = cram_shadow[i] = cram_pend[i];
    pal_go = -1;
}
#endif
#ifdef BG32
#include "rt_bg32.h"
static uint16_t bg32_cram[256]; static uint8_t bg32_own[256]; static int bg32_on;   /* (docs/42) */
int bg32_cols;                                                /* (docs/58: the last counted window's colours) */
uint32_t bg2_fpga_tile(unsigned t);
#if defined(BG32) && defined(BG2_FPGA_TILES)
/* (docs/59, tried and taken out) The FPGA's long for a tile - its hash and colour mask, asked twice a picture for
   each of 957 tiles, a cart read each - kept in SDRAM under the tile's write tag (rt_marks.v): the FPGA was asked
   once in 300 pictures, and the pictures were slower: 26.3 a second for 28.0 at game frame 16,284, 22.0 for 25.0 at
   72,284, the master's half of the background 0.83 refresh for 0.72. A 16 KB table read at random throws the lines
   out of the 4 KB cache that the band's loop lives in; the cart read goes past the cache and costs less than that */
/* (docs/59) ... each its own word of the long: one bus cycle, not two (the hash the first word, the mask the second) */
static uint16_t bg32_fpga_key(unsigned t) { return *(volatile uint16_t *)(0x22000000u + 0x1B8000u + (t & 2047) * 4u); }
static uint16_t bg32_fpga_mask(unsigned t) { return *(volatile uint16_t *)(0x22000000u + 0x1B8000u + (t & 2047) * 4u + 2u); }   /* (its colour mask, for bg32_masks_by) */
#endif
#if defined(FIFO_LIVE) && defined(SAPPHIRE_PLAY) && defined(SP_MENU)
#endif
#endif
static const uint8_t c5[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };   /* (tools/m1b_motion.py c32word: v * 255 / 7, top 5 bits) */
/* the picture's sprite colours (cram_pend, pal_dirty) and its number in the left border - the master's, or with SEC=2
   the secondary's (the master reads cram_pend and pal_dirty past its cache then: pace_vbi) */
static void pic_extras(const V32 *v, uint32_t num)
{
    int pd = 0;
    for (int i = 1; i < 256; i++) if (i & 15) {
        const unsigned c = v->color[256 + i];
        cram_pend[i] = (uint16_t)(c5[c & 7] << 10 | c5[(c >> 6) & 7] << 5 | c5[(c >> 3) & 7]);
#ifdef BG32
        if (((volatile uint8_t *)((uintptr_t)bg32_own | 0x20000000))[i])   /* (docs/42: the background's own entries) */
            cram_pend[i] = ((volatile uint16_t *)((uintptr_t)bg32_cram | 0x20000000))[i];
#endif
        pd |= cram_pend[i] != cram_shadow[i];
    }
    {   /* (docs/59) Entry 0 - every frame buffer pixel nothing was drawn on. Where the Genesis has no pixel either the
           32X shows it, whatever its "behind the Genesis" bit: it was left black, and stage 2 opens on a sky that is the
           PC Engine's colour 0 (0x106, blue) - black on the plane. Now the PC Engine's backdrop */
        const unsigned c = v->color[0];
        cram_pend[0] = (uint16_t)(0x8000 | c5[c & 7] << 10 | c5[(c >> 6) & 7] << 5 | c5[(c >> 3) & 7]);
        pd |= cram_pend[0] != cram_shadow[0];
    }
    pal_dirty = pd;
    /* (docs/32: only the blocks that differ from what this buffer shows - its number two pictures ago - and every
       block each 64th picture, in case something else wrote there) */
    static uint32_t shown[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
    const int buf = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;       /* (the buffer drawn: not the one shown) */
    const uint32_t chg = (num & 63) < 2 ? 0x1FF : (num ^ shown[buf]) & 0x1FF; shown[buf] = num;
    for (int bit = 0; bit < 9; bit++) {                       /* (the number: blocks at x 4-27, 12 lines each) */
        if (!(chg >> bit & 1)) continue;
#if defined(SAPPHIRE_PLAY) && !defined(PICNUM)
        const uint16_t w = 0x1010;                            /* (docs/46: the played game, no number - PICNUM=1 keeps it) */
#else
        const uint16_t w = (num >> bit) & 1 ? 0x2020 : 0x1010;
#endif
        for (int y = bit * 16; y < bit * 16 + 12; y++) for (int x = 2; x < 14; x++) FB[0x100 + y * 160 + x] = w;
    }
}
#ifdef SEC_TIMES
/* DIAG (RTX=-DSEC_TIMES, docs/66): where a picture's time goes, in watchdog ticks (4,096 clocks), summed over the run
   (read from an SDRAM dump): [0] the game from one display start to the next, [1] waiting for the picture before to
   be shown, [2] the background's list (the master), [3] then waiting for the secondary's sprites, [4] sending the
   list, [5] pictures, [6] hook-to-hook over one refresh's ticks */
uint32_t sec_t[16]; static uint32_t sec_last;   /* (docs/59, the layer's path: [10] the rule and the colour count, [11] the last sprites cleared and the entries, [12] the master's half of the background, [13] waiting for the secondary's, [7] the sprites, [14] the SH2's own swap asked) */
#define SEC_T(i, a) do { const uint32_t n_ = wdt_now(); sec_t[i] += n_ - (a); (a) = n_; } while (0)
#else
#define SEC_T(i, a) do { } while (0)
#endif
#ifdef DX_PLAY
/* PLAY=1 (docs/70): the game played with the controller, and a menu over it. The pad comes each picture through the
   stack's command 48 at the one moment nothing else is under way (the picture before shown, the frame buffer back:
   draw_hook), and goes to the game as the libretro mask at its next frame's end (rt_pad_live). Start opens the menu:
   the game waits inside draw_hook while the 32X layer shows the menu over the Genesis' background (each refresh an
   empty command 79 list for the swap). LEVEL SELECT starts another stage's program (chain.s), LIVES sets the game's
   rest count ($008D and its HUD copy $1616, BCD), CREDITS. A stage switch carries the lives through the 32X's
   palette (entries 0xD0-0xF0, which the sprites never use) */
#ifndef DX_STAGE
#define DX_STAGE 4
#endif
#define DX_FILE_ROM(s) (103 + 3 * (s))                         /* (dxspeed/game_files.h: S<s>ROM, S<s>BLK, S<s>STA) */
#define PLAY_MAGIC 0x4C56
#define M_TXT 0x20                                            /* (white: the number's colour) */
#define M_HI 0x30                                             /* (colours 0x30-0x50: index 0 of sprite palettes 3-5, */
#define M_DIM 0x40                                            /*  never drawn, never written by pace_vbi) */
#define M_BOX 0x50
extern int SetSH2SR(int level);
extern const uint8_t dx_chain_blob[], dx_chain_sec[], dx_chain_end[];
#define CHAIN_AT 0x2603F440u
static uint8_t *play_ram;                                     /* (the game's base RAM: main) */
static uint16_t pad_was;
static int play_lives = 3, play_open = 1;                     /* (open: the menu at the first picture, at power-on) */
static uint32_t pad_mask(uint16_t p)                          /* B: II (attack), C: I (jump), A: RUN, X Y Z MODE: SELECT */
{
    uint32_t m = 0;
    if (p & SEGA_CTRL_B) m |= 1u << 0;
    if (p & SEGA_CTRL_C) m |= 1u << 8;
    if (p & SEGA_CTRL_A) m |= 1u << 3;
    if (p & (SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z | SEGA_CTRL_MODE)) m |= 1u << 2;
    if (p & SEGA_CTRL_UP) m |= 1u << 4;
    if (p & SEGA_CTRL_DOWN) m |= 1u << 5;
    if (p & SEGA_CTRL_LEFT) m |= 1u << 6;
    if (p & SEGA_CTRL_RIGHT) m |= 1u << 7;
    return m;
}
static void lives_set(int n) { const uint8_t b = (uint8_t)((n / 10) << 4 | n % 10); play_ram[0x8D] = b; play_ram[0x1616] = b; }
static void m_fill(int x0, int y0, int x1, int y1, uint8_t c)  /* (x0, x1 even) */
{
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x += 2) FB[0x100 + y * 160 + (x >> 1)] = (uint16_t)(c << 8 | c);
}
static void m_text(int x, int y, const char *t, uint8_t c)    /* (x even) */
{
    for (; *t; t++, x += 8) {
        int ch = (uint8_t)*t; if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++) for (int b = 0; b < 8; b += 2)
            FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] = (uint16_t)(((g[r] & (0x80 >> b)) ? c : M_BOX) << 8 | ((g[r] & (0x40 >> b)) ? c : M_BOX));
    }
}
static void m_mid(int y, const char *t, uint8_t c) { m_text((160 - 4 * (int)strlen(t)) & ~1, y, t, c); }
static void m_clear(void)                                     /* the buffer drawn in: no sprite, nothing remembered */
{
    m_fill(32, 0, 288, 224, 0); spr_last_clear((MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1);
}
static void m_show(void)                                      /* the buffer drawn in on screen at the next blank */
{
    const int b = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
    FB[BG2_LIST >> 1] = 0; bg2_pairs = 0;
    bg2_send(BG2_LIST, 2 + b); pace_req = b; pal_go = b;
    while (pal_go >= 0) { }
    bg2_take();
}
static char *m_num(char *d, int v) { if (v >= 10) *d++ = (char)('0' + v / 10); *d++ = (char)('0' + v % 10); *d = 0; return d; }
static void play_chain(int s)                                 /* (docs/70: never returns) */
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    cram[0xF0] = PLAY_MAGIC; cram[0xE0] = (uint16_t)play_lives; cram[0xD0] = (uint16_t)s;
    for (int k = 0; k < 2; k++) {
        m_fill(0, 0, 320, 224, 0x10); m_fill(48, 96, 272, 128, M_BOX);   /* (opaque black over the Genesis: the loads'
                                                                 progress text goes onto its plane meanwhile) */
        char t[20] = "LOADING STAGE "; m_num(t + 14, s); m_mid(108, t, M_TXT); m_show();
    }
    SetSH2SR(15);
    const uint32_t *a = (const uint32_t *)dx_chain_blob; volatile uint32_t *d = (volatile uint32_t *)CHAIN_AT;
    for (unsigned i = 0; i < (unsigned)(dx_chain_end - dx_chain_blob + 3) / 4; i++) d[i] = a[i];
    SEC_JOB = 0xFFFFFFFFu;                                    /* (the secondary to its park in the blob) */
    volatile uint32_t *parked = (volatile uint32_t *)(CHAIN_AT + (uint32_t)(dx_chain_end - dx_chain_blob) - 4);
    while (*parked != 1) { }
    ((void (*)(int))CHAIN_AT)(DX_FILE_ROM(s));
    while (1) { }
}
static const char *const credits[] = {
    "AKUMAJOU DRACULA X", "CHI NO RONDO", "(C) KONAMI 1993", "", "THE CD32X PORT:", "THE GAME'S OWN CODE,",
    "RECOMPILED FOR THE SH2", "CD32X BOOT STACK", "REFERENCE: MEDNAFEN", 0 };
static void menu(void)
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) { }
    cram[M_HI] = 0x03FF; cram[M_DIM] = 0x4210; cram[M_BOX] = 0x2800;
    static const char *const rows[4] = { "CONTINUE", "LEVEL SELECT", "LIVES", "CREDITS" };
    int page = 0, row = 0, stage = DX_STAGE; const char *msg = 0;
    uint16_t was = cd_pad(0, 0);
    for (;;) {
        m_clear(); m_fill(48, 32, 272, 192, M_BOX);
        if (page == 0) {
            m_mid(40, "DRACULA X  CD32X", M_TXT); m_mid(56, "OPTIONS", M_DIM);
            for (int i = 0; i < 4; i++) {
                m_text(72, 80 + 16 * i, rows[i], i == row ? M_HI : M_TXT);
                if (i == 2) { char t[4]; m_num(t, play_lives); m_text(216, 80 + 16 * i, t, i == row ? M_HI : M_TXT); }
            }
        } else if (page == 1) {
            m_mid(40, "LEVEL SELECT", M_TXT);
            for (int i = 0; i < 9; i++) {
                char t[12] = "STAGE "; m_num(t + 6, i);
                m_text(104, 56 + 11 * i, t, i == stage ? M_HI : M_TXT);
                if (i == DX_STAGE) m_text(176, 56 + 11 * i, "*", M_DIM);
            }
        } else {
            for (int i = 0; credits[i]; i++) m_mid(40 + 12 * i, credits[i], M_TXT);
        }
        if (msg) m_mid(160, msg, M_HI);
        m_mid(176, "PORTED BY MICRONUT99", M_TXT);
        m_show();
        const uint16_t p = cd_pad(0, 0), k = (uint16_t)(p & ~was); was = p;
        if (k) msg = 0;
        const int ok = (k & (SEGA_CTRL_A | SEGA_CTRL_C | SEGA_CTRL_START)) != 0, back = (k & SEGA_CTRL_B) != 0;
        if (page == 0) {
            if (k & SEGA_CTRL_UP) row = (row + 3) & 3;
            if (k & SEGA_CTRL_DOWN) row = (row + 1) & 3;
            if (row == 2 && (k & (SEGA_CTRL_LEFT | SEGA_CTRL_RIGHT | SEGA_CTRL_A | SEGA_CTRL_C))) {
                play_lives += (k & SEGA_CTRL_LEFT) ? -1 : 1;
                if (play_lives < 1) play_lives = 99; if (play_lives > 99) play_lives = 1;
                lives_set(play_lives);
            } else if (back || (ok && row == 0)) break;
            else if (ok && row == 1) page = 1;
            else if (ok && row == 3) page = 2;
        } else if (page == 1) {
            if (k & SEGA_CTRL_UP) stage = stage ? stage - 1 : 8;
            if (k & SEGA_CTRL_DOWN) stage = stage < 8 ? stage + 1 : 0;
            if (back) page = 0;
            else if (ok) { if (cd_open(DX_FILE_ROM(stage)) > 0) play_chain(stage); msg = "NOT ON THIS DISC"; cd_error_clear(); }
        } else if (back || ok) page = 0;
    }
    for (int k = 0; k < 2; k++) { m_clear(); m_show(); }      /* (both buffers empty: the game draws everything again) */
    m_clear();
    pad_was = was; rt_pad_live = (int32_t)pad_mask(was);
    pace_last = gTicks;
}
static void play_pad(void)                                    /* (draw_hook: the picture before shown, nothing under way) */
{
    if (play_open) { play_open = 0; menu(); return; }
    const uint16_t p = cd_pad(0, 0), k = (uint16_t)(p & ~pad_was); pad_was = p;
    rt_pad_live = (int32_t)pad_mask(p);
    if (k & SEGA_CTRL_START) menu();
}
#endif
#ifdef KEEP_TEST
unsigned keep_bad;
#endif

#ifdef SPR_LATE
static uint32_t late_job;
#endif
#ifdef VLOG
#include <stddef.h>
static union { uint8_t b[offsetof(V32, vram)]; uint32_t align; } v32_head;   /* (docs/18) */
static int vlog_job_done(void) { return SEC_DONE == late_job; }
static void vlog_job_wait(void) { while (SEC_DONE != late_job) { } }
#endif
#ifdef PACE_HIST
uint32_t ph_t0, ph_t7, ph_t3;
#endif
#ifdef SAPPHIRE_PLAY
/* SPLAY=1 (docs/36): the game played with pad 1 from the state on, endlessly. B: II, C: I, A: RUN, X Y Z MODE: SELECT */
static uint32_t sp_pad_mask(uint16_t p)
{
    uint32_t m = 0;
    if (p & SEGA_CTRL_B) m |= 1u << 0;
    if (p & SEGA_CTRL_C) m |= 1u << 8;
    if (p & SEGA_CTRL_A) m |= 1u << 3;
    if (p & (SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z | SEGA_CTRL_MODE)) m |= 1u << 2;
    if (p & SEGA_CTRL_UP) m |= 1u << 4;
    if (p & SEGA_CTRL_DOWN) m |= 1u << 5;
    if (p & SEGA_CTRL_LEFT) m |= 1u << 6;
    if (p & SEGA_CTRL_RIGHT) m |= 1u << 7;
    return m;
}
#endif
#if defined(BG32) && defined(BG2_DRAW) && !defined(BG32_ALWAYS) && defined(BG2_FPGA_TILES)
/* (docs/58) Can the window's palettes go into the plane's 4 palettes of 15 colours, a PC Engine palette's colours
   together? used: the indices each palette shows; id: each colour's number (1 up, 60 at most). Greedy - the biggest
   first, into the palette where it adds fewest. On the PC (work/packfit.py, stage 1's first 3,100 frames: 28 of 78
   pictures can pack at all) it agreed with a full search on every picture */
static int popc32(uint32_t x) { x = x - ((x >> 1) & 0x55555555u); x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u); x = (x + (x >> 4)) & 0x0F0F0F0Fu; x += x >> 8; return (int)((x + (x >> 16)) & 0x3F); }
static int pack_fits(const V32 *v, const uint16_t used[16], const uint8_t id[512])
{
    uint32_t m[16][2]; int n[16], ord[16], np = 0;
    for (int p = 0; p < 16; p++) {
        m[p][0] = m[p][1] = 0; if (!(used[p] & 0xFFFE)) continue;
        for (int i = 1; i < 16; i++) if (used[p] >> i & 1) { const unsigned k = id[v->color[p * 16 + i] & 0x1FF] - 1u; m[p][k >> 5 & 1] |= 1u << (k & 31); }
        n[p] = popc32(m[p][0]) + popc32(m[p][1]);
        int at = np++; while (at > 0 && n[ord[at - 1]] < n[p]) { ord[at] = ord[at - 1]; at--; } ord[at] = p;
    }
    uint32_t b[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } }; int bc[4] = { 0, 0, 0, 0 };
    for (int o = 0; o < np; o++) { const int p = ord[o]; int best = -1, bn = 99, bt = 0;
        for (int q = 0; q < 4; q++) { const int tot = popc32(b[q][0] | m[p][0]) + popc32(b[q][1] | m[p][1]);
            if (tot <= 15 && tot - bc[q] < bn) { bn = tot - bc[q]; best = q; bt = tot; } }
        if (best < 0) return 0;
        b[best][0] |= m[p][0]; b[best][1] |= m[p][1]; bc[best] = bt; }
    return 1;
}
#endif
static void draw_hook(V32 *v)
{
#ifdef SEC_TIMES
    uint32_t ta = wdt_now();
    if (sec_last) { sec_t[0] += ta - sec_last; sec_t[5]++; }
    { static uint32_t e0; if (e0 && ta - e0 > 94) sec_t[6]++; e0 = ta; }
#else
    uint32_t ta = 0;
#endif
#ifdef PACE_HIST
    ph_t0 = wdt_now();
#endif
#ifdef VS_SEC
    if (vs_job) while (SEC_VSDONE != vs_job) { }             /* (docs/19: its command 78 sent) */
    SEC_T(9, ta);
#endif
#ifdef SPR_LATE
    if (late_job) while (SEC_SENT != late_job) { }           /* (docs/17: the picture before sent) */
#endif
    if (pace_req >= 0) {
#ifdef BG2_DRAW
        while (pal_go >= 0) { }                               /* (on screen, its colours in: pace_vbi saw the swap) */
        bg2_take();                                           /* (the commit done, the frame buffer back) */
        SEC_T(1, ta);
#else
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) != pace_req || pal_go >= 0) { }   /* (on screen, its colours in) */
#endif
        const uint32_t now = gTicks, took = now - pace_last; pace_last = now;
        if (took > 1) pace_missed += took - 1;
#ifdef PACE_HIST
        DIAGW[56 + (took < 7 ? took : 7)]++;
#endif
        if (took > pace_worst) pace_worst = took;
        pace_shown_at = now;
#ifdef DX_PLAY
        play_pad();
#endif
#ifdef SAPPHIRE_PLAY
#ifndef FIFO_LIVE
        rt_pad_live = (int32_t)sp_pad_mask(cd_pad(0, 0));     /* (docs/36: the pad, at the one moment nothing else is under way) */
#endif

#endif
    } else pace_last = gTicks;
#ifdef BG32
    {   /* (docs/42) a background beyond the Genesis plane's 4 palettes (the title): drawn whole on the 32X layer, behind
           the sprites, before they are listed - the buffer's sprite boxes forgotten, as the background covers them.
           Leaving the mode clears the buffer's picture once */
        static uint8_t had[2];
#if defined(BG2_DRAW) && !defined(BG32_ALWAYS)
#if defined(FIFO_LIVE) && defined(SAPPHIRE_PLAY) && defined(SP_MENU)
#define BG32_EXACT 1
#else
#define BG32_EXACT 1
#endif
        /* (docs/57) ... and only while the Genesis plane's own pack of the colours fails. "More than 4 palettes" alone sent
           most of stage 1 here (84 % of its first pictures), where the plane's pack fits them: 4 palettes of 15 hold a
           picture's 30-50 colours. The pack's answer is the picture before's (bg2.col_ok: the list is made after this);
           left only after 4 pictures that fit. BG32_ALWAYS: the rule as docs/42 had it */
        /* (docs/58) A picture with more than 60 colours can never go into 4 palettes of 15 - the title's 89, the
           intro's 71: always the 32X layer, as before, whatever the pack said of the picture before. The count: the
           window's tiles' colour masks, which the FPGA keeps (docs/31), a long a tile. (The secondary SH2's crash at the
           title with this rule was rt_bg2.c's colours(): 64-entry arrays overrun in a fade - mended there) */
        static int fit_n; static uint16_t bg32_used[16]; int fits = 0;
        /* (docs/59) The plane's state counts only when its list was made for the picture before (ran_prev). `stale`:
           the plane has missed lists (no list is made under the layer while the colours can't pack, docs/58) and is
           not shown before a list has left nothing over. Without it the plane came back as it was left - in ares, at
           stage 1's game frame 9,476, the stage's opening with its score of 0, striped where the new lists had got
           to: "4 palettes or fewer" switched at once, and fit_n counted pictures that had no list */
        static int ran_prev = 1, stale;
        if (!ran_prev) fit_n = 0;
        else if (bg2.col_ok && !bg2.late) { if (fit_n < 4) fit_n++; stale = 0; } else if (!bg2.col_ok) fit_n = 0;
        /* (docs/59) The plane's traffic. A background drawn anew into its tiles every frame - stage 1's boss burns in
           a fire of 4-pixel squares, the whole screen's tiles - has 4 palettes or fewer, so the rules say the plane;
           but its list is more than the 68000 sends in a blank: the plane was late picture after picture (holes
           where the fire should be, a picture every 4-6 refreshes). TRAFFIC_LATE pictures late in a row: the layer
           for TRAFFIC_OFF pictures with no list, then the list again under the layer until it leaves nothing over */
#define TRAFFIC_LATE 5
#define TRAFFIC_OFF 90
        static int late_n, toff;
        if (ran_prev && bg2.late) { if (++late_n >= TRAFFIC_LATE) { toff = TRAFFIC_OFF; stale = 1; late_n = 0; bg32_traffic++; } } else if (ran_prev) late_n = 0;
        int cols_ = 0;
        /* (docs/59) Every shown background on the 32X layer, the plane never: at 16 places of the game (work/bench.sh,
           each from a skipped start) 26.5-30.0 pictures a second, where the rules below gave 20-29 with the plane's
           late lists (a seam where the stage scrolls up, holes in the boss's fire) and its 60 colours. The layer is
           drawn whole each picture by both SH2s from kept tiles and swapped by the SH2 itself; no list, no 68000.
           RTX=-DBG32_NOPREFER: the rules alone, as docs/57-58 */
#ifndef BG32_NOPREFER
        const int pref = (v->t.CR & 0x80) != 0;
#else
        const int pref = 0;
#endif
        /* (docs/59) Lines scrolled one by one (more matches than the picture's four places) or a band with the
           background off: the layer, whatever the colours - the plane can show neither */
        const int must = bg32_must(v);
        const int layer = pref || must;                       /* (the layer whatever the colours: nothing counted for the rules - the
                                                                 palettes' walk, the colours' numbers and the pack's test were a
                                                                 tenth of a refresh a picture) */
        const int wanted = layer ? 1 : bg32_wanted(v);
        int want = wanted;
        if (v->xsl_n > 4) bg32_lines++;                       /* (DIAG: pictures with lines scrolled one by one) */
        if (wanted || stale) {
#ifdef BG2_FPGA_TILES
            uint16_t *const used = bg32_used; static uint8_t seen[512]; int cols = 0;
            bg32_masks_by(v, used, bg32_fpga_mask);           /* (docs/58: each band's window - the score bar's too; docs/59: every band, a map row once) */
            if (!layer) {
                memset(seen, 0, sizeof seen);
                for (int p_ = 0; p_ < 16; p_++) { int i = 1; for (unsigned u = used[p_] >> 1; u; u >>= 1, i++) if (u & 1) {
                    const unsigned c = v->color[p_ * 16 + i] & 0x1FF; if (!seen[c]) seen[c] = (uint8_t)++cols; } }   /* (its number, for pack_fits) */
                fits = cols <= 60 && pack_fits(v, used, seen);
            }
#else
            const int cols = 61;                              /* (no tile masks to count with: docs/42's rule) */
#endif
            bg32_cols = cols; cols_ = cols;
            want = wanted ? (cols > 60 || ((fit_n < 4) && BG32_EXACT)) : 0;
        }
        const int plane_next = !want;                         /* (the rules say the plane) */
        if (plane_next && stale) want = 1;                    /* (... not whole yet: the layer over it once more, its list made) */
        (void)cols_;
#ifdef BG32_SEC
        /* (docs/58) Under the 32X layer the plane's list is made only when its colours can pack (pack_fits: the count
           above, a millisecond): made every picture while the pack fails it took the secondary 34 ms a picture at stage
           1's start - the pack tried anew, the tiles converted again - for a plane nobody sees; asked for every 8th
           picture instead, that picture took four refreshes. When they can pack: every picture, and the plane is
           shown when it has fitted 4 times and is whole. (Should rt_bg2's own pack fail 8 times where pack_fits said
           yes: no list for 64 pictures) */
#ifdef BG32_FORCE                                             /* (DIAG, RTX=-DBG32_FORCE: every picture's background on the 32X
                                                                 layer and no list - the layer's speed alone, also from a
                                                                 SAPM_SKIP start, whose colours pack) */
        want = 1; SEC_NOBG = 1; (void)fits;
#else
        {   static int vain, off;
            if (off) off--;
            if (ran_prev && fits && !bg2.col_ok) { if (++vain > 8) { off = 64; vain = 0; } } else if (ran_prev) vain = 0;
            int run = plane_next || (fits && !off);
            if (toff) { toff--; run = 0; want = 1; }          /* (docs/59: the traffic's rest) */
            if (layer) { run = 0; want = 1; }
            SEC_NOBG = !run; ran_prev = run; if (!run) stale = 1; }
#endif
#endif
        const int tb = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
#else
        const int tb = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1, want = bg32_wanted(v);
#endif
        SEC_T(10, ta);
        if (want || had[tb]) {
            spr_last_clear(tb);
            if (want) {
                /* (docs/58: a tile's row at a time, and by both SH2s - rt_bg32.c) */
                uint8_t *const fbp = (uint8_t *)&MARS_FRAMEBUFFER + 0x200 + 32;
#if defined(BG2_DRAW) && !defined(BG32_ALWAYS) && defined(BG2_FPGA_TILES)
                const uint16_t *const um = bg32_used;         /* (the window's indices, a palette: counted above) */
#else
                uint16_t um[16]; bg32_masks(v, um);
#endif
#ifdef BG2_FPGA_TILES
                bg32_tile_key = bg32_fpga_key;                /* (a tile's words' hash: the FPGA's, as bg2_frame's) */
#endif
                if (!bg32_prep(v, bg32_cram, bg32_own, um)) bg32_short++;   /* (DIAG, docs/59: the 256 entries did not hold the picture's colours) */
#if defined(BG32_SEC) && defined(SPR_SEC)
                { static uint32_t n32; SEC_BV = v; SEC_B32 = ++n32;
                  SEC_T(11, ta);
                  bg32_rows(v, fbp, 320, 0, 112);
                  SEC_T(12, ta);
                  while (SEC_B32D != n32) { }
                  SEC_T(13, ta); }
#else
                bg32_rows(v, fbp, 320, 0, 224);
#endif
                /* (the borders: the opaque black 0x10 both buffers have from the start and from every clear - docs/45;
                   they were written again with the background's own black each picture, 7,168 words - docs/58) */
                if (!had[tb]) for (int y = 0; y < 224; y++) for (int x = 0; x < 16; x++) { FB[0x100 + y * 160 + x] = 0x1010; FB[0x100 + y * 160 + 144 + x] = 0x1010; }
            }
            else {                                            /* (the buffer as at the start: the picture clear, the borders
                                                                 the opaque 0x10 they always have - docs/45) */
                memset(bg32_own, 0, sizeof bg32_own);
                for (int y = 0; y < 224; y++) for (int k = 0; k < 160; k++) FB[0x100 + y * 160 + k] = (k < 16 || k >= 144) ? 0x1010 : 0;
            }
            had[tb] = (uint8_t)want;
        }
        bg32_on = want;
#ifdef BG32_NOSPR
        if (want) { extern int spr_ylim; spr_ylim = 0; }   /* (DIAG, docs/46: the background alone) */
#endif
    }
#endif
#ifndef SEC_BG
    pic_extras(v, pace_n);
#endif
    const int cur = MARS_VDP_FBCTL & MARS_VDP_FS;
#ifdef SPR_SEC
    static uint32_t sec_n;
    const uint32_t job = (++sec_n << 1) | (uint32_t)(cur ^ 1);
#ifdef VLOG
    vlog_sync();                                              /* (docs/18: the frame's VRAM writes in, the job before done) */
    memcpy(&v32_head, v, offsetof(V32, vram));                /* (the registers, the colours, the splits: the job's copy) */
    SEC_V = (const V32 *)&v32_head;
#else
    SEC_V = v;
#endif
#ifdef SEC_BG
    SEC_PN = pace_n;
#endif
    SEC_JOB = job;                                            /* (the sprites: the secondary, meanwhile the background) */
#ifdef SEC_BG
#ifdef SPR_LATE
    spr_frame_list(v, cur ^ 1);
    SEC_NC = spr_ncmd; SEC_LIST = job; late_job = job;       /* (the secondary draws them, then sends the picture) */
#ifdef VLOG
    vlog_start(v->vp);                                        /* (docs/18: the game runs on, its VRAM writes logged) */
#else
    while (SEC_DONE != job) { }                               /* (its background's list made: nothing it reads changes) */
#endif
    pace_n++;
    SET_COMM6((short)pace_missed);
    return;
#elif defined(SPR_SPLIT)
    spr_frame_list(v, cur ^ 1);
    SEC_T(8, ta);
    SEC_NC = spr_ncmd; SEC_LIST = job;                        /* (the cells' list: the secondary draws its band) */
    { extern int spr_ylim; spr_draw_cmds(spr_cmd, spr_ncmd, cur ^ 1, 0, SPLIT_Y < spr_ylim ? SPLIT_Y : spr_ylim); }   /* (docs/16) */
#else
    spr_frame(v, cur ^ 1);
#endif
#endif
#else
    spr_frame(v, cur ^ 1);
#endif
    pace_n++;
    SEC_T(7, ta);
#ifdef PACE_HIST
    ph_t7 = wdt_now();
#endif
#ifdef BG2_DRAW
    {
#ifdef SEC_BG
        SEC_T(2, ta);
        while (SEC_DONE != job) { }                           /* (the list made) */
        const int n = SEC_N & 0x7FFF; bg2_pairs = (SEC_N & 0x8000) != 0;
#else
        const int n = bg2_frame(&bg2, v, cur ^ 1); bg2_pairs = bg2.jn != 0;
        SEC_T(2, ta);
#ifdef SPR_SEC
        while (SEC_DONE != job) { }                           /* (the sprites drawn: the 68000 may have the buffer) */
#endif
#endif
        if (n == 0) FB[BG2_LIST >> 1] = 0;                    /* (no list this picture: an empty one, for the swap) */
        SEC_T(3, ta);
#ifdef PACE_HIST
        { extern uint32_t ph_t3; ph_t3 = wdt_now(); }
#endif
        bg2_sent++; bg2_words += (uint32_t)n;
#if defined(BG32_SEC) && defined(VS_SEC)
        /* (docs/58) No list and the plane hidden under the 32X layer: the swap is the SH2's own, at the next blank.
           Through the 68000 (an empty list, command 79) the picture came on a refresh later as often as not. Asked in
           the display, so the blank's interrupt finds the buffer swapped and writes the colours (pace_vbi) */
        if (n == 0 && SEC_NOBG && !bg2_pairs) {
            vs_job = job;
            bg2_take();
            while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }
            pace_req = cur ^ 1; pal_go = cur ^ 1; MARS_VDP_FBCTL = (uint16_t)(cur ^ 1); bg32_direct++;
            SET_COMM6((short)pace_missed);
            SEC_T(14, ta);
#ifdef SEC_TIMES
            sec_last = ta;
#endif
            return;
        }
#endif
#ifdef VS_SEC
        /* (docs/19: the list sent by pace_vbi at the next blank's start, so the 68000 runs it while no raster split is
           due - level 4 is masked while it runs - and command 78 the secondary's, after it) */
        vs_job = job; lst_op = 2 + (cur ^ 1);
#ifdef VS_SWAP
        if (bg2.vsp_dirty) vsplit_send();                     /* (docs/26: before the list; the stack switches it at the swap) */
#endif
#ifndef LIST_WPL
#define LIST_WPL 10   /* (the list's words a line, as the 68000 runs it in the display: docs/25; docs/35 tries lower) */
#endif
#ifndef LIST_FROM
#define LIST_FROM -29 /* (docs/33: LIST_FROM=0, never in the blank, was tried for line 0's late V scroll: no better) */
#endif
        {   /* (docs/25) at once if the 68000 can run it before line 200 (about 10 words a line, the masked raster
               interrupt's line 203 kept clear), else at the blank's start */
            const int tpl = 1, d = (int)((wdt_now() - wdt_vbl) * 262 / 94);   /* (94 ticks a refresh: 2.8 lines a tick) */
            const int line = d - 38, need = n / LIST_WPL + 12;
#ifdef LIST_ANYTIME
            if (!cdr_busy && MARS_SYS_COMM0 == 0) {          /* (DIAG, docs/25: the speed with no line to keep clear) */
#else
            if (tpl && line + need < 196 && !cdr_busy && MARS_SYS_COMM0 == 0 && line >= LIST_FROM) {
#endif
                bg2_send(BG2_LIST, lst_op); SEC_VS = vs_job; lists_now++;
            } else lst_ready = 1;
#ifdef PACE_HIST
            {   /* (docs/28) a ring of 64 pictures: the lines at start / sprites done / list made, the list's words, sent at once */
                static uint32_t ri, all; extern uint32_t ph_t0, ph_t7, ph_t3; const uint32_t te = wdt_now(); all++;
                if ((te - ph_t0) * 262 / 94 > 300) {      /* (the slow ones: lines from the start) */
                    volatile uint32_t *q = DIAGW + 64 + (ri++ & 15) * 6;
                    q[0] = (ph_t7 - ph_t0) * 262 / 94; q[1] = (ph_t3 - ph_t0) * 262 / 94; q[2] = (uint32_t)line; q[3] = (uint32_t)n;
                    q[4] = (uint32_t)spr_stat.conv; q[5] = all; }
            }
#endif
        }
#else
        if (bg2.vsp_dirty) vsplit_send();                     /* (docs/16: before the list that asks for the swap) */
        bg2_send(BG2_LIST, 2 + (cur ^ 1));                    /* (op 2 / 3, not waited for) */
#endif
        SEC_T(4, ta);
#ifdef SEC_TIMES
        sec_last = ta;
#endif
        pace_req = cur ^ 1; pal_go = cur ^ 1;                 /* (the swap: pace_vbi, at the next blank) */
#ifdef KEEP_TEST
        { extern unsigned keep_bad; SET_COMM6((short)keep_bad); }
#else
        SET_COMM6((short)pace_missed);
#endif
        return;
    }
#endif
    /* (an FS write in the vertical blank swaps at once - 32X manual, FS: so never in the blank the picture before came
       on in, or that one is not shown at all) */
    if (pace_req >= 0 && gTicks == pace_shown_at) while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }
    pal_go = cur ^ 1;
    MARS_VDP_FBCTL = (uint16_t)(cur ^ 1); pace_req = cur ^ 1;
    if ((MARS_VDP_FBCTL & MARS_VDP_FS) == pace_req) pace_vbi();   /* (asked in a blank: swapped at once, after its interrupt) */
    SET_COMM6((short)pace_missed);
}
#endif
#endif
#ifdef FIFO_PLAY
/* FIFO=<file> (docs/25): the CPU in the cart's FIFO, recorded (fpga/huc: RUN=1 run_vsys.sh, TB_FIFO), played from the
   cart (LEVEL00.BIN at 0x02200000) at the PC Engine's own rate: frame k is due at refresh k from the start. Its entries
   (docs/24) applied to the VDC model as fifo_v32.c does; at each display start the picture is drawn if no later
   frame is due yet (the newest), else only the frame's splits and marks move on. COMM8: the pictures drawn */
static uint32_t fifo_pics;
#ifdef SPR_FPGA_CELLS
/* (docs/29) the FPGA's cell store: the live cells at cart 0x170000 (cached: the cells the FIFO changed are purged
   before a picture), the frozen ones at 0x194000 (past the cache: written anew each lock); the frozen bits, the
   release, the locked frame and this CPU's frame count through the engine's window (registers 0xc6-0xca, answering after the unlock: 0xcb = 0xa5, then 0x5a) */
#define CS_ACE(off) (*(volatile uint16_t *)(0x22000000u + 0x1C0000u + (off)))
#define CS_WR(r, v) ((void)CS_ACE(0x20000u | (uint32_t)(v) << 9 | (uint32_t)(r) << 1))
#define CS_RD(r) CS_ACE((uint32_t)(r) << 1)
static uint8_t cs_dirty[64];
const uint8_t *spr_fpga_cell(unsigned c)
{
    c &= 511;
    CS_WR(0xc6, c >> 4);
    if (CS_RD(0xc7) >> (c & 15) & 1) return (const uint8_t *)(0x22000000u + 0x194000u + c * 288u);
    return (const uint8_t *)(0x02000000u + 0x170000u + c * 288u);
}
#ifdef BG2_FPGA_TILES
/* (docs/31) tile t's hash << 16 | colour mask as at the lock: the FPGA picks the frozen value or the live one */
uint32_t bg2_fpga_tile(unsigned t) { return *(volatile uint32_t *)(0x22000000u + 0x1B8000u + (t & 2047) * 4u); }
#endif
static void cs_purge(void)
{
#ifdef CS_PURGE_ALL
    for (unsigned g = 0; g < 64; g++) cs_dirty[g] = 0xFF;   /* (DIAG, docs/46: every live line purged) */
#endif
    for (unsigned g = 0; g < 64; g++) if (cs_dirty[g]) {
        for (unsigned c = g * 8; c < g * 8 + 8; c++) if (cs_dirty[g] >> (c & 7) & 1) {
            const uint32_t a0 = (0x02170000u + c * 288u) & ~15u, a1 = 0x02170000u + c * 288u + 288u;
            for (uint32_t a = a0; a < a1; a += 16) *(volatile uint32_t *)(0x40000000u | a) = 0;
        }
        cs_dirty[g] = 0;
    }
}
#endif
static int pic_free(void)
{
#ifdef VS_SEC
    if (vs_job && SEC_VSDONE != vs_job) return 0;
#endif
    return pace_req < 0 || pal_go < 0;
}
#if defined(FIFO_LIVE) && defined(FIFO_CDDA)
/* (docs/41) the CD audio the game asked for (FIFO type B: x = 1 to loop, d = the PC Engine's track, 0 to stop): the
   stack's command 45 on the CD32X disc, where the PC Engine's audio tracks 3-16 are tracks 2-15 */
uint32_t cdda_log[4] __attribute__((used));                    /* (DIAG: calls, the last track, loop, COMM12 after) */
static void fifo_cdda(int loop, unsigned trk)
{
    cdda_log[0]++; cdda_log[1] = trk; cdda_log[2] = (uint32_t)loop;
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
#endif
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }   /* (a command under way: its end - not bg2_take,
                                                                 which drops the list in flight: the title kept the
                                                                 loading screen's tiles, docs/41) */
    if (trk >= 3) cd_music_play((int)trk - 1, !loop); else cd_music_stop();
    cdda_log[3] = COMM12_16;
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
    cdr_busy = 0;
#endif
}
#endif
#if defined(FIFO_LIVE) && defined(SAPPHIRE_PLAY) && defined(SP_MENU)
/* SP_MENU=1 (docs/58; the owner: "Add an options menu like MetalSlug"): START opens it in the live build. The game stops
   under it - the cart's CPU makes a frame only when this program asks - the effects are silenced (the stack's command
   103, volume 0) and the voices' PWM is off. UP / DOWN pick, LEFT / RIGHT change, A / C / START do it, B goes back.
     RESUME
     LIVES           into the game's count (CD RAM $A00C, docs/37) through the cart's poke port (registers 0xcf-0xd1)
     INFINITE LIVES  the count written again every 32 frames
     AUTOFIRE        button II (B) held = pressed every other pair of frames
     MUSIC VOLUME    the CD audio, 0-8 (command 103, COMM2 = 3: the BIOS's fader)
     EFFECTS VOLUME  the PSG on the PCM chip, 0-8 (command 103, COMM2 = 2)
     EXACT COLOURS   ON: a picture whose colours the Genesis plane can't hold is drawn on the 32X layer (exact, slower);
                     OFF: always the plane (the nearest fit, never slow)
     CREDITS
   The menu is the 32X layer, opaque, in entries of the palette nothing else writes (index 0 of sprite palettes 2-5). */
static int opt_lives = 3, opt_inf, opt_auto, opt_mus = 8, opt_fx = 8, opt_req;
#define O_TXT 0x20
#define O_HI 0x30
#define O_DIM 0x40
#define O_BOX 0x50
static void o_fill(int x0, int y0, int x1, int y1, uint8_t c)
{
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x += 2) FB[0x100 + y * 160 + (x >> 1)] = (uint16_t)(c << 8 | c);
}
static void o_text(int x, int y, const char *t, uint8_t c)
{
    for (; *t; t++, x += 8) {
        int ch = (uint8_t)*t; if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++) for (int b = 0; b < 8; b += 2)
            FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] = (uint16_t)(((g[r] & (0x80 >> b)) ? c : O_BOX) << 8 | ((g[r] & (0x40 >> b)) ? c : O_BOX));
    }
}
static void o_mid(int y, const char *t, uint8_t c) { o_text((160 - 4 * (int)strlen(t)) & ~1, y, t, c); }
static void o_show(void)                                      /* the buffer drawn in on screen: an empty list for the swap */
{
    const int b = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
    FB[BG2_LIST >> 1] = 0; bg2_pairs = 0;
#ifdef VS_SEC
    pace_req = b; pal_go = b; lst_op = 2 + b; lst_ready = 1;
#else
    bg2_send(BG2_LIST, 2 + b); pace_req = b; pal_go = b;
#endif
    while (pal_go >= 0) { }
}
static uint16_t opt_pad(void)
{
#ifdef PADVB
    static uint16_t last; const uint16_t w = (uint16_t)MARS_SYS_COMM4; if (w != 0xFFFF) last = (w & 0xF000) == 0xF000 ? 0 : w; return last;
#else
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
    return cd_pad(0, 0);
#endif
}
static void opt_cmd103(unsigned comm2, uint32_t comm8)
{
#ifdef TPSG
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
    MARS_SYS_COMM2 = (uint16_t)comm2; COMM8_32 = comm8; COMM12_16 = 0;
    MARS_SYS_COMM0 = 103;
    for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
#else
    (void)comm2; (void)comm8;
#endif
}
static void opt_poke(unsigned a, unsigned val) { CS_WR(0xcf, a & 0xFF); CS_WR(0xd0, (a >> 8) & 0xFF); CS_WR(0xd1, val & 0xFF); }
static int opt_lives_on;                                      /* (the menu set the lives once: a new game's too) */
static void opt_lives_set(void) { opt_poke(0xC00C, (unsigned)opt_lives); }
static char *o_num(char *d, int v) { if (v >= 10) *d++ = (char)('0' + v / 10); *d++ = (char)('0' + v % 10); *d = 0; return d; }
static void opt_menu(void)
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    static const char *const rows[8] = { "RESUME", "STAGE", "LIVES", "INFINITE LIVES", "AUTOFIRE", "MUSIC VOLUME", "EFFECTS VOLUME", "CREDITS" };
    enum { R_RESUME, R_STAGE, R_LIVES, R_INF, R_AUTO, R_MUS, R_FX, R_CRED, R_N };
    /* (an EXACT COLOURS row was here: off, the Genesis plane even where its 4 palettes can't hold the picture. In ares
       the score bar's colours went first; with the 32X layer as fast as the plane - docs/58 - it bought nothing) */
    static int opt_stage = 1; int warp = 0;
    static const char *const cred[] = { "GINGA FUKEI DENSETSU", "SAPPHIRE", "(C) HUDSON SOFT 1995", "", "THE CD32X PORT:", "THE GAME'S OWN PROGRAM,", "ON THE CART'S OWN CPU",
        "EFFECTS: THE SEGA CD'S PCM CHIP", "MUSIC: CD AUDIO", "CD32X BOOT STACK", "REFERENCE: MEDNAFEN", 0 };
    pwm_on = 0;                                               /* (the voices' ring stands still under the menu) */
    opt_cmd103(2, 0);                                         /* (the effects silent: their notes would hang) */
    bg2_take();                                               /* (the last picture's pairs, op 1, left the 32X's VDP the 68000's:
                                                                 nothing drawn or coloured would land) */
    while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) { }
    cram[O_TXT] = 0x7FFF; cram[O_HI] = 0x03FF; cram[O_DIM] = 0x4210; cram[O_BOX] = 0x2800;
    int row = 0, page = 0, lives_touched = 0; uint16_t was = opt_pad() | SEGA_CTRL_START;
    for (;;) {
        o_fill(0, 0, 320, 224, O_BOX); spr_last_clear((MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1);
        if (page == 0) {
            o_mid(24, "SAPPHIRE  CD32X", O_TXT); o_mid(40, "OPTIONS", O_DIM);
            for (int i = 0; i < R_N; i++) {
                const uint8_t c = i == row ? O_HI : O_TXT; char t[8]; const char *val = 0; const int y = 58 + 13 * i;
                o_text(64, y, rows[i], c);
                if (i == R_STAGE) { o_num(t, opt_stage); val = t; }
                else if (i == R_LIVES) { o_num(t, opt_lives); val = t; }
                else if (i == R_INF) val = opt_inf ? "ON" : "OFF";
                else if (i == R_AUTO) val = opt_auto ? "ON" : "OFF";
                else if (i == R_MUS) { o_num(t, opt_mus); val = t; }
                else if (i == R_FX) { o_num(t, opt_fx); val = t; }
                if (val) { o_text(208, y, "<", O_DIM); o_text(224, y, val, c); o_text(256, y, ">", O_DIM); }
            }
            if (row == R_STAGE) o_mid(180, "A/START: A NEW GAME FROM THIS STAGE", O_DIM);
            o_mid(196, "UP/DOWN PICK  LEFT/RIGHT CHANGE", O_DIM); o_mid(206, "A/START: DO IT   B: BACK", O_DIM);
        } else {
            for (int i = 0; cred[i]; i++) o_mid(28 + 13 * i, cred[i], O_TXT);
            o_mid(196, "PORTED BY MICRONUT99", O_HI);
        }
        o_show();
        while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) { }           /* (the colours again, the 32X's VDP this CPU's: the swap is done) */
        cram[O_TXT] = 0x7FFF; cram[O_HI] = 0x03FF; cram[O_DIM] = 0x4210; cram[O_BOX] = 0x2800;
        uint16_t p, k;
        do { const uint32_t t = gTicks; while (gTicks == t) { } p = opt_pad(); k = (uint16_t)(p & ~was); was = p; } while (!k);
        const int ok = (k & (SEGA_CTRL_A | SEGA_CTRL_C | SEGA_CTRL_START)) != 0, back = (k & SEGA_CTRL_B) != 0;
        const int d = (k & SEGA_CTRL_RIGHT) ? 1 : (k & SEGA_CTRL_LEFT) ? -1 : 0;
        if (page) { if (ok || back) page = 0; continue; }
        if (k & SEGA_CTRL_UP) row = (row + R_N - 1) % R_N;
        if (k & SEGA_CTRL_DOWN) row = (row + 1) % R_N;
        if (back || (ok && row == R_RESUME)) break;
        if (ok && row == R_CRED) { page = 1; continue; }
        if (row == R_STAGE) {                                 /* (left/right the stage, A/START the new game) */
            if (d) { opt_stage += d; if (opt_stage < 1) opt_stage = 5; if (opt_stage > 5) opt_stage = 1; }
            else if (ok) { warp = opt_stage; break; }
            continue;
        }
        const int ch = d ? d : ok ? 1 : 0;
        if (!ch) continue;
        if (row == R_LIVES) { opt_lives += ch; if (opt_lives < 1) opt_lives = 9; if (opt_lives > 9) opt_lives = 1; lives_touched = 1; opt_lives_on = 1; }
        if (row == R_INF) opt_inf = !opt_inf;
        if (row == R_AUTO) opt_auto = !opt_auto;
        if (row == R_MUS) { opt_mus += ch; if (opt_mus < 0) opt_mus = 0; if (opt_mus > 8) opt_mus = 8; opt_cmd103(3, (uint32_t)opt_mus * 0x80); }   /* (heard at once) */
        if (row == R_FX) { opt_fx += ch; if (opt_fx < 0) opt_fx = 0; if (opt_fx > 8) opt_fx = 8; }
    }
    if (!warp && (lives_touched || opt_inf)) opt_lives_set();
    for (int b = 0; b < 2; b++) {                             /* both buffers as the game keeps them: the picture clear, the borders opaque */
        for (int y = 0; y < 224; y++) for (int x = 0; x < 160; x++) FB[0x100 + y * 160 + x] = (x < 16 || x >= 144) ? 0x1010 : 0;
        spr_last_clear((MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1);
        if (!b) o_show();
    }
    memset(bg32_own, 0, sizeof bg32_own);                     /* (a 32X-layer background takes its entries again) */
    opt_cmd103(2 | (unsigned)opt_fx << 8, 0);                 /* (the effects back, at the menu's volume) */
    while (opt_pad() & (SEGA_CTRL_START | SEGA_CTRL_A | SEGA_CTRL_B | SEGA_CTRL_C)) { const uint32_t t = gTicks; while (gTicks == t) { } }   /* (let go first) */
    if (warp) {                                               /* (docs/58: the cart's CPU reset, held to the stage - the engine
                                                                 presses RUN to the game's start and serves the stage's own
                                                                 sectors; the lives, if the menu ever set them, go in when
                                                                 the new game has written its own) */
        CS_WR(0xd3, opt_lives_on ? opt_lives : 0); CS_WR(0xd2, warp);
    }
    pace_last = gTicks;
}
#endif
static const uint8_t bit8_[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
__attribute__((optimize("O2")))                              /* (docs/58: the build is -Os, which calls the library for shifts
                                                                 like e >> 28 too: the master was in it for a quarter of the
                                                                 intro's time) */
static void fifo_play(V32 *v, long frames)
{
#ifdef FIFO_LIVE
    /* (docs/40) the FIFO a ring the cart's CPU writes as the game runs (ares: ARES_HUCHOST): read past the cache - a
       line from the lap before must not answer - and back to its start at 0x3F0000 */
    const uint32_t *f = (const uint32_t *)0x22200000;
#else
    const uint32_t *f = (const uint32_t *)0x02200000;
#endif
    uint32_t t0 = gTicks; long k = 0; unsigned addr = 0;      /* (t0 moves on by a menu's time) */
    v->xs_n = 0; v->xs_cur = -1; v->xsl_n = 0;
#ifdef SPR_FPGA_CELLS
    CS_WR(0xcb, 0xa5); CS_WR(0xcb, 0x5a);                     /* (docs/29: the store's start - its registers answer from here) */
#endif
    while (k < frames) {
        const uint32_t e = __builtin_bswap32(*f++);           /* (the FIFO file is little-endian, as the PC wrote it) */
#ifdef FIFO_LIVE
        if (f == (const uint32_t *)0x223F0000) f = (const uint32_t *)0x22200000;
#endif
        const unsigned t = e >> 28, x = (e >> 16) & 0xFFF, d = e & 0xFFFF;
        switch (t) {
        case 1: if (addr < 0x8000) { v->vram[addr] = (uint16_t)d; rt_marks.v[addr >> 4] = rt_marks.tag;
#ifdef SPR_FPGA_CELLS
                      cs_dirty[addr >> 9] |= bit8_[addr >> 6 & 7];   /* (docs/58: a table - the SH2 shifts by a variable through a
                                                                 library call, and this is every VRAM word the game writes) */
#endif
                  } addr = (addr + 1) & 0xFFFF; break;
        case 2: addr = d; break;
        case 3: {
            const unsigned sel = x >> 1, hi = x & 1;
#define SETB_(r, val) (r) = (uint16_t)(hi ? (((r) & 0x00FF) | (val) << 8) : (((r) & 0xFF00) | (val)))
            switch (sel) {
            case 5: SETB_(v->t.CR, d); if (v->sp_cur >= 0) { v->sp[v->sp_cur].cr = v->t.CR; v->sp[v->sp_cur].wrote |= 4; }
                    if (v->xs_cur >= 0) v->xs[v->xs_cur].cr = v->t.CR; break;
            case 6: SETB_(v->t.RCR, d); v->t.RCR &= 0x3FF; break;
            case 7: SETB_(v->BXR, d); v->BXR &= 0x3FF; if (v->sp_cur >= 0) { v->sp[v->sp_cur].bxr = v->BXR; v->sp[v->sp_cur].wrote |= 1; }
                    if (v->xs_cur >= 0) v->xs[v->xs_cur].bxr = v->BXR; break;
            case 8: SETB_(v->BYR, d); v->BYR &= 0x1FF; if (v->sp_cur >= 0) { v->sp[v->sp_cur].byr = v->BYR; v->sp[v->sp_cur].wrote |= 2; }
                    if (v->xs_cur >= 0) { v->xs[v->xs_cur].byr = v->BYR; v->xs[v->xs_cur].wrote |= 2; } break;
            case 9: SETB_(v->MWR, d); break;
            case 0x13: SETB_(v->DVSSR, d); break;
            }
#undef SETB_
            break;
        }
        case 4: v->color[x & 0x1FF] = (uint16_t)d; break;
        case 5:
#if defined(FIFO_LIVE) && defined(SAPPHIRE_PLAY)
            {   /* (docs/45) the pad to the cart's CPU at each picture (its pad register, 0xcd / 0xce), command 48 sent with
                   nothing else under way. It was in draw_hook, where the FIFO build never came: the game had no pad */
                static uint32_t last = 0xFFFFFFFFu;
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
                cdr_busy = 1;
#endif
#ifdef PADVB
                /* (docs/57) the pad from COMM4, where the 68000 puts it at every vertical blank (the stack's PADVB): no
                   command. Command 48 here, once a game frame, waited for whatever command was under way - a
                   background list the 68000 was still running - and held the game to the picture's rate */
                static int pv_on; if (!pv_on) { pv_on = 1; MARS_SYS_COMM4 = (short)0xFFFF; MARS_SYS_COMM6 = 0x5056; }
                static uint16_t pv_last; { const uint16_t w = (uint16_t)MARS_SYS_COMM4; if (w != 0xFFFF) pv_last = (w & 0xF000) == 0xF000 ? 0 : w; }
                const uint16_t rawp = pv_last; uint32_t m = sp_pad_mask(rawp);
#else
                for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
                const uint16_t rawp = cd_pad(0, 0); uint32_t m = sp_pad_mask(rawp);
#endif
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
                cdr_busy = 0;
#endif
#ifdef SP_MENU
                { static uint16_t st_was; if ((rawp & SEGA_CTRL_START) && !(st_was & SEGA_CTRL_START)) opt_req = 1; st_was = rawp; }
                if (opt_auto && (k & 2)) m &= ~1u;                /* (autofire: II let go every other pair of frames) */
                if (opt_inf && (k & 31) == 0) opt_lives_set();
#else
                (void)rawp;
#endif
                if (m != last) { CS_WR(0xcd, m & 0xFF); CS_WR(0xce, (m >> 8) & 0xFF); last = m; }
            }
#endif
#if defined(FIFO_LIVE) && defined(TPSG)
            {   /* (docs/54) the PSG on the Sega CD's PCM chip: the stack's command 103 (TPSG), once - the Sub-CPU then
                   reads the cart core's PSG write ring (cart 0x07F000: the 68000's 0x8FF000) by itself, every pass */
                static int tp_sent;
                if (!tp_sent) {
                    tp_sent = 1;
                    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
                    MARS_SYS_COMM2 = 1; COMM8_32 = 0x008FF000u; COMM12_16 = 0;
                    MARS_SYS_COMM0 = 103;
                    for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
                }
            }
#endif
#if defined(FIFO_LIVE) && defined(PWM_SOUND)
            if (!pwm_on) {                                    /* (docs/44: the sound on once the ring holds 3,000 samples,
                                                                 played 2,200 behind what was written) */
                const uint16_t n = *(volatile uint16_t *)(0x22000000u + 0x1C0000u + (0xccu << 1));
                if (n >= 3000) { pwm_rd = (uint32_t)(n - 2200); pwm_on = 1; }
            }
#endif
            for (int i = 0; i < v->sp_n; i++) v->spl[i] = v->sp[i];
            v->spl_n = v->sp_n; v->sp_n = 0; v->sp_cur = -1;
            for (int i = 0; i < v->xs_n; i++) v->xsl[i] = v->xs[i];   /* (docs/59: every match, for the layer) */
            v->xsl_n = v->xs_n; v->xs_n = 0; v->xs_cur = -1;
            while ((long)(gTicks - t0) < k) { }                 /* (not due yet: the PC Engine's own rate) */
            /* (docs/27: drawn only when the picture before is on screen - the frames keep coming meanwhile, their
               entries applied while the swap is awaited - and when it is the newest due) */
#ifdef SPR_FPGA_CELLS
            /* (docs/29) released as soon as the picture before is on screen; drawn at the frame the FPGA then locked */
            /* (the release before the count: the store, which follows this count, locks at this very frame) */
            /* (docs/33: a frame drawn at most every 2nd frame - a solid 30, no picture a refresh after the one before) */
            { static int released; static uint32_t want = 0xFFFFFFFFu; static long drawn_k = -2;
              /* (docs/57: and only when this CPU is not behind the game - a picture that takes four frames to draw was
                 asked for every second frame, so the game ran at the picture's rate: 13-27 frames a second in stage 1.
                 Behind by more than FIFO_LAG frames: the frames' entries only, until it has caught up. A picture at
                 least every 12th frame all the same) */
#ifndef FIFO_LAG
#define FIFO_LAG 1
#endif
              /* (docs/58) The game never runs faster than its own rate: what it fell behind beyond FIFO_DEBT frames
                 is forgiven, not made up later (after stage 1's slow start it ran 97 frames a second for 50 s) */
#ifndef FIFO_DEBT
#define FIFO_DEBT 8
#endif
              { const long b0 = (long)(gTicks - t0) - k; if (b0 > FIFO_DEBT) t0 += (uint32_t)(b0 - FIFO_DEBT); }
              const long behind = (long)(gTicks - t0) - k;
#ifdef SP_MENU
#define OPT_HOLD (!opt_req)                                   /* (no new picture asked for once the menu is: it opens when this one is shown) */
#else
#define OPT_HOLD 1
#endif
              if (!released && OPT_HOLD && pic_free() && k - drawn_k >= 2 && (behind <= FIFO_LAG || k - drawn_k >= 12)) { CS_WR(0xc8, 0); released = 1; want = 0xFFFFFFFFu;
                  pic_rel_tick = gTicks; { const long g_ = k - drawn_k; pic_gap[g_ > 7 ? 7 : g_]++; } }
              CS_WR(0xca, (k + 1) & 0xFF);                       /* (the boundaries this CPU has passed: the store's pace) */
              if (released && want == 0xFFFFFFFFu) { const uint16_t l = CS_RD(0xc9); if (l != 0xFFFF) want = l; }
              /* (docs/59: the store's count is 15 bits - 0xFFFF is "not locked yet". Compared whole, no picture was drawn
                 after game frame 65,535: the whole game's run in ares stopped showing pictures in stage 4) */
              if (released && ((uint32_t)(k + 1) & 0x7FFF) == want) { cs_purge(); PIC_AT(0); draw_hook(v); PIC_AT(1); fifo_pics++; released = 0; drawn_k = k; }
#ifdef SP_MENU
              if (opt_req && !released && pic_free() && MARS_SYS_COMM0 == 0) {   /* (docs/58: nothing under way - the menu) */
                  const uint32_t ta = gTicks; opt_req = 0; opt_menu(); t0 += gTicks - ta; drawn_k = k; }
#endif
              }
#else
            if (pic_free() && (long)(gTicks - t0) < k + 1) { draw_hook(v); fifo_pics++; }
#endif
            rt_marks_frame();
            k++;
            break;
        case 6: v->sp_cur = -1; v->xs_cur = -1; break;
#if defined(FIFO_LIVE) && defined(FIFO_CDDA)
        case 0xB: fifo_cdda((int)(x & 1), d); break;          /* (docs/41: FIFO_CDDA=1 - open: the title's pictures stop
                                                                 after it, and no sound comes in ares yet) */
#endif
        case 7: { const int j = v->sp_n < 4 ? v->sp_n++ : 3;   /* (docs/59: a frame with more matches than the four places - the
                                                                 last match keeps the last place, as the stand-in's split():
                                                                 the cart's FIFO has every match, the stand-in's only four) */
                  v->sp[j].line = (uint16_t)d; v->sp[j].bxr = v->BXR; v->sp[j].byr = v->BYR;
                  v->sp[j].cr = v->t.CR; v->sp[j].wrote = 0; v->sp_cur = (int8_t)j;
                  if (v->xs_n < XS_N) { const int q = v->xs_n++; v->xs[q].line = (uint16_t)d; v->xs[q].bxr = v->BXR; v->xs[q].byr = v->BYR;
                                        v->xs[q].cr = v->t.CR; v->xs[q].wrote = 0; v->xs_cur = (int16_t)q; } } break;
        case 9: if (d < 0x8000) { const unsigned len = d > 0x8000 - 0x100 ? 0x8000u - d : 256; memcpy(v->sat, &v->vram[d], len * 2); } break;
        default: break;
        }
    }
    COMM8_32 = fifo_pics;
    { extern uint32_t fifo_frt_check(void); MARS_SYS_COMM10 = (uint16_t)fifo_frt_check(); }
}
#endif
static Rt rt __attribute__((section(".bss.place_rt")));          /* (their places: dxspeed.ld, docs/35) */
static RtState st;
static uint8_t base_ram[8192] __attribute__((aligned(16), section(".bss.place_ram"))), bram[2048];   /* (aligned: the SH2 hot code reads zero-page words, docs/21) */

/* CDR=1 (docs/69): the PC Engine's data track read from the CD32X disc while the game runs. The drive model
   (tools/m2host/scsi_model.c, from the state's own table) asks for a sector when the game's read reaches it; the
   sectors come 16 at a time (32 KB, a chunk of the track) through the stack's command 91 (CDRING, ledger 29) into a
   ring of CDR_N chunks in the cart, read back past the cache. The disc's file: DXTRACK.TRK, file 102 (game_files.h) */
#ifdef RT_CD_RING
#ifdef RT_COMBO
#define CDR_N 12
#define CDR_CART 0x160000u                                    /* (docs/39: cart 0x160000-0x1BFFFF - a power-on state, with the
                                                                 whole game's pad masks, runs to 0x150000) */
#else
#define CDR_N 16
#define CDR_CART 0x140000u                                    /* (cart 0x140000-0x1BFFFF: past LEVEL01's resident slot) */
#endif
static uint16_t cdr_tag[CDR_N]; static uint32_t cdr_misses, cdr_fail;
#endif
#ifdef RT_POOL                                                 /* (Sapphire docs/09: with or without CDR=1) */
/* POOL=1 (docs/72): the RAM banks the game writes, one at a time in an SDRAM slot; the slot's bank before goes back to
   its place in the cart through the frame buffer (the background list's area: free once a list has run) and the
   stack's command 101 (FBCART), 4 KB at a time; then the cache is purged (the cart's lines changed under it) */
static uint8_t pool_slot[1][8192] __attribute__((aligned(16)));
static uint32_t pool_fail;
#ifdef RT_POOL_LOG
static uint32_t pool_nlog;                                    /* (DIAG, docs/74: each take, 4 words, in the frame buffer at
                                                                 0x18000 - free in the validation build - to SDRAM at the end) */
static void pool_log_fb(uint32_t fr, int b, int out, uint32_t h)
{
    if (pool_nlog >= 512) return;
    volatile uint32_t *l = (volatile uint32_t *)0x24018000 + 4 * pool_nlog++;
    l[0] = fr; l[1] = (uint32_t)b << 8 | (uint32_t)(out & 0xFF); l[2] = out >= 0 ? h : 0; l[3] = 0;
}
#endif
static int pool_wb(const uint8_t *mem, uint8_t *home)
{
    const uint32_t coff = (uint32_t)(uintptr_t)home & 0x00FFFFFFu;    /* (home: the cart, 0x02000000 up) */
    int ok = 1;
#ifdef BG2_DRAW
    while (pal_go >= 0) { }                                   /* (docs/75: a picture's swap asked for is shown first - the
                                                                 68000 flips the buffers at the blank after its list, and
                                                                 a flip between the copy below and command 101's read
                                                                 gives the cart the other buffer's bytes; pace_vbi clears
                                                                 it, so before cdr_busy) */
#endif
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
#ifdef BG2_DRAW
    bg2_take();                                              /* (a list or a commit under way: its end; the frame buffer) */
#else
#define BG2_LIST 0x11A00                                      /* (the validation build: the same spare area, no list) */
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
#endif
    for (int h = 0; h < 2; h++) {
        volatile uint16_t *d = (volatile uint16_t *)(0x24000000 + BG2_LIST);
        const uint16_t *q = (const uint16_t *)(mem + h * 4096);
        for (int i = 0; i < 2048; i++) d[i] = q[i];
        volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
        *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }      /* (the frame buffer to the 68000) */
        MARS_SYS_COMM2 = (uint16_t)(BG2_LIST >> 4); COMM8_32 = (coff + (uint32_t)h * 4096u) | 1u << 24; COMM12_16 = 0xFFFF;
        MARS_SYS_COMM0 = 101;
        for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
        if (COMM12_16 != 0) ok = 0;
        *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }   /* (and back) */
    }
    cdr_busy = 0;
    *(volatile uint8_t *)0xFFFFFE92 = 0x11;                                        /* (the cart's lines: purged) */
    if (!ok) pool_fail++;
    return ok;
}
#endif
#ifdef RT_CD_RING
#ifdef CDR_LOG
static uint32_t cdr_nlog;
#endif
#ifdef SA_LOG
/* SA_LOG=1 (docs/51): the console's log. A 4 KB block at cart SA_LOG_AT, written through the frame buffer by the stack's
   command 101 (FBCART) at the moments the frame buffer is already handed over safely - a combination's load, every 8th
   CD ring fill - never on the screen, never through anything the display uses while it shows (DraculaX docs/172: a
   log through the display's list area and a watchdog that drew were what stopped its game on the console). The owner's
   USB script reads it (edlink memrd); tools/sa_log.py prints it. Every wait that gives up counts here. */
#ifndef SA_LOG_AT
#define SA_LOG_AT 0x3EF000u                                   /* (window 1 past IMAGE21.RAW's end, below the sound ring) */
#endif
uint32_t sa_to[8];                                            /* the waits that gave up: [0] a command's start, [1] command 91,
                                                                 [2] command 38 (a load), [3] command 101, [4] bg2_wait,
                                                                 [5] the swap before a hand-over, [6] SAPK: a packed sector
                                                                 that failed its sum, [7] SAPK: command 68 */
uint32_t sa_where;                                            /* the last place passed (sa_log.py names them) */
static uint32_t sa_seq, sa_fills, sa_wfail;
static void sa_log_put(void)
{
    extern uint32_t combo_stat[4];
#ifdef BG2_DRAW
    while (pal_go >= 0) { }                                   /* (as pool_wb, docs/75: the picture asked for shown first) */
#endif
    cdr_busy = 1;
#ifdef BG2_DRAW
    bg2_take();
#else
#ifndef BG2_LIST
#define BG2_LIST 0x11A00
#endif
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
#endif
    volatile uint32_t *b = (volatile uint32_t *)(0x24000000 + BG2_LIST);   /* (built in the list area: no SDRAM to spare) */
    for (int i = 0; i < 1024; i++) b[i] = 0;
    b[0] = 0x53414C47u; b[1] = 1; b[2] = ++sa_seq; b[3] = (uint32_t)rt.frame; b[4] = rt.c.pc | (uint32_t)rt.c.MPR[2] << 16 | (uint32_t)rt.c.MPR[3] << 24;
    b[5] = sa_where; b[6] = sa_fills; b[7] = sa_wfail;
#ifdef RT_CD_RING
    b[8] = cdr_misses; b[9] = cdr_fail;
#endif
#ifdef RT_COMBO
    b[10] = combo_stat[0]; b[11] = combo_stat[1]; b[12] = combo_stat[3];
#endif
#ifdef BG2_DRAW
    b[13] = bg2_sent; b[14] = bg2_fail; b[15] = bg2_late;
#endif
    for (int i = 0; i < 8; i++) b[16 + i] = sa_to[i];
    b[24] = MARS_SYS_COMM0 | (uint32_t)MARS_SYS_COMM2 << 16; b[25] = COMM8_32; b[26] = COMM12_16;
#ifdef SAPK
    { extern uint32_t pk_reads, pk_bad, pk_switch; b[28] = pk_reads; b[29] = pk_bad; b[30] = pk_switch; }
#endif
    b[27] = rt.c.A | (uint32_t)rt.c.X << 8 | (uint32_t)rt.c.Y << 16 | (uint32_t)rt.c.S << 24;
    uint32_t f = 2166136261u; for (int i = 0; i < 1023; i++) { f ^= b[i]; f *= 16777619u; } b[1023] = f;   /* (its own sum) */
    int ok = 1;
    volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
    *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }
    const int lv = SetSH2SR(15);                              /* (the 68000 writes the cart with RV = 1: no interrupt meanwhile) */
    MARS_SYS_COMM2 = (uint16_t)(BG2_LIST >> 4); COMM8_32 = SA_LOG_AT | 1u << 24; COMM12_16 = 0xFFFF;
    MARS_SYS_COMM0 = 101;
    { int n = 0; for (; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { } if (MARS_SYS_COMM0 != 0) sa_to[3]++; }
    SetSH2SR(lv);
    if (COMM12_16 != 0) ok = 0;
    *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }
    cdr_busy = 0;
    CacheControl(0x11);
    if (!ok) sa_wfail++;
}
#endif
#ifdef SAPK
/* SAPK=1 (docs/52): the game's data from the cart, the CD for its music only. A packed launcher (BootCamp PACKED-1,
   ledger 35; tools/sa_pack.py) brings everything: the program in bank EDCD_W0, LEVEL01.BIN and IMAGE21.RAW where their
   loads put them, and a pack - the data track's different sectors (each an LZ4 block, tools/m3_trackpack.py) and the
   combination files (4 KB LZ4 pieces) - in the cart's spare places, with its directory at cart 0x040000 (window 0, the
   program's bank). A piece is in window 0 (no switch) or in a page that window 1 shows for the read: the stack's
   command 68, then back to page 1 (the blocks' page); the reads go cache-through, so no line is purged. Every sector is
   checked against its sum (a bad one counted) */
#define PK_DIR ((const uint32_t *)0x22040000u)
static int pk_cur = 1;
uint32_t pk_reads, pk_bad, pk_switch;
static int pk_page(int p)
{
    if (p == pk_cur) return 1;
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
    const int lv = SetSH2SR(15);                              /* (docs/52: the 68000 switches the page with RV = 1 - an
                                                                 interrupt's code fetched from the cart then reads garbage;
                                                                 ares: pri_vbi_handler's literal, an address error) */
    MARS_SYS_COMM2 = (uint16_t)p; COMM12_16 = 0xFFFF;
    MARS_SYS_COMM0 = 68;
    for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
    SetSH2SR(lv);
#ifdef SA_LOG
    if (MARS_SYS_COMM0 != 0) sa_to[7]++;
#endif
    pk_switch++;
    if (COMM12_16 != 1) return 0;
    pk_cur = p; return 1;
}
static const uint8_t *pk_at(uint32_t loc)                     /* page << 24 | offset: 0xFF = window 0; else window 1 */
{
    const uint32_t pg = loc >> 24, off = loc & 0xFFFFFF;
    if (pg == 0xFF) return (const uint8_t *)(0x22000000u + off);
    if (!pk_page((int)pg)) return 0;
    return (const uint8_t *)(0x22200000u + off);
}
static int pk_lz4(const uint8_t *s, uint32_t n, uint8_t *out, uint32_t cap, const uint8_t *dic, uint32_t dn)
{   /* (dic: dn bytes the output follows - a match may reach back into them) */
    uint32_t i = 0, o = 0;
    while (i < n) {
        const uint8_t t = s[i++]; uint32_t L = t >> 4;
        if (L == 15) { uint8_t x; do { if (i >= n) return -1; x = s[i++]; L += x; } while (x == 255); }
        if (i + L > n || o + L > cap) return -1;
        for (uint32_t k = 0; k < L; k++) out[o++] = s[i++];
        if (i >= n) break;
        if (i + 2 > n) return -1;
        const uint32_t off = s[i] | (uint32_t)s[i + 1] << 8; i += 2;
        uint32_t m = (t & 15) + 4;
        if ((t & 15) == 15) { uint8_t x; do { if (i >= n) return -1; x = s[i++]; m += x; } while (x == 255); }
        if (off == 0 || off > o + dn || o + m > cap) return -1;
        for (uint32_t k = 0; k < m; k++, o++) out[o] = off > o ? dic[dn + o - off] : out[o - off];
    }
    return (int)o;
}
static int pk_ok(void) { return PK_DIR[0] == 0x5341504Bu && PK_DIR[1] == 2; }   /* "SAPK", version 2 */
/* the directory (big-endian longs): [0] SAPK [1] version 1 [2] track sectors [3] different [4] the map's offset from the
   directory (a long a track sector: bit 31 stored as it is, the rest the different one's number) [5] the pieces' table
   (two longs a different sector: loc, length) [6] the sums (a long a track sector, FNV-1a) [7] combinations [8] their
   table's offset (a long each: the offset of its list - a count, then loc and (length << 16 | stored length) a piece)
   [9] the sectors' dictionary's offset [10] its length (64 KB: a sector's matches may reach back into it) */
#define PK_W(o) ((const uint32_t *)((const uint8_t *)PK_DIR + (o)))
static int pk_sector(int32_t sector, uint8_t *out)
{
    const uint32_t *d = PK_DIR;
    if (sector < 0 || (uint32_t)sector >= d[2]) return 0;
    cdr_busy = 1; pk_reads++;
    const uint32_t e = PK_W(d[4])[sector], u = e & 0xFFFFFF;
    const uint32_t *pc = PK_W(d[5]) + 2 * u;
    const uint8_t *src = pk_at(pc[0]);
    int ok = src != 0;
    if (ok) {
        if (e & 0x80000000u) { if (pc[1] != 2048) ok = 0; else for (int i = 0; i < 2048; i++) out[i] = src[i]; }
        else ok = pk_lz4(src, pc[1], out, 2048, (const uint8_t *)PK_W(d[9]), d[10]) == 2048;
    }
    pk_page(1);
    cdr_busy = 0;
#ifdef SA_LOG
    if ((pk_reads & 63) == 1) { sa_where = 0x9A000000u | (uint32_t)sector; sa_log_put(); }   /* (the 1st, 65th ... read) */
#endif
    if (ok) {
        uint32_t h = 2166136261u; for (int i = 0; i < 2048; i++) { h ^= out[i]; h *= 16777619u; }
        ok = h == PK_W(d[6])[sector];
    }
    if (!ok) { pk_bad++;
#ifdef SA_LOG
        sa_to[6]++;
#endif
        for (int i = 0; i < 2048; i++) out[i] = 0; }
    return ok;
}
/* a combination from the pack into window 1 at 0x200000: each 4 KB piece read (its page shown), unpacked into the frame
   buffer's list area and copied into the cart by the stack's command 101 (FBCART), as pool_wb / sa_log_put do */
static int pk_combo(int c)
{
    const uint32_t *d = PK_DIR;
    if ((uint32_t)c >= d[7]) return 0;
    const uint32_t *l = PK_W(PK_W(d[8])[c]);
    const uint32_t np = l[0];
    uint8_t *buf = (uint8_t *)(0x24000000 + BG2_LIST);        /* (the frame buffer's list area: no SDRAM to spare) */
    int ok = 1;
    for (uint32_t k = 0; k < np && ok; k++) {
        const uint32_t loc = l[1 + 2 * k], ln = l[2 + 2 * k] >> 16, sl = l[2 + 2 * k] & 0xFFFF;
#ifdef BG2_DRAW
        while (pal_go >= 0) { }
        cdr_busy = 1;
        bg2_take();                                           /* (the list area ours: a list or a commit under way ended) */
#else
        cdr_busy = 1;
        for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
#endif
        for (int i = 0; i < 2048; i++) ((volatile uint16_t *)buf)[i] = 0;   /* (ares, docs/52: a byte write of 0 into the frame
                                                                 buffer left the byte as it was - the area cleared by words first) */
        const uint8_t *src = pk_at(loc);
        if (!src) ok = 0;
        else if (sl == ln) for (uint32_t i = 0; i < ln; i++) buf[i] = src[i];
        else if (pk_lz4(src, sl, buf, 4096, 0, 0) != (int)ln) ok = 0;
        pk_page(1);
        if (!ok) { cdr_busy = 0; break; }
        volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
        *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }
        const int lv = SetSH2SR(15);                          /* (the 68000 writes the cart with RV = 1: no interrupt meanwhile) */
        MARS_SYS_COMM2 = (uint16_t)(BG2_LIST >> 4); COMM8_32 = (0x200000u + k * 4096u) | 1u << 24; COMM12_16 = 0xFFFF;
        MARS_SYS_COMM0 = 101;
        for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
        SetSH2SR(lv);
        if (COMM12_16 != 0) ok = 0;
        *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }
        cdr_busy = 0;
    }
    CacheControl(0x11);
    return ok;
}
#endif
int rt_disc_sector(int32_t sector, uint8_t *out)
{
    if (sector < 0) return 0;
#ifdef SAPK
    if (pk_ok()) return pk_sector(sector, out);             /* (docs/52: from the cart's pack, not the disc) */
#endif
    const unsigned c = (unsigned)sector >> 4, slot = c % CDR_N;
    if (cdr_tag[slot] != (uint16_t)(c + 1)) {
        cdr_busy = 1;                                         /* (pace_vbi sends nothing meanwhile) */
        for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }   /* (a list or a commit under way: its end) */
        MARS_SYS_COMM2 = (uint16_t)(c * 16); COMM8_32 = (CDR_CART + slot * 32768u) | 15u << 24; COMM12_16 = 0xFFFF;
        MARS_SYS_COMM0 = 91;
        for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
#ifdef SA_LOG
        if (MARS_SYS_COMM0 != 0) sa_to[1]++;
        sa_where = 0x91000000u | (c & 0xFFFFFF);
#endif
        const int ok = COMM12_16 == 0;
        cdr_busy = 0; cdr_misses++;
        if (!ok) { cdr_fail++; cdr_tag[slot] = 0; return 0; }
        cdr_tag[slot] = (uint16_t)(c + 1);
#ifdef SA_LOG
        if ((++sa_fills & 7) == 1) sa_log_put();             /* (the 1st, 9th, ... fill: the game is loading) */
#endif
    }
    const uint32_t *src = (const uint32_t *)(0x22000000u + CDR_CART + slot * 32768u + ((unsigned)sector & 15) * 2048u);
    uint32_t *dst = (uint32_t *)out; for (int i = 0; i < 512; i++) dst[i] = src[i];
#ifdef CDR_LOG
    if (cdr_nlog < 512) {                                     /* (DIAG, docs/74: each sector given and its FNV-1a, in the
                                                                 frame buffer at 0x1A000; to SDRAM after the pool's log) */
        uint32_t f = 2166136261u; for (int i = 0; i < 2048; i++) { f ^= out[i]; f *= 16777619u; }
        volatile uint32_t *l = (volatile uint32_t *)0x2401A000 + 2 * cdr_nlog++; l[0] = (uint32_t)sector; l[1] = f;
    }
#endif
    return 1;
}
#else
int rt_disc_sector(int32_t sector, uint8_t *out) { (void)sector; (void)out; return 0; }
#endif
#ifdef RT_COMBO
/* COMBO=1 (docs/39, tools/m3_combo.py): window 1's first 640 KB hold one combination of the game's blocks (a file
   C<ii>.BIN, 104 + i), the rest the shared ones (IMAGE21.RAW, once). At each frame's end a bank whose code no block of
   the two tables matches is looked up in the directory (8 sample bytes, then the hash of the code bytes); when another
   combination has it, its file is loaded - the game itself is loading its overlays then - and every bank matched again */
typedef struct { uint8_t bank, combo; uint16_t pos[8]; uint8_t val[8]; uint32_t hash; const uint8_t *bits; } ComboDir;
extern const ComboDir combo_dir[]; extern const int combo_count;
const BlockDef *rt_combo_tab;
static int combo_cur = -1;
uint32_t combo_stat[4] __attribute__((used));                 /* (loads, failed loads, lookups hashed, the current one) */
static int combo_want(Rt *r)
{
    for (const ComboDir *e = combo_dir; e->combo != 0xFF; e++) {
        if (e->combo == combo_cur) continue;
        const uint8_t *m = r->bank[e->bank];
        if (!m || r->bank_fn[e->bank]) continue;
        int i = 0; while (i < 8 && m[e->pos[i]] == e->val[i]) i++;
        if (i < 8) continue;
        combo_stat[2]++;
        uint32_t f = 2166136261u;
        for (int j = 0; j < 8192; j++) if (e->bits[j >> 3] >> (j & 7) & 1) { f ^= m[j]; f *= 16777619u; }
        if (f == e->hash) return e->combo;
    }
    return -1;
}
static int combo_load(Rt *r, int c)
{
#ifdef BG2_DRAW
    while (pal_go >= 0) { }                                   /* (as pool_wb: the picture asked for shown first) */
#endif
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
#endif
#ifdef BG2_DRAW
    bg2_take();
#endif
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
#ifdef SA_LOG
    sa_where = 0x38000000u | (uint32_t)(104 + c);
#endif
#ifdef SAPK
    int ok;
    if (pk_ok()) ok = pk_combo(c);                            /* (docs/52: from the cart's pack) */
    else {
#endif
    ok = cd_load(104 + c);
    CacheControl(0x11);                                       /* (the cart's lines: purged) */
#ifdef RT_CD_RING
    if (!cd_open(102)) ok = 0;                                /* (command 91 reads the open file: the track again) */
#endif
#ifdef SAPK
    }
#endif
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
    cdr_busy = 0;
#endif
    const struct { uint32_t magic, combo; const BlockDef *tab; const HotDef *hot; const char *hot_lma; char *hot_vma;
                   const char *hot_end; void *const *subtab; const int16_t *kofftab; } *h = (const void *)0x02200000;
    if (ok && h->magic == 0x53434232u && h->combo == (uint32_t)c) {
        rt_combo_tab = h->tab; combo_cur = c; rt_hot_tab = h->hot;
        if (h->hot) {                                         /* (its hot code into SDRAM, tools/m3_combo.py COMBO_SPLIT) */
            memcpy(h->hot_vma, h->hot_lma, (size_t)(h->hot_end - h->hot_vma));
            CacheControl(0x11);
            rt_hot_set(r, h->subtab, h->kofftab);
        }
    }
    else { rt_combo_tab = 0; rt_hot_tab = 0; combo_cur = -1; combo_stat[1]++; ok = 0; }
    combo_stat[0]++; combo_stat[3] = (uint32_t)combo_cur;
    rt_blocks_changed(r);
#ifdef SA_LOG
    sa_where = 0x38FF0000u | (uint32_t)(104 + c); sa_log_put();
#endif
    return ok;
}
static void combo_check(Rt *r)
{
    const int c = combo_want(r);
    if (c >= 0) combo_load(r, c);
}
#endif
#ifdef CARD_PAGE
static int cart_page(int p)                                   /* (docs/39) the stack's command 68 (CART=16 M1PAGE=1): window 1 = page p */
{
    MARS_SYS_COMM2 = (uint16_t)p; COMM12_16 = 0xFFFF;
    if (!cd_cmd(68)) return 0;
    CacheControl(0x11);                                       /* (the cart's lines changed under the cache) */
    return COMM12_16 == 1;
}
#endif

int main(void)
{
    cd_status(0x4E20);
#ifdef DX_PLAY
    int chained = 0;
    {                                                         /* (a stage switch's lives, in the palette: docs/70) */
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        if (cram[0xF0] == PLAY_MAGIC && cram[0xE0] >= 1 && cram[0xE0] <= 99) { chained = 1; play_lives = cram[0xE0]; }
    }
    if (!cd_load(100 + 4 + 3 * DX_STAGE) || !cd_load(200 + 5 + 3 * DX_STAGE)) { cd_status(0x4EE0); while (1) { } }
#else
#ifdef CARD_PAGE
    /* (docs/39) the Arcade Card's 2 MB in a cart page the SH2 never maps (page 6, cart 0xC00000): window 1 shows it while
       its two halves load (LEVEL03.BIN, file 103, at window 1's start; IMAGE24.RAW, file 24, at slot 24 = 1 MB on),
       then window 1 is page 1 again for the blocks. Only the EverDrive's engine reads it, by its physical address */
#ifndef CARD_NOLOAD
#ifdef SAPK
    if (!pk_ok())                                             /* (docs/52: packed - the card is in page 6 already) */
#endif
    if (!cart_page(CARD_PAGE) || !cd_load(103) || !cd_load(24) || !cart_page(1)) { cd_status(0x4EE6); while (1) { } }
#endif                                                        /* (CARD=2: from power-on - the game fills the card itself) */
#endif
#ifdef RT_COMBO
#ifdef SAPK
    if (!pk_ok())                                             /* (docs/52: packed - IMAGE21.RAW and LEVEL01.BIN are in place) */
#endif
    if (!cd_load(21) || !cd_load(201)) { cd_status(0x4EE0); while (1) { } }   /* (docs/39: the shared blocks; the
                                                                 combination comes at the first frame's end) */
#else
#ifdef SAPK                                                   /* (docs/53, the live build packed: the state is in place, no blocks) */
    if (!(((const uint32_t *)0x22040000u)[0] == 0x5341504Bu && ((const uint32_t *)0x22040000u)[1] == 2))
#endif
    if (!cd_load(100) || !cd_load(201)) { cd_status(0x4EE0); while (1) { } }
#endif
#endif
    CacheControl(0x11);                                       /* the 68000 wrote the cart: purge (docs/06 rule 10) */
    screen();
    if (!rt_state_load(STATE, &st, 0)) { text(16, 16, "LEVEL01.BIN IS NOT A DXS1 STATE", C_RED); show(); cd_status(0x4EE5); while (1) { } }
#ifndef FIFO_PLAY
#ifdef RT_COMBO
    if (!blocks_sh[0].key) { text(16, 16, "NO BLOCKS IN IMAGE21.RAW", C_RED); show(); cd_status(0x4EE5); while (1) { } }
#else
    if (!blocks[0].key) { text(16, 16, "NO BLOCKS IN LEVEL00.BIN", C_RED); show(); cd_status(0x4EE5); while (1) { } }
#endif
#endif
#ifndef SPR_PACE
    text(16, 16, "SASPEED: RUNNING", C_WHITE); show();
#else
    {                                                         /* (the paced build: no text; its two border colours) */
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        cram[16] = 0x0000; cram[32] = 0x7FFF;
        for (int i = 0; i < 256; i++) cram_shadow[i] = 0xFFFF;   /* (unknown: screen() left its own; no colour is FFFF) */
    }
#endif
    cd_status(0x4E21);

#ifdef HUC_MARKS
    uint8_t *banks[256] = { 0 };                              /* (on the stack: rt_init reads it once; its 1 KB of
                                                                 SDRAM goes to the marks, docs/54) */
#else
    static uint8_t *banks[256];                               /* base RAM and backup RAM in SDRAM; the rest stays
                                                                 where the state has it (the cart, read only) */
#endif
    banks[0xF8] = base_ram; banks[0xF7] = bram;
#ifdef SDRAM_BANKS
    {                                                         /* the Super RAM banks the stage reads most, in SDRAM
                                                                 (make SDRAM_BANKS=0x72,0x6A: tools/cachesim, docs/38) */
        static const uint8_t sb[] = { SDRAM_BANKS };
        static uint8_t sbank[sizeof sb][8192] __attribute__((aligned(16)));
        for (unsigned i = 0; i < sizeof sb; i++) banks[sb[i]] = sbank[i];
    }
#endif
    rt_init(&rt, &st, banks);
#ifdef RT_COMBO
    combo_check(&rt);                                         /* (docs/39: the state's combination, before the first frame) */
#endif
#ifdef RT_POOL
    { static uint8_t *const sl[1] = { pool_slot[0] }; rt_pool_init(sl, 1); rt_pool_wb = pool_wb; }
#ifdef RT_POOL_LOG
    rt_pool_log = pool_log_fb;
#endif
#endif
#ifdef POOL_TEST
    {                                                         /* DIAG (docs/72): command 101 checked - a pattern written
                                                                 back to the cart at 0x1C0000 (free: past the sector ring),
                                                                 read back past the cache; COMM8 = the bytes wrong */
        static const uint32_t at[4] = { 0x1C0000, 0x096064, 0x1C2002, 0x1C4800 };   /* (aligned; bank 6A's home; 2; 2 KB) */
        uint32_t bad = 0, first = 0xFFFFFFFF;
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < 8192; i++) pool_slot[0][i] = (uint8_t)(i * 7 + k * 31 + (i >> 8));
            if (!pool_wb(pool_slot[0], (uint8_t *)(0x02000000 + at[k]))) bad += 0x10000;
            const volatile uint8_t *c = (const volatile uint8_t *)(0x22000000 + at[k]);
            for (int i = 0; i < 8192; i++) if (c[i] != pool_slot[0][i]) { bad++; if (first == 0xFFFFFFFF) first = (uint32_t)k << 16 | (uint32_t)i; }
        }
        COMM8_32 = bad; SET_COMM4((uint16_t)(first >> 16)); SET_COMM6((uint16_t)first);
        cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef DX_PLAY
    play_ram = base_ram; rt_pad_live = 0;
    if (chained) { play_open = 0; lives_set(play_lives); }
    else { const uint8_t b = base_ram[0x8D]; play_lives = (b >> 4) * 10 + (b & 15); if (play_lives < 1 || play_lives > 99) play_lives = 3; }
#endif
#ifdef RT_CD_RING
    if (st.scsi_o) {                                          /* (the drive, docs/69: the track's file opened, kept open) */
        char err[4];
#ifdef SAPK
        if (!pk_ok())                                         /* (docs/52: the track is the pack's, no file to open) */
#endif
        if (!cd_open(102)) { cd_status(0x4EE3); while (1) { } }
        if (!scsi_start_tab(&rt.hw, st.scsi_o, st.scsi_fifo, st.toc, st.disc_tab,
                            st.io_t0 + (int64_t)(int32_t)(st.scsi_o[34] - st.io_regs[18]), err, sizeof err)) { cd_status(0x4EE4); while (1) { } }
    }
#endif
#ifdef HUC_MARKS
    {                                                         /* D6b's marks (docs/54): the key table, the seed */
        extern const uint8_t binary_rip_keys_start[], binary_rip_seed_start[];
        extern const uint8_t *rt_keys; extern uint32_t rt_nkeys; extern uint16_t rt_cdpos[512];
        const uint8_t *k = binary_rip_keys_start, *sd = binary_rip_seed_start;
        rt_keys = k; rt_nkeys = (uint32_t)k[0] << 24 | (uint32_t)k[1] << 16 | (uint32_t)k[2] << 8 | k[3];
        for (int i = 0; i < 512; i++) rt_cdpos[i] = (uint16_t)(sd[2 * i] << 8 | sd[2 * i + 1]);
    }
#endif
#ifdef SPR_DRAW
    {                                                         /* the window's marks at its start, the stage's cells
                                                                 (rt_pc RUN_MARKS_OUT, m3_rip.py cells: docs/56) */
        extern const uint8_t binary_rip_marks_start[], binary_rip_cells_start[];
        const uint8_t *m = binary_rip_marks_start;
        for (int i = 0; i < 2048; i++) rt_marks.v[i] = (uint16_t)(m[2 * i] << 8 | m[2 * i + 1]);
        spr_init(binary_rip_cells_start);
        spr_buf[0].ovl = (uint8_t *)0x24020000 + 0x200 + 32; spr_buf[0].nrm = (uint8_t *)0x24000000 + 0x200 + 32;
        spr_buf[0].tail = (uint8_t *)0x24000000 + 0x1D000;  /* (the buffer's spare part: the picture ends at 0x11A00; 9 KB) */
        spr_buf[1] = spr_buf[0];
#ifdef VLOG
        vlog_done = vlog_job_done; vlog_wait = vlog_job_wait; bg2_vram = rt.vdc.vram;   /* (docs/18) */
#endif
#ifndef SPR_NOHOOK
        rt_draw_hook = draw_hook;                             /* (SPR_NOHOOK, DIAG: the build without the drawing) */
#endif
#ifdef BG2_DRAW
        bg2_setup();
#endif
    }
#endif
#ifdef CONV_TEST
    conv_init();
#endif
#ifdef RT_DIGEST
    rt.on_idle = on_idle_digest;
#endif
    SH2_FRT_TCR = 0x01;                                       /* the system clock / 32 */
    {                                                         /* the timer's own check: two 32X refreshes in ticks
                                                                 (24,000 at /32 on the console) */
        uint32_t g = gTicks; while (gTicks == g) { }
        const uint16_t a = frt(); g = gTicks; while (gTicks - g < 2) { }
        frt_check = (uint16_t)(frt() - a);
    }
#ifdef FBVDP_TEST
    {                                                         /* DIAG (docs/61): command 79 - a list in the frame buffer's
                                                                 spare part run by the 68000 (the stack built GENPLANE=1
                                                                 VSPLIT=1 FBVDP=1). The backdrop red, the Genesis on, the
                                                                 32X's colour 0 see-through; then 10 lists of 4,000 data
                                                                 words timed: COMM8 = the refreshes << 16 | the
                                                                 replies that said done (11) */
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        cram[0] = 0x8000;
        volatile uint16_t *L = (volatile uint16_t *)0x24012000; int n = 0;
        L[n++] = 0x1003; L[n++] = 0x8F02; L[n++] = 0xC000; L[n++] = 0x0000;   /* increment 2, CRAM 0 */
        L[n++] = 0x2001; L[n++] = 0x000E;                                      /* red */
        L[n++] = 0x1002; L[n++] = 0x8700; L[n++] = 0x8174;                     /* backdrop 0, display on */
        L[n++] = 0;
        volatile uint16_t *L2 = (volatile uint16_t *)0x24014000; int m = 0;
        L2[m++] = 0x1003; L2[m++] = 0x8F02; L2[m++] = 0x4000; L2[m++] = 0x0002; /* VRAM 0x8000 */
        for (int r = 0; r < 4; r++) { L2[m++] = 0x2000 | 1000; for (int i = 0; i < 1000; i++) L2[m++] = (uint16_t)i; }
        L2[m++] = 0;
        volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
        uint32_t ok = 0;
        *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }                       /* the frame buffer to the 68000 */
        MARS_SYS_COMM2 = 0; COMM8_32 = 0x12000; COMM12_16 = 0xFFFF;
        if (!cd_cmd(79)) { cd_status(0x4EE7); while (1) { } }
        ok += COMM12_16 == 1;
        uint32_t g = gTicks; while (gTicks == g) { }
        g = gTicks;
        for (int k = 0; k < 10; k++) {
            MARS_SYS_COMM2 = 0; COMM8_32 = 0x14000; COMM12_16 = 0xFFFF;
            if (!cd_cmd(79)) { cd_status(0x4EE7); while (1) { } }
            ok += COMM12_16 == 1;
        }
        const uint32_t took = gTicks - g;
        *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }                    /* and back */
        COMM8_32 = took << 16 | ok; cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef GEN_BENCH
    {                                                         /* DIAG (docs/60): GEN_BENCH command-62 VRAM word writes (the
                                                                 stack built with GENPLANE=1); the refreshes they took in
                                                                 COMM8, then stop */
        uint32_t g = gTicks; while (gTicks == g) { }
        g = gTicks;
        for (int i = 0; i < GEN_BENCH; i++) {
            MARS_SYS_COMM2 = (uint16_t)(0xE000 + 2 * (i & 1023)); COMM8_32 = (uint32_t)i << 16;
            if (!cd_cmd(62)) { cd_status(0x4EE6); while (1) { } }
        }
        COMM8_32 = gTicks - g; cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef FB_BENCH
    {                                                         /* DIAG (docs/56): FB_BENCH 16-bit writes into the overwrite
                                                                 area, 1: one a loop turn, 2: 8 a turn; the refreshes they took
                                                                 in COMM8, then stop */
        volatile uint16_t *o = (volatile uint16_t *)0x24020000 + 0x100;
        uint32_t g = gTicks; while (gTicks == g) { }
        g = gTicks;
#if FB_BENCH == 1
        for (int i = 0; i < 1000000; i++) o[i & 0x3FFF] = 0x0101;
#else
        for (int i = 0; i < 125000; i++) { volatile uint16_t *p = o + ((i * 8) & 0x3FF8);
            p[0] = 0x0101; p[1] = 0x0101; p[2] = 0x0101; p[3] = 0x0101; p[4] = 0x0101; p[5] = 0x0101; p[6] = 0x0101; p[7] = 0x0101; }
#endif
        COMM8_32 = gTicks - g; cd_status(0x4B10); while (1) { }
    }
#endif
#if defined(PROF_LOG) || defined(SEC_TIMES) || defined(VS_SEC)
    prof_init();                                              /* (VS_SEC, docs/25: the line from the watchdog's time) */
#endif
    const uint32_t v0 = gTicks;
    t_last = frt();
#if defined(DX_PLAY) || defined(SAPPHIRE_PLAY)
    frames_to_run = 0x7FFFFFFF;                               /* (the play build: on and on) */
#else
    frames_to_run = st.nmasks < FRAMES ? st.nmasks : FRAMES;
#endif
#ifdef FIFO_PLAY
    fifo_play(&rt.vdc, frames_to_run);
#else
    rt_run(&rt, frames_to_run, on_frame);
#endif
    const uint32_t vblanks = (ticks_end ? ticks_end : gTicks) - v0;
#if defined(RT_DIGEST) && defined(DIGEST_END)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000;  /* (DIAG, docs/74: the state at the run's end, by frame,
                                                                 for ares's --dump-sdram: 'DGEN', the frame, then an FNV-1a
                                                                 per 1 KB of base RAM, VRAM, CD RAM, Super RAM) */
        o[0] = 0x4447454E; o[1] = (uint32_t)rt.frame; o += 4;
        const uint8_t *src[1 + 8 + 32]; int n = 0;
        src[n++] = rt.base_ram; for (int k = 0; k < 8; k++) src[n++] = (const uint8_t *)rt.vdc.vram + 8192 * k;
        for (int b = 0x80; b < 0x88; b++) src[n++] = rt.bank[b];
        for (int b = 0x68; b < 0x80; b++) src[n++] = rt.bank[b];
        for (int k = 0; k < n; k++) for (int j = 0; j < 8; j++) {
            uint32_t f = 2166136261u; const uint8_t *q = src[k] + 1024 * j;
            for (int i = 0; i < 1024; i++) { f ^= q[i]; f *= 16777619u; }
            *o++ = f;
        }
    }
#endif
#if defined(RT_DIGEST) && defined(RT_POOL_LOG)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000; const volatile uint32_t *l = (const volatile uint32_t *)0x24018000;
        o[0] = 0x44475050; o[1] = pool_nlog;                  /* 'DGPP', the takes, 4 words each */
        for (uint32_t i = 0; i < 4 * pool_nlog; i++) o[2 + i] = l[i];
#ifdef CDR_LOG
        o += 2 + 4 * pool_nlog; l = (const volatile uint32_t *)0x2401A000;
        o[0] = 0x44474344; o[1] = cdr_nlog;                   /* 'DGCD', the sectors, 2 words each */
        for (uint32_t i = 0; i < 2 * cdr_nlog; i++) o[2 + i] = l[i];
#endif
    }
#endif
#if defined(RT_DIGEST) && defined(DIGEST_FLOG)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000; const volatile uint32_t *l = (const volatile uint32_t *)0x24014000;
        o[0] = 0x4447464C; o[1] = (uint32_t)rt.frame;          /* 'DGFL', the frames, each frame's hash */
        for (long i = 0; i < rt.frame && i < 3000; i++) o[2 + i] = l[i]; }
#endif
#if defined(RT_DIGEST) && defined(DIGEST_LOG)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000; const volatile uint32_t *l = (const volatile uint32_t *)0x24014000;
        o[0] = 0x44474C47; o[1] = idles_hashed;               /* 'DGLG', the count, the hashes (ares --dump-sdram; over
                                                                 the hot code, run no more) */
        for (uint32_t i = 0; i < idles_hashed && i < 12000; i++) o[2 + i] = l[i]; }
#endif
#ifdef SPR_PACE
#ifdef BG2_DRAW
    while (pal_go >= 0) { }
    bg2_take();                                               /* (the last commit done before COMM8 is the report's) */
    MARS_SYS_COMM2 = (short)bg2_late;
#endif
    COMM8_32 = pace_n << 16 | (pace_worst & 0xFFFF); SET_COMM4((short)nframes); SET_COMM6((short)pace_missed);
    cd_status(0x4B10); while (1) { }                          /* (the last picture stays on screen) */
#endif
#ifdef SPR_DRAW
    uint32_t spr_hash = 2166136261u;                          /* the buffer drawn into, before the screen's text */
    for (int i = 0x100; i < 0x100 + 160 * 224; i++) { const uint16_t w = FB[i]; spr_hash ^= w >> 8; spr_hash *= 16777619u; spr_hash ^= w & 0xFF; spr_hash *= 16777619u; }
#endif
    cd_status(0x4E22);

    const uint32_t mean = nframes ? t_sum / nframes * 32 : 0;
    text(16, 16, "SASPEED: DONE      ", C_GREEN);
    line(32, "FRAMES          ", nframes, 0, C_WHITE);
    line(44, "SH2 CYCLES MEAN ", mean, 0, C_YELLOW);
    line(56, "SH2 CYCLES MAX  ", t_max * 32, 0, C_YELLOW);
    line(68, "SH2 CYCLES MIN  ", t_min * 32, 0, C_YELLOW);
    line(80, "32X REFRESHES   ", vblanks, 0, C_WHITE);
    line(196, "FRT IN 2 REFR.  ", frt_check, 0, C_GREY);
    line(96, "HASH            ", hash, 1, C_WHITE);
    line(104, "HASH WITH STACK ", hash_stack, 1, C_GREY);
    line(112, "BLOCK ENTRIES   ", (uint32_t)rt.block_entries, 0, C_GREY);
    line(124, "INTERPRETED     ", (uint32_t)rt.interpreted, 0, C_GREY);
    line(136, "INTERRUPTS      ", (uint32_t)(rt.irqs + rt.irq2s), 0, C_GREY);
    line(148, "IDLE EXITS      ", (uint32_t)rt.idles, 0, C_GREY);
    line(160, "VDC STEPS       ", (uint32_t)rt.vdc.t.steps, 0, C_GREY);
    line(172, "RO WRITES       ", (uint32_t)rt.ro_writes, 0, rt.ro_writes ? C_RED : C_GREY);
#ifndef RT_DIGEST
    line(0, "RO WRITE BANK   ", (uint32_t)rt.ro_write_bank, 1, C_GREY);
#endif
    line(184, "NOT MODELLED    ", (uint32_t)(rt.unmodelled + rt.hw.unmodelled), 0, (rt.unmodelled + rt.hw.unmodelled) ? C_RED : C_GREY);
    line(212, "COLD ENTRIES    ", (uint32_t)rt.cold_entries, 0, C_GREY);
#ifdef SPR_DRAW
    line(16, "SPRITE CELLS    ", spr_stat.cells, 0, C_GREY);
    line(24, "FROM THE CART   ", spr_stat.cart, 0, C_GREY);
    line(188, "CONVERTED       ", spr_stat.conv, 0, C_GREY);
#endif
#ifdef RT_DIGEST
    line(0, "IDLE DIGEST     ", digest, 1, C_WHITE);
    line(8, "IDLES HASHED    ", idles_hashed, 0, C_GREY);
    show();
    COMM8_32 = digest; SET_COMM4((short)nframes); SET_COMM6((short)vblanks);
#else
    show();
    COMM8_32 = mean; SET_COMM4((short)nframes); SET_COMM6((short)vblanks);
#endif
#ifdef HUC_MARKS
    {                                                         /* the marks' hash, as rt_pc prints it ("marks: hash") */
        extern long rt_entries, rt_entries_found;
        uint32_t h = 2166136261u; for (int i = 0; i < 2048; i++) { h ^= rt_marks.v[i]; h *= 16777619u; }
        line(220, "MARKS HASH      ", (h >> 16) ^ (h & 0xFFFF), 1, C_WHITE); show();
#ifndef RT_DIGEST
#ifdef SPR_DRAW
        (void)spr_ticks;
        COMM8_32 = spr_hash;                                  /* (the picture's hash: rt_pc RUN_SPR_ONEBUF's) */
#else
        COMM8_32 = ((h >> 16) ^ (h & 0xFFFF)) << 16 | ((uint32_t)rt_entries_found & 0xFFFF);   /* (instead of the mean) */
#endif
#endif
        (void)rt_entries;
    }
#endif
#ifdef PROF_LOG
    {   volatile uint16_t *o = (volatile uint16_t *)0x2600B000;   /* (cache-through) */
        o[0] = 0x5052; o[1] = (uint16_t)prof_n;                    /* 'PR', the frames logged; then 4 words each */
        for (uint32_t i = 0; i < 4 * prof_n; i++) o[2 + i] = prof_log[i];
    }
#endif
    cd_status(0x4B10);
    while (1) { }
}
