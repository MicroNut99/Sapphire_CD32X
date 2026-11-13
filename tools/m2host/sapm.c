/* sapm.c - Sapphire docs/53: DraculaX_32X's dxm.c (its docs/99-108; Gate of Thunder's gotm.c is the same library) with the
 * Arcade Card (ac_model.c: its registers at $1A00, its ports through banks 40-43, its 2 MB in the frame's md5).
 * DraculaX docs/99: the PC Engine CD machine alone, as a library (sapm.h). trace_main.c's --run path (the interpreter's
 * hooks: trace_check before each instruction, bm_byte after each block-move byte, rd_slow / wr_slow / wr_phys for the
 * hardware page) without the trace and without translated code, and the power-on state of Mednafen's reset in code.
 * Built with tools/m2_machine.py's runtime (huc_ops.h, huc_rt.c, interp.c; -DTRACECHECK) and this folder's models. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "huc_ops.h"
#include "vdc_model.h"
#include "io_model.h"
#include "ac_model.h"
#include "huc6280_cycles.h"
#include "huc6280_optab.h"
#include "sapm.h"
#ifdef SAPM_MD5
#include <openssl/evp.h>
#endif

void interp_one(Cpu *c);

static uint8_t base_ram[8192], cd_ram[65536], super_ram[196608], bram[2048], rom[262144], adpcm_ram[65536];
static Vdc vdc; static Hw hw; static Cpu cpu;
static ArcadeCard ac; static uint8_t ac_ram[0x200000];        /* Sapphire: the Arcade Card */
static SapmSector sector_fn; static void *sector_user;
static uint16_t pad_next; static int pad_now;                  /* pad_now: the cart's pad (sapm_pad_now), not per frame */
static void (*frame_fn)(void *, long); static void *frame_user;   /* the frame's end, at its point (a block move part-done) */
static long frame, frames_to;                                 /* frames ended; the run stops after frames_to */
static int64_t until = -1;                                    /* sapm_run_until's time */
static int stop;
static uint16_t stamp[1024], snap[5];                         /* docs/101: the video window's stamps and frame snapshot */
static SapmHave have_fn; static int waiting;                    /* the feed's wait: the drive's next sector not there */
static int hold_start;                                         /* a stop before an instruction: resume straight at its start */
static void stamp_vram(unsigned a) { stamp[(a >> 6) & 1023] = (uint16_t)frame; }
/* the running instruction (as trace_main.c): its opcode, start time, base cycles, accesses so far, wait so far */
static int64_t t_ins; static int cur_op, cur_base, cur_acc, prev_set, cur_u = 12, cpu_speed, bm_done, irq_done, prev_i_gate = -1, started;
static uint8_t cur_p; static uint16_t cur_pc; static int64_t cur_wait;
static int32_t ws_counter;

static int64_t acc_time(int steal)
{
    if (huc_class[cur_op] & OC_BLK) return t_ins + cur_u * (int64_t)(14 + 6 * (int64_t)(bm_done + 1) + cur_acc + steal) + cur_wait;
    return t_ins + cur_u * (int64_t)(cur_base + cur_acc + steal) + cur_wait;
}
/* Mednafen DoWaitStates: while the VDC holds an access back, the CPU is stalled to the VCE's next event (its count in
   master clocks, the stall in CPU cycles), the chips run on, until it lets go; 455 x 64 cycles a run slice at most */
