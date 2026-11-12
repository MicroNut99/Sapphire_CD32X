/* trackpak.c - Sapphire docs/49: a data-track sector from tools/m3_trackpack.py's file (the track's different sectors
   once each, LZ4 blocks), the way the cart's copy is read in place of the CD. Plain C with no library: the same code
   builds for the PC and the SH2. trackpak_read checks what it rebuilt against the sector's FNV-1a in the file.

   cc -O2 -DTRACKPAK_MAIN trackpak.c -o trackpak && ./trackpak <pak> <iso>  : every sector against the track */
#include <stdint.h>
#include <string.h>
#include "trackpak.h"

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int trackpak_open(TrackPak *k, const uint8_t *file, uint32_t size)
{
    if (size < 0x14 || memcmp(file, "SPAK", 4) || be32(file + 4) != 1) return -1;
    k->f = file; k->n = be32(file + 8); k->nu = be32(file + 12); k->data = be32(file + 16);
    k->map = file + 0x14; k->offs = k->map + 4 * k->n; k->sums = k->offs + 4 * (k->nu + 1);
    if (k->data > size || k->data + be32(k->offs + 4 * k->nu) > size) return -1;
    k->reads = k->bad = 0;
    return 0;
}

static int lz4_block(const uint8_t *s, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        const uint8_t t = s[i++]; uint32_t L = t >> 4;
        if (L == 15) { uint8_t x; do { if (i >= n) return -1; x = s[i++]; L += x; } while (x == 255); }
        if (i + L > n || o + L > cap) return -1;
        memcpy(out + o, s + i, L); i += L; o += L;
        if (i >= n) break;
        if (i + 2 > n) return -1;
        const uint32_t off = s[i] | (uint32_t)s[i + 1] << 8; i += 2;
        uint32_t m = (t & 15) + 4;
        if ((t & 15) == 15) { uint8_t x; do { if (i >= n) return -1; x = s[i++]; m += x; } while (x == 255); }
        if (off == 0 || off > o || o + m > cap) return -1;
        for (uint32_t j = 0; j < m; j++, o++) out[o] = out[o - off];   /* (overlapping: byte by byte) */
    }
    return (int)o;
}

int trackpak_read(TrackPak *k, uint32_t sector, uint8_t out[2048])
{
    k->reads++;
    if (sector >= k->n) { k->bad++; return -1; }
    const uint32_t e = be32(k->map + 4 * sector), u = e & 0xFFFFFF;
    const uint32_t a = be32(k->offs + 4 * u), b = be32(k->offs + 4 * u + 4);
    const uint8_t *p = k->f + k->data + a;
    if (e & 0x80000000u) { if (b - a != 2048) { k->bad++; return -1; } memcpy(out, p, 2048); }
    else if (lz4_block(p, b - a, out, 2048) != 2048) { k->bad++; return -1; }
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < 2048; i++) h = (h ^ out[i]) * 0x01000193u;
    if (h != be32(k->sums + 4 * sector)) { k->bad++; return -1; }
    return 0;
}

#ifdef TRACKPAK_MAIN
#include <stdio.h>
#include <stdlib.h>
static uint8_t *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); *n = ftell(f);
    fseek(f, 0, SEEK_SET); uint8_t *m = malloc(*n); if (fread(m, 1, *n, f) != (size_t)*n) return 0; fclose(f); return m; }
int main(int argc, char **argv)
{
    long np, ni; uint8_t *p = slurp(argv[1], &np), *iso = slurp(argv[2], &ni); TrackPak k; uint8_t s[2048];
    if (!p || !iso || trackpak_open(&k, p, (uint32_t)np)) { printf("trackpak: can't open\n"); return 2; }
    long diff = 0;
    for (uint32_t i = 0; i < k.n; i++) if (trackpak_read(&k, i, s) || memcmp(s, iso + (long)i * 2048, 2048)) diff++;
    printf("trackpak: %u sectors (%u different) from %.2f MB: %ld differ from the track, %u failed checks\n", k.n, k.nu, np / 1048576.0, diff, k.bad);
    return diff != 0;
}
#endif
