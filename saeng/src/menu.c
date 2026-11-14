/* menu.c - Sapphire CD32X docs/67: Gate of Thunder's options menu (below, its notes) with Sapphire's items - those the
   old route had (docs/58, 61, 63): RESUME, STAGE SELECT 1-5 (the engine restarted, RUN given until the game's start, the
   stage's own sectors fed in place of stage 1's and base RAM $069C set: docs/58's way, now the SH2's feed's), LIVES
   (CD RAM $A00C through the core's CD RAM poke), INFINITE LIVES, AUTOFIRE (button I let go every other pair of frames),
   EFFECTS VOLUME (the PSG's level on the PWM), the HIGH SCORE (CD RAM $A070-$A074, kept), STATUS, CREDITS.
   docs/67 (the program on the cart core): MUSIC VOLUME again (the stack's command 104, CDFADE=1: the CD music's level,
   0-8 as 0-0x400), and the settings and the HIGH SCORE kept on the EverDrive's SD card (the save line under it: where
   they go and whether the last save took).

   (GoT:) menu.c - Gate of Thunder docs/56: the options menu (the owner, 12 Nov 2026: "add the same options menu that Metal
   Slug has" - its pause menu: START held half a second; UP / DOWN pick, LEFT / RIGHT change, A / C / START do it, B
   goes back). Included by main.c before draw_hook: the menu's state and its picture on the 32X's layer. Its pad and
   the stage select's key sequence are eng.c's (gm_pad, gm_macro: they need the engine's picture).

   What the cart's engine allows:
   - RESUME. In a stage the game is paused with its own RUN while the menu is open (eng.c watches the sprite table
     stand still), and let go again.
   - STAGE SELECT: the game's own hidden one. At its title, I II II I SELECT I II I II SELECT SELECT adds STAGE to
     CONFIGURATION; eng.c types it, sets the stage and starts the game. From the title screen (or the intro) only -
     the game has no way back there from a stage - and only with a core that gives SELECT (step 19).
   - AUTOFIRE: button II held for the player in a stage (the game fires on while it is held).
   - STATUS (docs/60): the play's speed and the picture's counts, to read off the screen.
   - CREDITS.
   docs/58, with the core's step 20 (it says 0x20; the items say so with an older core):
   - STAGE SELECT from inside a stage too: the engine is put back into its reset and started again (the key 'GTRS',
     eng.c eng_restart), RUN is given at the System Card, then the stage select as from the intro.
   - LIVES: the game's count of ships (base RAM $0681, 3 at a game's start) written through the core's poke port;
     in a stage only. The game draws it when it next draws its HUD's count. */
#define GM_TXT 0x60                                           /* (the 32X's colours 0x30-0x60: index 0 of sprite palettes */
#define GM_HI 0x30                                            /*  3-6 - never drawn, never written by pace_vbi; 0x20 is the */
#define GM_DIM 0x40                                           /*  picture number's in the border, left dark) */
#define GM_BOX 0x50
enum { GM_RESUME, GM_STAGE, GM_LIVES, GM_INF, GM_AUTO,
#ifdef SAP_CDFADE
       GM_MUS,
#endif
       GM_FX, GM_STATUS, GM_CREDITS, GM_N };
/* docs/60: STATUS - what the USB log told, on the screen (the owner has no USB link any more: a photograph of this
   page is the console's report). The play before the menu opened, measured by eng.c gm_stats over 4 s at a time:
   the pictures a second (tenths), the slowest picture (60ths of a second), the engine's frames a second; the
   background's palettes and cells on the 32X now, the most cells in a picture, the palettes gone there and back
   and (docs/61, LATE) the pictures in which the master read the palettes before the secondary had chosen;
   the long lists (BG2_MORE); the blocks eng_verify read again; the list words the plane had no room for (bg2_over) */
static struct { uint16_t pps10, worst, fps, xpal, xcells, xmost; uint32_t moves, more, fix, over, early; } gm_stv;
#ifndef GOT_VER
#define GOT_VER 2                                             /* (Sapphire's engine program: v1 docs/67's first, v2 its second) */
#endif
static uint8_t gm_open, gm_sel, gm_page, gm_wipe_n, gm_pal, gm_autofire, gm_stage = 1, gm_core, gm_banner, gm_lives = 3;
static uint8_t gm_inf, gm_lives_on, gm_fx = 8, gm_mus = 8;    /* (Sapphire: infinite lives; the menu set the lives; the effects' and the music's levels 0-8) */
static uint8_t gm_sv_bank, gm_sv_state, gm_sv_fail; static uint16_t gm_sv_ok, gm_sv_bad;   /* (docs/67, eng.c sv_*: the EverDrive's save bank
                                                                 answers; the record at the start - 1 read, 2 none there, 3 damaged,
                                                                 4 not read; the last save refused; saves taken; saves refused) */
