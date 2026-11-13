# 61 - The options menu as Metal Slug CD32X's (13 Nov 2026)

The owner, 13 Nov: "Add an options menu like the one in MetalSlug" (MicroNut99/FBNeo_MetalSlug, `m3/m3main.c`
`pause_menu`, its docs/13 v20-v28).

docs/58's menu had Metal Slug's rows and keys but opened on any START press, and START was nothing else. Now it is
Metal Slug's in how it opens and how it looks:

| | Metal Slug CD32X | Sapphire now |
|---|---|---|
| Opens | START held half a second in a game; a short press is the game's own | START held half a second (30 game frames); let go before that it is **RUN** for 8 frames - the title's start, the pause, player 2 joining. A is RUN too |
| The game under it | held | held (docs/58) |
| Title | `METAL SLUG CD32X - PAUSED` | `SAPPHIRE CD32X - PAUSED` |
| Cursor, values | `>`, `< value >` | the same |
| Rows | RESUME, MISSION, CREDITS (the count), LIVES, AUTOFIRE, SMOOTH SCROLL, DETAIL | RESUME, STAGE, LIVES, INFINITE LIVES, AUTOFIRE, MUSIC VOLUME, EFFECTS VOLUME, CREDITS (the port's page) |
| Hint on the level row | `MISSION: SELECT LEVEL. PRESS A TO START` | `STAGE: SELECT LEVEL. PRESS A TO START` |
| Foot | `UP/DOWN PICK  LEFT/RIGHT CHANGE`, `A/START: DO IT   B: BACK`, `Ported by MicroNut99` | the same three lines |

In ares v1.9 with `ARES_32X_TRAP=1`, the pad alone: a short START in the intro goes to the title (RUN); START held
opens the menu, DOWN moves the cursor; no trap line but the 68000's FM at the start.

Not Metal Slug's: a row for the game's own credits count (the title's "CREDITS 4"). Its byte was looked for - 4 at
the title, 3 once a game runs - in base RAM and CD RAM, and not found; SMOOTH SCROLL and DETAIL have nothing to switch
here. Nothing on a console.
