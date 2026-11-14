/* eng.c - Sapphire CD32X docs/67: Gate of Thunder CD32X's engine program (MicroNut99/Gate-of-Thunder_CD32X goteng/,
 * 4a438c6: its docs/38-61, console-proven with its cart core step 20) for Sapphire's cart core - the same core with the
 * Arcade Card and the sector port (fpga/gotcore, docs/62-66; ares: the EverDrive PRO's 'sapx' configuration). Below,
 * GoT's notes as they were, then Sapphire's changes: the data track fed from the cart's pack (eng_feed), the start's parts.
 *
 * (GoT:) eng.c - Gate of Thunder CD32X, the engine disc (docs/38): Dracula X's eng.c (their docs/102-169, DraculaX_32X a2e10d8)
 * for GoT's cart - the Mega EverDrive PRO's 'gotx' configuration (fpga/, docs/19-37). The PC Engine CD runs on the cart;
 * the SH2 starts it and draws what it shows. Included at the end of main.c (its renderers, screen and pacing).
 *
 * GoT's contract (docs/36 §2, bank 26 since docs/38: the owner's choice, 11 Nov) is one bank - command 68's page 6 puts
 * banks 24-27 in windows 4-7, so bank 26 is window 6 (cart 0x300000, the SH2's 0x22300000 past the cache): the video
 * window at the offsets Dracula X's window 6 has, the register window, the load port and the pad at those its window 7
 * has. No sector feed: the data track is in PSRAM (banks 16-23), put there at the start.
 *
 * The start: page 4 + GOTTRKA.BIN (file 101: track 2's sectors 0-991 into banks 16-19, below the save area's 64 KB
 * that commands never write), page 5 + GOTTRKB.BIN (102: sectors 992-1881, banks 20-23), page 6 + IMAGE28.RAW (file 28:
 * the System Card at cart 0x380000 = bank 27) - page 6 stays; GOTFWTAB.BIN (file 203, resident) through the frame
 * buffer and command 101 into bank 26 + 0x41000 (the load port; its last word the key 'GTON'); the start read.
 * Then on and on, as Dracula X's: the pad, and at each new frame of the engine the picture. Not here yet: the CD
 * audio (GOT_AUDIO), the backup RAM (the cart has no port for it yet). */
#ifndef BG2_LIST
#define BG2_LIST 0x11A00                                      /* (v05, BG=0: the frame buffer's spare area, as main.c's validation build) */
#endif
#define ENG_W7 0x22300000u                                    /* (GoT: one bank - bank 26, window 6 with page 6: cart 0x300000) */
#define ENG_W6 0x22300000u
#define ENG_W7_CART 0x300000u                                 /* (its cart offset, for command 101) */
#define ENG_R(base, off) (*(volatile uint16_t *)((base) + (off)))
#ifndef GOT_RUN_MAX
#define GOT_RUN_MAX 8                                         /* (docs/57: a list's longest run in a picture with V splits) */
#endif
#define ENG_FB_TABLE 0x1D000u                                 /* (the frame buffer's spare part, for command 101) */

/* docs/52: the frame-end snapshot's CR. Gate of Thunder's stages run CR $8C (background on, sprites OFF) over the HUD and
   $CC below it by a raster interrupt (docs/07), and the snapshot has the HUD's: every sprite of a stage was dropped (no
   ship, no enemies). Sprites are drawn when either the sprites or the background are on; all off = a blank screen */
#define ENG_SPR_ON(cr) ((cr) & 0xC0)
static V32 eng_v __attribute__((aligned(16)));
static uint16_t eng_stamp[512];                               /* (the stamps the copy in eng_v has, VRAM's first 32 K words) */
static uint32_t eng_fed, eng_feed_fail, eng_frames, eng_last = 0xFFFFFFFFu;
#ifdef DX_PROF
/* docs/168: the profile, a line at the bottom each second (the owner films it): in that second - P pictures drawn,
   E engine frames gone by, then vertical blanks (60 = the whole second) spent: W waiting for a new engine frame,
   F in the feed, K in the pad's command, A in the audio, I making the picture (the cart's window read, the lists),
   D drawing and showing it; X pictures whose pad read was skipped (the 68000 still on a list) */
static uint32_t pf_t[5], pf_pics, pf_skip, pf_e0, pf_last;
#define PF(i, stmt) do { const uint32_t pf_a = gTicks; stmt; pf_t[i] += gTicks - pf_a; } while (0)
#else
#define PF(i, stmt) do { stmt; } while (0)
#endif
static uint32_t eng_want, eng_wants;                          /* (DX_NOFEED: the last sector the engine asked for, how often) */

static int eng_page(int p)                                    /* command 68: page p in window 1 (banks 4p..4p+3) */
{
    MARS_SYS_COMM2 = (uint16_t)p; COMM12_16 = 0;
    const int ok = ed_cmd(68) && COMM12_16 == 1;            /* (docs/121: RV = 1 - parked) */
    CacheControl(0x11);
    return ok;
}
/* the CD unit's next sector, fed if the spare doesn't hold it. The track's sectors come 16 at a time (32 KB, one CD
   command: a sector at a time costs the Sega CD a seek each, docs/102) into a ring of ENG_RN chunks in the cart, as the
   play build's (docs/69); the sector then goes to the feed's 2 KB through the frame buffer's spare part and command 101
   (the SH2 cannot write the cart), as the play build's bank write-back (pool_wb, docs/72). First the label: its high
   part, then its low; the 2 KB's last byte fills the spare */
static uint32_t eng_chunks;
static uint32_t eng_aud;                                      /* (docs/103: the play the Sega CD has: playing, mode, start) */
static uint32_t eng_cdfail; static uint16_t eng_cdlast;      /* (docs/169: commands that did not answer or refused; the last: 91xx / 4500 / 5400) */
static int eng_tlast;                                         /* (docs/50: the game's last CD-DA track asked, 0 none) */
static uint32_t eng_fixn;                                     /* (docs/50: VRAM blocks the verify found wrong in the copy) */
static int eng_playing;                                       /* (docs/169: the Sega CD is playing, as far as this program has told it) */
static uint32_t eng_plays, eng_audio_seen;                    /* (the plays sent; the engine's audio words seen changing) */
#ifdef DX_PADDIAG
static uint16_t pd_raw, pd_sent, pd_reads, pd_skip, pd_tick;           /* (docs/130: the pad's way to the engine, on screen) */
#endif
static uint16_t got_padw, got_padw_or, got_tick; static uint32_t got_padskip;
static uint32_t sv_act = 0x80000000u, sv_run = 0x80000000u; static int sv_dirty;   /* (Sapphire docs/63, 67: the pad in use, RUN, the save to make) */         /* (docs/41: the stack's pad word last read; reads skipped) */
#ifdef GOT_MENU
/* ---- docs/56: the options menu's pad (menu.c has its picture and what each item is) ----
   The engine's buttons: I 1, II 2, SELECT 4, RUN 8, UP 16, RIGHT 32, DOWN 64, LEFT 128. */
/* (Sapphire docs/67) in a stage: the frame-end CR the game keeps in one (0x8C over the status bar, 0xCC above it; the
   intro and the title have 0xCC / 0xC8 too) and its ships left (CD RAM $A00C: 0 until a game's stage block is in - the
   attract demo's as well) - the lives read every 30th engine frame through the core's CD RAM peek (sap_tick) */
static uint8_t sap_lives, sap_in;
#define GM_IN_STAGE() (sap_in)
static uint16_t gm_was; static uint32_t gm_run_until, gm_st_t0; static uint8_t gm_st, gm_frozen;
static uint32_t gm_h_last, gm_h_f;
static uint16_t eng_lines[224 * 2];                           /* (below: each line's scroll) */
static uint8_t gm_mac, gm_rs_req, gm_bset; static uint16_t gm_mf;
/* the core's poke port (GoT docs/58, its step 20): two reads; the byte at the address reads back at + 0x40002's high byte.
   Sapphire's core (fpga/gotcore, docs/63): h first - a read of + 0x43000 + (h << 1): bit 3 CD RAM, bits 2-0 its 8 KB
   bank - then the poke and the peek go to CD RAM, through the CPU's write path (+ 0x40010 bit 6 while one is under way)
   and its read (+ 0x42000 + (a << 1) for the address's bits 1-0; the byte at + 0x40010's high byte, bit 7 when there) */
static void gm_pk(uint32_t a, uint32_t write, uint32_t v)     /* (the address's bits 12-2, then its bits 1-0 with the byte) */
{
    (void)ENG_R(ENG_W7, 0x4D000u + ((a >> 2 & 0x7FFu) << 1));
    (void)ENG_R(ENG_W7, 0x4F000u + (write ? 0x800u : 0u) + ((a & 3u) << 9) + ((v & 0xFFu) << 1));
}
static uint32_t gm_peek(uint32_t a) { gm_pk(a, 0, 0); for (volatile int n = 0; n < 50; n++) { } return ENG_R(ENG_W7, 0x40002) >> 8 & 0xFF; }
static void gm_poke(uint32_t a, uint32_t v) { gm_pk(a, 1, v); }
static void sp_h(uint32_t h) { (void)ENG_R(ENG_W7, 0x43000u + ((h & 15u) << 1)); }
static void sp_cdpoke(uint32_t a, uint32_t v)                 /* CD RAM's byte a (0-0xFFFF) */
{
    sp_h(8u | (a >> 13 & 7u));
    gm_pk(a & 0x1FFFu, 1, v);
    for (int n = 0; n < 4000 && (ENG_R(ENG_W7, 0x40010) & 0x40); n++) { }
    sp_h(0);
}
static uint32_t sp_cdpeek(uint32_t a)
{
    sp_h(8u | (a >> 13 & 7u));
    (void)ENG_R(ENG_W7, 0x4D000u + ((a >> 2 & 0x7FFu) << 1));
    (void)ENG_R(ENG_W7, 0x42000u + ((a & 3u) << 1));
    for (volatile int n = 0; n < 50; n++) { }
    uint16_t w = 0;
    for (int n = 0; n < 4000; n++) { w = ENG_R(ENG_W7, 0x40010); if (w & 0x80) break; }
    sp_h(0);
    return w >> 8 & 0xFF;
}
#define SP_LIVES_AT 0xA00Cu                                   /* (CD RAM: the ships left - the old route's poke, docs/58) */
#define SP_STAGE_AT 0x069Cu                                   /* (base RAM: the stage, less one - docs/58) */
#define SP_SCORE_AT 0xA070u                                   /* (CD RAM: the score / 10, its bytes at $A070, $A072, $A074 - docs/63) */
/* ---- docs/67: the CD music's level (MUSIC VOLUME, the stack's command 104, CDFADE=1) ----
   COMM2 = the level to go to (0-0x400), COMM8 = the slope (0: at once - the BIOS's FDRSET; else that much a drive interrupt,
   75 a second: the BIOS's FDRCHG). The menu's 0-8 is 0-0x400. Sent at once when the menu changes it, after each track's
   command 45 (a new track starts at full volume, BootCamp ledger section 38), and as the game's fader asks (eng_fade). */