static uint32_t gm_best;                                      /* (the high score / 10, as CD RAM $A070-$A074 hold it) */
static uint8_t gm_in_stage;                                   /* (as the menu opened: eng.c) */
static const char *gm_msg;                                    /* (a line under the items: why not, what is going on) */
static void gm_fill(int x0, int y0, int x1, int y1, uint8_t c)   /* (x0, x1 even) */
{
    const uint16_t w = (uint16_t)(c << 8 | c);
    for (int y = y0; y < y1; y++) { volatile uint16_t *d = FB + 0x100 + y * 160 + (x0 >> 1); for (int n = (x1 - x0) >> 1; n > 0; n--) *d++ = w; }
}
static void gm_text(int x, int y, const char *t, uint8_t c)   /* (x even; on the box's colour) */
{
    for (; *t; t++, x += 8) {
        int ch = (uint8_t)*t; if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++) for (int b = 0; b < 8; b += 2)
            FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] = (uint16_t)(((g[r] & (0x80 >> b)) ? c : GM_BOX) << 8 | ((g[r] & (0x40 >> b)) ? c : GM_BOX));
    }
}
static int gm_len(const char *t) { int n = 0; while (t[n]) n++; return n; }
static void gm_mid(int y, const char *t, uint8_t c) { gm_text((160 - 4 * gm_len(t)) & ~1, y, t, c); }
static void gm_colours(void)                                  /* (pace_vbi, the 32X's palette its: once the menu is asked) */
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    cram[GM_TXT] = 0x7FFF; cram[GM_HI] = 0x03FF; cram[GM_DIM] = 0x4210; cram[GM_BOX] = 0x2800;
}
static void gm_wipe(int buf)                                  /* the menu's picture off the buffer drawn in: nothing remembered */
{
    gm_fill(32, 0, 288, 224, 0); spr_last_clear(buf);
}
static const char *const gm_credits[] = {
    "GINGA FUKEI DENSETSU", "SAPPHIRE", "(C) 1995 HUDSON SOFT", "", "THE CD32X PORT:", "THE GAME'S OWN PROGRAM ON A",
    "HUC6280 AND AN ARCADE CARD", "IN THE CART'S FPGA,", "ITS PICTURE AND SOUND ON THE 32X", "MUSIC: CD AUDIO", "CD32X BOOT STACK",
    "REFERENCE: MEDNAFEN", 0 };
