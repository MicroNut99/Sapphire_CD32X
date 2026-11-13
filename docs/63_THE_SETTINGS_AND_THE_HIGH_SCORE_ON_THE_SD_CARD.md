# 63 - The settings and the high score on the SD card (13 Nov 2026)

The owner, 13 Nov, asked whether the game saves to the cart, then (to docs/62's finding that Sapphire itself never saves):
keep **both** the options menu's settings and the high score; and "Please change: Best Score to High Score".

## 1. What there is to save

- **The game saves nothing** (docs/62 section 5): no read or write of its backup RAM in the whole recorded game, no
  `bm_read` / `bm_write` call in its code.
- **It shows no high score either**: the title has none (only "©1995 HUDSON SOFT"), the play screen shows `1P` and the
  current score. So "the high score" is the port's own: the best score seen, kept on the SD card and shown in the
  options menu as **HIGH SCORE**.
- **The score is found** (the PC machine's RAM every 250 frames, the whole recording): CD RAM $A070 / $A072 / $A074
  (poke addresses $C070 / $C072 / $C074) hold the score ÷ 10, low byte first - 240 at frame 9,000 with 2,400 on
  screen, 496 at 9,750 with 4,960 shown at 9,779; it never drops in 75,000 frames. $C071 / $C073 / $C075 stay 0 (most
  likely player 2's: the demo plays two players).

## 2. The record

One record, the BootCamp `sdcard/` kit's form so a blank or garbage area reads as no save: `SPS1`, the length (12), the
FNV-1a sum, its complement, then version 1, lives, lives set by the menu, infinite lives, autofire, music volume, effects
volume, a spare byte, the high score ÷ 10 (32 bits, big-endian). The magic is written last and the whole record read
back (`sv_write`). Where: the EverDrive's battery RAM (bank 31) + 0x1000, after the PC Engine's backup RAM at 0 (docs/62
section 5) - the 256 KB the EverDrive's OS keeps on the SD card as `MEGA/gamedata/<launcher>/bram.srm`.

**When:** loaded once after the start (music and effects volumes sent at once if not 8); written when the menu closes
and within 10 s of a new high score. **What counts as a score:** only while someone holds the pad (any button or
direction in the last 10 s) and not in the 2 s after a RUN - the title's attract demo plays two players with scores and
no one at the pad (in ares it saved 19,850 before this rule).

**Not restored at a start:** LIVES reaches the game, as before, only when the menu sets it or a STAGE SELECT game starts
(`opt_lives_on` is kept, so the next of those uses it).

## 3. The ports (the stand-in in ares; the cart core's are still to write)

The program's `CS_WR` / `CS_RD` registers of the cart window (docs/58's poke port is 0xcf-0xd1):

| Register | |
|---|---|
| 0xd4 / 0xd5 | the save area's address, low byte / high 3 bits (0-2047) |
| 0xd6 (write) | a byte at the address, which then steps |
| 0xd7 (read) | the byte at the address, which then steps |
| 0xd8 (read) | **the peek**: the game's RAM byte at the poke address (0xcf / 0xd0) |

ares: `fpga/ares/sapphire-on-ares-v1.9-a2e14e8.patch` (local `a39c246`): the board writes bank 31 of the EverDrive's 16 MB
(an even byte the word's high half), which ares keeps in the cart's RAM file and does not wipe at a launcher's start;
`sapmhost.c` `huchost_peek`. **The cart core has none of it yet**: on Gate of Thunder's core (docs/62) the save port goes
on the battery RAM chip, and the peek and poke must reach CD RAM - GoT's poke port reaches base RAM only, and Sapphire's
LIVES ($C00C) and score ($C070) are CD RAM, behind the core's cache (a poke must also update or drop the cached line).

## 4. Tried in ares (console timing, `ARES_32X_TRAP=1`; `work/save.sh`, `work/warp.sh`)

| Run | Result |
|---|---|
| r1: the recorded game from frame 9,000, the menu at 2,000: MUSIC VOLUME 8 → 6 | the menu shows HIGH SCORE = the game's score then (6,680); bank 31 + 0x1000 holds a good record: music 6, high score 7,450 (the 10 s rule after the menu) |
| r2 / r3: a second launch from r1's cart RAM (bank 31 kept, the rest wiped as the EverDrive's menu does) | the menu early in play, the game's score 4,930: MUSIC VOLUME 6 and HIGH SCORE 00007450 from the card |
| idle2: power-on, one RUN at the System Card, nothing more (before the pad rule) | the attract demo's scores saved: 19,850 - the reason for the rule |
| idle3: the same with the pad rule | the demo played (18,640 and 6,010 on screen); no record written |
| r4: recorded play with B held on the pad, the menu closed | a record with 6,710 |

The only trap lines: section 2 of docs/60's two (the 68000's FM write at the start; the backup RAM's note).

**The launcher's header:** it had no save RAM entry, so the EverDrive's OS would not have kept `bram.srm` at all.
`tools/m3_live_pack.sh` now builds it `EDCD_SAVE=1`: 0x1B0 = `RA F8 20 00200001 0023FFFF`, the entry the BootCamp
measured on the console (its manual §1.1, 25 Oct 2026). Rebuilt; r5 (r2 again on it): boots, settings and HIGH SCORE
restored.

**Not on a console.** Open: the core's ports (section 3); the menu's font draws 0 like 8 (as Gate of Thunder's STATUS
page found).
