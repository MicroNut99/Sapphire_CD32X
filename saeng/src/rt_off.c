/* rt_off.c - the Rt's offsets for the hand-written leave (rt_leave.s): .equ lines in this file's assembly (the
   Makefile takes them into build/rt_off.inc) */
#include <stddef.h>
#include "rt.h"
#define E(n, v) __asm__ volatile(".equ " #n ", %c0" :: "i"(v))
void rt_off(void)
{
    E(C_S, offsetof(Cpu, S)); E(C_P, offsetof(Cpu, P)); E(C_SPEED, offsetof(Cpu, speed)); E(C_MISS, offsetof(Cpu, miss));
    E(C_PC, offsetof(Cpu, pc)); E(C_MPR, offsetof(Cpu, MPR)); E(C_RP7, offsetof(Cpu, rp[7])); E(C_RAM, offsetof(Cpu, ram));
    E(C_IDLE, offsetof(Cpu, idle)); E(C_LEFT, offsetof(Cpu, left)); E(C_IRQ_WAIT, offsetof(Cpu, irq_wait));
    E(C_LEFT_MIN, offsetof(Cpu, vp.left_min)); E(C_LEFT_MIN1, offsetof(Cpu, vp.left_min1)); E(C_XOK, offsetof(Cpu, xok));
    E(C_XMIN, offsetof(Cpu, xmin));
    E(R_ST_MIN, offsetof(Rt, st_min)); E(R_IO_MIN, offsetof(Rt, io_min)); E(R_NOW, offsetof(Rt, now));
    E(R_ENTRY_T, offsetof(Rt, entry_t)); E(R_ENTRY_LEFT, offsetof(Rt, entry_left)); E(R_FRAME_END_AT, offsetof(Rt, frame_end_at));
    E(R_IRQ_AT, offsetof(Rt, irq_at)); E(R_IRQ_STALE, offsetof(Rt, irq_stale)); E(R_SCH_IR, offsetof(Rt, sch_ir));
    E(R_SCH_STALE, offsetof(Rt, sch_stale)); E(R_SCH_RPART, offsetof(Rt, sch_rpart)); E(R_VRAM_T, offsetof(Rt, vram_t));
    E(R_OBS_T, offsetof(Rt, obs_t)); E(R_IO_AT, offsetof(Rt, io_at)); E(R_FRAME, offsetof(Rt, frame));
    E(R_ON_IDLE, offsetof(Rt, on_idle)); E(R_LAST_IDLE_IRQS, offsetof(Rt, last_idle_irqs)); E(R_IRQS, offsetof(Rt, irqs));
    E(R_IRQ2S, offsetof(Rt, irq2s)); E(R_IDLES, offsetof(Rt, idles)); E(R_SCH, offsetof(Rt, sch));
    E(S_R, offsetof(VSched, r)); E(S_END, offsetof(VSched, end)); E(S_NR, offsetof(VSched, nr)); E(S_RK, offsetof(VSched, rk));
    E(R_BANK_DIRTY, offsetof(Rt, bank_dirty)); E(R_HW_IRQ_MASK, offsetof(Rt, hw.irq_mask)); E(R_HW_PORT2, offsetof(Rt, hw.port[2]));
    E(R_VT, offsetof(Rt, vdc.t)); E(T_NOW, offsetof(VTime, now)); E(T_NEXT, offsetof(VTime, t_next));
    E(T_STATUS, offsetof(VTime, status)); E(T_RAISED_TO, offsetof(VTime, raised_to));
}