#ifdef SAP_CDFADE
static uint8_t mus_off;                                       /* (104 not answered once: a stack without it - not sent again) */
static uint32_t mus_sent = 0xFFFFFFFFu, mus_n;
static void mus_level(uint32_t level, uint32_t slope)
{
    if (mus_off || (slope == 0 && level == mus_sent)) return;
    cdr_busy = 1;                                             /* (before the wait: the blank's commit must not come between, Dracula X docs/194) */
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { LT_POLL(); }
    MARS_SYS_COMM2 = (uint16_t)level; MARS_SYS_COMM8 = (short)slope;
    if (!cd_cmd(104)) { cd_answer_reset(); mus_off = 1; }
    cdr_busy = 0;
    mus_sent = slope ? 0xFFFFFFFEu : level; mus_n++;
}
#define MUS_FULL() ((uint32_t)gm_mus * 0x80u)
static uint8_t eng_fade_sent; static uint16_t eng_fades;
#ifdef GOT_SND
/* the game's own fades of its CD music: its fader $180F, which the core's sound log carries (code 0x1F; snd.c keeps the
   last value) - 0x0C / 0x08 the CD audio fading out (bit 2: in 2.5 s, else 6 s), 0x00 full again; 0x0E (bit 1) is the
   ADPCM's, not the music's. The recording: a fade at most stages' ends and before the briefings (sapm_pc SND_LOG=1F:
   20 in 21 minutes). As Dracula X's eng_fade (its docs/193): 104 to 0 with a slope that takes the menu's level there in
   that time (the drive's 75 interrupts a second), or back to the menu's level at once */
static void eng_fade(void)
{
    const uint32_t fc = SND_FADER;
    const int want = (fc & 0x0A) == 0x08;
    if (want == eng_fade_sent) return;
    eng_fade_sent = (uint8_t)want;
    if (want) { const uint32_t lv = MUS_FULL(), sl = (fc & 4) ? (lv + 186u) / 187u : (lv + 449u) / 450u; mus_level(0, sl ? sl : 1); eng_fades++; }
    else mus_level(MUS_FULL(), 0);
}
#endif
#endif
#ifdef SAP_SDSAVE
/* ---- docs/67: the settings and the high score on the EverDrive's SD card ----
   docs/63's record, where docs/63 had it (the EverDrive's bank 31 + 0x1000 = the sdcard/ kit's piece 1; the EverDrive's OS
   keeps the bank on the SD card as MEGA/gamedata/<the launcher's file name>/bram.srm): 'SPS1', the length (12), the
   data's FNV-1a sum and its complement, then version 1, lives, lives set by the menu, infinite lives, autofire, music
   volume, effects volume, a spare byte, the high score / 10 (32 bits; all big-endian). The BootCamp sdcard/ kit's two
   commands, merged into the build's copy of the stack as Dracula X's v37 has them (SDSAVE=1 SDSAVE_OUT=76 SDSAVE_IN=77,
   EDCD_SAVE=1): 58 puts a word into the Sub-CPU's 4 KB buffer, 76 copies the buffer into the piece and reads it back,
   77 copies the piece into the buffer (COMM2 = 0x8000: only asks whether the bank is there), 57 reads a word; 76 / 77
   answer COMM12 = 0x201. They copy with RV = 1: sent by ed_cmd (the secondary parked, the master in SDRAM with its
   interrupts masked), as 68 and 101. Saved when the menu closes (the game is paused then) and when play ends (no longer
   in a stage: a briefing, the game's end) - never in play. */
#define SV_PIECE 1
#define SV_OUT 76
#define SV_IN 77
#define SV_MAGIC 0x53505331u                                  /* 'SPS1' */
enum { SV_VER = 1, SV_LEN = 12, SV_WORDS = (16 + SV_LEN) / 2 };
static uint32_t sv_saved_h;                                   /* (the data as last saved or read: saved again only when it differs) */
static uint32_t sv_fnv(const uint8_t *p, unsigned n) { uint32_t h = 0x811C9DC5u; while (n--) { h ^= *p++; h *= 0x01000193u; } return h; }
static void sv_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t sv_get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void sv_data(uint8_t *d)
{
    d[0] = SV_VER; d[1] = gm_lives; d[2] = gm_lives_on; d[3] = gm_inf; d[4] = gm_autofire;
#ifdef SAP_CDFADE
    d[5] = gm_mus;
#else
    d[5] = 8;
#endif
    d[6] = gm_fx; d[7] = 0; sv_put32(d + 8, gm_best);
}
static int sv_cmd(uint16_t cmd, uint16_t piece)               /* 76 / 77 with RV = 1: 1 done */
{
    cdr_busy = 1;
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { LT_POLL(); }
    COMM12_16 = 0; MARS_SYS_COMM2 = piece;
    const int ok = ed_cmd(cmd) && COMM12_16 == 0x201;
    cdr_busy = 0;
    return ok;
}
static void sv_load(void)                                     /* at the start: the bank asked for, the record read */
{
    COMM12_16 = 0; MARS_SYS_COMM2 = 0x8000;
    if (!cd_cmd(SV_IN)) { cd_answer_reset(); return; }        /* (a stack without the commands: never answered) */
    gm_sv_bank = COMM12_16 == 0x201;
    if (!gm_sv_bank) return;                                  /* (the disc's own program: no bank - not saved) */
    gm_sv_state = 4;
    if (!sv_cmd(SV_IN, SV_PIECE)) return;
    uint8_t r[16 + SV_LEN]; const uint8_t *d = r + 16;
    for (int i = 0; i < SV_WORDS; i++) {
        MARS_SYS_COMM2 = (uint16_t)i;
        if (!cd_cmd(57)) { cd_answer_reset(); return; }
        const uint16_t w = (uint16_t)MARS_SYS_COMM8; r[2 * i] = (uint8_t)(w >> 8); r[2 * i + 1] = (uint8_t)w;
    }
    if (sv_get32(r) != SV_MAGIC) { gm_sv_state = 2; return; } /* (none: the first start, a new bram.srm) */
    const uint32_t h = sv_get32(r + 8);
    if (sv_get32(r + 4) != SV_LEN || sv_get32(r + 12) != ~h || sv_fnv(d, SV_LEN) != h || d[0] != SV_VER) { gm_sv_state = 3; return; }
    if (d[1] >= 1 && d[1] <= 9) gm_lives = d[1];
    gm_lives_on = d[2] != 0; gm_inf = d[3] != 0; gm_autofire = d[4] != 0;
#ifdef SAP_CDFADE
    if (d[5] <= 8) gm_mus = d[5];
#endif
    if (d[6] <= 8) gm_fx = d[6];
#ifdef GOT_SND
    SND_VOL = gm_fx;
#endif
    gm_best = sv_get32(d + 8); if (gm_best >= 10000000u) gm_best = 0;
    sv_saved_h = h; gm_sv_state = 1;
}
static void sv_write(void)                                    /* the record into the buffer, then the piece: when it changed */
{
    if (!gm_sv_bank) return;
    uint8_t r[16 + SV_LEN]; uint8_t *d = r + 16;
    sv_data(d);
    const uint32_t h = sv_fnv(d, SV_LEN);
    if (h == sv_saved_h && gm_sv_state != 3) { sv_dirty = 0; return; }
    sv_put32(r, SV_MAGIC); sv_put32(r + 4, SV_LEN); sv_put32(r + 8, h); sv_put32(r + 12, ~h);
    cdr_busy = 1;                                             /* (Dracula X docs/194: the blank's commit 79 must not come between a word's
                                                                 COMM2 / COMM8 and its 58 - held off before the wait) */
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { LT_POLL(); }
    for (int i = 0; i < SV_WORDS; i++) {
        MARS_SYS_COMM2 = (uint16_t)i; MARS_SYS_COMM8 = (short)(r[2 * i] << 8 | r[2 * i + 1]);
        if (!cd_cmd(58)) { cd_answer_reset(); cdr_busy = 0; gm_sv_bad++; gm_sv_fail = 1; sv_dirty = 0; return; }
    }
    cdr_busy = 0;
    if (!sv_cmd(SV_OUT, SV_PIECE)) { gm_sv_bad++; gm_sv_fail = 1; sv_dirty = 0; return; }   /* (refused: tried again at the next close or end) */
    sv_saved_h = h; gm_sv_ok++; gm_sv_fail = 0; gm_sv_state = 1; sv_dirty = 0;
}
#else
static void sv_write(void) { sv_dirty = 0; }
#endif
/* the stage select, docs/58's way on the cart: 20 the engine's restart asked of the main loop (eng_restart: 'GTRS', the
   start read again; it goes on to 21); 21 the CD-DA's play over (the System Card's reset of the drive) or 5 s; 30 RUN a
   tenth of a second each second - the System Card's screen, the title - until the feed is asked for the game's start
   (W_GAME: the briefing's program); 31 the new game's block (116 at W_BRIEF: 52 sectors of the game's program, 64 the
   first stage's own) - the feed sets $069C to the stage at its first sector and serves the stage's own 64 in place of
   stage 1's (eng_feed); the stage's block (596 at W_STAGE + 724 a stage) read: 32; 32 the game's own first write of its
   lives, then the menu's count if it ever set one. Each step given up after 60 s (the engine's frames) */
#define W_GAME 11004u
#define W_BRIEF 4196u
#define W_BRIEF_OWN (W_BRIEF + 52u)
#define W_BRIEF_N 4920u
#define W_STAGE 3600u
static void gm_mgo(int st, uint32_t f) { gm_mac = (uint8_t)st; gm_mf = (uint16_t)f; }
static uint32_t gm_macro(uint32_t f)                          /* the engine's buttons this picture */
{
    const uint16_t d = (uint16_t)(f - gm_mf);
    switch (gm_mac) {
    case 20: return 0;
    case 21: if (!(eng_aud >> 31) || d > 300) gm_mgo(30, f); return 0;
    case 30: if (d > 3600) break; return d % 60 < 6 ? 8u : 0u;
    case 31: if (d > 3600) break; return 0;
    case 32:
        if (d > 3600) break;
        if ((f & 15) == 0 && sp_cdpeek(SP_LIVES_AT)) { if (gm_lives_on) sp_cdpoke(SP_LIVES_AT, gm_lives); break; }
        return 0;
    default: break;
    }
    gm_mac = 0; gm_banner = 0; gm_wipe_n = 2; return 0;
}
/* docs/60: the STATUS page's measures - once a picture; 4 s at a time (240 blanks), the last whole 4 s kept. Nothing
   counted while the menu is open or the stage select works */
extern uint32_t bg2_over;                                     /* (rt_bg2.c, docs/59: list words left out) */
#ifdef BG2_X32
extern uint32_t bgx_cells;                                    /* (below, spr_under: the cells of the last picture) */
static uint32_t bgx_early;                                    /* (below, bgx_assign) */
#endif
static void gm_stats(uint32_t f)
{
    static uint32_t t0, tl, f0; static uint16_t n, worst, most;
    const uint32_t now = gTicks;
    if (gm_open || gm_mac) { t0 = 0; return; }
    if (!t0) { t0 = tl = now; f0 = f; n = 0; worst = 0; most = 0; return; }
    n++; if (now - tl > worst) worst = (uint16_t)(now - tl); tl = now;
#ifdef BG2_X32
    if (bgx_cells > most) most = (uint16_t)bgx_cells;
    gm_stv.xcells = (uint16_t)bgx_cells; { int k = 0; for (int p = 0; p < 16; p++) k += bgx_set >> p & 1; gm_stv.xpal = (uint16_t)k; }
#endif
    if (now - t0 >= 240) {
        gm_stv.pps10 = (uint16_t)((uint32_t)n * 600u / (now - t0)); gm_stv.worst = worst;
        gm_stv.fps = (uint16_t)(((f - f0) & 0xFFFFu) * 60u / (now - t0)); gm_stv.xmost = most;
#ifdef BG2_X32
        gm_stv.moves = *(volatile uint32_t *)((uintptr_t)&bg2_x32_moves | 0x20000000); gm_stv.early = bgx_early;
#endif
#ifdef BG2_MORE
        gm_stv.more = *(volatile uint32_t *)((uintptr_t)&got_more_n | 0x20000000);
#endif
        gm_stv.fix = eng_fixn; gm_stv.over = *(volatile uint32_t *)((uintptr_t)&bg2_over | 0x20000000);
        t0 = now; f0 = f; n = 0; worst = 0; most = 0;
    }
}
/* the 32X pad's word p, the engine's buttons b as eng_pad made them: Start is RUN when let go (a tap), the menu when
   held half a second once the game runs; the menu's own keys; the stage select's; autofire. Returns the buttons */
