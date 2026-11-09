// dumpvid.cpp - capture export for the DraculaX graphics extraction (not part of Mednafen).
// Copies the VDC's VRAM and SAT, the VCE colour table and the VDC registers that place the BG.
#include <mednafen/mednafen.h>
#define private public
#include "mednafen/pce/pce.h"
#undef private
#include "mednafen/pce/huc.h"
#define private public
#include "mednafen/pce/vce.h"
#undef private
extern VCE *vce;
extern "C" __attribute__((visibility("default")))
int dumpvid(uint16 *vram /*65536*/, uint16 *sat /*256*/, uint16 *pal /*512*/, uint16 *regs /*8*/)
{
	if (!vce) return 0;
	VDC &v = vce->vdc[0];
	memcpy(vram, v.VRAM, sizeof(v.VRAM));
	memcpy(sat, v.SAT, sizeof(v.SAT));
	memcpy(pal, vce->color_table, sizeof(vce->color_table));
	regs[0] = v.MWR; regs[1] = v.BXR; regs[2] = v.BYR; regs[3] = v.CR;
	regs[4] = v.HDR; regs[5] = v.VDR; regs[6] = v.VCR; regs[7] = v.DCR;
	return 1;
}

/* ---- M0 trackers: who writes the RAM, and what the CD reads ----
 * ramtrack(): called by the base RAM (8 KB, bank F8), CD RAM (64 KB, banks 80-87) and Super System Card RAM
 * (192 KB, banks 68-7F) write handlers. Flags per byte: 1 = written while the CPU ran System Card ROM code
 * (banks 00-3F: the BIOS, which is what copies CD data in), 2 = written by code anywhere else (the game).
 * Index: base RAM 0-8191, CD RAM 8192-73727, Super RAM 73728-270335. */
static uint8 ram_flags[8192 + 65536 + 196608];
static uint32 ram_epoch_frame;
extern "C" void ramtrack(uint32 index, uint8 v)
{
  (void)v;
  uint8 bank = HuCPU.MPR[(HuCPU.PC >> 13) & 7];
  if (index < sizeof(ram_flags))
    ram_flags[index] |= (bank < 0x40) ? 1 : 2;
}
extern "C" __attribute__((visibility("default"))) uint32 ramtrack_get(uint8 *out, int clear)
{
  memcpy(out, ram_flags, sizeof(ram_flags));
  if (clear) memset(ram_flags, 0, sizeof(ram_flags));
  return sizeof(ram_flags);
}
/* CD READ commands: start sector (LBA), sector count, in the order the game asked for them */
static uint32 cd_log[8192][2]; static uint32 cd_n;
extern "C" void cdtrack(uint32 sa, uint32 sc)
{
  if (cd_n < 8192) { cd_log[cd_n][0] = sa; cd_log[cd_n][1] = sc; } cd_n++;
}
extern "C" __attribute__((visibility("default"))) uint32 cdtrack_get(uint32 *out, int max, int clear)
{
  uint32 n = cd_n < (uint32)max ? cd_n : (uint32)max; if (n > 8192) n = 8192;
  memcpy(out, cd_log, n * 8);
  uint32 total = cd_n; if (clear) cd_n = 0;
  return total;
}

/* M0b: the per-line log the VDC fills (vdc.cpp): 512 lines x (BG X offset, BG Y offset, CR) */
extern uint16 dx_line_log[512][3];
extern "C" __attribute__((visibility("default"))) int dumplines(uint16 *out)
{
  memcpy(out, dx_line_log, sizeof(dx_line_log));
  return 512;
}

/* M0b: the instruction-start map the CPU fills (huc6280.cpp): 2 MB of physical address, one bit per byte */
extern uint8 dx_exec_map[(1 << 21) >> 3];
extern "C" __attribute__((visibility("default"))) int exectrack_get(uint8 *out, int clear)
{
  memcpy(out, dx_exec_map, sizeof(dx_exec_map));
  if (clear) memset(dx_exec_map, 0, sizeof(dx_exec_map));
  return sizeof(dx_exec_map);
}

/* M0b: copies of CD RAM (64 KB, banks 80-87) and Super System Card RAM (192 KB, banks 68-7F) */
extern uint8 *CDRAM;
extern uint8 *SysCardRAM;
extern "C" __attribute__((visibility("default"))) int ramcopy(uint8 *cd64k, uint8 *super192k)
{
  if (!CDRAM || !SysCardRAM) return 0;
  memcpy(cd64k, CDRAM, 65536);
  memcpy(super192k, SysCardRAM, 196608);
  return 1;
}

