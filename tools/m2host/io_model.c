/* io_model.c - M2: the PC Engine's other hardware as the game sees it (docs/14). Our own model, written from
 * Mednafen's behaviour (no Mednafen code) and checked against its trace:
 *   - the pad port ($1000): Mednafen's multitap, five ports, the gamepad on port 0; SEL picks the nibble;
 *   - the interrupt controller ($1400): the mask and the status; the timer ($0C00): not used by these windows;
 *   - the I/O data bus the PSG, timer, pad and interrupt registers share (what a read of the PSG returns);
 *   - the CD unit ($1800-$180F): its interrupt mask and status (IRQ2), ADPCM (address, length, play, the 32 kHz
 *     clock that plays a byte every two ticks and raises half / end), the fader, and CD-DA's sample clock (44.1 kHz:
 *     every sector of 588 samples it stuffs sub-channel bytes, setting status bit 4). The SCSI bus and CD reads are
 *     not modelled yet (counted as unmodelled if used).
 * Times are master clocks (21.48 MHz). */
#include <string.h>
#include "io_model.h"

static void note(Hw *io, const char *why) { if (!io->unmodelled++) io->why = why; }

static void update_irq(Hw *io)                          /* the CD unit's IRQ2 line */
{
    const int line = (io->port[2] & io->port[3] & 0x7C) != 0;
    if (line && io->irq2_rise == INT64_MAX) io->irq2_rise = io->now;
    else if (!line && io->irq2_rise != INT64_MAX) {
        io->irq2_hist[io->irq2_n++ & 7] = (IrqSpan){ io->irq2_rise, io->now };
        io->irq2_rise = INT64_MAX;
    }
}

int io_irq2_at(const Hw *io, int64_t t)
{
    if (io->irq2_rise <= t) return 1;
    for (int i = 0; i < 8 && i < io->irq2_n; i++) {
        const IrqSpan *s = &io->irq2_hist[(io->irq2_n - 1 - i) & 7];
        if (s->rise <= t && t < s->clear) return 1;
    }
    return 0;
}

static void adpcm_irq(Hw *io)
{
    io->port[3] = (uint8_t)((io->port[3] & ~0x0C) | (io->half ? 0x04 : 0) | (io->end ? 0x08 : 0));
    update_irq(io);
}

/* the drive's side (scsi_model.c): its interrupt bits, the sub-channel FIFO */
void io_cd_irq(Hw *io, int type)                  /* Mednafen CDIRQ: 1 transfer done (bit 5), 2 ready (bit 6) */
{
    if (type & 0x8000) { type &= 0x7FFF; if (type == 1) io->port[3] &= ~0x20; else if (type == 2) io->port[3] &= ~0x40; }
    else if (type == 1) io->port[3] |= 0x20;
    else if (type == 2) io->port[3] |= 0x40;
    update_irq(io);
}
void io_stuff_subchannel(Hw *io)
{
    if (io->fifo < 16) io->fifo++;
    io->port[3] |= 0x10; update_irq(io);
}
void io_stuff_subchannels(Hw *io, unsigned n)
{
    io->fifo = io->fifo + (int)n > 16 ? 16 : io->fifo + (int)n;
    io->port[3] |= 0x10; update_irq(io);
}
#define SIG(m) ((io->scsi.signals & (m)) != 0)
static uint8_t read_1808(Hw *io)                  /* the drive's data byte; on a data-in REQ, the CD unit ACKs it */
{
    const uint8_t r = io->scsi.db;
    if (SIG(0x008) && !SIG(0x040) && !SIG(0x002) && SIG(0x001)) {
        io->scsi.signals |= 0x040; io->ack = 1;
        scsi_run(io, io->now);
        io->ack_delay = 15 * 3;                   /* and lets go 45 clocks later */
    }
    return r;
}