static uint32_t gm_pad(uint16_t p, uint32_t b, uint32_t f)
{
    const uint32_t now = gTicks;
    {   /* (a paused game: two frames of the engine with the same sprite table and scroll) */
        uint32_t h = 2166136261u; const uint16_t *s = eng_v.sat;
        for (int i = 0; i < 256; i++) h = (h ^ s[i]) * 16777619u;
        for (int l = 4; l < 224; l += 8) h = (h ^ eng_lines[2 * l]) * 16777619u;
        if (f != gm_h_f) { gm_frozen = h == gm_h_last; gm_h_last = h; gm_h_f = f; }
    }
    b &= ~8u;                                                 /* (Start: below) */
    if (gm_mac) {                                             /* (the stage select at work: its keys alone; Start cancels) */
        if ((p & SEGA_CTRL_START) && !(gm_was & SEGA_CTRL_START)) { gm_mac = 0; gm_banner = 0; gm_wipe_n = 2; }
        gm_was = p; gm_st = 2;
        return gm_mac ? gm_macro(f) : 0;
    }
    if (gm_open) {
        const uint16_t k = (uint16_t)(p & ~gm_was); gm_was = p;
        const int ok = (k & (SEGA_CTRL_A | SEGA_CTRL_C | SEGA_CTRL_START)) != 0;
        int shut = 0;
        if (k) gm_msg = 0;
        if (gm_page) { if (k & (SEGA_CTRL_B | SEGA_CTRL_A | SEGA_CTRL_C | SEGA_CTRL_START)) gm_page = 0; }
        else {
            if (k & SEGA_CTRL_UP) gm_sel = (uint8_t)((gm_sel + GM_N - 1) % GM_N);
            if (k & SEGA_CTRL_DOWN) gm_sel = (uint8_t)((gm_sel + 1) % GM_N);
            const int dir = (k & SEGA_CTRL_RIGHT) ? 1 : (k & SEGA_CTRL_LEFT) ? -1 : 0;
            if (gm_sel == GM_STAGE && dir) gm_stage = (uint8_t)((gm_stage + 4 + dir) % 5 + 1);
            if (gm_sel == GM_LIVES && dir) { gm_lives = (uint8_t)((gm_lives + 8 + dir) % 9 + 1); gm_lives_on = 1; }
            if (gm_sel == GM_INF && (dir || ok)) { gm_inf ^= 1; if (gm_inf) gm_lives_on = 1; }
            if (gm_sel == GM_AUTO && (dir || ok)) gm_autofire ^= 1;
#ifdef GOT_SND
            if (gm_sel == GM_FX && dir) { const int v = gm_fx + dir; gm_fx = (uint8_t)(v < 0 ? 0 : v > 8 ? 8 : v); SND_VOL = gm_fx; }
#endif
#ifdef SAP_CDFADE
            if (gm_sel == GM_MUS && dir) { const int v = gm_mus + dir; gm_mus = (uint8_t)(v < 0 ? 0 : v > 8 ? 8 : v); mus_level(MUS_FULL(), 0); }   /* (heard at once) */
#endif
            if (k & SEGA_CTRL_B) shut = 1;
            else if (ok && gm_sel == GM_RESUME) shut = 1;
            else if (ok && gm_sel == GM_CREDITS) gm_page = 1;
            else if (ok && gm_sel == GM_STATUS) gm_page = 2;
            else if (ok && gm_sel == GM_STAGE) { if (gm_core < 0x20) gm_msg = "NEEDS THE NEW CORE"; else shut = 3; }
            else if (ok && gm_sel == GM_LIVES) {
                gm_lives_on = 1;
                if (gm_in_stage) { sp_cdpoke(SP_LIVES_AT, gm_lives); gm_msg = sp_cdpeek(SP_LIVES_AT) == gm_lives ? "SET" : "NOT TAKEN"; }
                else gm_msg = "A NEW GAME STARTS WITH THEM";
            }
        }
        if (shut) {
            gm_open = 0; gm_wipe_n = 2; gm_st = 2;            /* (Start, if it shut the menu, is not a tap) */
            sv_write();                                       /* (docs/67: the settings as the menu leaves them, saved now - the game
                                                                 still paused; nothing sent when they did not change) */
            if (shut == 3) { gm_mgo(20, f); gm_banner = 1; gm_rs_req = 1; gm_bset = 0; }
            else if (GM_IN_STAGE() && gm_frozen) gm_run_until = now + 12;   /* (the game let go again) */
        }
        return (int32_t)(gm_run_until - now) > 0 ? 8u : 0u;
    }
    gm_was = p;
    if (p & SEGA_CTRL_START) {
        if (!gm_st) { gm_st = 1; gm_st_t0 = now; }
        else if (gm_st == 1 && eng_tlast && now - gm_st_t0 >= 30) {   /* (held: the menu - in a stage the game paused) */
            gm_st = 2; gm_open = 1; gm_page = 0; gm_sel = 0; gm_msg = 0; gm_pal = 1;
            gm_in_stage = GM_IN_STAGE();
            if (gm_in_stage) { const uint32_t l = sp_cdpeek(SP_LIVES_AT); if (l >= 1 && l <= 9) gm_lives = (uint8_t)l; }
            if (GM_IN_STAGE() && !gm_frozen) gm_run_until = now + 12;
        }
    } else { if (gm_st == 1) gm_run_until = now + 12; gm_st = 0; }   /* (let go before: RUN) */
    if ((int32_t)(gm_run_until - now) > 0) b |= 8;
    if (p & 0x0FFF) sv_act = f;                                /* (docs/63: the pad in use - a score counts) */
    if (b & 8) sv_run = f;
    if (gm_autofire && GM_IN_STAGE() && (b & 1) && (f & 2)) b &= ~1u;   /* (I let go every other pair of frames: the old route's) */
    return b;
}
/* (Sapphire docs/67) once an engine frame: in a stage or not; a new game's own first write of its lives (0 -> n) followed by
   the menu's count; the infinite lives; the high score (docs/63: with the pad in use in the last 10 s and no RUN in the
   last 2 s - the attract demo plays with scores) */
static uint8_t sap_lives_was;
static void sap_tick(uint32_t f)
{
    const unsigned cr = eng_v.t.CR & 0xFF;
    if (f % 30 == 0) {
        sap_lives = (uint8_t)sp_cdpeek(SP_LIVES_AT);
        if (!sap_lives_was && sap_lives && gm_lives_on && !gm_mac && sap_lives != gm_lives) { sp_cdpoke(SP_LIVES_AT, gm_lives); sap_lives = gm_lives; }
        sap_lives_was = sap_lives;
        if (gm_inf && sap_lives && sap_lives != gm_lives) { sp_cdpoke(SP_LIVES_AT, gm_lives); sap_lives = gm_lives; }
    }
    sap_in = (cr == 0x8C || cr == 0xCC) && sap_lives >= 1 && sap_lives <= 9;
    {   /* (docs/67: play over - 10 s with no stage's frame-end CR (a briefing, the title after the game's end): a new high
           score saved then, never in play - a stage's CR stays 0x8C / 0xCC on the last ship too, when its count may read 0) */
        static uint16_t sv_stage_f; static uint8_t sv_end;
        if (cr == 0x8C || cr == 0xCC) { sv_stage_f = (uint16_t)f; sv_end = 0; }
        else if (!sv_end && (uint16_t)((uint16_t)f - sv_stage_f) >= 600) { sv_end = 1; if (sv_dirty && !gm_mac && !gm_open) sv_write(); }
    }
    if (f % 60 == 30 && f - sv_act < 600 && f - sv_run > 120) {
        uint32_t a = sp_cdpeek(SP_SCORE_AT) | sp_cdpeek(SP_SCORE_AT + 2) << 8 | sp_cdpeek(SP_SCORE_AT + 4) << 16;
        const uint32_t b = sp_cdpeek(SP_SCORE_AT) | sp_cdpeek(SP_SCORE_AT + 2) << 8 | sp_cdpeek(SP_SCORE_AT + 4) << 16;
        if (a == b && a > gm_best && a < 10000000u) { gm_best = a; sv_dirty = 1; }
    }
}
#endif
/* ---- Sapphire docs/67: the data track, fed through the core's sector port ----
   Sapphire's track 2 is 7,599 sectors (5,255 different): it does not fit PSRAM chip 1 beside the Arcade Card, so it is
   in chip 0 as the pack (tools/m3_trackpack.py, docs/49: the different sectors once each, LZ4 blocks, each sector's
   FNV-1a), the whole file from cart PK_FILE (bank 1 + 4 KB) on - its head (header, map, offsets, sums) and its first
   ~1 MB in banks 1-3, which windows 1-3 always show; the rest in banks 4-13, which command 68's pages 1-3 put in
   window 4-7 (page 6, the core's, back after). When the busy word's bit 1 says the CD unit wants a sector: its LBA, the
   sector made from the pack (the packed bytes a unit at a time: PK_BUF bytes read ahead under one page, so a run of
   sectors costs a page switch every PK_BUF bytes), then + 0x47000 (the sector's start: the core latches the LBA it wants
   then - read again first: a seek in between and the sector is not given), the 2,048 reads of + 0x46000 + (byte << 1),
   and + 0x47800 (its end: the CD unit may take it). The engine's master clock waits meanwhile (the CD firmware waits for
   a sector without limit, GoT docs/33), so a slow feed costs time, never a wrong frame. */
#define PK_FILE 0x081000u                                     /* (the pack's first byte: bank 1 + 4 KB; SAPFWTAB.BIN at bank 1) */
#define PK_LBA0 3590u                                         /* (track 2's first LBA: the disc's table) */
#ifndef PK_BUF
#define PK_BUF 4096                                           /* (SDRAM is nearly full: 4 KB of packed bytes, ~3 sectors a page switch) */
#endif
static uint32_t pk_n, pk_nu, pk_data, pk_pages, pk_busy, pk_bad;
static uint32_t pk_lo = 1, pk_hi;                             /* (the pack's bytes [pk_lo, pk_hi) in pk_buf: none) */
static uint8_t pk_buf[PK_BUF] __attribute__((aligned(4)));          /* (a fetch is PK_BUF bytes, or a unit: 4 KB at most) */
static uint8_t eng_sec[2048] __attribute__((aligned(4)));
static uint32_t eng_ready = 0xFFFFFFFFu;                      /* (the LBA eng_sec holds) */
static void eng_fail(const char *what, uint16_t code);
static int pk_page = 6;                                       /* (the page window 1 shows: 6, the core's, but while a read) */
static uint32_t pk_be32(uint32_t f)                           /* a big-endian long of the pack's head (banks 1-3: seen always) */
{
    const volatile uint16_t *w = (const volatile uint16_t *)(CART_BASE + PK_FILE + f);
    return (uint32_t)w[0] << 16 | w[1];
}
/* the page session: while window 1 shows a page of the pack, nothing may read the core's window - not pace_vbi on this
   SH2 (cdr_busy: it sends nothing, as for the feed's commands in Dracula X), not the secondary's sound or its lists
   (parked in SDRAM with its interrupts masked for the whole session, as command 68's own park). Opened only with no
   command under way and the secondary idle - else not now (-1): the main loop comes back at once. Closed with page 6 */
