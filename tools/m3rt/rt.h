/* rt.h - M3: the 32X runtime for the translated game (docs/17). The same C runs on the PC (rt_pc.c, checked
 * against the PC host of M2) and on the master SH2 (dxspeed/).
 *
 * It holds the PC Engine as the game sees it: the CPU (the translated blocks, the interpreter for the rest), the
 * memory map, the video chips at line level (rt_vdc.c) and the other hardware (tools/m2host/io_model.c). Time is
 * master clocks (21.48 MHz), in 32 bits after the video chips' base (rt_vdc.h), which moves up now and then (rt.c
 * rebase); the CD unit's model, shared with the exact host, keeps its own 64-bit time. The blocks count their cycles
 * down (HUC_TIMED, docs/17) and leave when the runtime's next event comes: an interrupt the video chips raise, the
 * frame's end, the game waiting (HUC_IDLE_EXIT). */
#pragma once
#include <stdint.h>
#include "huc_ops.h"
#include "rt_vdc.h"
#include "io_model.h"
#include "ac_model.h"                                /* Sapphire: the Arcade Card (tools/m2host) */

typedef struct { uint8_t bank; const char *key; void (*fn)(Cpu *); const uint8_t *code; const uint8_t *mask; } BlockDef;
#ifdef RT_COMBO
/* COMBO=1 (docs/39): the blocks in two tables - the shared ones (blocks_sh, in the cart page's fixed part, the same in
   every build) and the current combination's (its file's table, rt_combo_tab, at the page's start, loaded from the CD
   when the game's overlays change) */
extern const BlockDef blocks_sh[];
extern const BlockDef *rt_combo_tab;
#define FOR_BLOCKS(d) for (int t_ = 0; t_ < 2; t_++) for (const BlockDef *d = t_ ? rt_combo_tab : blocks_sh; d && d->key; d++)
struct Rt; void rt_blocks_changed(struct Rt *r);   /* (the combination's table changed: every bank matched again) */
typedef struct { const char *key; void (*fn)(Cpu *); uint8_t id, slot; } HotDef;
extern const HotDef *rt_hot_tab;                    /* (the combination's hot halves, its file's hot.c; 0: none) */
void rt_hot_set(struct Rt *r, void *const *subtab, const int16_t *kofftab);   /* (its merged hot code's tables) */
#define RT_HOTDEF_DONE
#else
extern const BlockDef blocks[];                     /* blocks.c (tools/m2_recomp.py) */
#define FOR_BLOCKS(d) for (const BlockDef *d = blocks; d->key; d++)
#endif
/* a block's hot half (tools/m3_speedtest.py split, docs/18): the labels a stage's recordings ran, linked into SDRAM;
   the block's own function is then the cold rest. Entered at an address it has no label for, it says c->miss = 3 */
#ifndef RT_HOTDEF_DONE
typedef struct { const char *key; void (*fn)(Cpu *); uint8_t id, slot; } HotDef;
#endif
/* id: in a stage's merged
   hot function (docs/19), the block's number there, for the slot it runs in (0: a function of its own) */
extern const HotDef hot_blocks[] __attribute__((weak));
void interp_one(Cpu *c);                            /* interp.c */

/* the starting state: a trace window's (tools/m3_speedtest.py pack), the same bytes on the PC and the SH2 */
typedef struct RtState {
    uint8_t A, X, Y, S, P, speed; uint16_t pc; uint8_t mpr[8];
    int64_t vdc_t0, io_t0;                          /* the chips' state times (the CPU starts at 0) */
    const uint8_t *base_ram, *cd_ram, *super_ram, *bram, *rom, *adpcm;   /* 8 K, 64 K, 192 K, 2 K, 256 K, 64 K (or 0) */
    const uint16_t *vram, *sat, *color;             /* 32 K words, 256, 512 (native order) */
    const uint32_t *vdc_regs, *io_regs;             /* 96 and 128 (native) */
    const uint16_t *masks; long nmasks;             /* the pad, a frame each (the libretro mask) */
    const uint32_t *scsi_o, *toc; const uint8_t *scsi_fifo; const int32_t *disc_tab;   /* (flag 2, docs/69: the drive's
                                                       state, the TOC, the disc's table - 0 when the state has none) */
    const uint32_t *ac_regs; const uint8_t *ac_ram; /* (flag 4, Sapphire docs/08: the Arcade Card's 19 registers and
                                                       2 MB; on the SH2 the 2 MB stay in the cart, read only) */
    const uint8_t *ac_reg[32]; int ac_regions;      /* (flag 8, docs/09: instead of the 2 MB, the 64 KB regions a
                                                       stage reads, 0 for the others) */
    uint32_t ac_phys[32];                           /* (docs/39: a region's physical cart address when it is in the card's
                                                       own page - only the EverDrive's engine reads it - else 0) */
} RtState;

