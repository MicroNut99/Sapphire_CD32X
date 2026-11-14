OUTPUT_ARCH(sh)
EXTERN (_start)
ENTRY (_start)
__DYNAMIC  =  0;

/* dxspeed.ld - the 32X toolchain's mars.ld with the translated blocks' section (.blk) added, and the runtime's
 * code moved into SDRAM with .data (the 32X boot ROM copies it there).
 *
 * The memory map look like this:
 * +--------------------+ <- 0x02000000
 * | .text              |
 * |                    |
 * |         __text_end |
 * +--------------------+
 * .                    .
 * .                    .
 * .                    .
 * +--------------------+ <- 0x06000000
 * | .data              | initialized data goes here
 * |                    |
 * |         __data_end |
 * +--------------------+
 * | .bss               |
 * |        __bss_start | start of bss, cleared by crt0
 * |                    |
 * |         __bss__end | start of heap, used by sbrk()
 * +--------------------+
 * .                    .
 * .                    .
 * .                    .
 * |            __stack | top of stack (for Master SH2)
 * +--------------------+ <- 0x0603FC00
 */

MEMORY
{
    rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00080000
    blk (rx) : ORIGIN = 0x02200000, LENGTH = 0x001F0000
    ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003FC00
}

/*
 * Allocate the stack to be at the top of memory, since the stack
 * grows down
 */

PROVIDE (__stack = 0x0603FC00);
PAD_RAM = 0x220;                                /* (docs/35: from tools/cachesim's scan) */
PAD_RT = 0xE4;                                  /* (docs/37: 0xC4 before; the Rt 1,216 bytes smaller) */