static int pk_sess;
static int pk_open(void)
{
    if (pk_sess) return 1;
    if (MARS_SYS_COMM0 != 0) return 0;
    cdr_busy = 1;
    if (MARS_SYS_COMM0 != 0) { cdr_busy = 0; return 0; }
    ED_PARK = 1;
    for (int n = 0; !ED_PARKED && n < 200000; n++) for (volatile int k = 0; k < 4; k++) { }
    if (!ED_PARKED) { ED_PARK = 0; cdr_busy = 0; pk_busy++; return 0; }   /* (the secondary at its job: later) */
    pk_sess = 1;
    return 1;
}
static int pk_go(int p)                                       /* window 1 to page p (command 68, in the session), the cache purged */
{
    if (pk_page == p) return 1;
    MARS_SYS_COMM2 = (uint16_t)p; COMM12_16 = 0;
    const int ok = ed_cmd_wait(68) && COMM12_16 == 1;         /* (the listener's four word writes to CTRL4-7) */
    CacheControl(0x11);
    if (!ok) return 0;
    pk_page = p; pk_pages++;
    return 1;
}
static void pk_close(void)
{
    if (!pk_sess) return;
    if (pk_page != 6 && !pk_go(6)) eng_fail("PAGE 6 (COMMAND 68) REFUSED", 0x4EE8);   /* (the core's page back) */
    ED_PARK = 0;
    for (int n = 0; ED_PARKED && n < 4000000; n++) for (volatile int k = 0; k < 4; k++) { }
    cdr_busy = 0; pk_sess = 0;
}
static int pk_fetch(uint32_t f, uint32_t n)                   /* the pack's bytes [f, f + n) into pk_buf; 0 a page refused, -1 not now */
{
    if (f >= pk_lo && f + n <= pk_hi) return 1;
    uint32_t want = f + PK_BUF;
    if (want < f + n) want = f + n;
    pk_lo = f; pk_hi = f;
    uint8_t *d = pk_buf;
    while (pk_hi < want) {
        const uint32_t c = PK_FILE + pk_hi;                   /* (the cart offset) */
        uint32_t len = want - pk_hi;
        const uint8_t *src;
        if (c < 0x200000u) { if (c + len > 0x200000u) len = 0x200000u - c; src = (const uint8_t *)(CART_BASE + c); }
        else {
            const uint32_t pe = (c & ~0x1FFFFFu) + 0x200000u;
            if (c + len > pe) len = pe - c;
            if (!pk_open()) { pk_lo = 1; pk_hi = 0; return -1; }
            if (!pk_go((int)(c >> 21))) { pk_lo = 1; pk_hi = 0; return 0; }
            src = (const uint8_t *)(CART_BASE + 0x200000u + (c & 0x1FFFFFu));
        }
        for (uint32_t i = 0; i < len; i++) d[i] = src[i];     /* (bytes: an odd start is a 16-bit bus read each) */
        d += len; pk_hi += len;
    }
    return 1;
}
static int pk_lz4(const uint8_t *s, uint32_t n, uint8_t *out)  /* (tools/m3rt/trackpak.c's) */
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        const uint8_t t = s[i++]; uint32_t L = t >> 4;
        if (L == 15) { uint8_t x; do { if (i >= n) return -1; x = s[i++]; L += x; } while (x == 255); }
        if (i + L > n || o + L > 2048) return -1;
        for (uint32_t j = 0; j < L; j++) out[o++] = s[i++];
        if (i >= n) break;
        if (i + 2 > n) return -1;
        const uint32_t off = s[i] | (uint32_t)s[i + 1] << 8; i += 2;
        uint32_t m = (t & 15) + 4;
        if ((t & 15) == 15) { uint8_t x; do { if (i >= n) return -1; x = s[i++]; m += x; } while (x == 255); }
        if (off == 0 || off > o || o + m > 2048) return -1;
        for (uint32_t j = 0; j < m; j++, o++) out[o] = out[o - off];
    }
    return (int)o;
}
static int pk_sector(uint32_t s, uint8_t *out)               /* track 2's sector s from the pack; 1 checked, 0 bad, -1 not now */
{
    if (s >= pk_n) return 0;
    const uint32_t e = pk_be32(0x14 + 4 * s), u = e & 0xFFFFFFu;
    if (u >= pk_nu) return 0;
    const uint32_t ofs = 0x14 + 4 * pk_n;
    const uint32_t a = pk_be32(ofs + 4 * u), b = pk_be32(ofs + 4 * u + 4);
    if (b <= a || b - a > 4096) return 0;
    { const int r = pk_fetch(pk_data + a, b - a); if (r <= 0) return r; }
    const uint8_t *p = pk_buf + (pk_data + a - pk_lo);
    if (e & 0x80000000u) { if (b - a != 2048) return 0; for (int i = 0; i < 2048; i++) out[i] = p[i]; }
    else if (pk_lz4(p, b - a, out) != 2048) return 0;
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < 2048; i++) h = (h ^ out[i]) * 0x01000193u;
    return h == pk_be32(0x14 + 4 * pk_n + 4 * (pk_nu + 1) + 4 * s);
}
static void eng_feed(void)
{
    if (!(ENG_R(ENG_W7, 0x40004) & 2)) return;                 /* (no sector wanted, or the port has it) */
    const uint32_t lba = (uint32_t)(ENG_R(ENG_W7, 0x40002) & 0x1F) << 16 | ENG_R(ENG_W7, 0x40000);
    eng_want = lba; eng_wants++;
    uint32_t src = lba;                                       /* (the sector whose bytes go: the one wanted, but the stage select's) */
#ifdef GOT_MENU
    if (gm_mac == 30 && lba == W_GAME) gm_mgo(31, eng_last);  /* (docs/58: the game's start asked - the block's turn next) */
    if (gm_mac == 31) {
        if (lba == W_BRIEF && !gm_bset) { gm_poke(SP_STAGE_AT, (uint32_t)(gm_stage - 1)); gm_bset = 1; }   /* (the block's read begun) */
        if (gm_stage >= 2 && lba >= W_BRIEF_OWN && lba < W_BRIEF_OWN + 64u) src = W_BRIEF_N + 724u * (gm_stage - 2u) + (lba - W_BRIEF_OWN);
        else if (lba >= W_STAGE + 724u * (gm_stage - 1u) + 590u && lba < W_STAGE + 724u * (gm_stage - 1u) + 596u) gm_mgo(32, eng_last);
    }
#endif
    if (src != eng_ready) {
        const uint32_t s = src - PK_LBA0;
        const int r = src < PK_LBA0 ? 0 : pk_sector(s, eng_sec);
        pk_close();                                           /* (page 6 again, the secondary and pace_vbi let go) */
        if (r < 0) return;                                    /* (not now: the next pass) */
        if (!r) {                                             /* (not track 2's, or the pack failed: zeros, counted) */
            for (int i = 0; i < 2048; i++) eng_sec[i] = 0;
            eng_feed_fail++; if (src >= PK_LBA0 && s < pk_n) pk_bad++;
        }
        eng_ready = src;
    }
    (void)ENG_R(ENG_W7, 0x47000);                             /* the sector's start: the core latches the LBA it wants now */
    const uint32_t again = (uint32_t)(ENG_R(ENG_W7, 0x40002) & 0x1F) << 16 | ENG_R(ENG_W7, 0x40000);
    if (again != lba) return;                                 /* (a seek in between: the next pass gives the new one) */
    const volatile uint16_t *sp = (const volatile uint16_t *)(ENG_W7 + 0x46000u);
    for (int i = 0; i < 2048; i++) (void)sp[eng_sec[i]];      /* + 0x46000 + (byte << 1): the byte, in order */
    (void)ENG_R(ENG_W7, 0x47800);                             /* the sector's end: the CD unit may take it */
    eng_fed++;
}
static void eng_pad(void)                                     /* the pad: Mednafen's order, a read at + 0x48000 + (b << 4) */
{
    /* docs/51: on the console the 68000 is still on the last picture's list at nearly every picture of the game, so the
       pad's command was skipped for good (docs/107 skipped it, never waited): the pad dead from the first music on.
       Now waited for, up to two vertical blanks; past that the last word read is sent again */
    static uint16_t p_last;
    /* docs/53: the wait itself with cdr_busy clear - pace_vbi returns at once while it is set, and a swap's blank that
       fell into the wait lost its colours and its commit for a frame (the title's flame band torn about once a second) */
    for (uint32_t t0 = gTicks; ; ) {
        while (MARS_SYS_COMM0 != 0 && gTicks - t0 < 2) { LT_POLL(); }
        cdr_busy = 1;
        if (MARS_SYS_COMM0 == 0 || gTicks - t0 >= 2) break;   /* (else the blank's commit came in between: again) */
        cdr_busy = 0;
    }
    uint16_t p = p_last;
    if (MARS_SYS_COMM0 != 0) {
#ifdef DX_PROF
        pf_skip++;
#endif
#ifdef DX_PADDIAG
        pd_skip++;
#endif
        got_padskip++; CRUMB_M(0x07);
    } else {
        p = cd_pad(0, 0); p_last = p;
        got_padw = p; got_padw_or |= p; got_tick = MARS_SYS_COMM10;   /* (docs/47: the word's bits ever seen; the 68000's vblank count, cmd 48's COMM10) */
#ifdef DX_PADDIAG
        pd_reads++; pd_raw |= p; pd_tick = MARS_SYS_COMM10;
#endif
    }
    cdr_busy = 0;
    uint32_t b = 0;
    if (p & SEGA_CTRL_C) b |= 1;                              /* I (jump) */
    if (p & SEGA_CTRL_B) b |= 2;                              /* II (attack) */
    /* (docs/132: on the console the stack's 6-button read reports X / Y / Z / MODE bits never pressed - they were
       SELECT, and RUN came with SELECT: the PC Engine's reset pair, which the System Card doesn't take as RUN. The
       three-button pad's buttons only: A is SELECT, Start alone is RUN) */
    if (p & SEGA_CTRL_A) b |= 4;                              /* SELECT */
    if (p & SEGA_CTRL_START) b |= 8;                          /* RUN */
    if (p & SEGA_CTRL_UP) b |= 16;
    if (p & SEGA_CTRL_RIGHT) b |= 32;
    if (p & SEGA_CTRL_DOWN) b |= 64;
    if (p & SEGA_CTRL_LEFT) b |= 128;
#ifdef GOT_AUTOSTART
    {   /* (docs/50: the console's pad gives no button (docs/46-48) - while the game's intro / title / game-over music is
           the last track asked (3-6), RUN for 12 of the 68000's vblanks (command 48's count) every 252, the first 240 after it began; off once a stage's
           or a boss's track (7-17) is asked, so never a pause in play. The pad's own buttons still pass) */
        static uint16_t t0; static int on, seen; const uint16_t now = MARS_SYS_COMM10;
        if (eng_tlast && (p & 0x0FFF)) seen = 1;               /* (docs/51: the pad works - no more RUN from here) */
        if (!seen && eng_tlast >= 4 && eng_tlast <= 6) {       /* (docs/51: not track 3, the intro) */
            if (!on) { on = 1; t0 = now; }
            const uint16_t d = (uint16_t)(now - t0);
            if (d >= 240) { if (d < 252) b |= 8u; else t0 = now; }
        } else on = 0;
    }
#endif
#ifdef DX_AUTORUN
    {   /* (docs/133: the console test without a hand on the pad - RUN alone for 10 reads every 200, from read 300 on;
           the pad itself is ignored) */
        static uint32_t ar_n; ar_n++;
        b = (ar_n >= 300 && ar_n % 200 < 10) ? 8u : 0u;
    }
#endif
#ifdef GOT_MENU
    b = gm_pad(p, b, eng_last);                               /* (docs/56: Start's tap and hold, the menu, the stage select) */
#endif
    (void)ENG_R(ENG_W7, 0x48000 + (b << 4));
#ifdef DX_PADDIAG
    pd_sent |= (uint16_t)b;
#endif
}
/* docs/114: the back sprites' palettes (main.c, spr_thr): each palette a back sprite on screen uses is shown through
   (bit 15) - in place if no front sprite on screen uses it, else in a palette none uses (the last picture's choice
   kept while it is free: the sprites' attributes unchanged, nothing redrawn); none free - drawn in front, as before */
