/* satest - Sapphire CD32X, M1's first test ROM (DraculaX_32X's dxtest, with the map any Genesis size: Sapphire's
 * BAT is 128 x 32, sent row by row with command 63, since command 67's column copy steps 64 cells a row).
 * One PC Engine frame drawn the REV way.
 *
 * LEVEL00.BIN (tools/m1_testframe.py, from a Mednafen capture) is loaded into the cart's level slot.
 * Then, through SuperTux CD32X REV's Genesis picture commands (stack built with GENPLANE=1):
 *   65  VDP registers: display off, plane A / B map at 0xE000 (the BAT's size), sprite table 0xD000,
 *       H scroll table 0xD400, full-screen scroll, window off, backdrop = palette 0 colour 0
 *   64  the 4 Genesis palettes (64 colours) from the cart
 *   63  the tiles from the cart into VRAM 0x0020 (tile 1 on), up to 1023 words a command
 *   62  the sprite table's first entry = 0 (no Genesis sprites), the H scroll of planes A and B
 *   63  the plane map, row by row (linear in VRAM from 0xE000), up to 1023 words a command
 *   66  the V scroll of planes A and B
 *   65  display on
 * The 32X draws the sprites in front (PRI 1); its empty pixels carry the through bit (behind the plane).
 * The D-pad scrolls the plane (62 / 66 every frame); the 32X layer stays.
 * A per-line frame (flags bit 0, a capture with the M0b line log): register 11 = 3, the H scroll table (63, 448
 * words) and the V scroll split list (78, the stack's VSPLIT: needs GENPLANE=1 VSPLIT=1); no D-pad scroll.
 * Status codes in COMM12: 4E20.. each step, 4EFx a command refused (x = command - 0x60), 4B10 running.
 */
#include <stdint.h>
#include "32x.h"
#include "cdapi.h"

volatile uint32_t gTicks = 0;
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
void pri_vbi_handler(void) { gTicks++; }
void sec_dma1_handler(void) { }
void secondary(void) { while (1) { } }

#define COMM8_32   (*(volatile uint32_t *)0x20004028)
#define COMM12_16  (*(volatile uint16_t *)0x2000402C)
#define FB         ((volatile uint16_t *)&MARS_FRAMEBUFFER)
#define CART_FILE  0x200000u                     /* LEVEL00.BIN: file 100 -> the level slot (game_files.h) */

typedef struct {
    char magic[4];
    uint16_t tiles, hscroll, vscroll, pad;
    uint32_t gcram, tile, map, zeros, cram32, layer, hs_table, vs_list;
    uint16_t mw, mh;                             /* the map's size in cells (SAF1) */
} Hdr;                                           /* pad = flags: bit 0 per-line scroll, bits 8-15 V split entries */

static int failed;

/* one Genesis picture command: COMM2, COMM8L, then the number; the reply (63 / 64 / 67) in COMM12 */
static int gen(uint16_t cmd, uint16_t comm2, uint32_t comm8, int replies)
{
    MARS_SYS_COMM2 = comm2;
    COMM8_32 = comm8;
    if (replies) COMM12_16 = 0xFFFF;
    if (!cd_cmd(cmd)) { failed = 1; return 0; }
    if (replies && COMM12_16 != 1)
    {
        cd_status((uint16_t)(0x4EF0 | (cmd - 0x60)));
        failed = 1;
        return 0;
    }
    return 1;
}
static void reg(uint16_t w) { gen(65, w, 0, 0); }
static void vram_word(uint16_t addr, uint16_t w) { gen(62, addr, (uint32_t)w << 16, 0); }
static void vsram(uint16_t addr, uint16_t w) { gen(66, addr, (uint32_t)w << 16, 0); }

static void flip(void)
{
    uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
    MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
}

