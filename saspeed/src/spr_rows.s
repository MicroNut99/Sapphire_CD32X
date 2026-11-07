! spr_rows.s - M3, the picture: a sprite cell's rows into the 32X frame buffer, and the clearing (docs/56; written by
! tools/m3_sprrows.py - edit that, not this). tools/m3rt/rt_spr.c's draw_cell calls the rows the picture's edge does
! not cut; its C does the same on the PC.
!
!   void spr_rows<n><d>(const uint8_t *src, uint8_t *dst, const uint16_t *tab, uint32_t rows, int32_t step)
!   n: 8 (an even x) or 9 words a row (an odd x); d: f, the row's bytes forwards (an H flip), b backwards
!   r4 src: the first row drawn (n bytes a row); r5 dst: its word 0 in the overwrite area; r6 tab: the palette's
!   table, centred (a byte read sign-extended, doubled, indexes it); r7 rows: the rows to draw, bit 31 the first,
!   then one a line down; @r15 step: the source from row to row (-n for a V flip).
!
! Each word: the byte by displacement into r0, doubled, the table's word, stored with predecrement from the row's
! end - a byte k forwards lands at word n-1-k (the H flip), backwards at word k. Four instructions a word, the
! loads' results used an instruction later. A row whose bit is 0 (all pixel 0) is not drawn.

	.section .text.spr_rows,"ax"
	.align 4
	.global _spr_rows8f
_spr_rows8f:
	mov.l	@r15,r3		! r3 = step
	mov.l	r8,@-r15
	mov.w	.L320_8f,r8
1:	shll	r7		! T = this row's bit
	bf	2f
	add	#16,r5		! the row's end
	mov.b	@(0,r4),r0
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(1,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(2,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(3,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(4,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(5,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(6,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(7,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	nop
	mov.w	r2,@-r5		! (r5 back at word 0)
2:	add	r3,r4
	tst	r7,r7
	bf/s	1b
	add	r8,r5		! the next line
	rts
	mov.l	@r15+,r8
	.align 1
.L320_8f:	.short	320

	.align 4
	.global _spr_rows8b
_spr_rows8b:
	mov.l	@r15,r3		! r3 = step
	mov.l	r8,@-r15
	mov.w	.L320_8b,r8
1:	shll	r7		! T = this row's bit
	bf	2f
	add	#16,r5		! the row's end
	mov.b	@(7,r4),r0
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(6,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(5,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(4,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(3,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(2,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(1,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(0,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	nop
	mov.w	r2,@-r5		! (r5 back at word 0)
2:	add	r3,r4
	tst	r7,r7
	bf/s	1b
	add	r8,r5		! the next line
	rts
	mov.l	@r15+,r8
	.align 1
.L320_8b:	.short	320

	.align 4
	.global _spr_rows9f
_spr_rows9f:
	mov.l	@r15,r3		! r3 = step
	mov.l	r8,@-r15
	mov.w	.L320_9f,r8
1:	shll	r7		! T = this row's bit
	bf	2f
	add	#18,r5		! the row's end
	mov.b	@(0,r4),r0
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(1,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(2,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(3,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(4,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(5,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(6,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(7,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(8,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	nop
	mov.w	r2,@-r5		! (r5 back at word 0)
2:	add	r3,r4
	tst	r7,r7
	bf/s	1b
	add	r8,r5		! the next line
	rts
	mov.l	@r15+,r8
	.align 1
.L320_9f:	.short	320

	.align 4
	.global _spr_rows9b
_spr_rows9b:
	mov.l	@r15,r3		! r3 = step
	mov.l	r8,@-r15
	mov.w	.L320_9b,r8
1:	shll	r7		! T = this row's bit
	bf	2f
	add	#18,r5		! the row's end
	mov.b	@(8,r4),r0
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(7,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(6,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(5,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(4,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(3,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(2,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(1,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	mov.b	@(0,r4),r0
	mov.w	r2,@-r5
	add	r0,r0
	mov.w	@(r0,r6),r2
	nop
	mov.w	r2,@-r5		! (r5 back at word 0)
2:	add	r3,r4
	tst	r7,r7
	bf/s	1b
	add	r8,r5		! the next line
	rts
	mov.l	@r15+,r8
	.align 1
.L320_9b:	.short	320

! void spr_clear(uint8_t *row, uint32_t words, uint32_t rows): words (1-17) 16-bit 0s from row (the normal area),
! rows lines down - a sprite's box from the drawing before. A jump into 17 stores (braf), each row from its end.
	.align 4
	.global _spr_clear
_spr_clear:
	mov	r5,r1
	shll	r1		! r1 = the row's bytes
	mov	#17,r2
	sub	r5,r2
	shll	r2		! r2 = the stores skipped, in bytes
	mov.w	.L320_c,r3
	mov	#0,r0
1:	mov	r4,r7
	braf	r2
	add	r1,r7		! (delay slot) r7 = the row's end
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	mov.w	r0,@-r7
	dt	r6
	bf/s	1b
	add	r3,r4
	rts
	nop
	.align 1
.L320_c:	.short	320