/* ---- the clocks ---- */
static int64_t adpcm_next(const Hw *io)
{
    int64_t r = (io->bigdiv + 65535) >> 16;
    if (io->write_pending > 0 && r > io->write_pending) r = io->write_pending;
    if (io->read_pending > 0 && r > io->read_pending) r = io->read_pending;
    return r;
}
static int adpcm_quiet(const Hw *io)                  /* its ticks change nothing but the clock's phase */
{
    return !io->playing && io->write_pending <= 0 && io->read_pending <= 0 && !(io->port[0xB] & 3);
}
/* playing, its byte fetches (every second tick) change what the CPU sees - half, end, playing - only where the length
   crosses 32768 or reaches 0; the others change only the read address, the length and the nibble (fast: they run in
   one go, adpcm_run). The fetches from now that change none of those: */
#define ADPCM_ANY 0x7FFFFFFF
static int32_t adpcm_plain_fetches(const Hw *io)
{
    const unsigned L = io->length; const int h = L < 32768;
    if (h != (io->half != 0)) return 0;
    if (io->last_cmd & 0x10) return ADPCM_ANY;        /* (the length kept: half the same, no end) */
    return h ? (int32_t)L : (int32_t)L - 32767;
}
static int adpcm_playing_alone(const Hw *io)          /* playing, and no CPU byte or DMA under way */
{
    return io->playing && io->write_pending <= 0 && io->read_pending <= 0 && !(io->port[0xB] & 3);
}
static int64_t adpcm_play_next(const Hw *io)          /* fast: the time to the first fetch that changes what is seen */
{
    const int64_t P = io->bigdivacc * (16 - io->freq);
    const int32_t F = adpcm_plain_fetches(io);
    if (F == ADPCM_ANY || P <= 0) return 0x7FFFFFFF;
    const int64_t m = io->nibble ? 2 * (int64_t)F + 2 : 2 * (int64_t)F + 1;   /* (its tick, from 1) */
    const int64_t t = (io->bigdiv + (m - 1) * P + 65535) >> 16;
    return t < 0x7FFFFFFF ? t : 0x7FFFFFFF;
}
int64_t io_next_event(const Hw *io)
{
    int64_t n = 0x7FFFFFFF;
    const int lazy = io->fast && scsi_quiet(io);
    const int64_t a = lazy && adpcm_quiet(io) ? n : lazy && adpcm_playing_alone(io) ? adpcm_play_next(io) : adpcm_next(io),
                  s = scsi_next(io, lazy);
    if (a < n) n = a;
    if (io->ack_delay > 0 && io->ack_delay < n) n = io->ack_delay;
    if (s < n) n = s;
    if (io->fade_on && !lazy && io->fade_ctr < n) n = io->fade_ctr;     /* (the fader: the sound alone) */
    return n;
}

