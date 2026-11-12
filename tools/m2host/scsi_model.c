/* scsi_model.c - M2: the PC Engine CD unit's drive (docs/15). Our own model, written from Mednafen's behaviour (no
 * Mednafen code) and checked against it: the SCSI bus (the target's phases, REQ / ACK hand-shakes, SEL, RST), the
 * commands the System Card sends (TEST UNIT READY, REQUEST SENSE, READ(6), and NEC's SET AUDIO PLAYBACK START /
 * END, PAUSE, READ SUB-CHANNEL Q, GET DIR INFO), the data FIFO (2048 bytes), the read timer (126,000 bytes a second:
 * three sector times to the first sector, one to each next; no seek time), CD-DA play (the 44.1 kHz sample clock,
 * sector by sector, sub-channel bytes) and the sub-channel Q position the drive reports, made from the disc's track
 * table as Mednafen makes it for a cue / image disc. The disc is the cue sheet's files (work folder, never in git).
 *
 * The drive steps once each time it runs: at every CD-unit event and every CPU access to the CD unit (io_model.c),
 * which is when Mednafen runs it. Times are master clocks (21.48 MHz).
 *
 * RT_NO_DISC (the SH2 build of M3's speed test, docs/17): no disc files - scsi_start says no, so only CD-DA's clock
 * runs, and a data read would give zeros and count as not modelled (the 32X's disc path is M3b's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io_model.h"

enum { BUS_FREE, COMMAND, DATA_IN, DATA_OUT, STATUS, MESSAGE_IN, MESSAGE_OUT };
#define S_IO 0x001
#define S_CD 0x002
#define S_MSG 0x004
#define S_REQ 0x008
#define S_BSY 0x010
#define S_RST 0x020
#define S_ACK 0x040
#define S_ATN 0x080
#define S_SEL 0x100

static void note(Hw *io, const char *why) { if (!io->unmodelled++) io->why = why; }
static uint8_t bcd(unsigned n) { return (uint8_t)(((n / 10) << 4) | (n % 10)); }
static unsigned unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

/* ---- the CD unit's side: its status bits and IRQ2 (io_model.c) ---- */
void io_cd_irq(Hw *io, int type);                /* 1 transfer done, 2 transfer ready, | 0x8000 to clear */
void io_stuff_subchannel(Hw *io);

static void set_sig(Hw *io, uint32_t m, int on)
{
    Scsi *d = &io->scsi;
    if (m == S_REQ && on && !(d->signals & S_REQ)) io_cd_irq(io, 3);   /* Mednafen's "magical REQ": no bit, a recheck */
    d->signals = on ? d->signals | m : d->signals & ~m;
}

static void change_phase(Hw *io, int p)
{
    static const uint8_t sig[7][5] = {   /* BSY MSG CD IO REQ */
        { 0, 0, 0, 0, 0 }, { 1, 0, 1, 0, 1 }, { 1, 0, 0, 1, 0 }, { 1, 0, 0, 0, 1 }, { 1, 0, 1, 1, 1 }, { 1, 1, 1, 1, 1 }, { 1, 1, 1, 0, 1 } };
    set_sig(io, S_BSY, sig[p][0]); set_sig(io, S_MSG, sig[p][1]); set_sig(io, S_CD, sig[p][2]); set_sig(io, S_IO, sig[p][3]);
    set_sig(io, S_REQ, sig[p][4]);
    if (p == BUS_FREE) io_cd_irq(io, 0x8000 | 1);
    io->scsi.phase = p;
}

static void fifo_flush(Scsi *d) { d->fifo_r = d->fifo_n = 0; }
static void fifo_write(Scsi *d, const uint8_t *b, int n)
{
    for (int i = 0; i < n && d->fifo_n < 2048; i++) d->fifo[(d->fifo_r + d->fifo_n++) & 2047] = b[i];
}

static void status_message(Hw *io, int status, uint8_t msg)
{
    Scsi *d = &io->scsi;
    if (d->fifo_n) fifo_flush(d);
    d->message_pending = msg; d->status_sent = d->message_sent = 0;
    d->db = (status == 0 || status == 2) ? 0x00 : 0x01;
    change_phase(io, STATUS);
}
static void cc_error(Hw *io, int key, int asc)
{
    Scsi *d = &io->scsi;
    d->key = (uint8_t)key; d->asc = (uint8_t)asc; d->ascq = 0; d->fru = 0;
    status_message(io, 1, 0x00);
}
static void data_in(Hw *io, const uint8_t *b, int n)
{
    fifo_write(&io->scsi, b, n);
    io->scsi.data_transfer_done = 1;
    change_phase(io, DATA_IN);
}