/* docs/36: a byte written into CD RAM (0-65535) or Super RAM (65536-262143), for the play runs' pokes (the lives) */
extern "C" __attribute__((visibility("default"))) int rampoke(uint32 off, uint8 v)
{
  if (!CDRAM || !SysCardRAM) return 0;
  if (off < 65536) CDRAM[off] = v; else if (off < 262144) SysCardRAM[off - 65536] = v; else return 0;
  return 1;
}

/* M0b: the sound log. Entries of 8 words: frame (set by the host, dx_setframe), CPU timestamp in the frame,
 * kind, then by kind:
 *   1 PSG write          a = register 0-9, v = value
 *   2 CD-unit register   a = $1800 + a (not $180A), v = value; x0 ADPCM read address, x1 length,
 *                        x2 = playing | rate << 8 | last $180D command << 16 (all after the write)
 *   3 CD audio command   a = D8 / D9 / DA, v = CDB bytes 1-4, x0 = bytes 5-8, x1 / x2 = play start / end sector
 *   4 (after 3)          a = command, v = play mode, x0 = CD-DA status, x1 = current sector, x2 = CDB byte 9 */
#define DX_SND_MAX (1 << 20)
static uint32 *dx_snd; static uint32 dx_snd_n, dx_frame;
extern "C" void sndtrack(uint32 kind, uint32 ts, uint32 a, uint32 v, uint32 x0, uint32 x1, uint32 x2)
{
  if (!dx_snd) dx_snd = (uint32 *)calloc(DX_SND_MAX, 32);
  if (dx_snd && dx_snd_n < DX_SND_MAX) {
    uint32 *e = dx_snd + dx_snd_n * 8;
    e[0] = dx_frame; e[1] = ts; e[2] = kind; e[3] = a; e[4] = v; e[5] = x0; e[6] = x1; e[7] = x2;
  }
  dx_snd_n++;
}
extern "C" __attribute__((visibility("default"))) void dx_setframe(uint32 f) { dx_frame = f; }
extern "C" __attribute__((visibility("default"))) uint32 sndtrack_get(uint32 *out, uint32 max, int clear)
{
  uint32 n = dx_snd_n < max ? dx_snd_n : max; if (n > DX_SND_MAX) n = DX_SND_MAX;
  if (out && dx_snd) memcpy(out, dx_snd, n * 32);
  uint32 total = dx_snd_n; if (clear) dx_snd_n = 0;
  return total;
}

/* M1b: VRAM and the sprite table at the first display line of the last frame (vdc.cpp) */
extern uint16 dx_vram0[65536], dx_sat0[256];
static uint16 dx_pal0[512];
extern "C" void dx_line0(void)
{
  if (vce) memcpy(dx_pal0, vce->color_table, sizeof(dx_pal0));
}
extern "C" __attribute__((visibility("default"))) int dumpvid0(uint16 *vram /*65536*/, uint16 *sat /*256*/, uint16 *pal /*512*/)
{
  memcpy(vram, dx_vram0, sizeof(dx_vram0));
  memcpy(sat, dx_sat0, sizeof(dx_sat0));
  if (pal) memcpy(pal, dx_pal0, sizeof(dx_pal0));
  return 1;
}

/* M2a: CPU counters (huc6280.cpp): instructions / cycles by physical bank, interrupts by vector (timer, IRQ1 =
   VDC, IRQ2 = CD), calls into the System Card jump table ($E000 + 3 n, bank 0 mapped at $E000); and, once
   cpustat_addr(1) is called, the CPU cycles spent at each physical address (2M x u32) */
extern uint32 dx_bank_ins[256], dx_bank_cyc[256], dx_irq[3], dx_bios[128];
extern uint32 *dx_addr_ins;
extern "C" __attribute__((visibility("default"))) void cpustat_get(uint32 *ins, uint32 *cyc, uint32 *irq, uint32 *bios, int clear)
{
  memcpy(ins, dx_bank_ins, sizeof(dx_bank_ins)); memcpy(cyc, dx_bank_cyc, sizeof(dx_bank_cyc));
  memcpy(irq, dx_irq, sizeof(dx_irq)); memcpy(bios, dx_bios, sizeof(dx_bios));
  if (clear) { memset(dx_bank_ins, 0, sizeof(dx_bank_ins)); memset(dx_bank_cyc, 0, sizeof(dx_bank_cyc));
               memset(dx_irq, 0, sizeof(dx_irq)); memset(dx_bios, 0, sizeof(dx_bios)); }
}
extern "C" __attribute__((visibility("default"))) uint32 *cpustat_addr(int on)
{
  if (on && !dx_addr_ins) dx_addr_ins = (uint32 *)calloc(1 << 21, 4);
  return dx_addr_ins;
}

