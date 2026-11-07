! rt_leave.s - the SH2 hot code's leave (tools/m3rt/rt.c rt_leave_fast), by hand for its quick case (docs/40).
!
! The hot code calls _rt_leave_fast at its budget's end or at a wait (r4 = GBR = the Cpu, the Rt's first member).
! Here: rt.c's rt_leave_fast checks and tail_quick, step for step - the interrupt the schedule's list says is up by
! then taken or left waiting, the wait's skip, the next budget and vp_update's thresholds - as tail_quick does them.
! Wherever rt.c would do anything else (return 0, run_tail and run_head, vram_find's look), nothing has been written
! yet and the C function (rt_leave_c: rt_leave_fast's tests, then run_tail and run_head) does it from the start.
! obs_find, which tail_quick calls when the VDC's next seen event has passed, is called here as there (only obs_t
! changes: the next look finds the same). The offsets: build/rt_off.inc (src/rt_off.c).
! r12 holds obs_t until the list is read, then the next raise ("next"). r1-r7 are caller-saved; the C calls reload them.
!
! r4: the Cpu / Rt (GBR the same). Kept across the C calls: r8 vt (the next VRAM event), r9 d (the VDC's seen time),
! r10 t (the time run to), r11 flags (1 idle, 2 touch0, 4 raising, 8 take, 16 irq1), r12 obs_t / next, r13 bits, st.
	.include "build/rt_off.inc"
	.section .text.rt_leave,"ax"
	.align 4
	.global _rt_leave_fast
_rt_leave_fast:
	! ---- rt_leave_fast's checks ----
	mov.b	@(C_MISS,gbr),r0
	tst	r0,r0
	bf	.Lslow0
	mov.b	@(C_P,gbr),r0
	tst	#8,r0			! (D)
	bf	.Lslow0
	mov.l	.Lrunf,r1
	mov.l	@(R_FRAME,gbr),r0
	mov.l	@r1,r1
	cmp/ge	r1,r0			! frame >= run_frames
	bt	.Lslow0
	mov.l	@(R_IRQ_STALE,gbr),r0
	tst	r0,r0
	bf	.Lslow0			! (stale: run_tail, run_head)
	mov.l	@(R_IRQ_AT,gbr),r0
	mov	r0,r2
	mov.l	@(R_FRAME_END_AT,gbr),r0
	cmp/ge	r0,r2
	bt	.Lslow0			! irq_at >= frame_end_at
	mov.w	.Lmask,r0
	mov.b	@(r0,r4),r0
	tst	#2,r0
	bt	.Lslow0			! (IRQ1 masked: no tail_quick)
	tst	#1,r0
	bt	.Lno2
	mov.w	.Lport2,r0
	mov.b	@(r0,r4),r0
	tst	#0x7C,r0
	bf	.Lslow0			! IRQ2's sources on
.Lno2:
	mov.l	@(C_LEFT,gbr),r0
	cmp/pl	r0
	bf	.Lc4
	mov.b	@(C_IDLE,gbr),r0
	tst	r0,r0
	bf	.Lc4
	mov.b	@(C_P,gbr),r0		! cycles left, no wait: only an interrupt waiting with I clear
	tst	#4,r0
	bf	.Lslow0
	mov.b	@(C_IRQ_WAIT,gbr),r0
	tst	r0,r0
	bf	.Lc4
	mov.w	.Lvst,r0
	mov.b	@(r0,r4),r0		! the VDC's status
	tst	#0x3F,r0
	bt	.Lslow0
.Lc4:
	mov.l	@(R_SCH_STALE,gbr),r0	! (tail_quick's)
	tst	r0,r0
	bf	.Lslow0
	mov.l	@(R_SCH_RPART,gbr),r0
	tst	r0,r0
	bf	.Lslow0
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov.l	r12,@-r15
	mov.l	r13,@-r15
	sts.l	pr,@-r15
	bsr	.Lt0			! t0 (r6), m (r7), frame_end_at (r1), &vdc.t + 64 (r3)
	nop
	bt	.Lslow1			! t0 >= frame_end_at
	! ---- vram_at, vdc_seen, obs_find ----
	mov	#T_NOW-64,r0
	mov.l	@(r0,r3),r9		! the VDC's done time
	mov.l	@(R_VRAM_T,gbr),r0
	mov	r0,r8			! r8 vt
	cmp/ge	r8,r9
	bt	.Lslow1			! (vram_find: rt.c)
	mov	#T_RAISED_TO-64,r0
	mov.l	@(r0,r3),r0
	cmp/gt	r9,r0
	bf	.Lseen
	mov	r0,r9			! r9 d = vdc_seen
.Lseen:
	mov.l	@(R_OBS_T,gbr),r0
	cmp/ge	r0,r9
	bf	.Lobsok
	mov.l	.Lobs,r0		! obs_find(r)
	jsr	@r0
	nop
	stc	gbr,r4
	bsr	.Lt0			! (its registers again)
	nop
.Lobsok:
	mov.l	@(R_OBS_T,gbr),r0
	mov	r0,r12			! r12 obs_t
	mov.l	@(R_IRQ_AT,gbr),r0
	mov	r0,r2			! r2 irq_at
	mov	r6,r10			! t = t0
	mov	#0,r11
	mov.b	@(C_IDLE,gbr),r0
	tst	r0,r0
	bt	.Lnoidle
	! the wait: obs_t after t0, the touch at t0, on to the interrupt
	cmp/ge	r12,r6
	bt	.Lslow1			! t0 >= obs_t
	mov	#1,r11
	mov	#T_NEXT-64,r0
	mov.l	@(r0,r3),r0
	cmp/ge	r0,r6
	bt	.Lnotouch		! t0 >= t_next: no touch
	mov	#3,r11			! touch0
	cmp/gt	r9,r6
	bf	.Lnotouch
	mov	r6,r9			! d = t0
.Lnotouch:
	cmp/gt	r10,r2
	bf	.Lnoidle
	mov	r2,r10			! t = irq_at
.Lnoidle:
	cmp/ge	r1,r10
	bt	.Lslow1			! t >= frame_end_at
	cmp/ge	r8,r10
	bt	.Lslow1			! t >= vt
	mov.l	@(R_SCH+S_END,gbr),r0
	cmp/gt	r0,r10
	bt	.Lslow1			! t > the list's end
	cmp/ge	r12,r10
	bf	.Lnoraise
	mov	#4,r0
	or	r0,r11			! raising
.Lnoraise:
	bra	.Llist
	nop

.Lslow1:
	lds.l	@r15+,pr
	mov.l	@r15+,r13
	mov.l	@r15+,r12
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	mov.l	@r15+,r8
.Lslow0:
	mov.l	.Lslowc,r0
	jmp	@r0
	stc	gbr,r4
	.align 1
.Lmask:	.short	R_HW_IRQ_MASK
.Lport2:	.short	R_HW_PORT2
.Lvst:	.short	R_VT + T_STATUS
	.align 2
.Lrunf:	.long	_rt_run_frames
.Lobs:	.long	_rt_obs_find
.Lslowc:	.long	_rt_leave_c

	! t0 = t_now (r6), m (r7: 3 fast, 12 slow), frame_end_at (r1), &vdc.t + 64 (r3); T: t0 >= frame_end_at
.Lt0:
	mov.w	.Lvt64t,r3
	add	r4,r3
	mov.b	@(C_SPEED,gbr),r0
	tst	r0,r0
	bt/s	.Lm12
	mov	#12,r7
	mov	#3,r7
.Lm12:
	mov.l	@(C_LEFT,gbr),r0
	mov	r0,r6
	mov.l	@(R_ENTRY_LEFT,gbr),r0
	sub	r6,r0
	mul.l	r7,r0
	mov.l	@(R_ENTRY_T,gbr),r0
	sts	macl,r6
	add	r0,r6
	mov.l	@(R_FRAME_END_AT,gbr),r0
	mov	r0,r1
	rts
	cmp/ge	r1,r6
	.align 1
.Lvt64t:	.short	R_VT + 64

.Llist:
	! the list: past d, then (raising) the raises up to t, each one's bit known
	mov.w	.Lsch,r3
	add	r4,r3			! r3 = &sch
	mov.w	.Lschrk,r5
	add	r4,r5			! r5 = &sch.rk
	mov.l	@(R_SCH_IR,gbr),r0
	mov	r0,r7			! r7 i
	mov.l	@(R_SCH+S_NR,gbr),r0
	mov	r0,r2			! r2 nr
	mov	#0,r13			! bits
.Ll1:
	cmp/ge	r2,r7
	bt	.Ll1e
	mov	r7,r0
	shll2	r0
	mov.l	@(r0,r3),r0
	cmp/gt	r9,r0
	bt	.Ll1e			! r[i] > d
	bra	.Ll1
	add	#1,r7
.Ll1e:
	mov	r11,r0
	tst	#4,r0
	bt	.Ll2e
.Ll2:
	cmp/ge	r2,r7
	bt	.Ll2e
	mov	r7,r0
	shll2	r0
	mov.l	@(r0,r3),r0
	cmp/gt	r10,r0
	bt	.Ll2e			! r[i] > t
	mov	r7,r0
	mov.b	@(r0,r5),r0
	tst	r0,r0
	bt	.Lslow1b		! a raise whose bit isn't known
	or	r0,r13
	bra	.Ll2
	add	#1,r7
.Ll2e:
	cmp/ge	r2,r7
	bt	.Lnolist
	mov	r7,r0
	shll2	r0
	bra	.Lnext
	mov.l	@(r0,r3),r12		! next = r[i]
.Lnolist:
	mov.l	@(R_SCH+S_END,gbr),r0
	cmp/ge	r1,r0
	bf	.Lslow1b		! the list not whole to the frame's end
	mov	r1,r12			! next = frame_end_at
.Lnext:
	! st = status | bits; irq1; take
	mov.w	.Lvst2,r0
	mov.b	@(r0,r4),r0
	or	r0,r13
	extu.b	r13,r13			! r13 st
	mov	r13,r0
	tst	#0x3F,r0
	bt	.Lnoirq1
	mov	#16,r0
	or	r0,r11			! irq1
	mov.b	@(C_P,gbr),r0
	tst	#4,r0
	bf	.Lnoirq1
	mov	#8,r0
	or	r0,r11			! take
.Lnoirq1:
	! pc (the vector if taken) and its bank's dirty flag
	mov	r11,r0
	tst	#8,r0
	bt/s	.Lpcold
	mov.w	@(C_PC,gbr),r0
	mov.l	@(C_RP7,gbr),r0
	tst	r0,r0
	bt	.Lslow1b		! (the vectors' slot not memory)
	mov	r0,r3
	mov.w	.Lvec,r0
	mov.b	@(r0,r3),r2
	add	#1,r0
	mov.b	@(r0,r3),r0
	extu.b	r2,r2
	shll8	r0
	or	r2,r0
.Lpcold:
	extu.w	r0,r0
	mov	r0,r3			! r3 pc
	shlr8	r0
	shlr2	r0
	shlr2	r0
	shlr	r0
	add	#C_MPR,r0
	mov.b	@(r0,r4),r0
	extu.b	r0,r2
	mov.w	.Lbd,r0
	add	r2,r0
	mov.b	@(r0,r4),r0
	tst	r0,r0
	bt	.Lcommit
.Lslow1b:
	bra	.Lslow1			! the bank written: rt.c matches it again
	nop
	.align 1
.Lsch:	.short	R_SCH
.Lschrk:	.short	R_SCH + S_RK
.Lvst2:	.short	R_VT + T_STATUS
.Lvec:	.short	0x1FF8
.Lbd:	.short	R_BANK_DIRTY

.Lcommit:
	! ---- the commit: nothing returns to C from here ----
	shll16	r13
	or	r3,r13			! r13 = st << 16 | pc
	mov	r11,r0
	tst	#1,r0
	bt	.Lcnoidle
	mov	r6,r0			! the wait's catch-up: now = t0, idles, the touch at t0
	mov.l	r0,@(R_NOW,gbr)
	mov.l	@(R_IDLES,gbr),r0
	add	#1,r0
	mov.l	r0,@(R_IDLES,gbr)
	mov	r11,r0
	tst	#2,r0
	bt	.Lnotouch0
	mov.w	.Lvt64c,r3
	add	r4,r3
	mov	#T_NOW-64,r0
	mov.l	@(r0,r3),r2
	cmp/gt	r2,r6
	bf	.Lnotouch0
	mov.l	r6,@(r0,r3)		! v32_touch(t0)
.Lnotouch0:
	mov.l	@(R_IRQS,gbr),r0
	mov	r0,r2
	mov.l	@(R_IRQ2S,gbr),r0
	add	r0,r2
	mov.l	@(R_LAST_IDLE_IRQS,gbr),r0
	cmp/eq	r0,r2
	bt	.Lcnoidle
	mov	r2,r0
	mov.l	r0,@(R_LAST_IDLE_IRQS,gbr)
	mov.l	@(R_ON_IDLE,gbr),r0
	tst	r0,r0
	bt	.Lcnoidle
	jsr	@r0			! on_idle(r)
	nop
	stc	gbr,r4
.Lcnoidle:
	mov	r10,r0
	mov.l	r0,@(R_NOW,gbr)	! now = t
	mov.w	.Lvt64c,r3
	add	r4,r3
	mov	r11,r0
	tst	#4,r0
	bt	.Lcnoraise
	mov	r13,r2
	shlr16	r2
	mov	#T_STATUS-64,r0
	mov.b	r2,@(r0,r3)		! status = st
	mov	#T_RAISED_TO-64,r0
	bra	.Lcraised
	mov.l	r10,@(r0,r3)		! raised_to = t
.Lcnoraise:
	mov	#T_NEXT-64,r0
	mov.l	@(r0,r3),r0
	cmp/ge	r0,r10
	bt	.Lcraised		! t >= t_next: no touch
	mov	#T_NOW-64,r0
	mov.l	@(r0,r3),r2
	cmp/gt	r2,r10
	bf	.Lcraised
	mov.l	r10,@(r0,r3)		! v32_touch(t)
.Lcraised:
	mov.l	@(R_FRAME_END_AT,gbr),r0
	mov	r0,r1			! r1 frame_end_at (again: on_idle)
	mov.l	@(R_IRQ_AT,gbr),r0
	cmp/gt	r10,r0
	bt	.Lirqat			! irq_at > t: kept
	mov	r12,r0
	cmp/gt	r1,r0
	bf	.Lirqmin
	mov	r1,r0
.Lirqmin:
	mov.l	r0,@(R_IRQ_AT,gbr)	! irq_at = min(next, frame_end_at)
.Lirqat:
	mov.b	@(C_SPEED,gbr),r0	! r7 = m (again)
	tst	r0,r0
	bt/s	.Lm12b
	mov	#12,r7
	mov	#3,r7
.Lm12b:
	mov	r11,r0
	tst	#8,r0
	bt	.Lnotake
	! take_irq_to: pc and P on the stack page, I set, T and D clear, the handler, 8 cycles
	mov.l	@(C_RAM,gbr),r0
	mov	r0,r2
	mov	#1,r0
	shll8	r0
	add	r0,r2			! r2 = the stack page (base RAM + 0x100)
	mov.b	@(C_S,gbr),r0
	extu.b	r0,r3			! r3 S
	mov.w	@(C_PC,gbr),r0
	mov	r0,r5			! r5 the old pc
	shlr8	r0
	mov	r0,r1
	mov	r3,r0
	mov.b	r1,@(r0,r2)		! [S] = pc >> 8
	add	#-1,r3
	extu.b	r3,r0
	mov.b	r5,@(r0,r2)		! [S - 1] = pc & 0xFF
	add	#-1,r3
	extu.b	r3,r3
	mov.b	@(C_P,gbr),r0
	and	#0xEF,r0		! P & ~B
	mov	r0,r1
	mov	r3,r0
	mov.b	r1,@(r0,r2)		! [S - 2] = P & ~B
	add	#-1,r3
	mov	r3,r0
	mov.b	r0,@(C_S,gbr)		! S -= 3
	mov.b	@(C_P,gbr),r0
	or	#4,r0
	and	#0xD7,r0		! (P | I) & ~(T | D)
	mov.b	r0,@(C_P,gbr)
	mov	r13,r0
	mov.w	r0,@(C_PC,gbr)		! pc = the handler
	mov.l	@(R_NOW,gbr),r0
	mov	r7,r2
	shll2	r2
	shll	r2
	add	r2,r0
	mov.l	r0,@(R_NOW,gbr)	! now += 8 m
	mov.l	@(R_IRQS,gbr),r0
	add	#1,r0
	mov.l	r0,@(R_IRQS,gbr)
	mov	#0,r0
	bra	.Lwaited
	mov.b	r0,@(C_IRQ_WAIT,gbr)
.Lnotake:
	mov	r11,r0
	shlr2	r0
	shlr2	r0
	and	#1,r0			! irq1
	mov.b	r0,@(C_IRQ_WAIT,gbr)
.Lwaited:
	! run_head: the budget to irq_at, the entry
	mov.l	@(R_IRQ_AT,gbr),r0
	mov	r0,r1
	mov.l	@(R_NOW,gbr),r0
	mov	r0,r10			! r10 now (the entry's time)
	bsr	.Lc2c
	sub	r0,r1			! r1 = irq_at - now
	cmp/pl	r1
	bt	.Lbud
	mov	#1,r1
.Lbud:
	mov	r1,r9			! r9 budget
	mov	r1,r0
	mov.l	r0,@(C_LEFT,gbr)
	mov.l	r0,@(R_ENTRY_LEFT,gbr)
	mov	r10,r0
	mov.l	r0,@(R_ENTRY_T,gbr)
	mov	#0,r0
	mov.b	r0,@(C_MISS,gbr)
	mov.b	r0,@(C_IDLE,gbr)
	! vp_update's: an interrupt up with I set waits; xok, xmin
	mov.b	@(C_P,gbr),r0
	tst	#4,r0
	bt	.Lnoi
	mov	r11,r0
	tst	#16,r0
	bt	.Lnoi
	mov	#1,r0
	mov.b	r0,@(C_IRQ_WAIT,gbr)
.Lnoi:
	mov.b	@(C_P,gbr),r0
	tst	#4,r0
	bf	.Lbase1			! I set: base_ok
	mov	r11,r0
	tst	#16,r0
	bf	.Lbase0			! irq1
	mov.b	@(C_IRQ_WAIT,gbr),r0
	tst	r0,r0
	bf	.Lbase0
.Lbase1:
	mov	#1,r0
	mov.b	r0,@(C_XOK,gbr)
	mov	#0,r0
	bra	.Lbased
	mov.l	r0,@(C_XMIN,gbr)
.Lbase0:
	mov	#0,r0
	mov.b	r0,@(C_XOK,gbr)
	mov.l	.Lmax,r0
	mov.l	r0,@(C_XMIN,gbr)
.Lbased:
	! obs_t = min(next, vt, frame_end_at)
	mov	r12,r2
	cmp/gt	r8,r2
	bf	.Lob1
	mov	r8,r2
.Lob1:
	mov.l	@(R_FRAME_END_AT,gbr),r0
	cmp/gt	r0,r2
	bf	.Lob2
	mov	r0,r2
.Lob2:
	mov	r2,r0
	mov.l	r0,@(R_OBS_T,gbr)
	mov	r2,r12			! r12 obs_t
	! st_min, io_min
	mov.l	.Lmax,r0
	mov.l	r0,@(R_ST_MIN,gbr)
	mov.l	r0,@(R_IO_MIN,gbr)
	mov.l	.Lanew,r1
	mov.l	@(R_IO_AT,gbr),r0
	cmp/eq	r1,r0
	bt	.Lnoio
	sub	r10,r0
	cmp/pl	r0
	bf	.Lnoio
	bsr	.Lc2c
	mov	r0,r1
	mov	r9,r0
	sub	r1,r0
	mov.l	r0,@(R_IO_MIN,gbr)	! io_min = budget - c2c(io_at - entry_t)
.Lnoio:
	! the VRAM port's thresholds
	mov	r8,r1
	sub	r10,r1
	cmp/pl	r1
	bt	.Lvp
	mov.l	.Lmax,r0
	mov.l	r0,@(C_LEFT_MIN,gbr)
	mov.l	.Lnever,r0
	bra	.Lvpd
	mov.l	r0,@(C_LEFT_MIN1,gbr)
.Lvp:
	bsr	.Lc2c
	nop
	mov	r9,r2
	sub	r1,r2			! left_min = budget - c2c(vt - entry_t)
	mov	r2,r0
	mov.l	r0,@(C_LEFT_MIN,gbr)
	mov.l	.Lnever,r1
	mov.b	@(C_MPR,gbr),r0
	cmp/eq	#-1,r0
	bf	.Lvpn
	mov	r2,r1
	add	#1,r1
.Lvpn:
	mov	r1,r0
	mov.l	r0,@(C_LEFT_MIN1,gbr)
.Lvpd:
	! st_find's: with I set, the status read in place before obs_t
	mov.b	@(C_P,gbr),r0
	tst	#4,r0
	bt	.Ldone
	mov	r12,r1
	sub	r10,r1
	cmp/pl	r1
	bf	.Ldone
	bsr	.Lc2c
	nop
	mov	r9,r0
	sub	r1,r0
	add	#1,r0
	mov.l	r0,@(R_ST_MIN,gbr)
.Ldone:
	lds.l	@r15+,pr
	mov.l	@r15+,r13
	mov.l	@r15+,r12
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	rts
	mov	#1,r0

	! clocks_to_cycles(r1, m = r7) -> r1: 0 for r1 <= 0; else ceil(min(r1, BUDGET_MAX * 12) / m) by a multiply.
	! r0, MACH, MACL taken
.Lc2c:
	cmp/pl	r1
	bt	.Lc2p
	rts
	mov	#0,r1
.Lc2p:
	mov.l	.Lbmax,r0
	cmp/gt	r0,r1
	bf	.Lc2a
	mov	r0,r1
.Lc2a:
	add	r7,r1
	add	#-1,r1
	mov.l	.Ldiv3,r0
	dmulu.l	r0,r1
	sts	mach,r1
	shlr	r1			! (x * 0xAAAAAAAB) >> 33: x / 3
	mov	r7,r0
	cmp/eq	#3,r0
	bt	.Lc2r
	shlr2	r1			! x / 12
.Lc2r:
	rts
	nop
	.align 1
.Lvt64c:	.short	R_VT + 64
	.align 2
.Lmax:	.long	0x7FFFFFFF
.Lnever:	.long	0x40000000
.Lanew:	.long	0x80000000
.Lbmax:	.long	0x7FFFFF * 12
.Ldiv3:	.long	0xAAAAAAAB