void (*io_adpcm_hook)(int nib);   /* (docs/50: each nibble the playback decodes, in order; -1 = the decoder reset) */
static uint8_t adpcm_byte;        /* (the byte the playback fetched last: Mednafen's ReadBuffer for the decoder) */
static void adpcm_run(Hw *io, int64_t ch)
{
    io->bigdiv -= ch << 16;
    if (io->bigdiv <= 0 && !io->playing) {            /* not playing: the ticks only keep the clock's phase - in one go */
        const int64_t step = io->bigdivacc * (16 - io->freq);
        if (step > 0 && step < 0x40000000 && -io->bigdiv < step * 0x40000000) io->bigdiv += (int64_t)(div64_32(-io->bigdiv, (int32_t)step) + 1) * step;
        else if (step > 0) io->bigdiv += (-io->bigdiv / step + 1) * step;
    }
    while (io->bigdiv <= 0) {                         /* a tick: a byte fetched every second one while playing */
        if (io->fast && io->playing && !io_adpcm_hook) {   /* (fast: the ticks whose fetches change nothing seen, in one go) */
            const int64_t P = io->bigdivacc * (16 - io->freq);
            const int32_t Fm = adpcm_plain_fetches(io);
            if (P > 0 && Fm > 0) {
                int64_t D = P < 0x40000000 && -io->bigdiv < P * 0x40000000 ? (int64_t)div64_32(-io->bigdiv, (int32_t)P) + 1
                                                                           : -io->bigdiv / P + 1;   /* (the ticks due) */
                const int64_t Dm = io->nibble ? 2 * (int64_t)Fm + 1 : 2 * (int64_t)Fm;
                if (D > Dm) D = Dm;
                if (D >= 2) {
                    const int32_t F = (int32_t)(io->nibble ? D / 2 : (D + 1) / 2);
                    io->read_addr = (uint16_t)(io->read_addr + F);
                    if (!(io->last_cmd & 0x10)) io->length = (uint16_t)(io->length - F);
                    if (D & 1) io->nibble ^= 4;
                    io->bigdiv += D * P;
                    continue;
                }
            }
        }
        io->bigdiv += io->bigdivacc * (16 - io->freq);
        if (io->playing && !io->nibble) {
            io->half = io->length < 32768;
            if (!io->length && !(io->last_cmd & 0x10)) {
                if (io->end) io->half = 0;
                io->end = 1;
                if (io->last_cmd & 0x40) io->playing = 0;
            }
            if (io->ram) adpcm_byte = io->ram[io->read_addr];
            io->read_addr++;
            if (io->length && !(io->last_cmd & 0x10)) io->length--;
        }
        if (io->playing && io_adpcm_hook) io_adpcm_hook((adpcm_byte >> (io->nibble ^ 4)) & 15);
        if (io->playing) io->nibble ^= 4;
    }
    if (io->write_pending > 0 && (io->write_pending -= (int32_t)ch) <= 0) {
        io->half = io->length < 32768;
        if (!(io->last_cmd & 0x10) && io->length < 0xFFFF) io->length++;
        if (io->ram) io->ram[io->write_addr] = io->wp_value;
        io->write_addr++;
        io->write_pending = 0;
    }
    if (io->write_pending <= 0 && (io->port[0xB] & 3)) {                 /* ADPCM DMA: a byte from the drive */
        if (!io->scsi.have) note(io, "ADPCM DMA from the CD (no drive)");
        else {
            scsi_run(io, io->now);
            if (!SIG(0x002) && SIG(0x001) && SIG(0x008) && !SIG(0x040)) { io->wp_value = read_1808(io); io->write_pending = 10 * 3; }
        }
    }
    if (io->read_pending > 0 && (io->read_pending -= (int32_t)ch) <= 0) {
        if (io->ram) io->read_buf = io->ram[io->read_addr];
        else { io->read_buf_known = 0; note(io, "ADPCM RAM read (no RAM image)"); }
        io->read_addr++; io->read_pending = 0;
        io->half = io->length < 32768;
        if (!(io->last_cmd & 0x10)) {
            if (io->length) io->length--;
            else { io->end = 1; io->half = 0; if (io->last_cmd & 0x40) io->playing = 0; }
        }
    }
    adpcm_irq(io);
}

static void fade_run(Hw *io, int64_t ch)             /* the fader's steps due in ch, in one go (the sound alone) */
{
    io->fade_ctr -= (int32_t)ch;
    if (io->fade_ctr <= 0 && io->fade_count) {
        const int32_t k = -io->fade_ctr / (int32_t)io->fade_count + 1;
        io->fade_vol = io->fade_vol > k ? io->fade_vol - k : 0; io->fade_ctr += k * (int32_t)io->fade_count;
    }
    while (io->fade_ctr <= 0) { if (io->fade_vol) io->fade_vol--; io->fade_ctr += (int32_t)io->fade_count; }
}
/* docs/75 (HUC_CDSTEP, rt_cdstep): n reads of the drive's data port ($1808) one each per clocks from now (the first now),
   in one go - only where nothing but their own handshakes happens meanwhile: no ACK pending now, a read's let-go
   before the next read (per > 45), the ADPCM and CD-DA at rest (the ADPCM's clock keeps only its phase: adpcm_run in
   one go; the fader's steps, the sound alone, in one go as io_run does them), the drive's read timer past the last (scsi_bytes: the bus quiet, the FIFO holding them). The time after:
   now + n * per, the state as io_run would leave it there after the n reads. 0: not so, nothing done */