/* ---- the disc: the cue sheet's tracks, laid out as Mednafen lays out a cue / image disc ---- */
static int track_of(const Disc *k, int32_t lba)          /* the TOC's FindTrackByLBA */
{
    int lvt = 0;
    for (int t = 1; t <= k->last; t++) { if (lba < k->t[t].lba) break; lvt = t; }
    return lvt;
}

/* sub-channel Q for a sector (adr 1: the position), as Mednafen synthesises it for an image without sub-channel data */
static void make_subq(const Disc *k, int32_t lba, uint8_t *q, int *pause)
{
    int t;
    for (t = k->first; t < k->last; t++)
        if (lba >= k->t[t].lba - k->t[t].pregap_dv - k->t[t].pregap && lba < k->t[t].lba + k->t[t].sectors + k->t[t].postgap) break;
    const uint32_t rel = lba < k->t[t].lba ? (uint32_t)(k->t[t].lba - 1 - lba) : (uint32_t)(lba - k->t[t].lba);
    const uint32_t ab = (uint32_t)(lba + 150);
    uint8_t control = k->t[t].control;
    *pause = lba < k->t[t].lba || lba >= k->t[t].lba + k->t[t].sectors;
    if (lba - k->t[t].lba < -150 && (k->t[t].control & 4) && t > k->first && !(k->t[t - 1].control & 4)) control = k->t[t - 1].control;
    memset(q, 0, 12);
    q[0] = (uint8_t)(1 | control << 4); q[1] = bcd((unsigned)t); q[2] = bcd(lba >= k->t[t].lba ? 1 : 0);
    q[3] = bcd(rel / 75 / 60); q[4] = bcd((rel / 75) % 60); q[5] = bcd(rel % 75);
    q[7] = bcd(ab / 75 / 60); q[8] = bcd((ab / 75) % 60); q[9] = bcd(ab % 75);
    /* (q[10..11]: the CRC; valid by construction, never reported) */
}
static void sector_subq(Hw *io, int32_t lba)             /* a sector read: its sub-channel's Q becomes the drive's */
{
    Scsi *d = &io->scsi; int pause;
    make_subq(&d->disc, lba, d->subq_last, &pause);
    memcpy(d->subq[1], d->subq_last, 12);
}

#ifndef RT_NO_DISC
#include "../m3rt/trackpak.c"   /* (one file, so every build of this model has it) */
#endif
uint32_t scsi_pak_reads, scsi_pak_bad;    /* (docs/49) sectors read from SCSI_PAK, and those that failed */
#ifndef RT_NO_DISC
int scsi_pak_sector(int32_t sector, uint8_t *out)        /* 1 read, 0 failed its check, -1 no pack (SCSI_PAK unset) */
{
    static int tried, ok; static TrackPak pk;
    if (!tried) {
        tried = 1; const char *pn = getenv("SCSI_PAK"); FILE *pf = pn ? fopen(pn, "rb") : 0;
        if (pf) { fseek(pf, 0, SEEK_END); const long n = ftell(pf); fseek(pf, 0, SEEK_SET); uint8_t *m = malloc((size_t)n);
            ok = m && fread(m, 1, (size_t)n, pf) == (size_t)n && !trackpak_open(&pk, m, (uint32_t)n); fclose(pf); }
        if (pn && !ok) fprintf(stderr, "SCSI_PAK: %s is not a pack\n", pn);
    }
    if (!ok) return -1;
    const int r = trackpak_read(&pk, (uint32_t)sector, out) ? 0 : 1;
    scsi_pak_reads = pk.reads; scsi_pak_bad = pk.bad;
    return r;
}
#endif
int32_t rt_disc_lba;                                               /* (docs/53, DraculaX docs/99: the sector's own LBA, for a reader that needs the track) */
static int read_data_sector(Hw *io, int32_t lba, uint8_t *out)   /* 2048 bytes of a data track; 0 if not one */
{
    Disc *k = &io->scsi.disc;
    const int t = track_of(k, lba);
    if (!t || !(k->t[t].control & 4)) return 0;
    if (lba < k->t[t].lba || lba >= k->t[t].lba + k->t[t].sectors) { memset(out, 0, 2048); return 1; }   /* gap */
    if (k->tab) { rt_disc_lba = lba; if (!rt_disc_sector(lba - k->t[t].lba, out)) { note(io, "data sector not read"); memset(out, 0, 2048); } return 1; }
#ifdef RT_NO_DISC
    note(io, "no disc files (RT_NO_DISC)"); memset(out, 0, 2048);
#else
    {   /* (docs/49) SCSI_PAK=<tools/m3_trackpack.py file>: the first data track from the cart's packed copy, not the disc */
        static int pk_track;
        if (!pk_track) for (int i = 1; i < 100 && !pk_track; i++) if (k->t[i].control & 4) pk_track = i;
        if (t == pk_track) { const int r = scsi_pak_sector(lba - k->t[t].lba, out);
            if (r == 0) { note(io, "SCSI_PAK: a sector failed its check"); memset(out, 0, 2048); }
            if (r >= 0) return 1; }
    }
    if (!k->t[t].f) { note(io, "data track file missing"); memset(out, 0, 2048); return 1; }
    fseek(k->t[t].f, k->t[t].offset + (long)(lba - k->t[t].lba) * 2048, SEEK_SET);
    if (fread(out, 1, 2048, k->t[t].f) != 2048) { note(io, "data track short"); memset(out, 0, 2048); }
#endif
    return 1;
}

