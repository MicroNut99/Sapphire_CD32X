# 56 - The PSG ring fetched all at once (12 Nov 2026)

The owner, of docs/55's open fault and its fix: "Yes Do it. If it breaks then fix it."

**Result (ares only):** the effects no longer drop out. Over the whole run the PCM chip follows Mednafen's PSG, and
the ring's reader never falls further behind than 447 writes.

## What was wrong (docs/55 §5)

The Sub-CPU read the ring's new writes two at a time, each pair a round trip through the 68000. With the 68000 busy
on a picture's list those round trips were slow; from game frame 11,500 the reader fell more than the ring's 1,024
writes behind, writes were lost, and the chip was silent for seconds.

## The fix needed no change to the listener

The 68000's routine 25, `copy_words`, copies from any address to any address, and it is already there (commands 50
and 51 use it). **TPSG-2** (`stack/subcpu/main.c`, in the `DX-TPSG` block):
1. the count: one long through the 68000, as before;
2. if there are new writes: one `copy_words` from the cart's ring into Word RAM (the 68000's 1M bank, + 0x1E000) -
   two when they pass the ring's end;
3. the Sub-CPU swaps the banks, reads the writes, swaps back, and gives the chip what changed.

Two round trips a pass, however many writes wait. The Sub-CPU's own bank is not written.

## Measured in ares

The live build from the packed launcher, the machine as the cart's stand-in, the `playL` recording from power-on:
30,000 ares frames = 15,464 game frames (the title, into stage 1, two minutes of it). The PCM chip alone against
Mednafen's PSG, in the game's time:

| Windows | Loudness correlation | Windows with sound: chip / PSG | Spectra (median) |
|---|---|---|---|
| 10 frames | **0.937** | 568 / 584 | - |
| 30 frames | **0.951** | 213 / 217 | 0.88 |
| 120 frames | **0.961** | 60 / 61 | 0.92 |

- docs/55's figure for the same run before the fix: 0.82 at 30 frames, 175 / 207.
- Game frames 11,380-11,800, silent before: the chip plays through all of them.
- The reader: 279,916 reads of the count, 199,020 writes, never over the ring.
- The pictures looked at 14,000, 24,000 and 29,500 frames are whole.

## Where it is

- BootCamp main `a5f9743`; ledger section 36 (docs main `8bbae28`) with these figures. With `TPSG` off nothing
  differs.
- Sapphire's build tree takes the stack's files from a BootCamp checkout (`tools/m3_live_pack.sh`, `TPSG_BC=`): now
  `hw_md.s` and `build.sh` too, since another port's PADVB-1 changed them the same night.

## Still open

- **Nothing on a console.** New there with this change: the 1M bank swap twice a pass, and how long a round trip
  takes with the 68000 on a list (two are needed where seventeen were).
- **The game runs at about half its speed in these ares runs** (0.52 game frames a sixtieth): the stand-in makes a
  frame only when the SH2 asks, and the SH2 does not keep up from power-on. The sound follows the game's time, so it
  is slow with it. docs/55's `ARES_HUC_PACE=1` (the game at its own rate) broke the pictures: the next thing to look at.
- A burst of writes still reaches the chip at once: a frame's worth, or several when a pass is slow.