int io_cd_bytes(Hw *io, unsigned n, int32_t per, uint8_t *out)
{
    const int64_t total = (int64_t)n * per;
    if (io->ack || io->ack_delay > 0 || per <= 15 * 3 || !adpcm_quiet(io) || io->cdda_status != 0 ||
        (io->read_timer > 0 && io->read_timer <= total) || !scsi_bytes(io, n, out)) return 0;
    io->now += total; if (io->fade_on) fade_run(io, total); adpcm_run(io, total);
    if (io->read_timer > 0) io->read_timer -= (int32_t)total;
    io->scsi.mono += total; io->scsi.last = io->now;
    return 1;
}

/* docs/76 (HUC_CDSTEP, rt_adstep): the System Card's AD_TRANS wait ($F3D0: TST #$04,$180C / BNE / LDA $1803 / AND #$20 /
   BEQ) while the CD unit's DMA moves the drive's bytes into ADPCM RAM, in one go. From a TST's read now: a read that sees
   a write pending takes 12 cycles to the next TST's read, one that sees none reads $1803 (7 cycles in) and takes 21. The
   DMA as io_run steps it - a byte read when the drive's REQ is up (ACK up, REQ down, 45 clocks to its let-go, its write
   30 clocks on), the next at the let-go - has a 45-clock rhythm from its last byte; or, its FIFO empty between sectors,
   nothing moves till the drive's read timer. Only within that: the FIFO not emptying, no sector arriving, the ADPCM's
   half flag unchanged, nothing playing; each branch back's POLL going on (left - 4 or - 13 >= 0). The time after: the
   last iteration's next TST read (the hook's point), the state io_run's there. Returns the iterations (0: none), the
   cycles they take and their $1803 reads (rt_adstep does those reads' toggle) */
