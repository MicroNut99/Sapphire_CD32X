/* io_model.h - M2: the PC Engine's other hardware as the game sees it (io_model.c, docs/14): the pad port ($1000),
 * the interrupt controller ($1400), the timer ($0C00, state only), the I/O data bus, and the CD unit's registers
 * ($1800-$180F) with ADPCM, the CD-DA sample clock and the fader. Times are master clocks (21.48 MHz). */
#pragma once
#include <stdint.h>

typedef struct { int64_t rise, clear; } IrqSpan;

/* n / d for a quotient that fits in 32 bits: on the SH2 its division unit (64 / 32 bits, 39 cycles; libgcc's 64-bit
   division costs over 1,000 there), elsewhere C's (the same result: both round toward zero) */
static inline int32_t div64_32(int64_t n, int32_t d)
{
#ifdef __32X__
    *(volatile int32_t *)0xFFFFFF00 = d;                  /* DVSR */
    *(volatile int32_t *)0xFFFFFF10 = (int32_t)(n >> 32); /* DVDNTH */
    *(volatile int32_t *)0xFFFFFF14 = (int32_t)n;         /* DVDNTL: starts it */
    return *(volatile int32_t *)0xFFFFFF14;               /* (the read waits for the result) */
#else
    return (int32_t)(n / d);
#endif
}

typedef struct Disc {                       /* the disc: the cue sheet's tracks (scsi_model.c) */
    int first, last; int32_t leadout; uint8_t leadout_control, tab;   /* (tab: from scsi_start_tab, docs/69 - in the padding: the struct's layout, which the hot code's offsets know, unchanged) */
    struct { int32_t lba, sectors, pregap, pregap_dv, postgap; uint8_t control; int data, sector_size; void *f; long offset; } t[101];
} Disc;
typedef struct Scsi {                       /* the CD drive (scsi_model.c, docs/15) */
    int have;                               /* a drive state and disc were given */
    int phase; uint32_t signals; uint8_t db;
    uint8_t message_pending; int status_sent, message_sent;
    uint8_t key, asc, ascq, fru;
    uint8_t cmd[256]; int cmd_pos;
    int data_transfer_done, last_rst;
    uint8_t fifo[2048]; int fifo_r, fifo_n; /* the data FIFO */
    uint32_t sector_addr, sector_count;
    int64_t mono, last_sapsp, last;         /* its own running time, the last play start, when it last ran (our time) */
    uint32_t system_clock, rate;
    uint8_t subq_last[12], subq[4][12];
    long commands;
    Disc disc;
} Scsi;

typedef struct Hw {
    /* the pad port, Mednafen's multitap (5 ports, a gamepad on port 0): SEL / CLR lines, the port being read */
    int sel, clr, read_index, multitap;
    uint16_t buttons;                       /* this frame's (Mednafen order: I, II, SELECT, RUN, up, right, down, left) */
    /* the CPU's interrupt controller and timer, the I/O bus */
    uint8_t irq_mask;                       /* Mednafen IRQMask, 1 = enabled: IRQ2 1, IRQ1 2, timer 4 */
    int timer_on; uint8_t io_bus;
    /* the CD unit */
    int64_t now;                            /* the time it has run to */
    uint8_t port[15]; int bram_on, ack; int32_t ack_delay;
    uint16_t addr, read_addr, write_addr, length;
    int half, end, playing, nibble;
    uint8_t last_cmd, read_buf, wp_value; uint32_t freq;
    uint8_t *ram; int read_buf_known;        /* ADPCM RAM (64 KB) if the window's start has it */
    int32_t read_pending, write_pending;
    int64_t bigdivacc, bigdiv;               /* the ADPCM clock: 1/65536 master clocks */
    uint8_t fade_cmd; int32_t fade_vol, fade_ctr; uint32_t fade_count; int fade_on;
    int fifo;                                /* sub-channel bytes waiting */
    /* CD-DA: playing (1) or scanning (2): the sample clock, 1/2^20 master clocks, 32 oversample steps; the sector */
    int cdda_status, play_mode; int64_t cdda_div; uint32_t cdda_acc; unsigned os_pos, read_pos;
    uint32_t read_sec, read_sec_end, read_sec_start, leadout;
    int32_t read_timer;
    /* the IRQ2 line (port2 & port3 & 0x7C): when it rose, and the last spans it was high */
    int64_t irq2_rise; IrqSpan irq2_hist[8]; int irq2_n;
    Scsi scsi;
    int fast;                                /* the 32X runtime: io_run takes only the events the CPU can see (below) */
    long unmodelled;                         /* accesses or events this model doesn't cover (reported) */
    const char *why;                         /* the first of them */
} Hw;

void io_start(Hw *io, const uint32_t *r, int64_t t0);   /* from the core's dumpiostate (128 u32), at time t0 */
void io_set_buttons(Hw *io, uint16_t retro_mask);       /* a frame's pad: the host's libretro mask */
void io_run(Hw *io, int64_t t);                         /* run the CD unit to time t */
/* fast (tools/m3rt): while the drive's bus is idle, io_run steps only at the events that change what the CPU can see -
   not at each CD-DA sample, ADPCM tick (not playing) or fader step, which it runs in one go to the next of those. The
   state at each CPU access is the same; what differs is how often the drive's bus steps, so a bus at work keeps every
   event (Mednafen steps the drive at each). The PC host (trace_main.c) keeps them all. */
int64_t io_next_event(const Hw *io);                    /* master clocks from its time to the CD unit's next event */
int io_irq2_at(const Hw *io, int64_t t);                /* was IRQ2 high at time t */
/* an access to the hardware page at offset 0x800-0x1FFF; returns 1 if this model covers it */
int io_read(Hw *io, unsigned off, int in_block_move, uint8_t *v);
int io_write(Hw *io, unsigned off, uint8_t v);
/* scsi_model.c */
int scsi_start(Hw *io, const uint32_t *o, const uint8_t *fifo, const uint32_t *toc, const char *cue, int64_t t0, char *err, int errn);
/* (docs/69) the same from the disc's table (m3_speedtest.py disc_table) instead of the cue sheet: no files - a data
   sector comes from rt_disc_sector(sector in the track, 2048 bytes), the game's own (the SH2: the CD32X disc) */
int scsi_start_tab(Hw *io, const uint32_t *o, const uint8_t *fifo, const uint32_t *toc, const int32_t *tab, int64_t t0, char *err, int errn);
int rt_disc_sector(int32_t sector, uint8_t *out);
void scsi_run(Hw *io, int64_t t);
int64_t scsi_next(const Hw *io, int lazy);           /* lazy: CD-DA's next event is its next sector, not sample */
int scsi_bytes(Hw *io, unsigned n, uint8_t *out);         /* docs/75: n data bytes in one go (rt_cdstep) */
int io_cd_bytes(Hw *io, unsigned n, int32_t per, uint8_t *out);
int io_ad_poll(Hw *io, int m, int32_t left, int32_t *used, unsigned *reads);   /* docs/76 (rt_adstep) */
int scsi_quiet(const Hw *io);                       /* no drive, its bus free and not selected, or waiting for the CPU */
void io_stuff_subchannels(Hw *io, unsigned n);      /* n sub-channel bytes at once (CD-DA run in one go) */
extern void (*io_psg_hook)(unsigned reg, uint8_t v);   /* (docs/44: each PSG register write, 0-15) */