#ifdef BG2_X32
#include "bgx.c"
#ifndef BGX_ROOM
#define BGX_ROOM 8
#endif
void spr_clear(uint8_t *row, uint32_t words, uint32_t rows);  /* (spr_rows.s: 16-bit zeros, in the normal image) */
/* docs/60: the background palettes the 32X draws this picture (bg2_x32_draw - the secondary's, read past the cache)
   each given a 32X palette no sprite uses now (bgx_taken: the sprites' and the back sprites' copies, spr_prio) - the
   one it had while that is free. None free: not drawn this picture. Called at pic_extras, after the blank */
/* docs/61: the pictures in which this read came before the secondary's choice - the choice's count (bg2_x32_seq) as
   it was before the job was given (seq0), at the read (seq1), and when the job is over (the next picture's seq0): a
   choice was made after the read if seq1 is still seq0 and the job's end is not. (A picture whose job makes no
   choice - the background off - counts nothing.) */
static uint32_t bgx_seq0, bgx_seq1, bgx_early;
static void bgx_assign(void)
{
    static uint8_t was[16];
    const uint16_t set = *(volatile uint16_t *)((uintptr_t)&bg2_x32_draw | 0x20000000);
    bgx_seq1 = *(volatile uint32_t *)((uintptr_t)&bg2_x32_seq | 0x20000000);   /* (as read; judged at the next picture, bgx_slots) */
    uint16_t got = 0, tk = bgx_taken;
    for (int q = 0; q < 16; q++) bgx_q[q] = 0;
    for (int p = 0; p < 16; p++) {
        if (!(set >> p & 1)) { was[p] = 0; continue; }
        int q = was[p] ? was[p] - 1 : -1;
        if (q < 0 || (tk >> q & 1)) { q = -1; for (int k = 0; k < 16; k++) if (!(tk >> k & 1)) { q = k; break; } }
        if (q < 0) { was[p] = 0; continue; }
        tk |= (uint16_t)(1u << q); was[p] = (uint8_t)(q + 1); bgx_base[p] = (uint8_t)(q << 4); bgx_q[q] = (uint8_t)(p + 1);
        got |= (uint16_t)(1u << p);
    }
    bgx_set = got;
}
/* the sprites' palettes this picture, and the room told to rt_bg2.c for its choice (before the picture's job is
   given): the 32X palettes free of sprites - by their most in the last pictures, falling one every 64 - less a spare,
   BGX_ROOM at most; none with the background off */
static void bgx_slots(uint16_t taken, int bg_on)
{
    static uint8_t peak, fall;
    {   const uint32_t now = *(volatile uint32_t *)((uintptr_t)&bg2_x32_seq | 0x20000000);   /* (the job before is over) */
        if (bgx_seq1 == bgx_seq0 && now != bgx_seq0) bgx_early++;
        bgx_seq0 = bgx_seq1 = now; }                          /* (before this picture's job is given) */
    bgx_taken = bg_on ? taken : 0xFFFF;
    int n = 0; for (int q = 0; q < 16; q++) n += taken >> q & 1;
    if (n >= peak) { peak = (uint8_t)n; fall = 0; } else if (++fall >= 64) { fall = 0; peak--; }
    const int room = 15 - peak;
    bg2_x32_room = room > BGX_ROOM ? BGX_ROOM : room < 0 ? 0 : room;
}
#endif
static void spr_prio(void)
{
    static uint8_t was[16];                                   /* (palette p's back copy last picture: q + 1, 0 none) */
#ifdef BG2_X32
    if (!(eng_v.t.CR & 0x80)) bgx_slots(0, 0);                /* (the background off: nothing of it drawn) */
#endif
    if (!(eng_v.t.CR & 0x80)) { spr_thr = 0; for (int p = 0; p < 16; p++) was[p] = 0; return; }   /* (docs/115: the
                                                     background off - all of it colour 0: the back sprites show) */
    uint16_t front = 0, back = 0;
    for (int i = 0; i < 64; i++) {
        const uint16_t *s = eng_v.sat + 4 * i; const int y = (s[0] & 0x3FF) - 64, x = (s[1] & 0x3FF) - 32;
        if (y >= 240 || y <= -64 || x >= 256 || x <= -32) continue;
        if (s[3] & 0x80) front |= (uint16_t)(1u << (s[3] & 15)); else back |= (uint16_t)(1u << (s[3] & 15));
    }
    uint8_t to[16]; uint16_t taken = front | back, thr = 0;
    for (int p = 0; p < 16; p++) {
        to[p] = (uint8_t)p;
        if (!(back >> p & 1)) { was[p] = 0; continue; }
        if (!(front >> p & 1)) { to[p] = (uint8_t)p; thr |= (uint16_t)(1u << p); spr_srcp[p] = (uint8_t)p; was[p] = (uint8_t)(p + 1); continue; }
        int q = was[p] ? was[p] - 1 : -1;
        if (q < 0 || q == p || (taken >> q & 1)) { q = -1; for (int k = 15; k >= 0; k--) if (!(taken >> k & 1)) { q = k; break; } }
        if (q < 0) { was[p] = 0; continue; }                  /* (no palette free: in front) */
        taken |= (uint16_t)(1u << q); thr |= (uint16_t)(1u << q); spr_srcp[q] = (uint8_t)p; to[p] = (uint8_t)q; was[p] = (uint8_t)(q + 1);
    }
    spr_thr = thr;
#ifdef BG2_X32
    bgx_slots(taken, 1);
#endif
    for (int i = 0; i < 64; i++) { uint16_t *s = eng_v.sat + 4 * i;
        if (!(s[3] & 0x80) && to[s[3] & 15] != (s[3] & 15)) s[3] = (uint16_t)((s[3] & ~15) | to[s[3] & 15]); }
}
/* the picture the engine has now: VRAM's changed blocks (each block's stamp: the frame of its last write), the colours,
   the sprite table, the snapshot's registers; the stamps as the sprite cells' tags (VSRC_CODE | the frame) */
static void eng_picture(uint32_t done)
{
    /* docs/106: the engine goes on while this runs - a block written in the frame under way (its stamp past done, the
       last whole frame) may be written again in it, under the same stamp: so its copy isn't kept as that stamp's (it is
       copied again at the next picture), and its tag is one the stamp's own never is */
    const uint32_t sb = ENG_W6 + 0x20000;
    for (int k = 0; k < 512; k++) {
        const uint16_t s = ENG_R(sb, 2 * k);
        if (s == eng_stamp[k]) continue;
        const int under_way = (int16_t)(s - (uint16_t)done) > 0;
        eng_stamp[k] = under_way ? (uint16_t)(s ^ 0x8000) : s;
        const volatile uint16_t *src = (const volatile uint16_t *)(ENG_W6 + 128u * k);
        uint16_t *d = eng_v.vram + 64 * k;
        for (int i = 0; i < 64; i++) d[i] = src[i];
        const uint16_t tag = (uint16_t)(VSRC_CODE | ((under_way ? s + 0x4000 : s) & 0x7FFF));
        for (int u = 0; u < 4; u++) rt_marks.v[(4 * k + u) & 2047] = tag;   /* (a cell, 4 units of 16 words) */
    }
    for (int i = 0; i < 512; i++) eng_v.color[i] = ENG_R(ENG_W6, 0x20800 + 2 * i);
    eng_v.BXR = ENG_R(ENG_W6, 0x20E00); eng_v.BYR = ENG_R(ENG_W6, 0x20E02); eng_v.MWR = ENG_R(ENG_W6, 0x20E04);
    eng_v.t.CR = ENG_R(ENG_W6, 0x20E06);
    /* docs/109: the sprites off (CR bit 6: the game's blank screens) - none drawn. (The background off has no such
       way: the Genesis' display off keeps its blank flag up, and the 68000's list waits for it to fall) */
    if (ENG_SPR_ON(eng_v.t.CR)) for (int i = 0; i < 256; i++) eng_v.sat[i] = ENG_R(ENG_W6, 0x20C00 + 2 * i);
    else for (int i = 0; i < 256; i++) eng_v.sat[i] = 0;
    spr_prio();
}
/* docs/50: the console showed blocks of the picture wrong and staying wrong (the intro's planet): a block is copied
   only when its stamp changes, so one wrong read of the cart's window keeps it wrong. Each picture ENG_VFY blocks in
   turn are read again; one that differs from the copy (its stamp unchanged) is read a third time, and if the two new
   reads agree it is taken and its units given a new tag (the drawing's caches convert it again) */
#ifndef ENG_VFY
#define ENG_VFY 8
#endif
static void eng_verify(void)
{
    static int vk; static uint16_t vtag = 0x6000;
    static uint16_t r1[64];
    const uint32_t sb = ENG_W6 + 0x20000;
    for (int n = 0; n < ENG_VFY; n++) {
        const int k = vk; vk = (vk + 1) & 511;
        if (ENG_R(sb, 2 * k) != eng_stamp[k]) continue;        /* (a change under way: the copy's own path takes it) */
        const volatile uint16_t *src = (const volatile uint16_t *)(ENG_W6 + 128u * k);
        uint16_t *d = eng_v.vram + 64 * k; int diff = 0;
        for (int i = 0; i < 64; i++) { r1[i] = src[i]; diff |= r1[i] != d[i]; }
        if (!diff) continue;
        for (int i = 0; i < 64; i++) if (src[i] != r1[i]) { diff = 0; break; }
        if (!diff || ENG_R(sb, 2 * k) != eng_stamp[k]) continue;
        for (int i = 0; i < 64; i++) d[i] = r1[i];
        vtag = (uint16_t)(vtag + 1);
        const uint16_t tag = (uint16_t)(VSRC_CODE | (vtag & 0x7FFF));
        for (int u = 0; u < 4; u++) rt_marks.v[(4 * k + u) & 2047] = tag;
        eng_fixn++;
    }
}
/* docs/103: the CD audio - the engine's drive plays a track (register words 3-4: playing, the play's mode, its start
   sector); the Sega CD plays the disc's own copy of it (tools/eng_music.py: the game's track n is disc track n, its 1 is
   2), once or repeating as the play's mode says, by command 45; command 54 when the engine's stops. The tracks from the
   load port's image (still in the cart's resident slot: FwState word 16 + 4n, track n's first sector) */
/* docs/105: the scroll a line - the window's line table (+ 0x21000: each display line's background X, + 0x21200: its
   Y, as the engine's VDC took them): X into the H scroll table a line (rt_bg2.c BG2_LINES), Y's bands - where Y minus
   the line changes - into command 78's list (VSPLIT: the Genesis H interrupt changes V there), sent when they change */
static uint16_t eng_lines[224 * 2];
static uint32_t eng_vs[16]; static int eng_nvs = -1;          /* (the bands as sent: line << 20 | V A << 10 | V B) */
static uint32_t eng_vs_sent;
#ifdef BG2_X32
/* docs/60: under the sprites (rt_spr.c's SPR_UNDER, between its clearing and its drawing) - the cells of the
   background's palettes the 32X shows. Each picture: the squares this buffer's last drawing of them is in are
   cleared (16 lines of 16 or 32 pixels at a time), this picture's cells drawn (bgx.c), and the squares of both marked
   changed - the sprites over them are drawn again, on top. */
