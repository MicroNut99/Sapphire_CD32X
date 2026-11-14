! chain.s - PLAY=1's stage switch (DraculaX docs/70): another stage's 32X program loaded into the cart and both SH2s
! restarted in it, as crt0's reset does after the boot ROM. main.c copies this blob to SDRAM 0x0603F440 (the 960 bytes
! above the master's stack, docs/67: SPLIT's cell list, unused while the menu is open) and runs it there past the
! cache: nothing in it is in the cart it overwrites, and it uses no stack and no globals.
!
! The master (dx_chain_blob, r4 = the new program's file number), interrupts off, the secondary parked in
! dx_chain_sec, no command under way:
!   1. CMD 38 opens the file; CMD 91 (CDRING) copies it 16 sectors at a time into the cart from 0 (the 68000's code at
!      0x800 is the same in every stage's program, so the 68000 runs on through the copy);
!   2. the new header's SDRAM part (0x3D4: source, destination, size) copied as crt0's pri_reset copies it;
!   3. COMM4 = "S_OK" holds the secondary in its new sec_start until the new pri_start clears it; the secondary let go;
!   4. VBR from the new header, then its pri_start.
! A failed command stops here with COMM12 left as the Sub-CPU set it.

        .text
        .align  2
        .global _dx_chain_blob, _dx_chain_sec, _dx_chain_end
        .equ    AT, 0x2603F440                  /* (where main.c copies the blob: SDRAM past the cache) */

_dx_chain_blob:
        mov.l   L_comm,r1                       /* r1 = COMM0 */
        mov     r4,r0
        mov.w   r0,@(2,r1)                      /* COMM2 = the file */
        mov     #38,r0
        mov.w   r0,@r1                          /* CMD 38: open */
1:      mov.w   @r1,r0
        tst     r0,r0
        bf      1b
        mov.l   @(8,r1),r6                      /* r6 = its length (COMM8) */
        mov     #0,r7                           /* r7 = the sector */
2:      mov     r7,r0
        mov.w   r0,@(2,r1)                      /* COMM2 = the first sector */
        mov     r7,r2
        shll8   r2
        shll2   r2
        shll    r2                              /* x 2048: the cart offset */
        mov.l   L_n16,r3
        or      r3,r2
        mov.l   r2,@(8,r1)                      /* COMM8 = offset | 15 << 24 (16 sectors) */
        mov     #-1,r0
        mov.w   r0,@(12,r1)                     /* COMM12 = 0xFFFF */
        mov     #91,r0
        mov.w   r0,@r1                          /* CMD 91 */
3:      mov.w   @r1,r0
        tst     r0,r0
        bf      3b
        mov.w   @(12,r1),r0
        tst     r0,r0
        bf      9f                              /* (refused: stop) */
        add     #16,r7
        mov     r7,r2
        shll8   r2
        shll2   r2
        shll    r2
        cmp/hs  r6,r2                           /* past the end? */
        bf      2b

        mov.l   L_hdr,r1                        /* the new header (0x3D4), past the cache */
        mov.l   @r1,r2                          /* source in the cart */
        mov.l   @(4,r1),r3                      /* destination in SDRAM */
        mov.l   @(8,r1),r4                      /* size */
        mov.l   L_cart,r0
        add     r0,r2
        mov.l   L_sdram,r0
        add     r0,r3
        shlr2   r4
4:      mov.l   @r2+,r0
        mov.l   r0,@r3
        dt      r4
        bf/s    4b
        add     #4,r3

        mov.l   L_ccr,r0
        mov     #0x10,r2                        /* the cache purged and off (pri_start turns it on) */
        mov.b   r2,@r0
        mov.l   L_comm4,r0
        mov.l   L_sok,r2
        mov.l   r2,@r0                          /* COMM4 = "S_OK": the secondary waits in sec_start */
        mov.l   L_go,r0
        mov     #1,r2
        mov.l   r2,@r0                          /* the secondary let go */
        mov.l   @(0x14,r1),r0
        ldc     r0,vbr                          /* the new primary VBR */
        mov.l   @(0x0C,r1),r0
        jmp     @r0                             /* the new pri_start */
        nop
9:      bra     9b
        nop

! the secondary, interrupts off: parked here until the master has loaded the program, then its sec_start
_dx_chain_sec:
        mov.l   L_parked,r1
        mov     #1,r0
        mov.l   r0,@r1                          /* parked */
        mov.l   L_go,r1
5:      mov.l   @r1,r0
        tst     r0,r0
        bt      5b
        mov.l   L_hdr,r1
        mov.l   @(0x18,r1),r0
        ldc     r0,vbr                          /* the new secondary VBR */
        mov.l   @(0x10,r1),r0
        jmp     @r0                             /* the new sec_start */
        nop

        .align  2
L_comm:   .long 0x20004020
L_comm4:  .long 0x20004024
L_n16:    .long 15 << 24
L_hdr:    .long 0x220003D4
L_cart:   .long 0x22000000
L_sdram:  .long 0x26000000
L_ccr:    .long 0xFFFFFE92
L_sok:    .ascii "S_OK"
L_go:     .long AT + (W_go - _dx_chain_blob)
L_parked: .long AT + (W_parked - _dx_chain_blob)
        .global _dx_chain_parked
W_go:     .long 0
W_parked: .long 0
_dx_chain_end:
        .equ    _dx_chain_parked, W_parked