static int64_t wait_states(void)
{
    int64_t w = 0; int steps = 0;
    while (vdc.pending_read || vdc.pending_write) {
        io_run(&hw, vdc.now);
        const int64_t cd_next = vdc.now + io_next_event(&hw);
        int32_t e = vt_next_event(&vdc, cd_next);
        steps++;
        if (e + ws_counter > 455 * 64) { e = 455 * 64 - ws_counter; if (e < 0) e = 0; }
        ws_counter += e;
        w += cur_u * (int64_t)e;
        vt_run(&vdc, vdc.now + cur_u * (int64_t)e);
        if (steps > 64) break;
    }
    return w;
}
static void sync_access(int data_port)
{
    vt_run(&vdc, acc_time(1));
    if (data_port) cur_wait += wait_states();
    cur_acc++;
}
static uint8_t model_read(uint16_t a)
{
    const unsigned off = a & 0x1FFF;
    if (off < 0x800) sync_access(off < 0x400 && (off & 3) >= 2);
    if (off < 0x400) return vdc_read(&vdc, off);
    if (off < 0x800) return vce_read(&vdc, off);
    if ((off & 0x1F00) == 0x1A00) return ac_read(&ac, off);   /* the Arcade Card's registers (as trace_main.c) */
    const int64_t t = acc_time(0);
    vt_run(&vdc, t); io_run(&hw, t);
    uint8_t m;
    int ok = io_read(&hw, off, (huc_class[cur_op] & OC_BLK) != 0, &m);
    if (!ok && (off & 0x1C03) == 0x1403) {                     /* the interrupt status: the three lines now */
        m = (uint8_t)(((vdc.status & 0x3F) ? 2 : 0) | ((hw.port[2] & hw.port[3] & 0x7C) ? 1 : 0) | (hw.io_bus & ~7));
        hw.io_bus = m; ok = 1;
    }
    return ok ? m : 0xFF;
}
static void model_write(uint32_t off, uint8_t v)
{
    off &= 0x1FFF;
    if (off < 0x800) sync_access(off < 0x400 && (off & 3) == 3 && (vdc.select == 1 || vdc.select == 2));
    if (off < 0x400) vdc_write(&vdc, off, v);
    else if (off < 0x800) vce_write(&vdc, off, v);
    else if ((off & 0x1F00) == 0x1A00) ac_write(&ac, off, v);
    else { const int64_t t = acc_time(0); vt_run(&vdc, t); io_run(&hw, t); (void)io_write(&hw, off, v); }
}

static uint8_t *bank_ptr(uint8_t b, int *writable)
{
    *writable = 0;
    if (b < 0x20) return rom + b * 8192;                      /* System Card ROM */
    if (b >= 0x68 && b < 0x80) { *writable = 1; return super_ram + (b - 0x68) * 8192; }
    if (b >= 0x80 && b < 0x88) { *writable = 1; return cd_ram + (b - 0x80) * 8192; }
    if (b >= 0xF8 && b < 0xFC) { *writable = 1; return base_ram; }
    return 0;                                                  /* hardware (FF), backup RAM (F7), unmapped */
}
static uint8_t bank_dirty[256];
void set_mpr(Cpu *c, int i, uint8_t v)
{
    int w; c->MPR[i] = v; c->rp[i] = bank_ptr(v, &w); c->wok[i] = (uint8_t)w; c->dirty[i] = &bank_dirty[v];
}
uint8_t rd_slow(Cpu *c, uint16_t a)
{
    const uint8_t b = c->MPR[a >> 13];
    if (b == 0xF7) return bram[a & 0x7FF];
    if (b >= 0x40 && b <= 0x43) return ac_read(&ac, ac_phys((uint32_t)b << 13 | (a & 0x1FFF)));   /* the card's data ports */
    if (b != 0xFF) return 0xFF;
    return model_read(a);
}
void wr_slow(Cpu *c, uint16_t a, uint8_t v)
{
    const uint8_t b = c->MPR[a >> 13];
    if (b == 0xF7) { bram[a & 0x7FF] = v; return; }
    if (b >= 0x40 && b <= 0x43) { ac_write(&ac, ac_phys((uint32_t)b << 13 | (a & 0x1FFF)), v); return; }
    if (b != 0xFF) return;                                     /* ROM, unmapped */
    model_write(a, v);
}
void wr_phys(Cpu *c, uint32_t pa, uint8_t v) { (void)c; model_write(pa, v); }