static void draw_layer(const volatile uint16_t *src)
{
    for (int i = 0; i < 256; i++)
        FB[i] = (uint16_t)(0x100 + (i < 224 ? i : 223) * 160);
    volatile uint16_t *p = FB + 0x100;
    for (int i = 0; i < 160 * 224; i++)
        p[i] = src[i];
}

#ifdef DXT_MOTION
/* ---- DXT_MOTION (M1b, docs/07 §6): a recorded stretch played back frame for frame (tools/m1b_motion.py,
 * LEVEL00.BIN 'DXM1'). Each frame: draw its sprite cells into the 32X back buffer (cleared where the frame
 * before last drew), its number in the left border; ask for the swap and wait for it (the vblank); then at once
 * its 32X colour changes, its scroll (62 H, 66 V when it changed: one write lands in vblank, docs/07 §4) and its
 * map columns (67). COMM4 = refreshes missed (a frame shown twice), COMM6 = the most refreshes one frame took,
 * COMM12 = 0x4D00 + frame (4DEE: a bad frame record - COMM4 frame, COMM6 column, COMM2 its number, COMM8 its
 * data offset). The file's u32 fields must be 4-byte aligned (m1b_motion.py does it): the SH2 can't read
 * them otherwise. The stretch loops; at the seam (last frame -> 0) the level jumps back, which costs refreshes:
 * that is the test, not the game. */
typedef struct {
    char magic[4];
    uint16_t frames, tiles, cells, flags;
    uint32_t gcram, tile, map, zero, cram32, frec, cell;
} MHdr;
typedef struct {
    uint16_t hs, vs, ncols, ndraw, npal, pad;
    uint32_t colidx, coldata, draw, pal, pad2;
} MFrame;
#define OVW ((volatile uint8_t *)&MARS_OVERWRITE_IMG)
#define FB8 ((volatile uint8_t *)&MARS_FRAMEBUFFER)
typedef struct { int16_t x0, x1, y0, y1; } Rect;
static Rect rects[2][128];
static int nrects[2];

static void m_draw(const volatile MHdr *h, int f, int buf)
{
    for (int i = 0; i < nrects[buf]; i++)                      /* what this buffer showed two frames ago */
    {
        const Rect *r = &rects[buf][i];
        for (int y = r->y0; y < r->y1; y++)
        {
            volatile uint16_t *d = (volatile uint16_t *)(FB8 + 0x200 + y * 320 + r->x0);
            for (int k = 0; k < (r->x1 - r->x0) / 2; k++) d[k] = 0;
        }
    }
    nrects[buf] = 0;
    const volatile MFrame *fr = (const volatile MFrame *)(CART_BASE + CART_FILE + h->frec + 32u * (uint32_t)f);
    const volatile int16_t *dl = (const volatile int16_t *)(CART_BASE + CART_FILE + fr->draw);
    const int nd = fr->ndraw;
    for (int i = 0; i < nd; i++)
    {
        const int x = dl[i * 4], y = dl[i * 4 + 1], c = (uint16_t)dl[i * 4 + 2];
        const volatile uint8_t *src = (const volatile uint8_t *)(CART_BASE + CART_FILE + h->cell + 544u * (uint32_t)c);
        int dx0 = x, w = 16, stride = 16;
        if (x & 1) { dx0 = x - 1; w = 18; stride = 18; src += 256; }   /* odd x: the copy 1 px right */
        int c0 = dx0 < 32 ? 32 : dx0, c1 = dx0 + w > 288 ? 288 : dx0 + w;
        int r0 = y < 0 ? -y : 0, r1 = y + 16 > 224 ? 224 - y : 16;
        if (c0 >= c1 || r0 >= r1) continue;
        for (int r = r0; r < r1; r++)
        {
            volatile uint16_t *d = (volatile uint16_t *)(OVW + 0x200 + (y + r) * 320 + c0);
            const volatile uint16_t *sw = (const volatile uint16_t *)(src + r * stride + (c0 - dx0));
            for (int k = 0; k < (c1 - c0) / 2; k++) d[k] = sw[k];
        }
        if (nrects[buf] < 128)
        {
            Rect *q = &rects[buf][nrects[buf]++];
            q->x0 = (int16_t)c0; q->x1 = (int16_t)c1; q->y0 = (int16_t)(y + r0); q->y1 = (int16_t)(y + r1);
        }
    }
    for (int bit = 0; bit < 9; bit++)                           /* the frame number in the left border */
    {
        const uint16_t v = (f >> bit & 1) ? 0x2020 : 0x1010;
        for (int y = bit * 16; y < bit * 16 + 12; y++)
        {
            volatile uint16_t *d = (volatile uint16_t *)(FB8 + 0x200 + y * 320 + 4);
            for (int k = 0; k < 12; k++) d[k] = v;
        }
    }
}