/* a bank's memory, writable, merged block and its slot (rt.c rec_set): what TAM's change reads, one cache line (docs/35) */
typedef struct BankRec { uint8_t *mem; uint8_t wok, hid, hslot, pad; } BankRec;

typedef struct Rt {                                 /* (what each dispatch and access reads first: one place on the SH2) */
    Cpu c;
    int32_t st_min, io_min;                         /* the SH2 hot code's reads of the VDC status and the ADPCM status
                                                       in place while left > these (rt.c vp_update, docs/33) */
    int16_t koff[108];                              /* the SH2 hot code's RAM offsets loaded into r0 most (tools/m3_sh2.py
                                                       koff_choose, docs/35): a GBR word load, no literal (hs_kofftab) */
    int32_t now;                                    /* the time the CPU has run to (after vdc.t.base, as all below) */
    int32_t entry_t; int32_t entry_left;            /* when the current block was entered, with what budget */
    int32_t frame_end_at, irq_at;                   /* the next frame end, the video chips' next interrupt */
    int irq_stale;                                  /* irq_at must be found again (a register write) */
    int sch_ir, sch_iw, sch_stale;                  /* the VDC's schedule (sch, below): its next raise and VRAM event;
                                                       stale: a timing register changed */
    int sch_rpart;                                  /* raises past the list by look-ahead (rt.c next_raise, docs/24) */
    int32_t vram_t;                                 /* the next VRAM event or the frame's end (rt.c vram_at) */
    int32_t obs_t;                                  /* the VDC's next event anything sees (rt.c chips_lazy) */
    int32_t io_at;                                  /* the CD unit's next event (rt_run; rt.c io_set: IO_ANEW to be
                                                       found again, IO_FAR at most) */
    int64_t io_abs;                                 /* the same in the CD unit's time (0: to be found again) */
    long frame;                                     /* frames run */
    void (*on_idle)(struct Rt *r);                  /* each time the game starts waiting after an interrupt */
    long last_idle_irqs;
    const uint16_t *masks; long nmasks;
    /* counts */
    long block_entries, interpreted, irqs, irq2s, idles, rematches, ro_writes, unmodelled;
    long cold_entries;                              /* entries to a split block's cold half */
    long chained;                                   /* blocks entered straight after another (rt_run) */
    VSched sch;                                     /* the VDC's schedule to frame_end_at (docs/21, docs/24) */
    uint32_t ro_write_bank;
    uint8_t *base_ram, *cd_ram, *super_ram, *bram, *rom;
    void (*bank_fn[256])(Cpu *); void (*bank_hot[256])(Cpu *);   /* the block for each bank; its hot half (or 0) */
    uint8_t bank_hid[256], bank_hslot[256];         /* its number in the merged hot function, and that block's slot */
    uint8_t bank_dirty[256], has_blocks[256];
    uint8_t *bank[256]; uint8_t writable[256];      /* each bank's memory (0: hardware or unmapped) */
    BankRec bank_rec[256];                          /* the same, a bank's in one place for the SH2 hot code's TAM */
    int bank_block[256];
    Hw hw;
    V32 vdc;
    ArcadeCard ac;                                  /* Sapphire: its ports through banks 40-43 and $1A00-$1AFF */
} Rt;

int rt_state_load(const uint8_t *blob, RtState *st, void *(*alloc)(unsigned long));   /* rt_state.c; 1 = ok */
void rt_init(Rt *r, const RtState *st, uint8_t *const ram_banks[256]);   /* ram_banks: where each RAM bank's bytes
                                                     live for writing (0: keep it read-only where the state has it) */
int rt_leave_fast(Cpu *c);                          /* the SH2 hot code at its budget's end or a wait (docs/34) */
void rt_run(Rt *r, long frames, void (*each_frame)(Rt *r));
#ifdef RT_SDUMP
long rt_sdump(const Rt *r, uint8_t *out);           /* the state to compare, PC and SH2 alike (DIAG, docs/74) */
#endif
#ifdef RT_POOL
#ifndef RT_POOL_MAX
#define RT_POOL_MAX 4
#endif
void rt_pool_init(uint8_t *const *slots, int n);    /* the pool's 8 KB slots (docs/72) */
extern int (*rt_pool_wb)(const uint8_t *mem, uint8_t *home);   /* a pooled bank's 8 KB back to its place in the cart */
extern long rt_pool_takes, rt_pool_wbs;
#ifdef RT_POOL_LOG
extern void (*rt_pool_log)(uint32_t frame, int b, int out, uint32_t h);   /* (DIAG, docs/74) */
#endif
#endif
#ifdef RT_PLAY
extern volatile int32_t rt_pad_live;                /* the pad from the player (dxspeed PLAY=1), -1: the recording's */
#endif
int rt_xcheck(Cpu *c);                              /* (the SH2 hot code's: rt.c) */
uint32_t rt_hash(const Rt *r, int with_stack);       /* FNV-1a over the state tools/m0_replay.py frame_hash takes */