static int cond_true(int k, uint8_t p)
{
    switch (k) {
    case 1: return !(p & NF); case 2: return (p & NF) != 0; case 3: return !(p & VF); case 4: return (p & VF) != 0;
    case 5: return !(p & CF); case 6: return (p & CF) != 0; case 7: return !(p & ZF); case 8: return (p & ZF) != 0;
    }
    return 0;
}
static void ins_end(uint16_t pc, int64_t *end, int64_t *sample)
{
    const uint8_t k = huc_class[cur_op];
    int taken = 0;
    if (k & OC_BRA) taken = 1;
    else if (k & OC_BCC) taken = cond_true(huc_cond[cur_op], cur_p);
    else if (k & OC_BBX) taken = pc != (uint16_t)(cur_pc + 3);
    if (cur_op == 0x54 || cur_op == 0xD4) {                    /* CSL / CSH: 2 cycles at the old speed, the last at the new */
        cpu_speed = cur_op == 0xD4;
        *sample = t_ins + 2 * cur_u; *end = *sample + (cpu_speed ? 3 : 12); return;
    }
    *sample = (k & OC_BLK) ? t_ins + cur_u * (int64_t)(cur_base + cur_acc) + cur_wait - cur_u
                           : t_ins + cur_u * (int64_t)(cur_base + 2 * taken) - cur_u;
    *end = t_ins + cur_u * (int64_t)(cur_base + 2 * taken + cur_acc) + cur_wait;
}
static void take_irq(Cpu *c, uint16_t pc, uint8_t vec, int64_t end)
{
    PUSH(pc >> 8); PUSH(pc & 0xFF); PUSH(c->P & ~BF);
    c->P = (c->P | IF) & ~(TF | DF);
    const uint16_t v = 0xFF00 | vec;
    c->pc = (uint16_t)(RD_(c, v) | RD_(c, (uint16_t)(v + 1)) << 8);
    irq_done = 1;
    t_ins = end + 8 * (cpu_speed ? 3 : 12);                    /* the interrupt's entry: 8 cycles */
}
/* Mednafen ends a run slice after the instruction in which the VCE reaches line 123 or the frame's end: the wait count
   restarts, and at the frame's end the next frame's pad buttons come in */