int io_ad_poll(Hw *io, int m, int32_t left, int32_t *used, unsigned *reads)
{
    Scsi *d = &io->scsi;
    const uint32_t s = d->signals;
    if (!d->have || !(io->port[0xB] & 3) || io->playing || io->read_pending > 0 || io->cdda_status != 0 ||
        d->phase != 2 /* DATA_IN */ || (s & 0x088) || ((s & 0x020) != 0) != (d->last_rst != 0) || (io->port[3] & 0x20)) return 0;
    const int64_t h = io->now;
    int64_t T0 = 0, W = INT64_MAX; int stream;
    if ((s & 0x040) && io->ack && io->ack_delay > 0 && io->ack_delay <= 45) {      /* a byte read at T0, not let go */
        stream = 1; T0 = h - (45 - io->ack_delay);
        if (io->write_pending > 0 ? io->write_pending != 30 - (h - T0) : h - T0 < 30) return 0;
        W = T0 + 45 * ((int64_t)d->fifo_n + 1);              /* (the let-go that would find the FIFO empty) */
    } else if (!(s & 0x040) && !io->ack && io->ack_delay <= 0 && io->write_pending <= 0 && d->fifo_n == 0 &&
               !d->data_transfer_done && !(io->port[3] & 0x40)) stream = 0;   /* between sectors: the drive's timer */
    else return 0;
    if (io->read_timer > 0) { if (h + io->read_timer < W) W = h + io->read_timer; }
    else if (!stream) return 0;
    /* the writes that keep the half flag: each sets it from the length before its count (adpcm_run) */
    int64_t maxw = INT64_MAX;
    if (stream) {
        const unsigned L = io->length; const int kept = (io->last_cmd & 0x10) != 0;
        if ((L < 32768) != (io->half != 0)) return 0;
        if (!kept && L < 32768) maxw = 32768 - (int64_t)L;
    }
    const int64_t w0 = stream && io->write_pending > 0 ? T0 + 30 : T0 + 45 + 30;   /* (the first write still to come) */
    int64_t t = h; int32_t lf = left, u = 0; unsigned q = 0; int n = 0;
    if (W - h > 0x20000000) W = h + 0x20000000;               /* (the spans in 32 bits) */
    while (n < 20000) {
        const int pend = stream && (int32_t)(t - T0) % 45 < 30;   /* (32-bit: no libgcc 64-bit division in SDRAM) */
        const int c_ = pend ? 12 : 21, pl = pend ? 4 : 13;
        if (lf - pl < 0) break;
        const int64_t tn = t + (int64_t)c_ * m;
        if (tn >= W) break;
        if (stream && maxw != INT64_MAX && tn >= w0 && (int32_t)(tn - w0) / 45 + 1 > maxw) break;
        lf -= c_; u += c_; q += !pend; t = tn; n++;
    }
    if (!n) return 0;
    const int64_t e = t - h;
    if (stream) {
        const int64_t k = (int32_t)(t - T0) / 45;            /* (reads after T0 by t: each one pops a byte) */
        #define VAL(j) ((j) == 0 ? io->wp_value : d->fifo[(d->fifo_r + (int)(j) - 1) & 2047])
        for (int64_t j = io->write_pending > 0 ? 0 : 1; j <= k && T0 + 45 * j + 30 <= t; j++) {   /* the writes due */
            io->half = io->length < 32768;
            if (!(io->last_cmd & 0x10) && io->length < 0xFFFF) io->length++;
            if (io->ram) io->ram[io->write_addr] = VAL(j);
            io->write_addr++;
        }
        const int64_t tk = T0 + 45 * k;
        const uint8_t vk = VAL(k);
        #undef VAL
        if (k) { d->db = vk; d->fifo_r = (d->fifo_r + (int)k) & 2047; d->fifo_n -= (int)k; }
        io->wp_value = vk; io->write_pending = t - tk < 30 ? (int32_t)(30 - (t - tk)) : 0;
        io->ack_delay = (int32_t)(45 - (t - tk));
    }
    io->now = t;
    if (io->fade_on) fade_run(io, e);
    io->bigdiv -= e << 16;                                   /* (not playing: the ADPCM clock's phase, as adpcm_run) */
    if (io->bigdiv <= 0) {
        const int64_t step = io->bigdivacc * (16 - io->freq);
        if (step > 0 && step < 0x40000000 && -io->bigdiv < step * 0x40000000) io->bigdiv += (int64_t)(div64_32(-io->bigdiv, (int32_t)step) + 1) * step;
        else if (step > 0) io->bigdiv += (-io->bigdiv / step + 1) * step;
    }
    if (io->read_timer > 0) io->read_timer -= (int32_t)e;
    d->mono += e; d->last = t;
    adpcm_irq(io);
    *used = u; *reads = q;
    return n;
}

void io_run(Hw *io, int64_t t)
{
    while (io->now < t) {
        int64_t ch = t - io->now;
        const int64_t n = io_next_event(io);
        if (n < ch) ch = n;
        if (ch <= 0) ch = 1;
        io->now += ch;
        if (io->ack_delay > 0 && (io->ack_delay -= (int32_t)ch) <= 0) {   /* the CD unit lets go of ACK */
            io->ack = 0; io->scsi.signals &= ~0x040;
            if (io->scsi.have) scsi_run(io, io->now); else note(io, "SCSI ACK delay");
            if (SIG(0x002)) io->port[0xB] &= ~1;
        }
        if (io->fade_on) fade_run(io, ch);
        adpcm_run(io, ch);
        if (!io->scsi.have && io->read_timer > 0) note(io, "CD read (no drive)");
        scsi_run(io, io->now);
    }
}

/* ---- the registers ---- */
static uint8_t pad_read(const Hw *io)
{
    uint8_t r;
    const int port = io->multitap ? io->read_index : 0;
    if (port > 4) r = 0;
    else if (port == 0) r = (uint8_t)(0xF ^ (io->sel ? (io->buttons >> 4) & 0xF : io->buttons & 0xF));
    else r = 0xF;                                    /* gamepads with nothing pressed */
    return (uint8_t)(r | 0x30);                      /* bit 7 clear: the CD unit is attached; bit 6 clear (Mednafen) */
}