#ifndef RT_NO_DISC
static int disc_open(Hw *io, const char *cue)
{
    Disc *k = &io->scsi.disc;
    memset(k, 0, sizeof *k);
    FILE *f = fopen(cue, "r");
    if (!f) return 0;
    char dir[512]; snprintf(dir, sizeof dir, "%s", cue);
    char *sl = strrchr(dir, '/'); if (sl) sl[1] = 0; else dir[0] = 0;
    char line[512], file[512] = ""; int t = 0, first_in_file = 0;
    while (fgets(line, sizeof line, f)) {
        char a[64], b[256]; int n, m, s, fr;
        if (sscanf(line, " FILE \"%255[^\"]\"", b) == 1) { snprintf(file, sizeof file, "%s%s", dir, b); first_in_file = 1; }
        else if (sscanf(line, " TRACK %d %63s", &n, a) == 2) {
            t = n; if (!k->first) k->first = t; k->last = t;
            k->t[t].data = strcmp(a, "AUDIO") != 0; k->t[t].sector_size = k->t[t].data ? 2048 : 2352;
            if (k->t[t].data && strcmp(a, "MODE1/2048")) { fclose(f); return 0; }   /* only these two kinds on this disc */
            k->t[t].control = k->t[t].data ? 4 : 0;
            k->t[t].f = first_in_file ? fopen(file, "rb") : 0; first_in_file = 0;
            if (k->t[t].f) { fseek(k->t[t].f, 0, SEEK_END); k->t[t].sectors = (int32_t)(ftell(k->t[t].f) / k->t[t].sector_size); }
            if (!k->t[t].data && k->t[t].f) { fclose(k->t[t].f); k->t[t].f = 0; }   /* audio: only its length is needed */
        } else if (sscanf(line, " PREGAP %d:%d:%d", &m, &s, &fr) == 3) k->t[t].pregap = (m * 60 + s) * 75 + fr;
        else if (sscanf(line, " INDEX %d %d:%d:%d", &n, &m, &s, &fr) == 4 && n == 1 && (m || s || fr)) { fclose(f); return 0; }
    }
    fclose(f);
    int32_t lba = -150; k->t[k->first].pregap += 150;
    for (t = k->first; t <= k->last; t++) { lba += k->t[t].pregap; k->t[t].lba = lba; lba += k->t[t].sectors; }
    k->leadout = lba;
    return k->first > 0;
}
#endif

/* ---- the commands ---- */
static void do_read(Hw *io, uint32_t sa, uint32_t sc)
{
    Scsi *d = &io->scsi; Disc *k = &d->disc;
    if ((int32_t)sa > k->leadout) { cc_error(io, 5, 0x25); return; }
    const int t = track_of(k, (int32_t)sa);
    if (!t) { cc_error(io, 5, 0x25); return; }
    if (!(k->t[t].control & 4)) { cc_error(io, 3, 0x1D); return; }
    if (!sc && (int32_t)sa == k->leadout) { cc_error(io, 3, 0x16); return; }
    d->sector_addr = sa; d->sector_count = sc;
    if (sc) io->read_timer = (int32_t)((uint64_t)3 * 2048 * d->system_clock / d->rate);
    else { io->read_timer = 0; status_message(io, 0, 0); }
    io->cdda_status = 0;
}