static uint16_t snap_lines[263][2];                           /* docs/105: the frame's line table, at its end */                           /* docs/105: the frame's line table, at its end */
static void slice_end(int64_t t)
{
    if (!vdc.exit_req || vdc.exit_time > t) return;
    ws_counter = 0;
    if (vdc.exit_req == 2) {
        snap[0] = vdc.BXR; snap[1] = vdc.BYR; snap[2] = vdc.MWR; snap[3] = vdc.CR; snap[4] = (uint16_t)frame;
        memcpy(snap_lines, vdc.lines, sizeof snap_lines);
        if (frame_fn) frame_fn(frame_user, frame);
        frame++; if (!pad_now) io_set_buttons(&hw, pad_next);
    }
    vdc.exit_req = 0;
}
void bm_byte(Cpu *c, unsigned done)
{
    (void)c;
    bm_done = (int)done;
    const int64_t t = t_ins + cur_u * (int64_t)(14 + 6 * (int64_t)done + cur_acc) + cur_wait;
    vt_run(&vdc, t); io_run(&hw, t);
    slice_end(t);
}
/* before each instruction (the runtime's TRACECHECK hook): the last one's end, the interrupts, the next one's start */
int trace_check(Cpu *c, uint16_t pc)
{
    static int held; static int64_t held_end, held_sample;   /* a stop at this boundary: its end done, its interrupt check not */
    if (hold_start) { hold_start = 0; goto start; }            /* (a feed wait's resume: this instruction's start, again) */
    if (irq_done) { irq_done = 0; vt_run(&vdc, t_ins); io_run(&hw, t_ins); slice_end(t_ins); }
    else if (started) {
        int64_t end, sample;
        if (held) { held = 0; end = held_end; sample = held_sample; }
        else {
            ins_end(pc, &end, &sample);
            t_ins = end;
            vt_run(&vdc, end); io_run(&hw, end);
            slice_end(end);
            if (frame >= frames_to || (until >= 0 && t_ins >= until)) { held = 1; held_end = end; held_sample = sample; c->miss = 2; c->pc = pc; stop = 1; return 1; }
        }
        const int i_set = prev_i_gate >= 0 ? prev_i_gate : (c->P & IF) != 0;
        if (!i_set && (hw.irq_mask & 2) && vt_irq_at(&vdc, sample)) { take_irq(c, pc, 0xF8, end); return 1; }
        if (!i_set && (hw.irq_mask & 1) && io_irq2_at(&hw, sample)) { take_irq(c, pc, 0xF6, end); return 1; }
    }
    /* (a stop here - after an interrupt's entry, or before the first instruction - resumes at this instruction's start:
       the last one's end is done; docs/104 - before, the resume took that end again) */
    if (frame >= frames_to || (until >= 0 && t_ins >= until)) { if (started) hold_start = 1; c->miss = 2; c->pc = pc; stop = 1; return 1; }
start:
    started = 1;
    { static long itf = -2; if (itf == -2) itf = getenv("ITRACE") ? atol(getenv("ITRACE")) : -1;
      if (frame == itf) { static int once; if (!once) { once = 1; printf("mpr"); for (int i = 0; i < 8; i++) printf(" %02X", c->MPR[i]); printf("\n"); }
          printf("i %04X %02X %02X %02X %02X %02X t %lld\n", pc, c->A, c->X, c->Y, c->S, c->P, (long long)t_ins); } }
    const uint8_t op = RD_(c, pc), k = huc_class[op];
    {   /* (docs/58, IFF=1: the first run of three $FF opcodes - a jump into nothing - with the 48 instructions before it) */
        static int on = -1, run; static uint16_t ring[64]; static uint8_t rb[64], ra[64]; static unsigned rn;
        if (on < 0) on = getenv("IFF") != NULL;
        if (on) { ring[rn & 63] = pc; rb[rn & 63] = c->MPR[pc >> 13]; ra[rn & 63] = c->A; rn++; run = op == 0xFF ? run + 1 : 0;
            if (run == 3) { printf("iff frame %ld:", frame); for (unsigned i = rn - 51; i != rn; i++) printf(" %02X:%04X(%02X)", rb[i & 63], ring[i & 63], ra[i & 63]);
                printf("\nmpr"); for (int i = 0; i < 8; i++) printf(" %02X", c->MPR[i]); printf("  S %02X\n", c->S); on = 0; } }
    }
    prev_i_gate = (k & OC_IGATE) ? (c->P & IF) != 0 : -1;
    cur_op = op; cur_pc = pc; cur_p = c->P; cur_acc = 0; cur_wait = 0; cur_u = cpu_speed ? 3 : 12; bm_done = 0;
    cur_base = huc_cycles[op] + ((k & OC_DEC) && (c->P & DF) ? 1 : 0) + (prev_set && (k & OC_T) ? 3 : 0);
    if (k & OC_BLK) { const unsigned len = RD_(c, (uint16_t)(pc + 5)) | RD_(c, (uint16_t)(pc + 6)) << 8; cur_base += 6 * (int)(len ? len : 65536); }
    /* docs/101: the feed's wait - if the drive's read timer could end within this instruction (its longest: its cycles at
       this speed, and four lines for a VDC wait - a longer one that reaches the read finds no sector: sapm_missed counts
       it) and the sector it reads isn't there, stop before it; the machine's time stands
       still until it is (on the cart: the master clock held while the firmware waits for it, docs/95 §8a) */
    if (have_fn && hw.scsi.have && hw.read_timer > 0 && hw.scsi.sector_count > 0 &&
        hw.read_timer <= (int64_t)cur_u * (cur_base + 16) + 4 * 1365 && !have_fn(sector_user, (int32_t)hw.scsi.sector_addr)) {
        waiting = 1; hold_start = 1; c->miss = 2; c->pc = pc; stop = 1; return 1;
    }
    waiting = 0;
    prev_set = (k & OC_SET) != 0;
    return 0;
}

static long missed;
int rt_disc_sector(int32_t s, uint8_t *out) { (void)s; const int ok = sector_fn ? sector_fn(sector_user, rt_disc_lba, out) : 0; if (!ok) missed++; return ok; }
long sapm_missed(void) { return missed; }

/* Mednafen's power-on, as the state loaders take it (m2_trace.py's dumps at the core's first instruction, docs/98):
   the video chips (dumpvdcstate), the I/O page and CD unit (dumpiostate), the drive (dumpscsistate); nothing else is
   non-zero but the colours (VCE::Reset's pattern) */
static const struct { uint8_t i; uint32_t v; } VDC_ON[] = {
    {18, 0xFFFF}, {22, 0xFFFF}, {24, 0xFFFF}, {25, 0xFFFF}, {38, 1}, {39, 0xED}, {40, 1}, {41, 0x101D}, {43, 1},
    {45, 0x29D6}, {48, 4}, {51, 1}, {53, 1}, {77, 4}, {78, 4}, {79, 0x400} };