int io_read(Hw *io, unsigned off, int in_block_move, uint8_t *v)
{
    switch (off & 0x1C00) {
    case 0x0800: *v = in_block_move ? 0 : io->io_bus; return 1;            /* the PSG: the bus */
    case 0x0C00: note(io, "timer read"); return 0;
    case 0x1000: if (in_block_move) { *v = 0; return 1; } *v = io->io_bus = pad_read(io); return 1;
    case 0x1400:
        if (in_block_move) { *v = 0; return 1; }
        if (!(off & 2)) *v = io->io_bus;
        else if (!(off & 1)) { *v = (uint8_t)(io->irq_mask ^ 7); if (io->timer_on) note(io, "timer acknowledge"); }
        else return 0;                               /* the status: trace_main.c adds the VDC's line */
        io->io_bus = *v; return 1;
    case 0x1800:
        if ((off & 0x1E00) == 0x1A00) { note(io, "Arcade Card read"); return 0; }   /* the host enables it; the game never uses it */
        if ((off & 0x18C0) == 0x18C0) { static const uint8_t sig[4] = { 0x00, 0xAA, 0x55, 0x03 }; *v = (off & 0xC) ? 0xFF : sig[off & 3]; return 1; }
        switch (off & 0xF) {
        case 0x0: if (!io->scsi.have) break;
            *v = (uint8_t)((SIG(0x010) ? 0x80 : 0) | (SIG(0x008) ? 0x40 : 0) | (SIG(0x004) ? 0x20 : 0) | (SIG(0x002) ? 0x10 : 0) | (SIG(0x001) ? 0x08 : 0));
            return 1;
        case 0x1: if (!io->scsi.have) break; *v = io->scsi.db; return 1;
        case 0x8: if (!io->scsi.have) break; *v = read_1808(io); return 1;
        case 0x2: *v = io->port[2]; return 1;
        case 0x3: io->bram_on = 0; *v = io->port[3]; io->port[3] ^= 2; return 1;   /* bit 1: left / right, toggles */
        case 0x4: *v = io->port[4]; return 1;
        case 0xA: if (!io->read_buf_known) return 0; io->read_pending = 19 * 3; *v = io->read_buf; return 1;
        case 0xB: *v = io->port[0xB]; return 1;
        case 0xC: *v = (uint8_t)((io->end ? 1 : 0) | (io->playing ? 8 : 0) | (io->write_pending > 0 ? 4 : 0) | (io->read_pending > 0 ? 0x80 : 0)); return 1;
        case 0xD: *v = io->last_cmd; return 1;
        case 0x9: case 0xE: case 0xF: *v = 0; return 1;
        }
        note(io, "SCSI / CD-DA register read"); return 0;                  /* $1805-$1807 (and the drive without its state) */
    case 0x1C00: *v = 0xFF; return 1;
    }
    return 0;
}

void (*io_psg_hook)(unsigned reg, uint8_t v);          /* (docs/44: the PSG's register writes, to a synthesizer) */
/* docs/66 (Gate of Thunder docs/54): the sound's writes as the cart core's log has them (got_cart.sv e_sndw): the PSG's
   registers ($0800-$0BFF: code = the low 4 bits) and the CD unit's $1808-$180F but $180A (code 0x10 | the low 4 bits);
   the Arcade Card's $1Axx never reach here (sapm.c takes them first), as the core's ARCADE decode keeps them off the log */