static int32_t amsf_lba(uint8_t m, uint8_t s, uint8_t f) { return (int32_t)((unbcd(m) * 60 + unbcd(s)) * 75 + unbcd(f)) - 150; }

static void do_command(Hw *io)
{
    Scsi *d = &io->scsi; Disc *k = &d->disc; const uint8_t *c = d->cmd;
    uint8_t buf[32];
    switch (c[0]) {
    case 0x00: status_message(io, 0, 0); return;                           /* TEST UNIT READY */
    case 0x03: {                                                           /* REQUEST SENSE */
        memset(buf, 0, 18); buf[0] = 0x70; buf[2] = d->key; buf[7] = 0x0A; buf[12] = d->asc; buf[13] = d->ascq; buf[14] = d->fru;
        data_in(io, buf, 18); d->key = d->asc = d->ascq = d->fru = 0; return; }
    case 0x08: {                                                           /* READ(6) */
        uint32_t sc = c[4]; if (!sc) sc = 256;
        do_read(io, (uint32_t)((c[1] & 0x1F) << 16 | c[2] << 8 | c[3]), sc); return; }
    case 0xD8: {                                                           /* SET AUDIO PLAYBACK START */
        int32_t ns;
        switch (c[9] & 0xC0) {
        default: ns = c[3] << 16 | c[4] << 8 | c[5]; break;
        case 0x40: ns = amsf_lba(c[2], c[3], c[4]); break;
        case 0x80: { int t = (int)unbcd(c[2]); if (!t) t = 1; else if (t >= k->last + 1) t = 100; ns = t == 100 ? k->leadout : k->t[t].lba; break; }
        }
        if (io->cdda_status == 1 && (uint32_t)ns == io->read_sec_start && (d->mono - d->last_sapsp) * 1000 / d->system_clock < 190) {
            d->last_sapsp = d->mono; status_message(io, 0, 0); io_cd_irq(io, 1); return;   /* the same start again, at once */
        }
        d->last_sapsp = d->mono;
        io->read_sec = io->read_sec_start = (uint32_t)ns; io->read_sec_end = (uint32_t)k->leadout;
        io->read_pos = 588; io->cdda_status = -1; io->play_mode = 0;
        if (c[1]) { io->play_mode = 1; io->cdda_status = 1; }
        status_message(io, 0, 0); io_cd_irq(io, 1); return; }
    case 0xD9: {                                                           /* SET AUDIO PLAYBACK END */
        int32_t ne;
        switch (c[9] & 0xC0) {
        default: ne = c[3] << 16 | c[4] << 8 | c[5]; break;
        case 0x40: ne = (int32_t)(unbcd(c[4]) + 75 * (unbcd(c[3]) + 60 * unbcd(c[2]))) - 150; break;
        case 0x80: { int t = (int)unbcd(c[2]); if (!t) t = 1; else if (t >= k->last + 1) t = 100; ne = t == 100 ? k->leadout : k->t[t].lba; break; }
        }
        io->read_sec_end = (uint32_t)ne;
        switch (c[1]) {
        default: case 3: io->play_mode = 1; io->cdda_status = 1; break;   /* normal */
        case 2: io->play_mode = 2; io->cdda_status = 1; break;            /* interrupt at the end */
        case 1: io->play_mode = 3; io->cdda_status = 1; break;            /* loop */
        case 0: io->play_mode = 0; io->cdda_status = 0; break;            /* silent: stop */
        }
        status_message(io, 0, 0); return; }
    case 0xDA:                                                             /* PAUSE */
        if (io->cdda_status != 0) { io->cdda_status = -1; status_message(io, 0, 0); }
        else cc_error(io, 5, 0x2C);
        return;
    case 0xDD: {                                                           /* READ SUB-CHANNEL Q */
        const uint8_t *q = d->subq[1];
        memset(buf, 0, 10);
        buf[2] = q[1]; buf[3] = q[2]; buf[4] = q[3]; buf[5] = q[4]; buf[6] = q[5]; buf[7] = q[7]; buf[8] = q[8]; buf[9] = q[9];
        buf[0] = io->cdda_status == -1 ? 2 : (io->cdda_status == 1 || io->cdda_status == 2) ? 0 : 3;
        data_in(io, buf, 10); return; }
    case 0xDE: {                                                           /* GET DIR INFO */
        int n = 0; memset(buf, 0, sizeof buf);
        switch (c[1]) {
        default: case 0: buf[0] = bcd((unsigned)k->first); buf[1] = bcd((unsigned)k->last); n = 2; break;
        case 1: { const uint32_t a = (uint32_t)(k->leadout + 150); buf[0] = bcd(a / 75 / 60); buf[1] = bcd((a / 75) % 60); buf[2] = bcd(a % 75); n = 3; break; }
        case 2: {
            int t = (int)unbcd(c[2]);
            if (!t) t = 1; else if (c[2] == 0xAA) t = 100; else if (t > 99) { cc_error(io, 5, 0x22); return; }
            const uint32_t a = (uint32_t)((t == 100 ? k->leadout : k->t[t].lba) + 150);
            buf[0] = bcd(a / 75 / 60); buf[1] = bcd((a / 75) % 60); buf[2] = bcd(a % 75);
            buf[3] = t == 100 ? k->leadout_control : k->t[t].control;
            n = 4; break; }
        }
        data_in(io, buf, n); return; }
    }
    cc_error(io, 5, 0x20);                                                 /* not a command the PC Engine drive has */
}