static void motion(const volatile MHdr *h)
{
    const uint32_t base = CART_FILE;
    reg(0x8134); reg(0x8238); reg(0x8405); reg(0x8568); reg(0x8D35); reg(0x8B00);
    reg(0x9011); reg(0x9100); reg(0x9200); reg(0x8700); reg(0x8C81);   /* plane B: 0xA000, an empty map */
    gen(64, 0, (64u << 22) | (base + h->gcram), 1);
    {
        uint32_t words = (uint32_t)h->tiles * 16, off = base + h->tile; uint16_t dst = 0x0020;
        while (words && !failed)
        {
            uint32_t k = words > 1023 ? 1023 : words;
            gen(63, dst, (k << 22) | off, 1);
            words -= k; off += k * 2; dst = (uint16_t)(dst + k * 2);
        }
    }
    for (int i = 0; i < 4; i++) vram_word((uint16_t)(0xD000 + 2 * i), 0);
    for (int c = 0; c < 64 && !failed; c += 4)
    {
        gen(67, (uint16_t)(0xE000 + 2 * c), (3u << 30) | (64u << 22) | (base + h->map + (uint32_t)c * 128), 1);
        gen(67, (uint16_t)(0xA000 + 2 * c), (3u << 30) | (64u << 22) | (base + h->zero), 1);
    }
    const volatile MFrame *f0 = (const volatile MFrame *)(CART_BASE + base + h->frec);
    uint16_t hs = f0->hs, vs = f0->vs;
    vram_word(0xD400, hs); vram_word(0xD402, 0); vsram(0, vs); vsram(2, 0);
    cd_status(0x4D01);

    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256 | MARS_VDP_PRIO_32X;
    {
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        const volatile uint16_t *c = (const volatile uint16_t *)(CART_BASE + base + h->cram32);
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        for (int i = 0; i < 256; i++) cram[i] = c[i];
    }
    for (int b = 0; b < 2; b++)                                 /* both buffers: line table, see-through, border */
    {
        for (int i = 0; i < 256; i++) FB[i] = (uint16_t)(0x100 + (i < 224 ? i : 223) * 160);
        for (int y = 0; y < 224; y++)
            for (int k = 0; k < 160; k++) FB[0x100 + y * 160 + k] = (k < 16 || k >= 144) ? 0x1010 : 0;
        nrects[b] = 0;
        flip();
    }
    reg(0x8174);
    cd_status(0x4D02);

    const int n = h->frames;
    int f = 0, buf = 0, missed = 0, worst = 0;
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    uint32_t last = gTicks;
    while (!failed)
    {
        m_draw(h, f, buf);
        flip();                                                 /* frame f is on the 32X from this vblank */
        const uint32_t now = gTicks, took = now - last; last = now;
        if (took > 1) missed += (int)(took - 1);
        if ((int)took > worst) worst = (int)took;
        const volatile MFrame *fr = (const volatile MFrame *)(CART_BASE + base + h->frec + 32u * (uint32_t)f);
        if (fr->npal)
        {
            const volatile uint16_t *pl = (const volatile uint16_t *)(CART_BASE + base + fr->pal);
            for (int i = 0; i < fr->npal; i++) cram[pl[i * 2]] = pl[i * 2 + 1];
        }
        if (fr->hs != hs) { hs = fr->hs; vram_word(0xD400, hs); }
        if (fr->vs != vs) { vs = fr->vs; vsram(0, vs); }
        const uint32_t o_ci = fr->colidx, o_cd = fr->coldata; const int nc = fr->ncols;
        const volatile uint16_t *ci = (const volatile uint16_t *)(CART_BASE + base + o_ci);
        for (int i = 0; i < nc; i++)
        {
            const uint16_t col = ci[i];
            if (col > 63 || (o_cd & 1) || o_cd > 0x40000)          /* a bad record: stop, 4DEE, with what was read */
            {
                MARS_SYS_COMM4 = (short)f; MARS_SYS_COMM6 = (short)i; COMM8_32 = o_cd;
                MARS_SYS_COMM2 = (short)col; COMM12_16 = 0x4DEE; while (1) { }
            }
            gen(67, (uint16_t)(0xE000 + 2 * col), (64u << 22) | (base + o_cd + 128u * (uint32_t)i), 1);
        }
        MARS_SYS_COMM4 = (short)missed; MARS_SYS_COMM6 = (short)worst;
        COMM12_16 = (uint16_t)(0x4D00 + (f & 0xFF));
        f = f + 1 == n ? 0 : f + 1;
        buf ^= 1;
    }
    while (1) { }
}
#endif