SECTIONS
{
  /* dxspeed: the translated blocks (blk_*.o, blocks.o: code and tables) live in the cart's level slot, where
     LEVEL00.BIN is loaded (file 100: cart 0x200000); the ROM is the rest (docs/17) */
  .blk 0x02200000 :
  AT ( 0x00200000 )
  {
    __blk_start = .;
    *blk_*.o(.text .text.* .rodata .rodata.* .data .data.*)
    *hot_*.o(.rodata .rodata.*)                 /* the hot halves' label tables (32 KB each): their code is in SDRAM */
    *blocks.o(.text .text.* .rodata .rodata.* .data .data.*)
    . = ALIGN(16);
    __blk_end = .;
  } > blk

  .text 0x02000000 :
  AT ( 0x00000000 )
  {
    __text_start = .;
    *(EXCLUDE_FILE (*rt.o *rt_leave.o *rt_vdc.o *io_model.o *scsi_model.o *huc_rt.o *libgcc*.a:* *hot_*.o) .text
      EXCLUDE_FILE (*rt.o *rt_leave.o *rt_vdc.o *io_model.o *scsi_model.o *huc_rt.o *libgcc*.a:* *hot_*.o) .text.*)
    *(.gnu.linkonce.t.*)
    /* the runtime's code no stage's window runs (tools/cachesim fcount, docs/40): in the cart, its SDRAM for data */
    *scsi_model.o(.text.scsi_bus)
    *libgcc.a:_divdi3.o(.text)
    *libgcc.a:_sdivsi3.o(.text)
    *rt_vdc.o(.text.step_off)
    *io_model.o(.text.io_irq2_at)
    *rt_vdc.o(.text.next_of)
    *rt.o(.text.rt_init)
    *rt.o(.text.unlikely.rt_speed)                /* (CSL / CSH: the System Card's backup RAM calls, docs/50) */
    *scsi_model.o(.text.status_message.constprop.0)
    *rt_vdc.o(.text.v32_vce_read)
    *scsi_model.o(.text.make_subq)
    *rt_vdc.o(.text.v32_rebase)
    *rt_vdc.o(.text.v32_next_irq)
    *rt_vdc.o(.text.v32_next_frame_end)
    *io_model.o(.text.io_cd_irq)
    *scsi_model.o(.text.scsi_start)
    *scsi_model.o(.text.scsi_start_tab)          /* (docs/69: the drive from the state's table, once) */
    *scsi_model.o(.text.scsi_state)
    *scsi_model.o(.text.data_in)
    *rt.o(.text.rec_set)
    *io_model.o(.text.io_start)
    *scsi_model.o(.text.cc_error)
    *scsi_model.o(.text.unbcd)
    *rt_vdc.o(.text.schedule_run)
    *scsi_model.o(.text.bcd)
    *rt.o(.text.match_)
    *rt.o(.text.pool_take)                      /* (docs/72: the pool, run at a bank take only) */
    *rt.o(.text.remap_slots)
    *rt.o(.text.unlikely.pool_take)             /* (docs/77: their names as gcc gives them - cold) */
    *rt.o(.text.unlikely.remap_slots)
    *rt.o(.text.rt_pool_init)
    *rt.o(.text.rt_cdstep)                      /* (docs/75, docs/76: once a run of a System Card loop's iterations) */
    *rt.o(.text.rt_adstep)
    *rt.o(.text.rt_pollskip)
    *scsi_model.o(.text.scsi_bytes)
    *scsi_model.o(.text.set_sig)
    *scsi_model.o(.text.change_phase)
    *io_model.o(.text.note)
    *scsi_model.o(.text.run_read)
    *libgcc.a:_udiv_qrnnd_16.o(.text)
    *rt_vdc.o(.text.start_dma.isra.0)
    *libgcc.a:_udivsi3.o(.text)
    *rt.o(.text.fnv16)
    *libgcc.a:_udivdi3.o(.text)
    *libgcc.a:_clz.o(.rodata)
    *rt.o(.text.rebase)
    *rt_vdc.o(.text.record)

    . = ALIGN(16);
    __INIT_SECTION__ = .;
    KEEP (*(.init))
    SHORT (0x000B)	/* rts */
    SHORT (0x0009)	/* nop */
    . = ALIGN(16);
    __FINI_SECTION__ = .;
    KEEP (*(.fini))
    SHORT (0x000B)	/* rts */
    SHORT (0x0009)	/* nop */

    *(.eh_frame_hdr)
    KEEP (*(.eh_frame))
    *(.gcc_except_table)
    *(.gcc_except_table.*)
    KEEP (*(.jcr))

    . = ALIGN(16);
     __CTOR_LIST__ = .;
    ___CTOR_LIST__ = .;
    LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    LONG(0)
    __CTOR_END__ = .;

    . = ALIGN(16);
    __DTOR_LIST__ = .;
    ___DTOR_LIST__ = .;
    LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
     LONG(0)
    __DTOR_END__ = .;

    *(.rdata)
    *(EXCLUDE_FILE (*rt.o *rt_leave.o *rt_vdc.o *io_model.o *scsi_model.o *huc_rt.o *libgcc*.a:* *hot_*.o) .rodata
      EXCLUDE_FILE (*rt.o *rt_leave.o *rt_vdc.o *io_model.o *scsi_model.o *huc_rt.o *libgcc*.a:* *hot_*.o) .rodata.*)
    *(.gnu.linkonce.r.*)
    . = ALIGN(16);
    __text_end = .;
  } > rom
  __text_size = __text_end - __text_start;

  .data 0x06000000 :
  AT ( LOADADDR (.text) + SIZEOF (.text) )
  {
    __data_start = .;
    *(.data)
    /* the runtime's code and its constants in SDRAM (docs/17): a cache line from the cart costs 66 clocks, from
       SDRAM 11 (the BootCamp's ares, console timing); the SH2's 4 KB cache can't hold the runtime and the blocks */
    . = ALIGN(16);
    . = ALIGN(. - 0x2B0, 0x400) + 0x2B0;        /* (the runtime at 0x2B0 mod 1 KB, where its order was found, whatever
                                                   is before it - the entries' table in the cart or not: docs/38) */
    __sdcode_start = .;
    /* the runtime's functions in a measured order (docs/35), each on a cache line */
    . = ALIGN(16);
    *scsi_model.o(.rodata)
    . = ALIGN(16);
    *rt_vdc.o(.text.line_times)
    . = ALIGN(16);
    *libgcc.a:_lshiftrt.o(.text)
    . = ALIGN(16);
    *rt.o(.text.chips_to)
    . = ALIGN(16);
    *rt.o(.text.timing_write)
    . = ALIGN(16);
    *rt.o(.text.steps_meet)
    . = ALIGN(16);
    *rt.o(.text.vram_find)
    . = ALIGN(16);
    *rt.o(.text.obs_find)
    . = ALIGN(16);
    *rt.o(.text.raises_seen)
    . = ALIGN(16);
    *rt.o(.text.run_head)
    . = ALIGN(16);
    *rt.o(.text.vp_update)
    . = ALIGN(16);
    *rt.o(.text.vram_port)
    . = ALIGN(16);
    *rt.o(.text.set_mpr)
    . = ALIGN(16);
    *rt.o(.text.unlikely.rd_slow)
    . = ALIGN(16);
    *rt.o(.text.run_tail)
    . = ALIGN(16);
    *rt.o(.text.wr_phys)
    . = ALIGN(16);
    *rt.o(.text.rt_xcheck)
    . = ALIGN(16);
    *rt.o(.text.wr_phys2)
    . = ALIGN(16);
    *rt.o(.text.rt_run)
    . = ALIGN(16);
    *rt.o(.text.rt_hash)
    . = ALIGN(16);
    *rt.o(.rodata)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_run_events)
    . = ALIGN(16);
    *huc_rt.o(.text.unlikely)
    . = ALIGN(16);
    *rt.o(.text.sched_ins)
    . = ALIGN(16);
    *rt_vdc.o(.text.settle)
    . = ALIGN(16);
    *rt.o(.text.RD_)
    . = ALIGN(16);
    *rt.o(.text.irq2_up_now)
    . = ALIGN(16);
    *rt.o(.text.rt_leave_c)
    . = ALIGN(16);
    *rt.o(.text.unlikely.wr_slow)
    . = ALIGN(16);
    *rt_vdc.o(.text.steps_at.constprop.0)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_write)
    . = ALIGN(16);
    *rt_vdc.o(.text.sched_add)
    . = ALIGN(16);
    *rt_vdc.o(.text.raise)
    . = ALIGN(16);
    *rt_vdc.o(.text.next_vphase)
    . = ALIGN(16);
    *rt_vdc.o(.text.want_sat_test)
    . = ALIGN(16);
    *rt_leave.o(.text.rt_leave)
    . = ALIGN(16);
    *rt_vdc.o(.text.rare)
    . = ALIGN(16);
    *rt_vdc.o(.text.run_c)
    . = ALIGN(16);
    *io_model.o(.rodata.str1.4)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_schedule_)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_load)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_vce_write)
    . = ALIGN(16);
    *rt_vdc.o(.rodata)
    . = ALIGN(16);
    *io_model.o(.text.adpcm_irq)
    . = ALIGN(16);
    *io_model.o(.text.io_stuff_subchannel)
    . = ALIGN(16);
    *io_model.o(.text.io_stuff_subchannels)
    . = ALIGN(16);
    *io_model.o(.text.io_next_event)
    . = ALIGN(16);
    *io_model.o(.text.io_read)
    . = ALIGN(16);
    *io_model.o(.text.update_irq)
    . = ALIGN(16);
    *io_model.o(.text.io_set_buttons)
    . = ALIGN(16);
    *huc_rt.o(.text)
    . = ALIGN(16);
    *rt_vdc.o(.text.run)
    . = ALIGN(16);
    *scsi_model.o(.text.scsi_next)
    . = ALIGN(16);
    *scsi_model.o(.rodata.str1.4)
    . = ALIGN(16);
    *scsi_model.o(.text.scsi_quiet)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_read)
    . = ALIGN(16);
    *io_model.o(.text.io_write)
    . = ALIGN(16);
    *rt.o(.text.rcr_changed.part.0.constprop.0)
    . = ALIGN(16);
    *libgcc.a:_ashiftrt.o(.text)
    . = ALIGN(16);
    *libgcc.a:_ashiftrt_n.o(.text)
    . = ALIGN(16);
    *io_model.o(.rodata)
    . = ALIGN(16);
    *rt_vdc.o(.text.v32_schedule)
    . = ALIGN(16);
    *libgcc.a:_ashiftlt.o(.text)
    . = ALIGN(16);
    *rt.o(.text.next_raise)
    . = ALIGN(16);
    *rt.o(.text.io_set)
    . = ALIGN(16);
    *io_model.o(.text.io_run)
    . = ALIGN(16);
    *scsi_model.o(.text.run_cdda)
    . = ALIGN(16);
    *rt.o(.text.budget_cut)
    . = ALIGN(16);
    *rt.o(.text.rt_obs_find)
    . = ALIGN(16);
    *scsi_model.o(.text.scsi_run)
    . = ALIGN(16);
    *rt.o(.text.hw_write)
    . = ALIGN(16);
    *main.o(.sdtext)
    . = ALIGN(16);
    *rt.o(.text.take_irq)
    . = ALIGN(16);
    *rt_vdc.o(.text.copy_l)
    . = ALIGN(16);
    *rt.o(.text .text.* .rodata .rodata.*)
    *rt_vdc.o(.text .text.* .rodata .rodata.*)
    *io_model.o(.text .text.* .rodata .rodata.*)
    *scsi_model.o(.text .text.* .rodata .rodata.*)
    *huc_rt.o(.text .text.* .rodata .rodata.*)
    *libgcc*.a:*(.text .text.* .rodata .rodata.*)
    . = ALIGN(16);
    *(.sdtext)                                  /* (the speed test's frame hook: src/main.c SDTEXT, docs/38) */
    . = ALIGN(16);
    *(.sdtext.*)                                /* (rt_bg2.c's HUD check, every picture on the secondary: docs/84) */
    /* SDCODE: (the Makefile's SDCODE= objects here: docs/67) */
    __sdcode_end = .;
    /* the blocks' hot halves (tools/m3_speedtest.py split, docs/18): the code a stage's recordings ran */
    . = ALIGN(16);
    . = ALIGN(1024);                            /* (the cache's 64 sets x 16 bytes: the hot code's sets don't
                                                   move when the runtime's size does, docs/20) */
    __hot_start = .;
    *hot_*.o(.text .text.*)
    __hot_end = .;
    *(.data.*)
    *(.gnu.linkonce.d.*)
    CONSTRUCTORS

    *(.lit8)
    *(.lit4)
    *(.sdata)
    *(.sdata.*)
    *(.gnu.linkonce.s.*)
    . = ALIGN(16);
    __data_end = .;
  } > ram
  __data_size = __data_end - __data_start;

  .bss :
  {
    __bss_start = .;
    /* the base RAM and the Rt where the SH2 cache's simulation over the stages' traces finds the fewest line fills
       (docs/35): their addresses mod 1 KB the same on every stage, whatever its hot code's size */
    . = ALIGN(1024);
    . = . + PAD_RAM;                            /* (the base RAM at PAD_RAM mod 1 KB) */
    *(.bss.place_ram)
    . = . + PAD_RT;                             /* (8 KB on: the Rt at PAD_RAM + PAD_RT mod 1 KB) */
    *(.bss.place_rt)
    *(.bss)
    *(.bss.*)
    *(.gnu.linkonce.b.*)
    *(.sbss)
    *(.sbss.*)
    *(.gnu.linkonce.sb.*)
    *(.scommon)
    *(COMMON)
    . = ALIGN(16);
    end = .;
    _end = end;
    __end = _end;
    __bss_end = .;
  } > ram
  __bss_size = __bss_end - __bss_start;
  ASSERT(__bss_end <= 0x0603E800, "the bss reaches the master SH2's stack (its top 0x0603F440: 3 KB kept, docs/38)")

}
