# 03 - D1: both carts, 4 MB and 14 MB (7 Nov 2026)

**The owner's decision (7 Nov 2026): build for both.** Every milestone from here produces both builds. One
recompiled game, one runtime; the two differ only in where the Arcade Card's data and each stage's program
sit, and in how they get there. This round lays the Arcade Card out for both and checks the layouts against
the running game (`tools/make_packs.py`).

## 1. What goes in the cart

The game has the Arcade Card's 2 MB in six states (docs/02 §4), taken from Mednafen at these moments:

| Image | Moment | What is in it |
|---|---|---|
| 0 | after the boot's load (`idle_100`) | the title, the intro, the briefings |
| 1-5 | each stage's start, 1,300 frames after its state (past its load) | that stage |

Cut into 64 KB regions:
- **113 distinct non-zero regions (7,232 KB)** over the six images, where 6 × 2 MB would be 12 MB.
- **17 regions (1,088 KB) are shared** by two or more images (the boot block's parts that stay).
- Each image's own regions: 12-18 (768-1,152 KB).
- The runtime reaches the Arcade Card through a table per image: 32 entries, region → cart offset, or "zero".
  The table costs one lookup per port access the runtime already makes (the port's 24-bit address, its top
  5 bits the region).

**Checked:** each of the six images rebuilt from each cart's files through its table is **equal to the image
taken from Mednafen** (12 of 12).

## 2. The two layouts

The stack's maps: BootCamp docs/04 (the 4 MB RAM cart), docs/11 and ledger §16 (the 16 MB EverDrive: window 0
= cart 0-2 MB, window 1 = cart 2-4 MB with pages 1-7 by command 68; banks 28-31 unusable, so pages 1-6).

### 14 MB: the EverDrive PRO (EDCD), `CART=ED`

| Cart | Window | What | Size |
|---|---|---|---|
| page 0, `0x000000-0x07FFFF` | 0 (fixed) | `GAME.32X`: the runtime, the 68000's code | ≤ 512 KB |
| page 0, `0x080000-0x18FFFF` | 0 | the 17 shared regions | 1,088 KB |
| page 0, `0x190000-0x1FFFFF` | 0 | free | 448 KB |
| page 1 | 1 | image 0 (title, intro): 18 regions | 1,152 KB |
| pages 2-6 | 1 | stage 1-5: 18, 17, 15, 12, 16 regions | 1,152 / 1,088 / 960 / 768 / 1,024 KB |
| each page's `0x3F0000-0x3FFFFF` | 1 | kept free: the save area | 64 KB |

- At power-on every page is copied into the cart once (commands 68 + 46); after that, a stage start is one
  command 68 and no CD read. The stage's program goes in its page beside its regions: **832-1,216 KB free in
  each page**, plus page 0's 448 KB.

### 4 MB: the RAM cart

| Cart | What | Size |
|---|---|---|
| `0x000000-0x07FFFF` | `GAME.32X` | ≤ 512 KB |
| `0x080000-0x18FFFF` | the 17 shared regions (the kit's resident slot 4) | 1,088 KB |
| `0x190000-0x1FFFFF` | free | 448 KB |
| `0x200000-0x3EFFFF` | the stage slot (the kit's level slot 16, 1,984 KB): `STGn.PAK`, image *n*'s own regions, loaded at its start | 768-1,152 KB |
| `0x3F0000-0x3FFFFF` | the save area | 64 KB |

- A stage start loads its `STGn.PAK` (and its program) from the CD32X disc, as the PC Engine loads its stage
  (DraculaX's play disc switches stages the same way: docs/70 §4). **832-1,216 KB free in the stage slot**
  for the stage's program, plus 448 KB resident.

## 3. What is the same, and what is open

- **The same in both:** the recompiled game, the runtime, the Arcade Card's table format, the regions' bytes.
  The 14 MB build is the 4 MB build with the loads replaced by a page switch.
- **Open (measure, not estimate):**
  1. **The stage's program size: measured (docs/08 §4), too big as it stands.** Stage 3's whole translation is
     1.61 MB of SH2 code at `-Os`, beside ~0.6 MB of state and 0.6-1.0 MB of the card: 3.3-3.7 MB on the 4 MB cart,
     and more than a 14 MB page's 0.8-1.2 MB of room. The answer is DraculaX's (hot code dense in SDRAM, the rest
     smaller in the cart), to be measured in M3 step 2.
  2. **The load times:** the 14 MB build's power-on copy (~7.2 MB) and the 4 MB build's per-stage load
     (0.8-1.2 MB plus the program), with the stack's `STREAM` / `CARTW` switches (DraculaX docs/64: 60 s → 12 s
     for its cart load). In ares, then on the console.
  3. **The save area** sits in window 1 on the EverDrive: which page's `0x3F0000` command 61 writes depends on
     the page shown. Kept free in every page for now; to settle with the ledger's §5 before M5.
  4. Image 0 (title / intro) plays from the boot's own Arcade Card; the cutscene data off the Arcade Card
     (LBA 8244-11003, docs/02 §3) is not placed yet.

## How to make it again

```
python3 tools/make_packs.py $W          # needs setup_states.py's idle_100 / 140 / 240 / 365 / 460
```
`$W/packs/`: `ac<n>.bin`, `cd<n>.bin`, `sup<n>.bin` (the images and the RAM at the same moments),
`cart14/PAGE0-6.BIN`, `cart4/RESIDENT.BIN` + `STG0-5.PAK`, each cart's `actab.bin` (per image 32 big-endian
words), `layout.txt`. Game data: the work folder only.