static const struct { uint8_t i; uint32_t v; } IO_ON[] = {
    {19, 0x7FFFFFFF}, {35, 0x29D55A5}, {37, 0x29D55A50}, {51, 0xF381A90}, {57, 0xFFFFFFFF}, {60, 0x3604E}, {67, 1},
    {83, 1}, {89, 0x20}, {94, 0x400}, {96, 0xFF}, {98, 0x29D6} };
static const struct { uint8_t i; uint32_t v; } SCSI_ON[] = { {23, 0xFFFFFFFF}, {30, 0xF381A90}, {35, 0x147B798}, {36, 0x1EC30}, {38, 1} };

int sapm_power(const uint8_t *syscard, const uint8_t *bram_in, const int32_t *tab, SapmSector sector, void *user)
{
    memset(base_ram, 0, sizeof base_ram); memset(cd_ram, 0, sizeof cd_ram); memset(super_ram, 0, sizeof super_ram);
    memset(adpcm_ram, 0, sizeof adpcm_ram); memcpy(rom, syscard, sizeof rom);
    if (bram_in) memcpy(bram, bram_in, sizeof bram); else memset(bram, 0, sizeof bram);
    sector_fn = sector; sector_user = user;
    { static const uint32_t ac0[19]; memset(ac_ram, 0, sizeof ac_ram); ac_start(&ac, ac0, ac_ram); }   /* the card at power-on: all zero */
    static uint32_t r[96], q[128], o[256 + 512], toc[204];
    memset(r, 0, sizeof r); memset(q, 0, sizeof q); memset(o, 0, sizeof o); memset(toc, 0, sizeof toc);
    for (unsigned k = 0; k < sizeof VDC_ON / sizeof *VDC_ON; k++) r[VDC_ON[k].i] = VDC_ON[k].v;
    for (unsigned k = 0; k < sizeof IO_ON / sizeof *IO_ON; k++) q[IO_ON[k].i] = IO_ON[k].v;
    for (unsigned k = 0; k < sizeof SCSI_ON / sizeof *SCSI_ON; k++) o[SCSI_ON[k].i] = SCSI_ON[k].v;
    vdc_init(&vdc);
    for (int i = 0; i < 512; i++) vdc.color[i] = ((i ^ (i >> 3)) & 1) ? 0x000 : 0x1FF;
    vdc.read_buffer = r[18]; vdc.pending_read_addr = r[22]; vdc.pending_write_addr = r[24]; vdc.pending_write_latch = r[25];
    vt_start(&vdc, r, 0);
    vdc_vram_hook = stamp_vram; memset(stamp, 0, sizeof stamp); memset(snap, 0, sizeof snap); waiting = 0; hold_start = 0;
    io_start(&hw, q, 0);
    hw.ram = adpcm_ram;
    toc[0] = (uint32_t)tab[0]; toc[1] = (uint32_t)tab[1];       /* the core's TOC, from the table: tracks, lead-out */
    for (int t = 0; t < 101; t++) { toc[2 + 2 * t] = (uint32_t)tab[3 + 6 * t]; toc[3 + 2 * t] = 0x100 | (uint32_t)tab[3 + 6 * t + 3]; }
    for (int t = 0; t < 101; t++) if (t < tab[0] || t > tab[1]) toc[2 + 2 * t] = toc[3 + 2 * t] = 0;   /* (as the core's) */
    toc[2 + 200] = (uint32_t)tab[2]; toc[3 + 200] = 0x100 | (uint32_t)tab[3 + 6 * tab[1] + 3];
    char err[256];
    if (!scsi_start_tab(&hw, o, (const uint8_t *)(o + 256), toc, tab, 0, err, sizeof err)) { fprintf(stderr, "sapm: the drive: %s\n", err); return 0; }
    pad_next = 0; io_set_buttons(&hw, 0);
    memset(&cpu, 0, sizeof cpu); Cpu *c = &cpu;
    c->ram = base_ram;
    for (int i = 0; i < 8; i++) set_mpr(c, i, 0);
    c->A = c->X = c->Y = c->S = 0; c->P = 0x04;                /* Mednafen's power-on: I set, at low speed */
    c->pc = (uint16_t)(rom[0x1FFE] | rom[0x1FFF] << 8);        /* the reset vector (MPR7 = 0: the System Card's bank 0) */
    cpu_speed = 0; c->speed = 0;
    t_ins = 0; frame = 0; started = 0; irq_done = 0; prev_i_gate = -1; prev_set = 0; ws_counter = 0; stop = 0;
    return 1;
}
void sapm_pad(uint16_t m) { pad_next = m; pad_now = 0; }
void sapm_pad_now(uint8_t b) { pad_now = 1; if ((b & 0x50) == 0x50) b &= ~0x50; if ((b & 0xA0) == 0xA0) b &= ~0xA0; hw.buttons = b; }
void sapm_feed(SapmHave have) { have_fn = have; }
int sapm_waiting(void) { return waiting; }
int32_t sapm_drive_next(void) { return hw.scsi.have ? (int32_t)hw.scsi.sector_addr : 0; }
uint32_t sapm_audio(void) { return (hw.cdda_status == 1 ? 1u << 31 : 0) | (uint32_t)(hw.play_mode & 3) << 24 | (hw.read_sec_start & 0x1FFFFF); }
const uint16_t *sapm_stamps(void) { return stamp; }
const uint16_t *sapm_snapshot(void) { return snap; }
void sapm_on_frame(void (*fn)(void *, long), void *user) { frame_fn = fn; frame_user = user; }
static void run(void)
{
    Cpu *c = &cpu; stop = 0;
    while (!stop) {
        c->miss = 0;
        if (trace_check(c, c->pc)) { if (stop) break; continue; }
        interp_one(c);
    }
}
long sapm_run_frames(long n) { frames_to = frame + n; until = -1; run(); return frame; }
int64_t sapm_run_until(int64_t t) { frames_to = 0x7FFFFFFF; until = t; run(); until = -1; return t_ins; }
int64_t sapm_now(void) { return t_ins; }
long sapm_frame(void) { return frame; }
unsigned sapm_pc(void) { return cpu.pc; }                  /* (docs/60: for the stand-in's trap - the CPU in the I/O page) */