static uint16_t bgx_sq[2][14];
uint32_t bgx_cells;                                           /* (the cells of the last picture: for the log) */
void spr_under(const V32 *v, int buf, const SprBuf *b, uint16_t *dirty)
{
    uint16_t *sq = bgx_sq[buf & 1];
    for (int r = 0; r < 14; r++) { const unsigned m = sq[r]; if (!m) continue;
        dirty[r] |= (uint16_t)m; sq[r] = 0;
        uint8_t *row = b->nrm + 16 * r * SPR_LINE;
        for (int c = 0; c < 16; c++) if (m >> c & 1) {
            if (c < 15 && (m >> (c + 1) & 1)) { spr_clear(row + 16 * c, 16, 16); c++; }
            else spr_clear(row + 16 * c, 8, 16); }
    }
    if (!bgx_set || !(v->t.CR & 0x80)) { bgx_cells = 0; return; }
    bgx_cells = (uint32_t)bgx_draw(v->vram, v->MWR, eng_lines, bgx_set, bgx_base, b->ovl, sq);
    for (int r = 0; r < 14; r++) dirty[r] |= sq[r];
}
/* docs/60: after the sprites - a sprite behind the background (its palette shown through, spr_thr) that lies over
   cells the 32X draws: the cells' pixels back over it, in its box (bgx.c bgx_over) */
static void bgx_after(const V32 *v, int buf)
{
    if (!bgx_set || !spr_thr || !(v->t.CR & 0x80)) return;
    for (int i = 0; i < 64; i++) {
        const uint16_t *s = v->sat + 4 * i; const unsigned at = s[3];
        if ((at & 0x80) || !(spr_thr >> (at & 15) & 1)) continue;
        const int sy = (s[0] & 0x3FF) - 64, sx = (s[1] & 0x3FF) - 32;
        const int w = at & 0x100 ? 32 : 16, hc = (int)(at >> 12) & 3, h = hc == 0 ? 16 : hc == 1 ? 32 : 64;
        const int x0 = sx < 0 ? 0 : sx, y0 = sy < 0 ? 0 : sy, x1 = sx + w > 256 ? 256 : sx + w, y1 = sy + h > 224 ? 224 : sy + h;
        if (x0 >= x1 || y0 >= y1) continue;
        {   /* (docs/61: only a sprite whose box touches a square this buffer's cells are in - most pictures of stages
               1, 3, 6 and 7 have sprites behind the background, few of them over such cells) */
            const uint16_t *sq = bgx_sq[buf & 1]; unsigned hit = 0;
            const unsigned m = (0xFFFFu >> (15 - ((x1 - 1) >> 4))) & (0xFFFFu << (x0 >> 4));
            for (int r = y0 >> 4; r <= (y1 - 1) >> 4; r++) hit |= sq[r] & m;
            if (!hit) continue;
        }
        bgx_over(v->vram, v->MWR, eng_lines, bgx_set, bgx_base, spr_buf[buf & 1].nrm, spr_thr, x0, y0, x1, y1);
    }
}
#endif
static void eng_scroll_bands(void);
static void eng_scroll(void)
{
    for (int l = 0; l < 224; l++) { eng_lines[2 * l] = ENG_R(ENG_W6, 0x21000 + 2 * l); eng_lines[2 * l + 1] = ENG_R(ENG_W6, 0x21200 + 2 * l); }
    eng_scroll_bands();
}
#ifdef SPR_YLIM
extern int spr_ylim;
/* docs/67: where the game's status bar starts - sprites are off from there. Its band shows BAT row 234 (Y 234; 235 when
   the screen shakes) from line 192 down; the game turns sprites off at its split (CR $8C): over the recording (sapm_pc
   SPLIT_LOG, 75,000 frames) 61,695 + 386 frames turn them off at line 203 with Y 234 / 235 there and nowhere else, but
   a 600-frame curtain before stage 4 and 13 single frames */
static int spr_hud_line(void)
{
    for (int l = 192; l < 224; l++) {
        const unsigned y = eng_lines[2 * l + 1] & 0x1FF;
        if ((y == 234 || y == 235) && ((eng_lines[2 * l + 1] - l) & 0x1FF) != ((eng_lines[2 * l - 1] - (l - 1)) & 0x1FF)) return l;
    }
    return 224;
}
#endif
static void eng_scroll_bands(void)
{
#ifdef SPR_YLIM
    {   const int y = spr_hud_line();
        if (y != spr_ylim) { spr_ylim = y; gm_wipe_n = 2; }   /* (a change: both buffers drawn again whole - the old boxes below the
                                                                 cut are not cleared by the sprite drawing) */
    }
#endif
#ifndef BG2_DRAW
    return;                                                   /* (v05, docs/128: no Genesis plane, no V splits - and no
                                                                 command 78 in a stack built without VSPLIT: unanswered,
                                                                 it held COMM0 and eng_pad never read the pad) */
#endif
    uint32_t e[16]; int n = 0; unsigned last = 0xFFFF;
    for (int l = 0; l < 224 && n < 16; l++) {
        const unsigned v = (unsigned)(eng_lines[2 * l + 1] - l) & 0x3FF;
        if (v != last) { e[n++] = (uint32_t)l << 20 | v << 10 | v; last = v; }
    }
    if (n == 1) n = 0;                                        /* (one band: the kept part's V alone, no splits) */
#ifdef BG2_MORE
    bg2_run_max = n ? GOT_RUN_MAX : 0xFFF;                              /* (docs/57: a picture with V splits - the list in runs of 8 words, so the
                                                                 68000 lets the splits' H interrupt in within a line) */
#endif
    int same = n == eng_nvs;
    for (int i = 0; same && i < n; i++) same = e[i] == eng_vs[i];
    if (same) return;
    cdr_busy = 1;
    for (int k = 0; k < 4000000 && MARS_SYS_COMM0 != 0; k++) { LT_POLL(); }
    for (int i = 0; i < n; i++) { MARS_SYS_COMM2 = (uint16_t)i; COMM8_32 = e[i]; cd_cmd(78); eng_vs[i] = e[i]; }
    MARS_SYS_COMM2 = (uint16_t)(0x8000 | n); cd_cmd(78);
    cdr_busy = 0;
    eng_nvs = n; eng_vs_sent++;
}
/* docs/122: the next picture read during the master's wait for the secondary (draw_hook) - into staging, not eng_v,
   which the secondary is reading: the changed blocks (up to ENG_STK; the rest read the old way after), the colours,
   the sprite table, the registers and the line table, of the newest whole frame the engine has then. Applied to eng_v
   (SDRAM to SDRAM) once the secondary is done: the cart reads off the secondary's idle time */
#define ENG_STK 40
static struct {
    uint32_t f; int ok, nb, more;
    uint16_t blk[ENG_STK], st[ENG_STK], data[ENG_STK][64];
    uint16_t color[512], sat[256], r[4], lines[224 * 2];
} eng_stg;
static uint32_t eng_stg_used, eng_stg_more;
static void eng_prefetch(void)
{
    eng_stg.ok = 0;
    const uint32_t f = ENG_R(ENG_W6, 0x20E08);
    if (f == eng_last) return;                                /* (no newer frame yet) */
    eng_stg.r[0] = ENG_R(ENG_W6, 0x20E00); eng_stg.r[1] = ENG_R(ENG_W6, 0x20E02); eng_stg.r[2] = ENG_R(ENG_W6, 0x20E04);
    eng_stg.r[3] = ENG_R(ENG_W6, 0x20E06);
    for (int l = 0; l < 224; l++) { eng_stg.lines[2 * l] = ENG_R(ENG_W6, 0x21000 + 2 * l); eng_stg.lines[2 * l + 1] = ENG_R(ENG_W6, 0x21200 + 2 * l); }
    if (ENG_R(ENG_W6, 0x20E08) != f) return;                  /* (the frame ended meanwhile: the snapshot not one frame's) */
    const uint32_t sb = ENG_W6 + 0x20000; int nb = 0, more = 0;
    for (int k = 0; k < 512; k++) {
        const uint16_t s = ENG_R(sb, 2 * k);
        if (s == eng_stamp[k]) continue;
        if (nb == ENG_STK) { more = 1; break; }
        eng_stg.blk[nb] = (uint16_t)k; eng_stg.st[nb] = s;
        const volatile uint16_t *src = (const volatile uint16_t *)(ENG_W6 + 128u * k);
        for (int i = 0; i < 64; i++) eng_stg.data[nb][i] = src[i];
        nb++;
    }
    for (int i = 0; i < 512; i++) eng_stg.color[i] = ENG_R(ENG_W6, 0x20800 + 2 * i);
    if (ENG_SPR_ON(eng_stg.r[3])) for (int i = 0; i < 256; i++) eng_stg.sat[i] = ENG_R(ENG_W6, 0x20C00 + 2 * i);
    eng_stg.f = f; eng_stg.nb = nb; eng_stg.more = more; eng_stg.ok = 1;
}
static void eng_apply(void)                                   /* (eng_picture and eng_scroll, from the staging) */
{
    const uint32_t done = eng_stg.f;
    for (int n = 0; n < eng_stg.nb; n++) {
        const int k = eng_stg.blk[n]; const uint16_t s = eng_stg.st[n];
        const int under_way = (int16_t)(s - (uint16_t)done) > 0;
        eng_stamp[k] = under_way ? (uint16_t)(s ^ 0x8000) : s;
        uint32_t *d = (uint32_t *)(eng_v.vram + 64 * k); const uint32_t *q = (const uint32_t *)eng_stg.data[n];
        for (int i = 0; i < 32; i++) d[i] = q[i];
        const uint16_t tag = (uint16_t)(VSRC_CODE | ((under_way ? s + 0x4000 : s) & 0x7FFF));
        for (int u = 0; u < 4; u++) rt_marks.v[(4 * k + u) & 2047] = tag;
    }
    if (eng_stg.more) {                                       /* (more blocks than the staging: the rest the old way) */
        eng_stg_more++;
        const uint32_t sb = ENG_W6 + 0x20000;
        for (int k = 0; k < 512; k++) {
            const uint16_t s = ENG_R(sb, 2 * k);
            if (s == eng_stamp[k]) continue;
            const int under_way = (int16_t)(s - (uint16_t)done) > 0;
            eng_stamp[k] = under_way ? (uint16_t)(s ^ 0x8000) : s;
            const volatile uint16_t *src = (const volatile uint16_t *)(ENG_W6 + 128u * k);
            uint16_t *d = eng_v.vram + 64 * k;
            for (int i = 0; i < 64; i++) d[i] = src[i];
            const uint16_t tag = (uint16_t)(VSRC_CODE | ((under_way ? s + 0x4000 : s) & 0x7FFF));
            for (int u = 0; u < 4; u++) rt_marks.v[(4 * k + u) & 2047] = tag;
        }
    }
    for (int i = 0; i < 512; i++) eng_v.color[i] = eng_stg.color[i];
    eng_v.BXR = eng_stg.r[0]; eng_v.BYR = eng_stg.r[1]; eng_v.MWR = eng_stg.r[2]; eng_v.t.CR = eng_stg.r[3];
    if (ENG_SPR_ON(eng_v.t.CR)) for (int i = 0; i < 256; i++) eng_v.sat[i] = eng_stg.sat[i];
    else for (int i = 0; i < 256; i++) eng_v.sat[i] = 0;
    spr_prio();
    for (int i = 0; i < 224 * 2; i++) eng_lines[i] = eng_stg.lines[i];
    eng_scroll_bands();
    eng_stg.ok = 0; eng_stg_used++;
}
static int eng_track_of(uint32_t lba)
{
    const volatile uint32_t *w = (const volatile uint32_t *)(CART_BASE + 0x080000u);
    const int first = (int)w[12], last = (int)w[13]; int t = 0;
    for (int n = first; n <= last; n++) if (w[16 + 4 * n] <= lba) t = n;
    return t;
}
static void eng_audio(void)
{
    const uint32_t w3 = ENG_R(ENG_W7, 0x40006), w4 = ENG_R(ENG_W7, 0x40008);
    const uint32_t a = (w3 & 0x8000 ? 1u << 31 : 0) | (w3 >> 13 & 3) << 24 | (w3 & 0x1F) << 16 | w4;
    if (a == eng_aud) return;
    if (a >> 31) eng_tlast = eng_track_of(a & 0x1FFFFF);       /* (docs/50: for GOT_AUTOSTART) */
#ifndef GOT_AUDIO
    eng_aud = a; return;                                      /* (GoT: the disc has no CD audio tracks yet) */
#endif
    eng_audio_seen++;
    const int was = eng_aud >> 31, now = a >> 31;
    eng_aud = a;
    cdr_busy = 1;
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { LT_POLL(); }
    if (now) {
        const int t = eng_track_of(a & 0x1FFFFF), disc = t == 1 ? 2 : t;
        if (disc >= 2) { MARS_SYS_COMM2 = (uint16_t)(disc | ((a >> 24 & 3) == 3 ? 0 : 0x8000));
            if (!cd_cmd(45)) { cd_answer_reset(); eng_cdfail++; eng_cdlast = 0x4500; } eng_plays++; eng_playing = 1;
#ifdef SAP_CDFADE
            mus_sent = 0x400;                                 /* (docs/67: 45 starts the track at full volume - the menu's level or the
                                                                 game's fade-out put back; cdr_busy stays set through it) */
            mus_level(eng_fade_sent ? 0 : MUS_FULL(), 0); cdr_busy = 1;
#endif
        }
    } else if (was) { if (eng_playing && !cd_cmd(54)) { cd_answer_reset(); eng_cdfail++; eng_cdlast = 0x5400; } eng_playing = 0; }
    cdr_busy = 0;
}
#ifdef GOT_MENU
/* docs/58, the core's step 20: the engine put back into its reset and started again - the options menu's STAGE SELECT
   from inside a stage. The key 'GTRS' is the load port's word 0xFF, written as the start writes the table: through the
   frame buffer (the list's area, free once the picture's list has run - as pool_wb) and command 101, here 2 KB from the
   word itself (+ 0x413FC; what follows the word is past the port). Then the start read again. The core keeps its
   memories, its frame count and its sound log's pointer, so nothing of this program's copies is redone: the System
   Card's writes arrive as any picture's. */