/* ---- the clocks ---- */
static void run_read(Hw *io, int64_t run)
{
    Scsi *d = &io->scsi;
    if (io->read_timer <= 0) return;
    io->read_timer -= (int32_t)run;
    if (io->read_timer > 0) return;
    const int32_t one = (int32_t)((uint64_t)2048 * d->system_clock / d->rate);
    if (2048 - d->fifo_n < 2048) { io->read_timer += one; return; }        /* the FIFO not empty yet: a sector later */
    uint8_t b[2048];
    if ((int32_t)d->sector_addr >= d->disc.leadout) { cc_error(io, 5, 0x25); return; }
    if (!read_data_sector(io, (int32_t)d->sector_addr, b)) { d->data_transfer_done = 0; cc_error(io, 5, 0); return; }
    fifo_write(d, b, 2048);
    sector_subq(io, (int32_t)d->sector_addr);
    io_cd_irq(io, 2);
    d->sector_addr++; d->sector_count--;
    if (d->phase != DATA_IN) change_phase(io, DATA_IN);
    if (d->sector_count) { d->data_transfer_done = 0; io->read_timer += one; }
    else d->data_transfer_done = 1;
}

static int cdda_tick(Hw *io)                                              /* a tick of the sample clock; 0: it stopped */
{
    Scsi *d = &io->scsi;
    io->cdda_div += io->cdda_acc;
    if (!(io->os_pos & 1)) {                                               /* a sample */
        if (io->read_pos == 588) {                                         /* the next sector */
            if (io->read_sec >= io->read_sec_end) {
                if (io->play_mode == 3) io->read_sec = io->read_sec_start;                /* loop */
                else { io->cdda_status = 0; if (io->play_mode == 2) io_cd_irq(io, 1); return 0; }
            }
            if ((int32_t)io->read_sec >= (d->have ? d->disc.leadout : (int32_t)io->leadout)) { io->cdda_status = 0; return 0; }
            io->read_pos = 0;
            if (d->have) sector_subq(io, (int32_t)io->read_sec);
            io->read_sec++;
        }
        if (!(io->read_pos % 6)) io_stuff_subchannel(io);
        io->read_pos++;
    }
    io->os_pos = (io->os_pos + 1) & 0x1F;
    return 1;
}

static int64_t cdda_ticks_to_sector(const Hw *io)                        /* the ticks before the next sector's sample */
{
    return (int64_t)(io->os_pos & 1) + 2 * (int64_t)(588 - (int)io->read_pos);
}