int main(void)
{
    cd_status(0x4E20);
    if (!cd_load(100)) { cd_status(0x4EE0); while (1) { } }
    const volatile Hdr *h = (const volatile Hdr *)(CART_BASE + CART_FILE);
#ifdef DXT_MOTION
    if (h->magic[3] == 'M' || ((const volatile char *)h)[2] == 'M') motion((const volatile MHdr *)h);
#endif
    if (h->magic[0] != 'S' || h->magic[1] != 'A' || h->magic[2] != 'F' || h->magic[3] != '1')
    {
        cd_status(0x4EE5); while (1) { }
    }
    cd_status(0x4E21);

    /* ---- the Genesis: registers, colours, tiles, map, scroll (commands 62-67) */
    reg(0x8134);                                 /* display off (V interrupt stays on) */
    reg(0x8238);                                 /* plane A map 0xE000 */
    reg(0x8407);                                 /* plane B map 0xE000 too: the same picture under A */
    reg(0x8568);                                 /* sprite table 0xD000 */
    reg(0x8D35);                                 /* H scroll table 0xD400 */
    int per_line = h->pad & 1, nsplit = h->pad >> 8;
    reg(per_line ? 0x8B03 : 0x8B00);             /* H scroll per line (the table) or full-screen; V full-screen */
    {   /* register 16: the planes' size, as the BAT's (32 / 64 / 128 cells: 0 / 1 / 3) */
        uint16_t hsz = h->mw == 128 ? 3 : h->mw == 64 ? 1 : 0, vsz = h->mh == 128 ? 3 : h->mh == 64 ? 1 : 0;
        reg((uint16_t)(0x9000 | vsz << 4 | hsz));
    }
    reg(0x9100); reg(0x9200);                    /* window off */
    reg(0x8700);                                 /* backdrop: palette 0, colour 0 */
    reg(0x8C81);                                 /* H40 */
    cd_status(0x4E22);
    gen(64, 0, (64u << 22) | (CART_FILE + h->gcram), 1);
    cd_status(0x4E23);
    {
        uint32_t words = (uint32_t)h->tiles * 16, off = CART_FILE + h->tile;
        uint16_t dst = 0x0020;
        while (words && !failed)
        {
            uint32_t k = words > 1023 ? 1023 : words;
            gen(63, dst, (k << 22) | off, 1);
            words -= k; off += k * 2; dst = (uint16_t)(dst + k * 2);
        }
    }
    cd_status(0x4E24);
    for (int i = 0; i < 4; i++) vram_word((uint16_t)(0xD000 + 2 * i), 0);   /* sprite 0: link 0, off screen */
    {   /* the map: row by row is VRAM's own order from 0xE000 */
        uint32_t words = (uint32_t)h->mw * h->mh, off = CART_FILE + h->map;
        uint16_t dst = 0xE000;
        while (words && !failed)
        {
            uint32_t k = words > 1023 ? 1023 : words;
            gen(63, dst, (k << 22) | off, 1);
            words -= k; off += k * 2; dst = (uint16_t)(dst + k * 2);
        }
    }
    cd_status(0x4E25);
    uint16_t hs = h->hscroll, vs = h->vscroll;
    if (per_line)
    {   /* M0b: the PC Engine's line scroll.  H: the table, 224 lines x (A, B) = 448 words (63).
           V: the split list, the V scroll changed part-way down by the Genesis H interrupt (78, VSPLIT) */
        gen(63, 0xD400, (448u << 22) | (CART_FILE + h->hs_table), 1);
        const volatile uint32_t *e = (const volatile uint32_t *)(CART_BASE + CART_FILE + h->vs_list);
        for (int i = 0; i < nsplit; i++) gen(78, (uint16_t)i, e[i], 0);
        gen(78, (uint16_t)(0x8000 | nsplit), 0, 0);
    }
    else
    {
        vram_word(0xD400, hs); vram_word(0xD402, hs);
        vsram(0, vs); vsram(2, vs);
    }
    cd_status(0x4E26);

    /* ---- the 32X: 256 colours, in front of the Genesis; the sprite layer in both buffers */
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256 | MARS_VDP_PRIO_32X;
    {
        const volatile uint16_t *c = (const volatile uint16_t *)(CART_BASE + CART_FILE + h->cram32);
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        for (int i = 0; i < 256; i++) cram[i] = c[i];
    }
    const volatile uint16_t *layer = (const volatile uint16_t *)(CART_BASE + CART_FILE + h->layer);
    for (int i = 0; i < 2; i++) { draw_layer(layer); flip(); }
    cd_status(0x4E27);

    reg(0x8174);                                 /* display on */
    if (!failed) cd_status(0x4B10);

#ifdef DXT_VSWAP
    /* ---- DXT_VSWAP (D5): the split list changed every frame while a picture is on screen.
     * Layout 0 = the capture's list, layout 1 = the same lines with both V scrolls + 8. Each frame: wait for
     * the swap (vblank), send the NEXT frame's layout at once (the picture now on screen is still being
     * drawn), commit, put the layout's marker on the 32X layer (top-left 16 x 8: colour 254 red = 0, 255
     * blue = 1) and ask for the swap. Right: every picture's bands are the layout its marker says.
     * COMM12 = 0x4C00 + frames done (runs for ever). */
    if (per_line)
    {
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        const volatile uint32_t *e = (const volatile uint32_t *)(CART_BASE + CART_FILE + h->vs_list);
        reg(0x8174);
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        cram[254] = 0x001F; cram[255] = 0x7C00;
        for (uint32_t f = 1; !failed; f++)
        {
            const uint32_t add = (f & 1) ? ((8u << 10) | 8u) : 0;
            for (int i = 0; i < nsplit; i++)
            {
                uint32_t x = e[i];
                uint32_t va = ((x >> 10) + (add >> 10)) & 0x3FF, vb = (x + add) & 0x3FF;
                gen(78, (uint16_t)i, (x & 0xFFF00000u) | (va << 10) | vb, 0);
            }
            gen(78, (uint16_t)(0x8000 | nsplit), 0, 0);
            const uint16_t mk = (f & 1) ? 0xFFFF : 0xFEFE;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) FB[0x100 + y * 160 + x] = mk;
            flip();
            COMM12_16 = (uint16_t)(0x4C00 + (f & 0x3FF));
        }
    }