/* M2: the instruction trace. Per instruction (before it runs): logical PC, A, X, Y, S, P, MPR 0-7, whether an
   interrupt was taken just before it (its vector), the CPU timestamp (21.48 MHz units) and the speed (CSL / CSH). Per hardware access: the instruction's number,
   kind (0 read, 1 write, 2 ST0-ST2 physical write), the address (logical; physical for kind 2), the value.
   trace_start(max instructions, max accesses), trace_stop(), trace_get(). */
#pragma pack(push, 1)
typedef struct { uint16 pc; uint8 a, x, y, s, p, irq; uint8 mpr[8]; uint32 ts; uint8 speed, pad[3]; } dx_ins_t;   /* 24 bytes */
typedef struct { uint32 n; uint32 addr; uint8 kind, v, pad[2]; } dx_io_t;           /* 12 bytes */
#pragma pack(pop)
int dx_trace_on;
static dx_ins_t *dx_ins; static dx_io_t *dx_io;
static uint32 dx_nins, dx_nio, dx_maxins, dx_maxio; static uint8 dx_pending_irq;
extern "C" void dx_trace_ins(uint16 pc, uint8 a, uint8 x, uint8 y, uint8 s, uint8 p, const uint8 *mpr, uint32 ts, uint8 speed)
{
  if (dx_nins < dx_maxins) {
    dx_ins_t *e = &dx_ins[dx_nins];
    e->pc = pc; e->a = a; e->x = x; e->y = y; e->s = s; e->p = p; e->irq = dx_pending_irq;
    memcpy(e->mpr, mpr, 8); e->ts = ts; e->speed = speed;
  }
  dx_pending_irq = 0; dx_nins++;
}
extern "C" void dx_trace_irq(int vector) { dx_pending_irq = (uint8)(vector & 0xFF); }   /* F6 / F8 / FA */
extern "C" void dx_trace_io(int kind, uint32 address, uint8 v)
{
  if (dx_nio < dx_maxio && dx_nins > 0) {
    dx_io_t *e = &dx_io[dx_nio]; e->n = dx_nins - 1; e->addr = address; e->kind = (uint8)kind; e->v = v;
  }
  dx_nio++;
}
extern "C" __attribute__((visibility("default"))) void trace_start(uint32 maxins, uint32 maxio)
{
  free(dx_ins); free(dx_io);
  dx_ins = (dx_ins_t *)calloc(maxins, sizeof(dx_ins_t)); dx_io = (dx_io_t *)calloc(maxio, sizeof(dx_io_t));
  dx_maxins = maxins; dx_maxio = maxio; dx_nins = dx_nio = 0; dx_pending_irq = 0; dx_trace_on = 1;
}
extern "C" __attribute__((visibility("default"))) void trace_stop(void) { dx_trace_on = 0; }
extern "C" __attribute__((visibility("default"))) void trace_get(void *ins, void *io, uint32 *nins, uint32 *nio)
{
  nins[0] = dx_nins; nio[0] = dx_nio;
  if (ins) memcpy(ins, dx_ins, (dx_nins < dx_maxins ? dx_nins : dx_maxins) * sizeof(dx_ins_t));
  if (io) memcpy(io, dx_io, (dx_nio < dx_maxio ? dx_nio : dx_maxio) * sizeof(dx_io_t));
}

/* M2: the VDC's and VCE's whole register state (for a model to start from), 48 x u32 in this order */
extern "C" __attribute__((visibility("default"))) int dumpvdcstate(uint32 *o)
{
  if (!vce) return 0;
  VDC &v = vce->vdc[0];
  uint32 k[] = { v.select, v.MAWR, v.MARR, v.CR, v.RCR, v.BXR, v.BYR, v.MWR, v.HSR, v.HDR, v.VSR, v.VDR, v.VCR, v.DCR,
                 v.SOUR, v.DESR, v.LENR, v.DVSSR, v.read_buffer, v.write_latch, v.status,
                 v.pending_read, v.pending_read_addr, v.pending_write, v.pending_write_addr, v.pending_write_latch,
                 v.DMAReadBuffer, v.DMAReadWrite, v.DMARunning, v.DMAPending, v.SATBPending, (uint32)v.sat_dma_counter,
                 v.burst_mode, (uint32)v.VDMA_CycleCounter,
                 vce->CR, vce->ctaddress, vce->dot_clock, vce->lc263 };
  /* 38-95 (M2 timing): the horizontal / vertical counters a timing model starts from */
  uint32 t[] = { (uint32)vce->hblank, (uint32)vce->hblank_counter, (uint32)vce->vblank, (uint32)vce->vblank_counter,
                 (uint32)vce->clock_divider, (uint32)vce->child_event[0], (uint32)vce->ws_counter, (uint32)vce->cd_event,
                 (uint32)vce->scanline, (uint32)vce->NeedSLReset, (uint32)vce->dot_clock_ratio, (uint32)vce->last_ts,
                 (uint32)v.HPhase, (uint32)v.HPhaseCounter, (uint32)v.VPhase, (uint32)v.VPhaseCounter,
                 (uint32)v.mystery_counter, (uint32)v.mystery_phase, (uint32)v.sprite_cg_fetch_counter, v.RCRCount,
                 (uint32)v.NeedRCRInc, (uint32)v.NeedVBIRQTest, (uint32)v.NeedSATDMATest, (uint32)v.NeedBGYInc,
                 v.HSW_cache, v.HDS_cache, v.HDW_cache, v.HDE_cache, v.VDS_cache, v.VSW_cache, v.VDW_cache, v.VCR_cache,
                 v.CR_cache, v.MWR_cache, (uint32)v.in_exhsync, (uint32)v.in_exvsync, (uint32)v.active_sprites,
                 (uint32)v.DMAReadBuffer,
                 HuCPU.timestamp, (uint32)HuCPU.next_event, (uint32)HuCPU.next_user_event, (uint32)HuCPU.timer_div };
  memset(o, 0, 96 * 4);
  memcpy(o, k, sizeof(k));
  memcpy(o + 38, t, sizeof(t));
  return 96;
}