static uint32_t fnv(uint32_t h, const uint8_t *p, long n) { while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static uint32_t fnv16(uint32_t h, const uint16_t *p, long n)
{
    while (n--) { const uint16_t w = *p++; h ^= (uint8_t)w; h *= 16777619u; h ^= (uint8_t)(w >> 8); h *= 16777619u; }
    return h;
}
uint32_t sapm_hash(int with_stack)
{
    uint32_t h = 2166136261u;
    if (with_stack) h = fnv(h, base_ram, 8192); else { h = fnv(h, base_ram, 0x100); h = fnv(h, base_ram + 0x200, 8192 - 0x200); }
    h = fnv(h, cd_ram, 65536); h = fnv(h, super_ram, 196608);
    h = fnv16(h, vdc.vram, 32768); h = fnv16(h, vdc.sat, 256); h = fnv16(h, vdc.color, 512);
    return h;
}
#ifdef SAPM_MD5
void sapm_md5(uint8_t md[16])
{
    static const uint8_t zero[16384]; unsigned len;
    EVP_MD_CTX *x = EVP_MD_CTX_new(); EVP_DigestInit_ex(x, EVP_md5(), 0);
    (void)zero; EVP_DigestUpdate(x, base_ram, 8192);            /* as Sapphire's tools/m0b_run.py hashes a frame */
    EVP_DigestUpdate(x, cd_ram, 65536); EVP_DigestUpdate(x, super_ram, 196608); EVP_DigestUpdate(x, ac_ram, sizeof ac_ram);
    EVP_DigestUpdate(x, vdc.vram, sizeof vdc.vram); EVP_DigestUpdate(x, vdc.sat, sizeof vdc.sat); EVP_DigestUpdate(x, vdc.color, sizeof vdc.color);
    EVP_DigestFinal_ex(x, md, &len); EVP_MD_CTX_free(x);
}
#endif
uint8_t *sapm_base_ram(void) { return base_ram; }
uint8_t *sapm_cd_ram(void) { return cd_ram; }
uint8_t *sapm_super_ram(void) { return super_ram; }
uint8_t *sapm_bram(void) { return bram; }
uint8_t *sapm_card(void) { return ac_ram; }
struct Vdc *sapm_vdc(void) { return &vdc; }                  /* (docs/55: the models themselves, for a host that makes pictures and sound) */
struct Hw *sapm_hw(void) { return &hw; }
uint8_t *sapm_adpcm_ram(void) { return adpcm_ram; }
const uint16_t *sapm_lines(void) { return &snap_lines[0][0]; }
uint16_t *sapm_vram(void) { return vdc.vram; }
uint16_t *sapm_sat(void) { return vdc.sat; }
uint16_t *sapm_colors(void) { return vdc.color; }
void sapm_regs(uint16_t *bxr, uint16_t *byr, uint16_t *mwr, uint16_t *cr) { *bxr = vdc.BXR; *byr = vdc.BYR; *mwr = vdc.MWR; *cr = vdc.CR; }

/* the hidden state, as text (trace_main.c's RUN_STATE: to diff two runs) */
void sapm_state_text(void)
{   /* the hidden state after frame k, as text */
        const Scsi *d = &hw.scsi; unsigned ah = 0; for (int i = 0; hw.ram && i < 65536; i++) ah = ah * 31 + hw.ram[i];
        printf("state port2 %u port3 %u port4 %u portB %u addr %u read_addr %u write_addr %u length %u half %d end %d playing %d "
               "cmd %u freq %u read_pending %d write_pending %d nibble %d bigdiv %lld fade_vol %d fade_ctr %d fade_on %d "
               "cdda %d cdda_div %lld os_pos %u read_pos %u read_sec %u read_sec_end %u read_sec_start %u mode %d read_timer %d "
               "ack_delay %d phase %d signals %u db %u cmd_pos %d done %d sector %u count %u fifo %d adpcm_ram %u\n",
               hw.port[2], hw.port[3], hw.port[4], hw.port[0xB], hw.addr, hw.read_addr, hw.write_addr, hw.length, hw.half, hw.end,
               hw.playing, hw.last_cmd, hw.freq, hw.read_pending, hw.write_pending, hw.nibble, (long long)hw.bigdiv, hw.fade_vol,
               hw.fade_ctr, hw.fade_on, hw.cdda_status, (long long)hw.cdda_div, hw.os_pos, hw.read_pos, hw.read_sec, hw.read_sec_end,
               hw.read_sec_start, hw.play_mode, hw.read_timer, hw.ack_delay, d->phase, d->signals, d->db, d->cmd_pos,
               d->data_transfer_done, d->sector_addr, d->sector_count, d->fifo_n, ah);
        printf("state2 MAWR %u MARR %u CR %u RCR %u BXR %u BYR %u MWR %u HSR %u HDR %u VSR %u VDR %u VCR %u DCR %u SOUR %u DESR %u "
               "LENR %u DVSSR %u read_buffer %u write_latch %u status %u select %u pending_write %u ctaddress %u vce_cr %u "
               "hblank %d hblank_ctr %d vblank %d vblank_ctr %d clkdiv %d scanline %d hphase %d hphase_ctr %d vphase %d vphase_ctr %d "
               "myst_ctr %d myst_ph %d spr_fetch %d rcr_count %u sat_dma %d sel %d clr %d read_index %d irq_mask %u io_bus %u "
               "mono %lld last_sapsp %lld subq %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
               vdc.MAWR, vdc.MARR, vdc.CR, vdc.RCR, vdc.BXR, vdc.BYR, vdc.MWR, vdc.HSR, vdc.HDR, vdc.VSR, vdc.VDR, vdc.VCR, vdc.DCR,
               vdc.SOUR, vdc.DESR, vdc.LENR, vdc.DVSSR, vdc.read_buffer, vdc.write_latch, vdc.status, vdc.select, vdc.pending_write,
               vdc.ctaddress, vdc.vce_cr, vdc.hblank, vdc.hblank_ctr, vdc.vblank, vdc.vblank_ctr, vdc.clkdiv, vdc.scanline, vdc.hphase,
               vdc.hphase_ctr, vdc.vphase, vdc.vphase_ctr, vdc.myst_ctr, vdc.myst_ph, vdc.spr_fetch, vdc.rcr_count, vdc.sat_dma,
               hw.sel, hw.clr, hw.read_index, hw.irq_mask, hw.io_bus, (long long)d->mono, (long long)d->last_sapsp,
               d->subq[1][0], d->subq[1][1], d->subq[1][2], d->subq[1][3], d->subq[1][4], d->subq[1][5], d->subq[1][6], d->subq[1][7], d->subq[1][8], d->subq[1][9]);
    }