void (*io_snd_hook)(unsigned code, uint8_t v);
static void snd_log(unsigned off, uint8_t v)
{
    unsigned code;
    if ((off & 0x1C00) == 0x0800) code = off & 15;
    else if ((off & 0x1C00) == 0x1800 && (off & 8) && (off & 7) != 2) code = 0x10 | (off & 15);
    else return;
    if (io_snd_hook) io_snd_hook(code, v);
}
int io_write(Hw *io, unsigned off, uint8_t v)
{
    snd_log(off, v);
    switch (off & 0x1C00) {
    case 0x0800: io->io_bus = v; if (io_psg_hook) io_psg_hook(off & 0xF, v); return 1;   /* the PSG (sound: docs/44's hook) */
    case 0x0C00: io->io_bus = v; note(io, "timer write"); return 1;
    case 0x1000: {
        io->io_bus = v;
        const int ns = v & 1, nc = (v >> 1) & 1;
        if (io->multitap) {
            if (ns && !io->clr && nc) io->read_index = 0;                   /* SEL high, CLR rising: back to port 0 */
            else if (!io->clr && !nc && !io->sel && ns) { if (io->read_index < 255) io->read_index++; }   /* SEL rising */
        }
        io->sel = ns; io->clr = nc;
        return 1;
    }
    case 0x1400:
        io->io_bus = v;
        if (!(off & 2)) return 1;
        if (!(off & 1)) io->irq_mask = (uint8_t)((v & 7) ^ 7);
        else if (io->timer_on) note(io, "timer acknowledge");
        return 1;
    case 0x1800:
        if ((off & 0x1E00) == 0x1A00) { note(io, "Arcade Card write"); return 1; }
        switch (off & 0xF) {
        case 0x0:                                                           /* SEL: the drive is selected */
            if (!io->scsi.have) break;
            io->scsi.signals |= 0x100; scsi_run(io, io->now); io->scsi.signals &= ~0x100; scsi_run(io, io->now);
            io->port[3] &= ~(0x20 | 0x40); update_irq(io); return 1;
        case 0x1: if (!io->scsi.have) break; io->port[1] = v; io->scsi.db = v; scsi_run(io, io->now); return 1;
        case 0x2:
            if (io->scsi.have) { io->scsi.signals = (v & 0x80) ? io->scsi.signals | 0x040 : io->scsi.signals & ~0x040; scsi_run(io, io->now); }
            else if ((v & 0x80) != (io->ack ? 0x80 : 0)) note(io, "SCSI ACK");
            io->port[2] = v; io->ack = (v & 0x80) != 0; update_irq(io); return 1;
        case 0x3: case 0xC: return 1;                                       /* read only */
        case 0x4:
            if (io->scsi.have) { io->scsi.signals = (v & 2) ? io->scsi.signals | 0x020 : io->scsi.signals & ~0x020; scsi_run(io, io->now); }
            else if (v & 2) note(io, "SCSI reset");
            if (v & 2) { io->port[3] &= ~0x70; update_irq(io); }
            io->port[4] = v; return 1;
        case 0x7: if (v & 0x80) io->bram_on = 1; return 1;
        case 0x8: case 0x9:
            if (io->last_cmd & 0x80) return 1;
            io->addr = (off & 1) ? (uint16_t)((io->addr & 0x00FF) | v << 8) : (uint16_t)((io->addr & 0xFF00) | v);
            if (io->last_cmd & 0x10) io->length = io->addr;
            return 1;
        case 0xA: io->write_pending = 3 * 11; io->wp_value = v; return 1;
        case 0xB: io->port[0xB] = v; return 1;
        case 0xD:
            if (v & 0x80) {                                                 /* ADPCM reset */
                io->addr = io->read_addr = io->write_addr = io->length = 0; io->last_cmd = 0;
                io->playing = io->half = io->end = 0; io->nibble = 0;
                if (io_adpcm_hook) io_adpcm_hook(-1);
                adpcm_irq(io); return 1;
            }
            if (io->playing && !(v & 0x20)) io->playing = 0;
            if (!io->playing && (v & 0x20)) {                               /* play: the clock restarts */
                io->bigdiv = io->bigdivacc * (16 - io->freq);
                io->playing = 1; io->half = 0; io->nibble = 0;
                if (io_adpcm_hook) io_adpcm_hook(-1);
            }
            if (v & 0x10) { io->length = io->addr; io->end = 0; }           /* the length latched */
            if (!(io->last_cmd & 0x08) && (v & 0x08)) io->read_addr = (v & 4) ? io->addr : (uint16_t)(io->addr - 1);
            if (!(io->last_cmd & 0x02) && (v & 0x02)) io->write_addr = (v & 1) ? io->addr : (uint16_t)(io->addr - 1);
            io->last_cmd = v;
            adpcm_irq(io);
            return 1;
        case 0xE: io->freq = v & 0xF; return 1;
        case 0xF:
            io->fade_cmd = v;
            if (!(v & 8)) { io->fade_vol = 65536; io->fade_ctr = 0; io->fade_count = 0; io->fade_on = 0; }
            else { io->fade_count = 3 * ((v & 4) ? 273 : 655); if (!io->fade_on) io->fade_ctr = (int32_t)io->fade_count; io->fade_on = 1; }
            return 1;
        }
        note(io, "SCSI / CD-DA register write"); return 0;                 /* $1805 $1806 (and the drive without its state) */
    case 0x1C00: return 1;
    }
    return 0;
}