#endif

#ifdef DXT_LAT
    /* ---- DXT_LAT: how many display lines after the 32X swap the stack's Genesis commands land.
     * Each frame: ask for a swap, wait until it happened (the vblank), then send commands and count
     * the 32X's H blank edges until each is done (COMM0 back to 0; the VDP write is one round trip
     * before that). The plane scrolls 1 px a frame so a late write shows as a tear in a screenshot.
     * Results (lines, after 240 frames): COMM2 = lines a frame (check: 262), COMM4 = max to the 1st
     * command (62, H scroll), COMM6 = max to 4 commands (SuperTux's ha, va, hb, vb), COMM12 =
     * 0x7000 + max to a 63 of 896 bytes (a per-line H scroll table) after them. */
    {
        int mx1 = 0, mx4 = 0, mx63 = 0, per_frame = 0;
        uint16_t last;
#define EDGE() do { uint16_t hb_ = MARS_VDP_FBCTL & MARS_VDP_HBLK; if (hb_ && !last) n++; last = hb_; } while (0)
        for (int f = 0; f < 240 && !failed; f++)
        {
            int n = 0;
            uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
            MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
            while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
            last = MARS_VDP_FBCTL & MARS_VDP_HBLK;
            hs = (uint16_t)((hs - 1) & 0x3FF);
            const uint16_t args[4][2] = { { 0xD400, hs }, { 0, vs }, { 0xD402, hs }, { 2, vs } };
            const uint16_t cmds[4] = { 62, 66, 62, 66 };
            for (int k = 0; k < 4; k++)
            {
                MARS_SYS_COMM2 = args[k][0]; COMM8_32 = (uint32_t)args[k][1] << 16;
                MARS_SYS_COMM0 = cmds[k];
                while (MARS_SYS_COMM0 != 0) EDGE();
                if (k == 0 && n > mx1) mx1 = n;
            }
            if (n > mx4) mx4 = n;
            MARS_SYS_COMM2 = 0xD800; COMM8_32 = (448u << 22) | (CART_FILE + h->tile);
            COMM12_16 = 0xFFFF; MARS_SYS_COMM0 = 63;
            while (MARS_SYS_COMM0 != 0) EDGE();
            if (n > mx63) mx63 = n;
            if (f == 120)                                    /* one whole frame, vblank start to vblank start */
            {
                while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) { }
                while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }
                n = 0; last = MARS_VDP_FBCTL & MARS_VDP_HBLK;
                while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) EDGE();
                while (MARS_VDP_FBCTL & MARS_VDP_VBLK) EDGE();
                while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) EDGE();
                per_frame = n;
            }
        }
        MARS_SYS_COMM2 = (short)per_frame; MARS_SYS_COMM4 = (short)mx1; MARS_SYS_COMM6 = (short)mx4;
        COMM12_16 = (uint16_t)(0x7000 + mx63);
        while (1) { }
    }
#endif

    /* ---- the D-pad scrolls the Genesis plane: two registers a frame, no drawing */
    while (1)
    {
        uint16_t pad = cd_pad(0, 0);
        int dx = (pad & SEGA_CTRL_LEFT) ? 1 : (pad & SEGA_CTRL_RIGHT) ? -1 : 0;
        int dy = (pad & SEGA_CTRL_UP) ? -1 : (pad & SEGA_CTRL_DOWN) ? 1 : 0;
        if (!per_line && dx) { hs = (uint16_t)((hs + dx) & 0x3FF); vram_word(0xD400, hs); vram_word(0xD402, hs); }
        if (!per_line && dy) { vs = (uint16_t)((vs + dy) & 0x3FF); vsram(0, vs); vsram(2, vs); }
        uint32_t t = gTicks; while (gTicks == t) { }
    }
}