static void run_cdda(Hw *io, int64_t run)
{
    if (io->cdda_status != 1 && io->cdda_status != 2) return;
    if (io->cdda_status == 2) { note(io, "CD-DA scanning"); return; }
    io->cdda_div -= run << 20;
    while (io->cdda_div <= 0) {
        /* the ticks due before the next sector's sample, in one go: the samples count on, a sub-channel byte each 6 */
        const int64_t due = (io->cdda_acc < 0x40000000u && -io->cdda_div < (int64_t)io->cdda_acc * 0x40000000
                             ? div64_32(-io->cdda_div, (int32_t)io->cdda_acc) : -io->cdda_div / io->cdda_acc) + 1;
        const int64_t m = cdda_ticks_to_sector(io), b = due < m ? due : m;
        if (b > 1) {
            const unsigned smp = (unsigned)((io->os_pos & 1) ? b / 2 : (b + 1) / 2), a = io->read_pos;
            const unsigned stuffs = (a + smp + 5) / 6 - (a + 5) / 6;
            io->read_pos = a + smp; io->cdda_div += b * io->cdda_acc; io->os_pos = (unsigned)(io->os_pos + (unsigned)(b & 31)) & 0x1F;
            if (stuffs) io_stuff_subchannels(io, stuffs);
            continue;
        }
        if (!cdda_tick(io)) break;
    }
}

int64_t scsi_next(const Hw *io, int lazy)
{
    int64_t n = 0x7FFFFFFF;
    if (io->read_timer > 0 && io->read_timer < n) n = io->read_timer;
    if (io->cdda_status == 1 || io->cdda_status == 2) {
        const int64_t k = lazy && io->cdda_status == 1 ? cdda_ticks_to_sector(io) : (int64_t)(io->os_pos & 1);
        const int64_t s = (io->cdda_div + (int64_t)io->cdda_acc * k + ((1 << 20) - 1)) >> 20;
        if (s > 0 && s < n) n = s;
    }
    return n;
}

/* quiet: no drive; or its bus free and not selected; or the bus waiting for the CPU - REQ up, ACK down in a phase whose
   step then changes nothing (scsi_bus: COMMAND, STATUS, MESSAGE_IN, DATA_IN), no RST edge to come. Every CPU write to
   the bus's lines runs scsi_run at once (io_model.c), so a step between two of them could only repeat that nothing. */
int scsi_quiet(const Hw *io)
{
    const Scsi *d = &io->scsi;
    if (!d->have) return 1;
    const uint32_t s = d->signals;
    if (d->phase == BUS_FREE) return !(s & S_SEL);
    if (((s & S_RST) != 0) != (d->last_rst != 0)) return 0;
    if ((s & (S_REQ | S_ACK)) == S_REQ) return d->phase != DATA_OUT && d->phase != MESSAGE_OUT;
    /* (docs/75) or the bus waiting for the drive - REQ, ACK and ATN down, a command taken (COMMAND, none of its bytes
       held) or a read's sectors all sent but the next (DATA_IN, the FIFO empty, the transfer not done, its ready
       interrupt already cleared): scsi_bus does nothing then but the timers, whose ends are io_next_event's */
    if (s & (S_REQ | S_ACK | S_ATN)) return 0;
    if (d->phase == COMMAND) return d->cmd_pos == 0;
    return d->phase == DATA_IN && d->fifo_n == 0 && !d->data_transfer_done && !(io->port[3] & 0x40);
}

static void virtual_reset(Hw *io)
{
    Scsi *d = &io->scsi;
    fifo_flush(d); io->read_timer = 0; d->last_sapsp = d->mono; d->sector_addr = d->sector_count = 0;
    io->read_sec_start = io->read_sec = 0; io->read_sec_end = ~0u; io->play_mode = 0; io->read_pos = 0; io->cdda_status = 0;
    io->cdda_div = 0; io->os_pos = 0;
    io->cdda_acc = (uint32_t)((int64_t)d->system_clock * (1024 * 1024) / (2 * 44100));
    change_phase(io, BUS_FREE);
}

