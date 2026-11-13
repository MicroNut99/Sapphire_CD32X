/* sapm.h - docs/99: the PC Engine CD machine alone, as a library: the interpreter (tools/m2_machine.py's generic
 * runtime, Mednafen's semantics), the video chips and their timing, the I/O page and CD unit, the drive. The run is
 * trace_main.c's --run path (checked against Mednafen: the whole boot recording equal from power-on, docs/99) without
 * the trace; the power-on state is Mednafen's reset values, in code. For the ares model of the cart's engine.
 *
 * Times are master clocks (21.48 MHz) from power-on. One machine a process (the models' state is static). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {   /* (C linkage, so a C++ host can declare it inside its own namespace: ares's board.cpp) */
#endif

/* the disc's table (scsi_model.c scsi_start_tab, tools/m3_speedtest.py disc_table): first, last, lead-out, then for
   tracks 0-100: lba, sectors, pregap, control, data, sector size - 3 + 606 int32, host order */
enum { SAPM_TAB_WORDS = 3 + 6 * 101 };

/* a data sector, by its LBA (from the disc's start, as the TOC has it); 2048 bytes into out; 0 if not had */
typedef int (*SapmSector)(void *user, int32_t lba, uint8_t *out);

/* power-on: the System Card (256 KB), the backup RAM (2 KB; NULL: zeros), the disc's table, its sectors */
int sapm_power(const uint8_t *syscard, const uint8_t *bram, const int32_t *tab, SapmSector sector, void *user);
/* called at each frame's end (Mednafen's: after the instruction or block-move byte in which the VCE reaches it - so a
   move can be part-done), with the frame's number from 0; the next frame's pad is taken right after it returns */
void sapm_on_frame(void (*fn)(void *user, long frame), void *user);
void sapm_pad(uint16_t retro_mask);
/* the cart's own (docs/101): the pad in the engine's order (I, II, SELECT, RUN, up, right, down, left), at once - as
   map_dx's dx_pad feeds dx_io, not per frame */
void sapm_pad_now(uint8_t buttons);
/* the feed (docs/101): have(lba) says whether that sector can be read now; while the drive's next read could end within
   the coming instruction and its sector can't, the run stops before it (sapm_waiting) and the machine's time stands still */
typedef int (*SapmHave)(void *user, int32_t lba);
void sapm_feed(SapmHave have);
int sapm_waiting(void);
long sapm_missed(void);                      /* sector reads the reader couldn't give (zeros read instead) */
int32_t sapm_drive_next(void);               /* the sector the drive reads next (the register window's want) */
uint32_t sapm_audio(void);                   /* docs/103: CD-DA - bit 31 playing, 25-24 the play's mode, 20-0 its start */
const uint16_t *sapm_stamps(void);           /* each 64-word block of VRAM: the frame (frames ended) of its last write */
const uint16_t *sapm_lines(void);            /* docs/105: at the last frame's end, for raster counts 0-262: (X, Y) - the
                                               background's offsets each line was drawn with (BXR, and Y's count) */
const uint16_t *sapm_snapshot(void);         /* at the last frame's end: BXR, BYR, MWR, CR, that frame's number */          /* the pad from the next frame's start (libretro's mask, as io_set_buttons) */
long sapm_run_frames(long n);                /* run n more frames (to their ends); returns the frames run since power-on */
int64_t sapm_run_until(int64_t t);           /* run to the first instruction boundary at or after time t; returns it */
int64_t sapm_now(void);                      /* the time of the next instruction's start */
long sapm_frame(void);                       /* frames ended since power-on */
unsigned sapm_pc(void);                                  /* (docs/60: the CPU's PC now) */
uint32_t sapm_hash(int with_stack);          /* tools/m3rt rt_hash: RAM, VRAM, sprite table, colours */
void sapm_md5(uint8_t md[16]);               /* tools/m0_replay.py frame_hash's md5 (needs openssl: SAPM_MD5) */

/* the memories, for the windows (docs/96): base RAM (8 KB), CD RAM (64 KB), Super RAM (192 KB), backup RAM (2 KB),
   VRAM (64 K words), the sprite table (256), the colours (512); the VDC's registers */
uint8_t *sapm_card(void);                     /* Sapphire: the Arcade Card's 2 MB */
uint8_t *sapm_adpcm_ram(void);
struct Vdc; struct Hw;
struct Vdc *sapm_vdc(void); struct Hw *sapm_hw(void);   /* Sapphire docs/55: the video chips' and the I/O page's models */
uint8_t *sapm_base_ram(void); uint8_t *sapm_cd_ram(void); uint8_t *sapm_super_ram(void); uint8_t *sapm_bram(void);
uint16_t *sapm_vram(void); uint16_t *sapm_sat(void); uint16_t *sapm_colors(void);
void sapm_regs(uint16_t *bxr, uint16_t *byr, uint16_t *mwr, uint16_t *cr);
void sapm_state_text(void);                  /* the CD unit's and video chips' hidden state, as text (trace_main.c RUN_STATE) */
#ifdef __cplusplus
}
#endif
