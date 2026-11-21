# 92. v27: the red mixed in (20 Nov 2026)

**State: untested on the console.** The owner's v26 videos (233917133 stage 1, 234017761 its end, 234103293 stage 2,
234203186 its end, 234330467 stage 3; two players) and "Analyse for lost tiles, frames and glitching".

## Every camera frame (scan.py: events stage 1 31, stage 2 2 + 0, stage 3 7; v24's 983 / 985: 11 / 31)
- **No torn picture, no status bar garbage** in 5 minutes of play (v24: one every 40 s) - v25's fix holds.
- Stage 3's boss (234330467 40.9-44.3 s) a **solid red-orange silhouette** in picture after picture while hit: the game's
  red one-colour palette ($038) alternates with the boss's own; v23 kept the red (the dying airships), v26 held it in
  front. Mednafen: the airship's hit flash is white (palette 0) on even frames - the red is the dying one.
- The bomb white disc / blue ball by turns: **the game's** - Mednafen, playL 8,484-8,535: it pulses white / blue every
  ~6 frames.
- Stage 2's end (234203186 11.4 s): one picture with a light-blue band over the fortress as the screen fades out (as
  983 55.3 s): not found yet.
- 234017761 / 234330467 end: black, then the high-score table (the game over).

## v27
- `eng_red_blend` (ROM): with HIT FLASH off, each red ($038) one-colour sprite palette is drawn as the mix of its sprites'
  own palette (flash_pal) and the red, channel by channel - a television's mix of the two frames. Other one-colour
  palettes (the game's fades and texts) untouched.
- STEADY BLINK's retry (a picture made again from the newest frame when blocks were under way) waits for an even frame
  (`eng_even`, the window read every ~40 us).
- STEADY BLINK only inside a stage (`sap_in`): ares, first builds - the briefing at half speed (the engine waits on
  the pictures there).
ares, playL: the hit airship red-tinted with its hull's detail, then red as it dies; the pace v26's in play (998 of
1,000 frames); the menu as v26.