static void gm_num(char *d, uint32_t v, int w)                /* v right-aligned in w characters at d */
{
    /* (a zero is written as the letter O: this font's 0 has a stroke through it and reads as an 8 on the screen -
       seen in ares, "CORE STEP 28" for 20 - and this page is read off a photograph) */
    for (int i = w - 1; i >= 0; i--) { d[i] = (char)(v || i == w - 1 ? (v % 10 ? '0' + v % 10 : 'O') : ' '); v /= 10; }
}
static void gm_status(void)
{
    gm_mid(16, "STATUS", GM_HI);
    { char t[32] = "PROGRAM V       CORE STEP   "; gm_num(t + 9, GOT_VER, 2);
      t[26] = "O123456789ABCDEF"[gm_core >> 4]; t[27] = "O123456789ABCDEF"[gm_core & 15]; gm_text(40, 36, t, GM_TXT); }   /* (Sapphire's core: A1) */
    { char t[32] = "PICTURES A SECOND        . "; gm_num(t + 22, gm_stv.pps10 / 10, 3); t[26] = (char)('0' + gm_stv.pps10 % 10); gm_text(40, 56, t, GM_TXT); }
    { char t[32] = "SLOWEST PICTURE         /6O"; gm_num(t + 20, gm_stv.worst, 4); gm_text(40, 70, t, GM_TXT); }
    { char t[32] = "ENGINE FRAMES A SECOND     "; gm_num(t + 23, gm_stv.fps, 4); gm_text(40, 84, t, GM_TXT); }
    { char t[32] = "32X BACKGROUND PALETTES    "; gm_num(t + 23, gm_stv.xpal, 4); gm_text(40, 104, t, GM_TXT); }
    { char t[32] = "  CELLS NOW      MOST      "; gm_num(t + 12, gm_stv.xcells, 4); gm_num(t + 23, gm_stv.xmost, 4); gm_text(40, 118, t, GM_TXT); }
    { char t[32] = "  MOVES        LATE        "; gm_num(t + 8, gm_stv.moves, 6); gm_num(t + 20, gm_stv.early, 7); gm_text(40, 132, t, GM_TXT); }
    { char t[32] = "LONG LISTS                 "; gm_num(t + 19, gm_stv.more, 8); gm_text(40, 152, t, GM_TXT); }
    { char t[32] = "BLOCKS READ AGAIN          "; gm_num(t + 19, gm_stv.fix, 8); gm_text(40, 166, t, GM_TXT); }
    { char t[32] = "LIST WORDS LEFT OUT        "; gm_num(t + 19, gm_stv.over, 8); gm_text(40, 180, t, GM_TXT); }
    {   char t[32] = "SD SAVES      TAKEN    BAD   "; gm_num(t + 19, gm_sv_ok, 3); gm_num(t + 26, gm_sv_bad, 3);   /* (docs/67) */
        const char *w = !gm_sv_bank ? "NONE" : gm_sv_state == 1 ? "READ" : gm_sv_state == 2 ? "NEW " : gm_sv_state == 3 ? "BAD " : "?   ";
        for (int i = 0; i < 4; i++) t[9 + i] = w[i];
        gm_text(40, 192, t, GM_TXT); }
    gm_mid(208, "B: BACK", GM_DIM);
}
static void gm_draw(void)                                     /* the menu, whole, into the buffer drawn in */
{
    gm_fill(32, 0, 288, 224, GM_BOX);
    if (gm_page == 2) { gm_status(); return; }
    if (gm_page) {
        for (int i = 0; gm_credits[i]; i++) gm_mid(16 + 13 * i, gm_credits[i], i < 3 ? GM_HI : GM_TXT);
        gm_mid(186, "PORTED BY MICRONUT99", GM_TXT); gm_mid(204, "B: BACK", GM_DIM);
        return;
    }
    gm_mid(14, "SAPPHIRE CD32X", GM_TXT); gm_mid(26, "OPTIONS", GM_DIM);
    for (int i = 0; i < GM_N; i++) {
        const uint8_t c = i == gm_sel ? GM_HI : GM_TXT; const int y = 40 + 13 * i;
        gm_text(48, y, i == gm_sel ? ">" : " ", c);
        if (i == GM_RESUME) gm_text(64, y, "RESUME", c);
        else if (i == GM_STAGE) { char t[24] = "STAGE SELECT    < 1 >"; t[18] = (char)('0' + gm_stage); gm_text(64, y, t, c); }
        else if (i == GM_LIVES) { char t[24] = "LIVES           < 3 >"; t[18] = (char)('0' + gm_lives); gm_text(64, y, t, c); }
        else if (i == GM_INF) gm_text(64, y, gm_inf ? "INFINITE LIVES  < ON >" : "INFINITE LIVES  < OFF >", c);
        else if (i == GM_AUTO) gm_text(64, y, gm_autofire ? "AUTOFIRE        < ON >" : "AUTOFIRE        < OFF >", c);
#ifdef SAP_CDFADE
        else if (i == GM_MUS) { char t[24] = "MUSIC VOLUME    < 8 >"; t[18] = (char)('0' + gm_mus); gm_text(64, y, t, c); }
#endif
        else if (i == GM_FX) { char t[24] = "EFFECTS VOLUME  < 8 >"; t[18] = (char)('0' + gm_fx); gm_text(64, y, t, c); }
        else if (i == GM_STATUS) gm_text(64, y, "STATUS", c);
        else gm_text(64, y, "CREDITS", c);
    }
    {   char t[24] = "HIGH SCORE  "; uint32_t v = gm_best * 10u;   /* (the score as the game shows it: 8 digits) */
        for (int k = 7; k >= 0; k--) { t[12 + k] = (char)(v % 10u ? '0' + v % 10u : 'O'); v /= 10u; } t[20] = 0;
        gm_mid(156, t, GM_TXT); }
    gm_mid(166, !gm_sv_bank ? "NOT SAVED: NO SD SAVE BANK" : gm_sv_fail ? "THE LAST SAVE FAILED" : "KEPT ON THE SD CARD", GM_DIM);   /* (docs/67) */
    if (gm_msg) gm_mid(178, gm_msg, GM_HI);
    else if (gm_sel == GM_STAGE) gm_mid(178, "A: A NEW GAME AT THIS STAGE", GM_DIM);
    else if (gm_sel == GM_LIVES) gm_mid(178, gm_in_stage ? "A: SET THE SHIPS LEFT NOW" : "A NEW GAME STARTS WITH THEM", GM_DIM);
    else if (gm_sel == GM_INF) gm_mid(178, "THE SHIPS LEFT KEPT", GM_DIM);
    else if (gm_sel == GM_AUTO) gm_mid(178, "HOLD I: THE SHIP FIRES ON", GM_DIM);
    else if (gm_sel == GM_STATUS) gm_mid(178, "A: HOW THE CONSOLE RUNS IT", GM_DIM);
    gm_mid(190, "UP/DOWN PICK  LEFT/RIGHT CHANGE", GM_DIM);
    gm_mid(200, "A/C/START: DO IT   B: BACK", GM_DIM);
    gm_mid(212, "PORTED BY MICRONUT99", GM_TXT);
}
static void gm_note(void)                                     /* (the stage select at work: a line over the game's picture) */
{
    char t[24] = "STARTING STAGE 1 ..."; t[15] = (char)('0' + gm_stage);   /* (Sapphire: 1-5) */
    gm_fill(32, 208, 288, 224, GM_BOX); gm_mid(212, gm_banner == 2 ? "RESTARTING THE GAME ..." : t, GM_HI);
}