static void eng_restart(uint32_t f)
{
    pal_wait(0x15);                                           /* (a swap asked for is shown first, as pool_wb) */
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
    bg2_take();                                               /* (a list or a commit under way: its end; the frame buffer) */
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
    volatile uint16_t *d = (volatile uint16_t *)(0x24000000u + BG2_LIST);
    d[0] = 0x4754; d[1] = 0x5253; for (int i = 2; i < 1024; i++) d[i] = 0;
    volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
    *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }          /* (the frame buffer to the 68000) */
    MARS_SYS_COMM2 = (uint16_t)(BG2_LIST >> 4); COMM8_32 = ENG_W7_CART + 0x413FCu; COMM12_16 = 0xFFFF;
    const int ok = ed_cmd(101) && COMM12_16 == 0;
    *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }       /* (and back) */
    cdr_busy = 0;
    if (!ok) { gm_mac = 0; gm_banner = 0; gm_wipe_n = 2; return; }
    for (volatile int n = 0; n < 20000; n++) { }              /* (the reset held a moment) */
#ifdef GOT_SND
    SND_RST = 1;
#endif
    (void)ENG_R(ENG_W7, 0x40080);                             /* the start, again */
    gm_mgo(21, f);
}
#endif
#ifdef DX_CONDIAG
/* docs/125: the console diagnostic (RTX=-DDX_CONDIAG) - each start step a line on the 32X screen, in both frame buffers
   (so the swaps don't lose it), and while no picture comes the engine's state every second: what the cart answers
   (README-FPGA-FINDINGS rule 13). Lines from y = 40, 9 apart; x from 8 */
static void cd_both(int y, const char *s, uint8_t c)
{
    for (int k = 0; k < 2; k++) { text(8, y, "                                ", c); text(8, y, s, c); show(); }
}
static int cdg_row;
#ifdef DX_LOADPIC
/* docs/168: the loading picture (the stack's boot picture, IMAGE.RAW, left on the screen by CDLOADER) stays until the
   engine's first picture - the start's lines are not printed over it */
static void cdg(const char *s) { (void)s; (void)cdg_row; }
#else
static void cdg(const char *s) { cd_both(40 + 9 * cdg_row, s, C_GREEN); if (cdg_row < 17) cdg_row++; }
#endif
static void cdg_hex4(char *d, uint32_t v) { for (int i = 0; i < 4; i++) d[i] = "0123456789ABCDEF"[(v >> (12 - 4 * i)) & 15]; d[4] = 0; }
static void cdg_state(void)                                   /* the engine's words, 4 lines at the bottom */
{
    /* (the first picture has put the game's colours in the 32X palette - all black while the engine has drawn
       nothing: the text in entries 0x10 / 0x20 / 0x30, which the pictures never set - pace_vbi skips i & 15 == 0) */
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    cram[0x10] = 0x7FFF; cram[0x20] = 0x03E0; cram[0x30] = 0x03FF;
    static uint32_t n; char b[48], *p;
    uint16_t w[8], r[4]; for (int i = 0; i < 8; i++) w[i] = ENG_R(ENG_W7, 0x40010 + 2 * i);
    for (int i = 0; i < 4; i++) r[i] = ENG_R(ENG_W7, 0x40000 + 2 * i);
    const uint16_t fr = ENG_R(ENG_W6, 0x20E08), cr = ENG_R(ENG_W6, 0x20E06);
    p = str(b, "PC "); cdg_hex4(p, w[0]); p += 4; p = str(p, " EDGES "); cdg_hex4(p, w[1]); p += 4; p = str(p, " ROMCHG "); cdg_hex4(p, w[2]);
#ifdef DX_NOFEED
    p += 4; p = str(p, " W "); cdg_hex4(p, eng_want);         /* (the sector the engine asked for last) */
#endif
    cd_both(184, b, 0x30);
    p = str(b, "FL "); cdg_hex4(p, w[3]); p += 4; p = str(p, " FRAME "); cdg_hex4(p, w[4]); p += 4; p = str(p, " ID "); cdg_hex4(p, w[7]); p += 4;
    p = str(p, " ON "); cdg_hex4(p, w[6]);
    cd_both(193, b, 0x30);
#ifdef DX_PADDIAG
    /* (docs/130: P the stack's pad words OR'd, B the buttons sent to the core OR'd, K reads, S skipped for COMM0) */
    (void)r; (void)cr;
    p = str(b, "P "); cdg_hex4(p, pd_raw); p += 4; p = str(p, " B "); cdg_hex4(p, pd_sent); p += 4;
    p = str(p, " K "); cdg_hex4(p, pd_reads); p += 4; p = str(p, " S "); cdg_hex4(p, pd_skip); p += 4;
    p = str(p, " SN "); cdg_hex4(p, fr);
#else
    p = str(b, "R0 "); cdg_hex4(p, r[0]); p += 4; p = str(p, " R1 "); cdg_hex4(p, r[1]); p += 4; p = str(p, " R2 "); cdg_hex4(p, r[2]); p += 4;
    p = str(p, " SN "); cdg_hex4(p, fr); p += 4; p = str(p, " CR "); cdg_hex4(p, cr);   /* (39 characters: x 8-319) */
#endif
    cd_both(202, b, 0x30);
    p = str(b, "FED "); p = dec(p, eng_fed); p = str(p, "/"); p = dec(p, eng_feed_fail);
    p = str(p, " PICS "); p = dec(p, eng_frames); p = str(p, " N "); p = dec(p, ++n);
#ifdef DX_NOFEED
    p = str(p, " WN "); p = dec(p, eng_wants);                /* (how often it asked) */
#endif
#ifdef DX_PADDIAG
    p = str(p, " T "); cdg_hex4(p, pd_tick);                  /* (docs/131: the 68000's vblanks - counting = its handler runs) */
#endif
    cd_both(211, b, 0x10);
    /* docs/169: the CD's side - A the engine's play as last seen (bit 15 playing, the mode), Y the Sega CD told to
       play, PL plays sent, CH chunks read, CF commands failed and the last (91xx / 4500 / 5400), RF not sent */
    p = str(b, "A "); cdg_hex4(p, eng_aud >> 16); p += 4; p = str(p, " Y "); p = dec(p, (uint32_t)eng_playing); p = str(p, " PL "); p = dec(p, eng_plays);
    p = str(p, " CH "); p = dec(p, eng_chunks); p = str(p, " CF "); p = dec(p, eng_cdfail); p = str(p, " "); cdg_hex4(p, eng_cdlast);
    cd_both(175, b, 0x10);
}
#ifndef BG2_DRAW
static void cdg_each_second(void) { static uint32_t t; if (gTicks - t > 60) { t = gTicks; cdg_state(); } }
#endif
#define CDG(s) cdg(s)
#else
#define CDG(s) do { } while (0)
#endif
static void eng_fail(const char *what, uint16_t code)
{
#ifdef DX_LOADPIC
    screen();                                                 /* (the loading picture off: the text's colours) */
#endif
    text(16, 16, what, C_RED); show(); cd_status(code); while (1) { }
}
static void eng_bram_check(void) { }                          /* (GoT: no backup RAM port on the cart yet, docs/36) */
uint16_t got_pk_flags;                                         /* (docs/40: 0x100 packed; 1/2/4/8 track A, B, the System Card, GOTFWTAB as packed) */
static uint32_t got_sum(uint32_t a, uint32_t n)               /* (docs/40: the big-endian longs added, as got_pack.py) */
{
    const volatile uint32_t *p = (const volatile uint32_t *)a; uint32_t t = 0;
    for (uint32_t i = 0; i < n / 4; i++) t += p[i];
    return t;
}
static void eng_main(void)
{
    cd_rv_hook = ed_cmd;                                      /* (docs/121: 46's chunks parked too) */
    cd_status(0x4E20);
#ifndef DX_LOADPIC
    screen();
    text(16, 16, "SAPPHIRE: STARTING THE CART", C_WHITE); show();
#endif
#if defined(DX_CONDIAG) && !defined(DX_LOADPIC)
    cd_both(16, "DXENG: STARTING (CONSOLE DIAG)", C_WHITE);
#endif
#ifdef DX_CONDIAG
    { char b[40], *p = str(b, "BEFORE KEY: ID "); cdg_hex4(p, ENG_R(ENG_W7, 0x4001E)); cdg(b); }   /* (RAM's word: off) */
#endif
    /* Sapphire docs/67: the packed launcher (tools/sap_pack.py) - the EverDrive's menu has loaded everything into its
       banks with the program: SAPFWTAB.BIN at cart 0x080000 (bank 1), the track's pack from 0x081000 (banks 1-13), the
       Arcade Card's 2 MB as zeros (banks 16-19: Mednafen's card starts at zero, and the core reads it from there), the
       System Card (bank 27). The table at the program's bank + 0x40000 (window 0): 'SA' 'PK', 1, then the sums (the
       big-endian longs added) of SAPFWTAB.BIN's 2 KB and of the System Card, the pack's length. Nothing comes from the
       disc but the music. */
    const volatile uint16_t *pk = (const volatile uint16_t *)(CART_BASE + 0x40000u);
#define PK_SUM(i) ((uint32_t)pk[3 + 2 * (i)] << 16 | pk[4 + 2 * (i)])
    if (!(pk[0] == 0x5341 && pk[1] == 0x504B && pk[2] == 1)) eng_fail("NOT THE PACKED LAUNCHER (SAP_PACK.PY)", 0x4EE0);
    got_pk_flags = 0x100;
    if (pk_be32(0) != 0x5350414Bu || pk_be32(4) != 1) eng_fail("THE TRACK'S PACK IS NOT AT 0X081000", 0x4EE0);
    pk_n = pk_be32(8); pk_nu = pk_be32(12); pk_data = pk_be32(16);
    if (pk_n != 7599 || pk_nu > pk_n || pk_data + pk_be32(0x14 + 4 * pk_n + 4 * pk_nu) != PK_SUM(2)) eng_fail("THE TRACK'S PACK: ITS HEAD IS WRONG", 0x4EE0);
    got_pk_flags |= 1; CDG("PACKED: THE TRACK'S PACK OK");
    if (!eng_page(4)) eng_fail("PAGE 4 (COMMAND 68) REFUSED", 0x4EE8);
    {   const volatile uint32_t *c = (const volatile uint32_t *)(CART_BASE + 0x200000u); uint32_t o = 0;
        for (uint32_t i = 0; i < 0x200000u / 4; i++) o |= c[i];
        if (o) eng_fail("THE ARCADE CARD'S 2 MB (BANKS 16-19) ARE NOT ZERO", 0x4EE0); }
    got_pk_flags |= 2; CDG("PAGE 4: THE ARCADE CARD'S 2 MB ZERO");
    if (!eng_page(6)) eng_fail("PAGE 6 (COMMAND 68) REFUSED", 0x4EE8);
    if (got_sum(CART_BASE + 0x380000u, 0x40000u) != PK_SUM(1)) eng_fail("THE SYSTEM CARD (BANK 27) IS NOT THE PACK'S", 0x4EE0);
    got_pk_flags |= 4; CDG("PAGE 6: THE SYSTEM CARD OK");
    if (got_sum(CART_BASE + 0x080000u, 0x800u) != PK_SUM(0)) eng_fail("SAPFWTAB.BIN (CART 0X080000) IS NOT THE PACK'S", 0x4EE0);
    got_pk_flags |= 8;
#undef PK_SUM
    CDG("SAPFWTAB.BIN OK");
    CRUMB_M(0x64);
#ifdef SAP_SDSAVE
    sv_load();                                                /* (docs/67: the settings and the high score from the SD card) */
#endif
    {   /* the load port: the image (2 KB, its first 1 KB the words) through the frame buffer, command 101 */
        const volatile uint16_t *s = (const volatile uint16_t *)(CART_BASE + 0x080000u);
        volatile uint16_t *d = (volatile uint16_t *)(0x24000000u + ENG_FB_TABLE);
        for (int i = 0; i < 1024; i++) d[i] = s[i];
        volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
        *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }      /* (the frame buffer to the 68000) */
        MARS_SYS_COMM2 = (uint16_t)(ENG_FB_TABLE >> 4); COMM8_32 = ENG_W7_CART + 0x41000u; COMM12_16 = 0xFFFF;
        const int ok = ed_cmd(101) && COMM12_16 == 0;
        *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }   /* (and back) */
        if (!ok) eng_fail("THE LOAD PORT (COMMAND 101) REFUSED", 0x4EE9);
        for (int n = 0; n < 1000000 && (ENG_R(ENG_W7, 0x40004) & 1); n++) { }   /* (its last word taken) */
    }
    CDG("LOAD PORT WRITTEN");
    CRUMB_M(0x65);
    CRUMB_M(0x66);
    (void)ENG_R(ENG_W7, 0x40080);                             /* the start */
