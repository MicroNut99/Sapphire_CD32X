#!/usr/bin/env python3
"""m3_trackpack.py <data track .iso> <out .pak> - Sapphire docs/49: the game's data track for the cart's memory, so
the CD is used for its music alone.

The track's 2 KB sectors are kept once each: of 7,599, 5,255 are different (1,795 are all zero). Each different
sector is compressed on its own as an LZ4 block (lz4's block format: a token, literals, a 16-bit offset back, a
match length; no frame), so a sector can be rebuilt by itself, in the order the game asks for it, by the SH2, the
68000 or the FPGA. A sector that doesn't get smaller is kept as it is.

The file (big-endian):
  0x00  'SPAK', version 1, the track's sector count, the different sectors' count, the data's offset   (4 longs + 'SPAK')
  0x14  a long a track sector: bit 31 set = its piece is stored as it is (2,048 bytes); bits 0-23 = which different
        sector it is
  ...   a long a different sector: its piece's offset from the data's start; one more, the data's end
  ...   the 32-bit FNV-1a of each track sector (the reader checks what it rebuilt)
  data  the pieces
Game data: the file stays in the work folder. tools/m3rt/trackpak.c reads it (and checks every sector here)."""
import sys, struct

def lz4_compress(src, depth=48, start=0):
    """LZ4 block, lz4's end rules (the last match ends 12 bytes before the end at the latest, the last 5 bytes are
    literals). Matches from hash chains of 4 bytes (`depth` tried), one step of lazy matching: where the next byte
    starts a longer match, this byte goes as a literal (as lz4's high-compression mode). start > 0: src[:start] is a
    dictionary the decoder has before its output (matches may reach into it); only src[start:] is coded."""
    n = len(src); out = bytearray(); anchor = start; i = start; head = {}; prev = [-1] * n
    limit = n - 12
    def emit(lit, mlen, off):
        L = len(lit); tok_l = min(L, 15); tok_m = 0 if mlen is None else min(mlen - 4, 15)
        out.append(tok_l << 4 | tok_m)
        if L >= 15:
            r = L - 15
            while r >= 255: out.append(255); r -= 255
            out.append(r)
        out.extend(lit)
        if mlen is None: return
        out.extend(struct.pack('<H', off))
        if mlen - 4 >= 15:
            r = mlen - 4 - 15
            while r >= 255: out.append(255); r -= 255
            out.append(r)
    ins = 0
    def insert_to(p):  # (the dictionary's places go in as the coding reaches them: all of it before the first byte)
        nonlocal ins
        while ins <= p and ins < limit:
            k = src[ins:ins + 4]; prev[ins] = head.get(k, -1); head[k] = ins; ins += 1
    def best(p):
        insert_to(p)
        j = prev[p]; bl = 0; bo = 0; d = 0
        while j >= 0 and p - j <= 65535 and d < depth:
            if src[j + bl] == src[p + bl] if p + bl < n - 5 else False:
                m = 0
                while p + m < n - 5 and src[j + m] == src[p + m]: m += 1
                if m > bl: bl, bo = m, p - j
            j = prev[j]; d += 1
        return (bl, bo) if bl >= 4 else (0, 0)
    while i < limit:
        m, o = best(i)
        if m:
            if i + 1 < limit:
                m2, o2 = best(i + 1)
                if m2 > m + 1: i += 1; continue          # (the next byte's match is longer: this one a literal)
            emit(src[anchor:i], m, o); i += m; anchor = i
        else:
            i += 1
    emit(src[anchor:], None, 0)
    return bytes(out)

def lz4_decompress(b, size, dic=b''):
    out = bytearray(dic); size += len(dic); i = 0
    while i < len(b):
        t = b[i]; i += 1; L = t >> 4
        if L == 15:
            while True:
                x = b[i]; i += 1; L += x
                if x != 255: break
        out += b[i:i + L]; i += L
        if i >= len(b): break
        off = b[i] | b[i + 1] << 8; i += 2; m = (t & 15) + 4
        if (t & 15) == 15:
            while True:
                x = b[i]; i += 1; m += x
                if x != 255: break
        for _ in range(m): out.append(out[-off])
    assert len(out) == size
    return bytes(out[len(dic):])

def fnv(b):
    h = 0x811C9DC5
    for x in b: h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read(); n = len(d) // 2048
    secs = [d[i * 2048:(i + 1) * 2048] for i in range(n)]
    uniq = {}; order = []
    for s in secs:
        if s not in uniq: uniq[s] = len(order); order.append(s)
    pieces = []; raw = []
    for s in order:
        c = lz4_compress(s)
        assert lz4_decompress(c, 2048) == s
        if len(c) >= 2048: pieces.append(s); raw.append(True)
        else: pieces.append(c); raw.append(False)
    offs = [0]
    for p in pieces: offs.append(offs[-1] + len(p))
    head = 0x14 + 4 * n + 4 * (len(order) + 1) + 4 * n
    out = bytearray(b'SPAK' + struct.pack('>IIII', 1, n, len(order), head))
    for s in secs:
        u = uniq[s]; out += struct.pack('>I', (0x80000000 if raw[u] else 0) | u)
    for o in offs: out += struct.pack('>I', o)
    for s in secs: out += struct.pack('>I', fnv(s))
    assert len(out) == head
    for p in pieces: out += p
    open(sys.argv[2], 'wb').write(out)
    print('%d sectors (%.2f MB): %d different, %d kept as they are; the file %.2f MB (the table %.0f KB)' %
          (n, len(d) / 2**20, len(order), sum(raw), len(out) / 2**20, head / 1024))