/* the host's libretro mask (RETRO_DEVICE_ID_JOYPAD_*: B 0, Y 1, SELECT 2, START 3, UP 4, DOWN 5, LEFT 6, RIGHT 7,
   A 8, ...) in the core's order: I = A, II = B, SELECT, RUN = START, up, right, down, left; the core's libretro
   front end drops up + down and left + right pressed together */
void io_set_buttons(Hw *io, uint16_t m)
{
    static const int map[8] = { 8, 0, 2, 3, 4, 7, 5, 6 };
    uint16_t b = 0;
    for (int i = 0; i < 8; i++) if (m >> map[i] & 1) b |= (uint16_t)(1 << i);
    if ((b & 0x50) == 0x50) b &= ~0x50;
    if ((b & 0xA0) == 0xA0) b &= ~0xA0;
    if (m & ~0x1FD) note(io, "pad buttons beyond the eight");
    io->buttons = b;
}

void io_start(Hw *io, const uint32_t *r, int64_t t0)
{
    memset(io, 0, sizeof *io);
    io->now = t0;
    for (int i = 0; i < 15; i++) io->port[i] = (uint8_t)r[i];
    io->bram_on = (int)r[15]; io->ack = (int)r[16]; io->ack_delay = (int32_t)r[17];
    io->addr = (uint16_t)r[20]; io->read_addr = (uint16_t)r[21]; io->write_addr = (uint16_t)r[22]; io->length = (uint16_t)r[23];
    io->half = (int)r[24]; io->end = (int)r[25]; io->playing = (int)r[26]; io->last_cmd = (uint8_t)r[27]; io->freq = r[28];
    io->read_buf = (uint8_t)r[30]; io->read_buf_known = 1; io->read_pending = (int32_t)r[31]; io->write_pending = (int32_t)r[32]; io->wp_value = (uint8_t)r[33];
    io->nibble = (int)r[34];
    io->bigdivacc = (int64_t)((uint64_t)r[35] | (uint64_t)r[36] << 32); io->bigdiv = (int64_t)((uint64_t)r[37] | (uint64_t)r[38] << 32);
    io->fade_cmd = (uint8_t)r[39]; io->fade_vol = (int32_t)r[40]; io->fade_ctr = (int32_t)r[41]; io->fade_count = r[42]; io->fade_on = (int)r[43];
    io->fifo = (int)r[44];
    const uint32_t *s = r + 48;
    io->cdda_status = (int32_t)s[0]; io->cdda_div = (int64_t)((uint64_t)s[1] | (uint64_t)s[2] << 32); io->cdda_acc = s[3];
    io->os_pos = s[4]; io->read_timer = (int32_t)s[5]; io->read_pos = s[7]; io->read_sec = s[8]; io->read_sec_end = s[9];
    io->read_sec_start = s[10]; io->play_mode = (int)s[11]; io->leadout = s[12];
    if (s[18] || !s[19]) note(io, "no disc");
    io->sel = (int)r[80]; io->clr = (int)r[81]; io->read_index = (int)r[82]; io->multitap = (int)r[83];
    io->irq_mask = (uint8_t)r[88]; io->timer_on = r[90] != 0; io->io_bus = (uint8_t)r[96];
    if (io->timer_on || (r[89] & 4)) note(io, "the timer runs");
    io->irq2_rise = (io->port[2] & io->port[3] & 0x7C) ? t0 : INT64_MAX;
    io->scsi.last = t0;                              /* the drive (scsi_start) or, without it, CD-DA's clock alone */
}