#ifdef GOT_MENU
    gm_core = (uint8_t)(ENG_R(ENG_W7, 0x40004) >> 8);         /* (docs/56: step 19 on says its number here; 0 before) */
#endif
#ifdef GOT_SND
    SND_GO = 1;                                               /* (docs/54: the secondary starts the sound) */
#endif
#ifdef DX_CONDIAG
    { char b[40], *p = str(b, "STARTED: ON "); for (int n = 0; n < 100000; n++) { } cdg_hex4(p, ENG_R(ENG_W7, 0x4001C)); cdg(b); }
#endif
#ifdef DX_LOADPIC
    screen();                                                 /* (the loading picture off: the engine is started, its
                                                                 first picture is next - before the renderers' set-up,
                                                                 which screen() would undo) */
#endif
    CRUMB_M(0x67);
    cd_status(0x4E21);
    {   /* the renderers, as the play build sets them up - no ripped cells: every sprite cell converted from VRAM,
           cached by its stamp */
        for (int i = 0; i < 2048; i++) rt_marks.v[i] = VSRC_CODE;
        rt_marks.tag = rt_marks.code = VSRC_CODE;
        for (int k = 0; k < 512; k++) eng_stamp[k] = 0xFFFF;
        spr_init(0);
        spr_buf[0].ovl = (uint8_t *)0x24020000 + 0x200 + 32; spr_buf[0].nrm = (uint8_t *)0x24000000 + 0x200 + 32;
        spr_buf[0].tail = (uint8_t *)0x24000000 + 0x1D000;
        spr_buf[1] = spr_buf[0];
#ifdef BG2_DRAW
        bg2_setup();
        bg2_lines = eng_lines;                                /* (docs/105: the scroll a line) */
#endif
    }
    pace_last = gTicks;
#if defined(DX_CONDIAG) && !defined(BG2_DRAW)
    cdg_tick = cdg_each_second;
#endif
    for (;;) {
        LT_POLL();
#ifdef DX_CONDIAG
        CDG_BEAT++; CDG_WHERE = 1; CDG_SPIN(0x10);
#endif
        CRUMB_M(1);
        PF(0, eng_feed());
        const int stg = eng_stg.ok;                           /* (docs/122: read during the last picture's wait) */
        const uint32_t f = stg ? eng_stg.f : ENG_R(ENG_W6, 0x20E08);   /* the snapshot's frame: a new picture? */
#ifdef DX_CONDIAG
        {   /* (no picture for 2 s: the engine's state each second, the frame buffer taken back first) */
            static uint32_t t_pic, t_pr; if (!t_pic) t_pic = gTicks;
#ifdef BG2_DRAW
            if (f != eng_last) t_pic = gTicks;
            else if (gTicks - t_pic > 120 && gTicks - t_pr > 60) { t_pr = gTicks; bg2_take(); cdg_state(); }
#else
            /* (v05, docs/128: the 32X layer only - the state every second, pictures or not; only with the last swap
               shown and none asked for, so cd_both's two flips leave the screen where it was) */
            (void)t_pic;
            if (gTicks - t_pr > 60 && pal_go < 0 && (pace_req < 0 || (MARS_VDP_FBCTL & MARS_VDP_FS) == pace_req)) {
                t_pr = gTicks; cdg_state(); }
#endif
        }
#endif
#ifdef DX_PROF
        if (gTicks - pf_last >= 60 && eng_frames) {           /* (the line: as the state lines, the frame buffer taken back) */
            const uint32_t all = gTicks - pf_last, work = pf_t[0] + pf_t[1] + pf_t[2] + pf_t[3] + pf_t[4];
            char b[48], *p = str(b, "P"); p = dec(p, pf_pics); p = str(p, " E"); p = dec(p, (uint32_t)(f - pf_e0) & 0xFFFF);
            p = str(p, " W"); p = dec(p, all > work ? all - work : 0); p = str(p, " F"); p = dec(p, pf_t[0]);
            p = str(p, " K"); p = dec(p, pf_t[1]); p = str(p, " A"); p = dec(p, pf_t[2]); p = str(p, " I"); p = dec(p, pf_t[3]);
            p = str(p, " D"); p = dec(p, pf_t[4]); p = str(p, " X"); p = dec(p, pf_skip); str(p, "  ");
            for (char *q = b; *q; q++) if (*q == '0') *q = 'O';   /* (the font's zero reads as a B on a television) */
            volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM; cram[0x10] = 0x7FFF;
#ifdef BG2_DRAW
            bg2_take();
#endif
            cd_both(208, b, 0x10);
            pf_last = gTicks; pf_e0 = f; pf_pics = pf_skip = 0; for (int i = 0; i < 5; i++) pf_t[i] = 0;
        }
#endif
        if (f == eng_last) continue;
        eng_last = f;

#ifdef DX_CONDIAG
        CDG_WHERE = 2;
#endif
        CRUMB_M(2);
        PF(1, eng_pad());
#ifdef GOT_MENU
        sap_tick(f);                                          /* (Sapphire docs/67: the lives, the high score) */
#endif
#ifdef GOT_MENU
        if (gm_rs_req) { gm_rs_req = 0; eng_restart(f); }     /* (docs/58: the menu's restart) */
        gm_stats(f);                                          /* (docs/60: the STATUS page) */
#endif
#ifdef DX_CONDIAG
        CDG_WHERE = 3;
#endif
        CRUMB_M(3);
        PF(2, eng_audio());
#if defined(SAP_CDFADE) && defined(GOT_SND)
        eng_fade();                                           /* (docs/67: the game's own fades of its CD music) */
#endif
#ifdef DX_CONDIAG
        CDG_WHERE = 4;
#endif
        CRUMB_M(4);
        PF(3, if (stg) eng_apply(); else { eng_picture(f); eng_scroll(); } eng_verify());
#ifdef DX_CONDIAG
        CDG_WHERE = 5;
#endif
        CRUMB_M(5);
        PF(4, draw_hook(&eng_v));
#ifdef DX_PROF
        pf_pics++;
#endif
#ifdef DX_CONDIAG
        CDG_WHERE = 6;
#endif
        CRUMB_M(6);
        eng_frames++;
        {   /* (docs/41: the program's values for the USB script, MCU bytes 21-28 - eight reads a picture) */
            SLOG(0, got_padw); SLOG(1, *(volatile uint32_t *)((uintptr_t)&got_more_n | 0x20000000));   /* (docs/55: slot 1 was the pad word's high byte, always 0x10) */
#ifdef GOT_SND
            SLOG(2, *(volatile uint32_t *)((uintptr_t)&snd_irqs | 0x20000000)); SLOG(3, *(volatile uint32_t *)((uintptr_t)&snd_writes | 0x20000000));   /* (docs/54: the sound's buffers played, its writes taken) */
#else
            SLOG(2, got_padw_or); SLOG(3, got_padw_or >> 8);
#endif   /* (docs/47: was swaps mended / stuck - 0 in every console run) */
            SLOG(4, eng_fixn); SLOG(6, got_padskip);
#ifdef GOT_SND
            SLOG(5, *(volatile uint32_t *)((uintptr_t)&snd_plays | 0x20000000));   /* (docs/54: ADPCM plays started; was the 68000's vblanks) */
#else
            SLOG(5, got_tick);
#endif           /* (docs/47: was commands refused - 0 in every run) */
#ifdef GOT_PACKED
#endif
            SLOG(7, eng_frames);
        }
        MARS_SYS_COMM4 = (uint16_t)eng_frames;
        MARS_SYS_COMM6 = (uint16_t)f;                         /* (the engine's frame drawn last: docs/105, ares's --shots) */
        eng_bram_check();
    }
}
