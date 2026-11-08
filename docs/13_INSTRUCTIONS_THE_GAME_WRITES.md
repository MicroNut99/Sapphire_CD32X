# 13 - The instructions the game writes (7 Nov 2026)

docs/12's 14 MB build, profiled in ares (stage 3, `--profile` from frame 4000, docs/11 §1's method):

| Part (master SH2) | docs/11 (old hot set) | docs/12 build |
|---|---|---|
| the runtime | 20.5 % | 28.2 % |
| the game's hot code, SH2 assembly in SDRAM | 15.2 % | 25.7 % |
| the VDC / VRAM model (`vram_port` 10.9 %, `block_move` 6.5 %) | 13.4 % | 22.2 % |
| the game's cold code, in the cart | 43.5 % | 10.8 % |
| the interpreter (`interp_one`, `RD_`, `WR_`) | 4.9 % | 8.9 % |
| the Arcade Card's reads and the pokes | 2.5 % | 4.3 % |

## 1. What was interpreted

rt_pc `RUN_INTERP=1` (new: each interpreted instruction's place and the opcode found there) on every stage's 600
frames. All but 1 of 11,500-14,700 interpreted instructions a stage are in three places in bank 69:

| Place | The code map's instruction | What the game writes there when it runs it | Interpreted, stage 3 |
|---|---|---|---|
| `69:1235` | `JMP abs` (4C) | `CPY abs` (CC) | 3,000 |
| `69:12E4` | `JMP abs` | `LDX abs,Y` (BE) | 3,000 |
| `69:1345` | `JMP abs` | `LDX abs,Y` (BE) | 7,944 |

The game rewrites all three bytes of each, opcode included, every frame (rt_pc `RUN_ROHIST`: 600 writes to each
byte). The translation guards such a place: it runs while the opcode is the one the code map saw, and otherwise
leaves to the interpreter. The interpreter runs from the cart, so each of these cost about 2,000 SH2 clocks.

## 2. The change

- **`m2_recomp.py` `PATCH_ALTS`:** for a place whose opcode the game writes, the other opcodes seen there. The
  translation reads the opcode and jumps to a translation of each one (operands read when it runs, as for any
  written operand). An opcode not in the list still goes to the interpreter. The list is the game's, so it lives in
  `<work>/m2/patch_alts.json` (`{"69:1235": [204], "69:12E4": [190], "69:1345": [190]}`).
  `m3_speedtest.py split` loads it too.
- **`m3_sh2.py`:** the hot assembly's guard leaves to the cold C copy (`miss = 3`) instead of the interpreter.
  `split` keeps these places in the cold half as well, so the C is there.

| Check (rt_pc, the new translation) | Stage 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| hash at frame 599 = before | yes | yes | yes | yes | yes |
| interpreted (was 11,500-14,700) | 1 | 0 | 0 | 1 | 0 |

## 3. Speed (ares, console timing, 600 frames each; every hash = rt_pc's)

| Stage | 14 MB + core: docs/12 → now | interpreted | 4 MB plain EverDrive: docs/12 → now | interpreted |
|---|---|---|---|---|
| 1 | 973 → **960** | 212 | 1,637 → **1,626** | 3 |
| 2 | 707 → **691** | 345 | 1,302 → **1,279** | 0 |
| 3 | 1,062 → **1,055** | 254 | 1,846 → **1,829** | 0 |
| 4 | 547 → **532** | 249 | 989 → **985** | 0 |
| 5 | 673 → **665** | 133 | 1,162 → **1,133** | 0 |

The gain is 1-3 %, not the profile's 8.9 %. Two reasons:
- `RD_` and `WR_`, counted under the interpreter, also serve the cold C code.
- An alternative reached from the hot code still costs two trips through the dispatcher: the hot code leaves to
  the C, and the C leaves to come back.

**Next for this:** the alternatives in the hot assembly itself (`m3_sh2.py`), to save both trips.

## 4. Next

- The VDC model's data port (`vram_port`, `block_move`): 17 % between them.
- The runtime's slow memory paths (`rd_slow` 6.8 %, `wr_slow` 6.5 %: the card's ports, the pokes, the hardware page).
- The console runs of `Sapphire\console_test` (docs/11 §5).
