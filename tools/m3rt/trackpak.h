/* trackpak.h - Sapphire docs/49: the data track's sectors from the cart's packed copy (tools/m3_trackpack.py) */
#ifndef TRACKPAK_H
#define TRACKPAK_H
#include <stdint.h>
typedef struct { const uint8_t *f, *map, *offs, *sums; uint32_t n, nu, data, reads, bad; } TrackPak;
int trackpak_open(TrackPak *k, const uint8_t *file, uint32_t size);      /* 0, or -1: not a pack */
int trackpak_read(TrackPak *k, uint32_t sector, uint8_t out[2048]);       /* 0, or -1: out of range or a bad check */
#endif