/* M2 timing: each wait-state step of the VDC data port (VCE::WS_Hook with the event-based count). Logged before
   the steal: kind 10 = the count | which counter set it << 24 (0 hblank, 1 vblank, 2 CD, 3 VDC), v = flags;
   kind 11 = hblank_counter | HPhaseCounter << 16, v = HPhase | clock_divider << 4; kind 12 = cd_event (24 bits) |
   sprite fetch counter << 24, v = mystery_counter; kind 13 = sat_dma_counter | ws_counter << 16, v = child_event. */
extern "C" void dx_ws_log(int32 to_steal)
{
  if (!vce) return;
  VCE &c = *vce; VDC &v = c.vdc[0];
  const int32 cand[4] = { c.hblank_counter, c.vblank_counter, c.cd_event, c.child_event[0] * c.dot_clock_ratio - c.clock_divider };
  int src = 0;
  for (int i = 1; i < 4; i++) if (cand[i] < cand[src]) src = i;
  const uint8 fl = (uint8)(c.hblank | c.vblank << 1 | v.mystery_phase << 2 | (v.DMARunning ? 8 : 0) | (v.sat_dma_counter > 0 ? 16 : 0) |
                           (v.pending_write ? 64 : 0) | (v.pending_read ? 128 : 0));
  dx_trace_io(10, ((uint32)to_steal & 0xFFFFFF) | (uint32)src << 24, fl);
  dx_trace_io(11, ((uint32)c.hblank_counter & 0xFFFF) | ((uint32)v.HPhaseCounter & 0xFFFF) << 16, (uint8)(v.HPhase | c.clock_divider << 4));
  dx_trace_io(12, ((uint32)c.cd_event & 0xFFFFFF) | (uint32)(v.sprite_cg_fetch_counter > 255 ? 255 : (v.sprite_cg_fetch_counter < 0 ? 0 : v.sprite_cg_fetch_counter)) << 24,
              (uint8)(v.mystery_counter < 0 ? 0 : v.mystery_counter));
  dx_trace_io(13, ((uint32)(v.sat_dma_counter < 0 ? 0 : v.sat_dma_counter) & 0xFFFF) | ((uint32)c.ws_counter & 0xFFFF) << 16,
              (uint8)(c.child_event[0] > 255 ? 255 : c.child_event[0]));
}

/* M2 hardware: the CD unit (0-47: pcecd.cpp dx_pcecd_state; 48-79: the SCSI CD, scsicd.cpp dx_scsicd_state), the pad
   port (80-83: input.cpp dx_input_state) and the CPU's interrupt controller and timer (88-99), 128 x u32, for a model
   to start from */
extern "C" void dx_pcecd_state(uint32 *o);
extern "C" void dx_input_state(uint32 *o);
extern "C" __attribute__((visibility("default"))) int dumpiostate(uint32 *o)
{
  memset(o, 0, 128 * 4);
  dx_pcecd_state(o);
  dx_input_state(o + 80);
  o[88] = HuCPU.IRQMask; o[89] = HuCPU.IRQlow; o[90] = HuCPU.timer_status; o[91] = (uint32)HuCPU.timer_value;
  o[92] = (uint32)HuCPU.timer_load; o[93] = HuCPU.timer_inreload; o[94] = (uint32)HuCPU.timer_div; o[95] = (uint32)HuCPU.timer_lastts;
  o[96] = HuCPU.IODataBuffer; o[97] = HuCPU.timestamp; o[98] = vce ? (uint32)vce->cd_event : 0; o[99] = vce ? (uint32)vce->last_ts : 0;
  return 128;
}