/* Mednafen SCSICD_Run: the clocks to t, then one step of the bus */
static void scsi_bus(Hw *io, int64_t run);
void scsi_run(Hw *io, int64_t t)
{
    Scsi *d = &io->scsi;
    const int64_t run = t - d->last;
    d->mono += run; d->last = t;
    if (!d->have) { run_cdda(io, run); return; }     /* no drive state: CD-DA's clock only (docs/14) */
    scsi_bus(io, run);
}
/* (the drive's bus, apart: the SH2 has no drive state - RT_NO_DISC - and links it in the cart, DraculaX docs/40) */
static __attribute__((noinline)) void scsi_bus(Hw *io, int64_t run)
{
    Scsi *d = &io->scsi;
    run_read(io, run);
    run_cdda(io, run);
    const uint32_t s = d->signals;
    const int rst = (s & S_RST) != 0, req = (s & S_REQ) != 0, ack = (s & S_ACK) != 0;
    const int reset = rst && !d->last_rst;
    d->last_rst = rst;
    if (reset) { virtual_reset(io); return; }
    if (d->phase == BUS_FREE) { if (s & S_SEL) change_phase(io, COMMAND); return; }
    if ((s & S_ATN) && !req && !ack) { change_phase(io, MESSAGE_OUT); return; }
    static const int cdb_len[16] = { 6, 6, 10, 10, 10, 10, 10, 10, 10, 10, 12, 12, 10, 10, 10, 10 };
    switch (d->phase) {
    case COMMAND:
        if (req && ack) { d->cmd[d->cmd_pos++] = d->db; set_sig(io, S_REQ, 0); }
        if (!(d->signals & S_REQ) && !(d->signals & S_ACK) && d->cmd_pos) {
            if (d->cmd_pos == cdb_len[d->cmd[0] >> 4]) { do_command(io); d->cmd_pos = 0; d->commands++; }
            else set_sig(io, S_REQ, 1);
        }
        break;
    case DATA_OUT: note(io, "SCSI data out (MODE SELECT)"); break;
    case MESSAGE_OUT:
        if (req && ack) { set_sig(io, S_REQ, 0); fifo_flush(d); io->read_timer = 0; io->cdda_status = 0; change_phase(io, BUS_FREE); }
        break;
    case STATUS:
        if (req && ack) { set_sig(io, S_REQ, 0); d->status_sent = 1; }
        if (!(d->signals & S_REQ) && !(d->signals & S_ACK) && d->status_sent) {
            d->status_sent = 0; d->db = d->message_pending; change_phase(io, MESSAGE_IN);
        }
        break;
    case DATA_IN:
        if (!req && !ack) {
            if (!d->fifo_n) {                                              /* all sent */
                io_cd_irq(io, 0x8000 | 2);
                if (d->data_transfer_done) { status_message(io, 0, 0); d->data_transfer_done = 0; io_cd_irq(io, 1); }
            } else { d->db = d->fifo[d->fifo_r]; d->fifo_r = (d->fifo_r + 1) & 2047; d->fifo_n--; set_sig(io, S_REQ, 1); }
        }
        if ((d->signals & S_REQ) && (d->signals & S_ACK)) set_sig(io, S_REQ, 0);
        break;
    case MESSAGE_IN:
        if (req && ack) { set_sig(io, S_REQ, 0); d->message_sent = 1; }
        if (!(d->signals & S_REQ) && !(d->signals & S_ACK) && d->message_sent) { d->message_sent = 0; change_phase(io, BUS_FREE); }
        break;
    }
}

/* docs/75 (HUC_CDSTEP, rt_cdstep): n data bytes read in one go, each as read_1808 then the ACK's let-go take one - with
   the bus quiet before each (DATA_IN, REQ up, ACK and ATN down, no RST edge) and the FIFO holding the n bytes after the
   one on the bus, so it never empties between them (no interrupt changes; set_sig's REQ recheck changes nothing). out:
   the bytes read; the bus after: as after the n-th read's ACK let go. 0: not so, nothing done */
int scsi_bytes(Hw *io, unsigned n, uint8_t *out)
{
    Scsi *d = &io->scsi;
    const uint32_t s = d->signals;
    if (!d->have || d->phase != DATA_IN || (s & (S_REQ | S_ACK | S_ATN)) != S_REQ || ((s & S_RST) != 0) != (d->last_rst != 0) ||
        n == 0 || (unsigned)d->fifo_n < n) return 0;
    out[0] = d->db;
    for (unsigned i = 1; i < n; i++) out[i] = d->fifo[(d->fifo_r + (int)i - 1) & 2047];
    d->db = d->fifo[(d->fifo_r + (int)n - 1) & 2047]; d->fifo_r = (d->fifo_r + (int)n) & 2047; d->fifo_n -= (int)n;
    return 1;
}

#ifdef RT_NO_DISC
#define SERR(e, n, ...) ((n) > 0 ? (void)((e)[0] = '!', (e)[(n) > 1] = 0) : (void)0)   /* (no stdio on the SH2) */
#else
#define SERR snprintf
#endif
static int scsi_state(Hw *io, const uint32_t *o, const uint8_t *fifo, const uint32_t *toc, int64_t t0, char *err, int errn);
int scsi_start_tab(Hw *io, const uint32_t *o, const uint8_t *fifo, const uint32_t *toc, const int32_t *tab, int64_t t0, char *err, int errn)
{
    Scsi *d = &io->scsi; Disc *k = &d->disc;
    memset(d, 0, sizeof *d);
    k->first = tab[0]; k->last = tab[1]; k->leadout = tab[2]; k->tab = 1;
    for (int t = 0; t < 101; t++) { const int32_t *x = tab + 3 + 6 * t;
        k->t[t].lba = x[0]; k->t[t].sectors = x[1]; k->t[t].pregap = x[2]; k->t[t].control = (uint8_t)x[3]; k->t[t].data = x[4]; k->t[t].sector_size = x[5]; }
    return scsi_state(io, o, fifo, toc, t0, err, errn);
}
int scsi_start(Hw *io, const uint32_t *o, const uint8_t *fifo, const uint32_t *toc, const char *cue, int64_t t0, char *err, int errn)
{
    Scsi *d = &io->scsi;
    memset(d, 0, sizeof *d);
#ifdef RT_NO_DISC
    (void)o; (void)fifo; (void)toc; (void)cue; (void)t0; if (errn > 0) err[0] = 0;
    return 0;
#else
    if (!cue || !disc_open(io, cue)) { snprintf(err, errn, "the disc's cue sheet (%s) can't be read", cue ? cue : "none"); return 0; }
    return scsi_state(io, o, fifo, toc, t0, err, errn);
#endif
}
static int scsi_state(Hw *io, const uint32_t *o, const uint8_t *fifo, const uint32_t *toc, int64_t t0, char *err, int errn)
{
    Scsi *d = &io->scsi;
    {
    Disc *k = &d->disc;
    if ((int)toc[0] != k->first || (int)toc[1] != k->last || (int32_t)toc[2 + 200] != k->leadout) {
        SERR(err, errn, "the disc's tracks %d-%d, lead-out %d differ from the core's %u-%u, %d", k->first, k->last, k->leadout, toc[0], toc[1], (int32_t)toc[202]);
        return 0;
    }
    for (int t = k->first; t <= k->last; t++)
        if ((int32_t)toc[2 + 2 * t] != k->t[t].lba || (toc[3 + 2 * t] & 0xFF) != k->t[t].control) {
            SERR(err, errn, "track %d at %d (control %d) differs from the core's %d (%u)", t, k->t[t].lba, k->t[t].control, (int32_t)toc[2 + 2 * t], toc[3 + 2 * t] & 0xFF);
            return 0;
        }
    k->leadout_control = (uint8_t)toc[3 + 200];
    d->phase = (int)o[0]; d->signals = o[1]; d->db = (uint8_t)o[2]; d->message_pending = (uint8_t)o[3];
    d->status_sent = (int)o[4]; d->message_sent = (int)o[5];
    d->key = (uint8_t)o[6]; d->asc = (uint8_t)o[7]; d->ascq = (uint8_t)o[8]; d->fru = (uint8_t)o[9];
    d->cmd_pos = (int)o[10]; d->data_transfer_done = (int)o[11]; d->last_rst = (int)o[13];
    d->sector_addr = o[14]; d->sector_count = o[15];
    d->fifo_n = (int)o[17]; d->fifo_r = 0; memcpy(d->fifo, fifo, (size_t)d->fifo_n);
    d->mono = (int64_t)((uint64_t)o[18] | (uint64_t)o[19] << 32); d->last_sapsp = (int64_t)((uint64_t)o[20] | (uint64_t)o[21] << 32);
    d->system_clock = o[35]; d->rate = o[36];
    d->last = t0;
    for (int i = 0; i < 12; i++) d->subq_last[i] = (uint8_t)o[40 + i];
    for (int q = 0; q < 4; q++) for (int i = 0; i < 12; i++) d->subq[q][i] = (uint8_t)o[52 + q * 12 + i];
    for (int i = 0; i < 16; i++) d->cmd[i] = (uint8_t)o[200 + i];
    if (o[12] || o[37] || !o[38]) { SERR(err, errn, "disc changed, tray open or no disc in the core"); return 0; }
    d->have = 1;
    return 1;
    }
}
