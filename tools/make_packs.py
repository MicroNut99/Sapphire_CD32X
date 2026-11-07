#!/usr/bin/env python3
"""make_packs.py <work> - D1 (docs/03): the Arcade Card for both carts, 4 MB and 14 MB, from the running game.

1. Takes the Arcade Card's 2 MB as the game has it at six moments, from Mednafen (exact, whatever the game
   unpacked): after the boot's load (the title and the intro: image 0) and after each stage's load (images
   1-5), with CD RAM and Super RAM at the same moments (the stage's start: the recompiled game's seed).
   The demo's states: setup_states.py's idle_100 (boot), idle_140 (stage 1, and stage 5 with $0E15 = 4),
   idle_240, idle_365, idle_460; each run 1,300 frames, past its load (docs/02 §4: the loads end by
   frame 1,133).
2. Cuts each image into 32 regions of 64 KB. A region that is zero everywhere is not stored; equal regions
   are stored once. The runtime reaches the Arcade Card through a table per image: region -> cart place.
3. Lays the regions out for the two carts (docs/03 §2):
   - 14 MB (EverDrive, CART=ED: window 0 = page 0, fixed; window 1 = pages 1-6 by CMD 68): regions used by
     two or more images in page 0 from 0x080000 (after GAME.32X), each image's own regions in its own page;
     every page's last 64 KB (0x3F0000, the save area in window 1) left free.
   - 4 MB (the RAM cart): the shared regions in 0x080000-0x1FFFFF, each image's own regions in a stage pack
     (STGn.PAK) the stack loads into 0x200000-0x3EFFFF (1,984 KB) at the image's start.
4. Checks: every image rebuilt from each cart and its table equals the image taken from Mednafen.
Writes <work>/packs/: ac<n>.bin (the images), cd<n>.bin / sup<n>.bin (the RAM), and for each cart
cart14/PAGEn.BIN, cart4/RESIDENT.BIN + STGn.PAK, and the tables (actab.bin: per image 32 words, the cart
offset of the region, 0xFFFFFFFF zero) and layout.txt. All game data: the work folder only.
"""
import ctypes as C
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host

REG = 0x10000
NREG = 32
MOMENTS = [(0, 'idle_100.st', None, 0), (1, 'idle_140.st', None, 1300), (2, 'idle_240.st', None, 1300),
           (3, 'idle_365.st', None, 1300), (4, 'idle_460.st', None, 1300), (5, 'idle_140.st', 4, 1300)]
ROM = 0x080000           # GAME.32X's room at the cart's start (the BootCamp's default, ≤ 512 KB)
PAGE = 0x200000
SAVE = 0x3F0000          # the save area in window 1: cart 0x3F0000-0x3FFFFF
ZERO = 0xFFFFFFFF


def take(W):
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    h.run(2)
    imgs = {}
    for n, st, poke, frames in MOMENTS:
        h.load_state(W + '/states/' + st)
        if poke is not None:
            h.ram()[0xE15] = poke
        h.run(frames + 1)
        stage = int(h.ram()[0xE15])
        cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)()
        h.lib.ramcopy(cd, sup)
        imgs[n] = (h.ac_ram().copy(), bytes(cd), bytes(sup), stage)
    return imgs


def main():
    W = sys.argv[1]
    out = W + '/packs'; os.makedirs(out + '/cart14', exist_ok=True); os.makedirs(out + '/cart4', exist_ok=True)
    imgs = take(W)
    log = []
    for n, (ac, cd, sup, stage) in imgs.items():
        open('%s/ac%d.bin' % (out, n), 'wb').write(ac.tobytes())
        open('%s/cd%d.bin' % (out, n), 'wb').write(cd); open('%s/sup%d.bin' % (out, n), 'wb').write(sup)
        log.append('image %d: $0E15 = %d, Arcade Card md5 %s' % (n, stage, hashlib.md5(ac.tobytes()).hexdigest()[:12]))
    # regions: hash -> bytes; per image the 32 hashes (None = zero)
    store, users, tab = {}, {}, {}
    for n, (ac, _, _, _) in imgs.items():
        row = []
        for r in range(NREG):
            b = ac[r * REG:(r + 1) * REG].tobytes()
            if not any(b):
                row.append(None); continue
            k = hashlib.md5(b).digest()
            store[k] = b; users.setdefault(k, set()).add(n); row.append(k)
        tab[n] = row
    shared = [k for k in store if len(users[k]) > 1]
    own = {n: [k for k in dict.fromkeys(tab[n]) if k is not None and len(users[k]) == 1] for n in imgs}
    log.append('regions: %d distinct non-zero (%d KB); %d shared by two or more images (%d KB); own: %s'
               % (len(store), len(store) * 64, len(shared), len(shared) * 64,
                  ', '.join('%d: %d' % (n, len(v)) for n, v in own.items())))

    def build(cart):
        """{hash: cart offset} and the cart files, or an error"""
        place, files = {}, {}
        if cart == 14:
            p0 = bytearray(PAGE); at = ROM
            for k in shared:
                if at + REG > PAGE: return None, 'page 0 full: shared regions need %d KB' % (len(shared) * 64)
                p0[at:at + REG] = store[k]; place[k] = at; at += REG
            log.append('cart 14 MB: page 0 = GAME.32X room 0x000000-0x07FFFF, shared regions 0x080000-0x%06X (%d KB free)'
                       % (at - 1, (PAGE - at) // 1024))
            files['PAGE0.BIN'] = p0
            for n in imgs:
                pg = n + 1 if n < 5 else 6          # images 0-5 -> pages 1-6
                buf = bytearray(PAGE); at = 0
                for k in own[n]:
                    if at + REG > SAVE - PAGE: return None, 'page %d full' % pg
                    buf[at:at + REG] = store[k]; place[(n, k)] = PAGE + at; at += REG
                files['PAGE%d.BIN' % pg] = buf
                log.append('cart 14 MB: page %d = image %d, %d regions, %d KB of 1,984' % (pg, n, len(own[n]), at // 1024))
        else:
            res = bytearray(PAGE - ROM); at = 0
            for k in shared:
                if at + REG > len(res): return None, 'resident full: shared regions need %d KB, room %d KB' % (len(shared) * 64, len(res) // 1024)
                res[at:at + REG] = store[k]; place[k] = ROM + at; at += REG
            files['RESIDENT.BIN'] = bytes(res[:at])
            log.append('cart 4 MB: shared regions 0x080000-0x%06X (%d KB; %d KB free before 0x200000)'
                       % (ROM + at - 1, at // 1024, (len(res) - at) // 1024))
            for n in imgs:
                buf = bytearray(); at = 0
                for k in own[n]:
                    if at + REG > SAVE - PAGE: return None, 'stage pack %d over 1,984 KB' % n
                    buf += store[k]; place[(n, k)] = PAGE + at; at += REG
                files['STG%d.PAK' % n] = bytes(buf)
                log.append('cart 4 MB: STG%d.PAK = image %d, %d KB (loaded into 0x200000 at its start)' % (n, n, at // 1024))
        return place, files

    ok = True
    for cart in (14, 4):
        place, files = build(cart)
        if place is None:
            log.append('cart %d MB: DOES NOT FIT: %s' % (cart, files)); ok = False; continue
        d = '%s/cart%d' % (out, cart)
        for name, b in files.items():
            open('%s/%s' % (d, name), 'wb').write(b)
        tabbin = b''
        for n in imgs:
            offs = [ZERO if k is None else place[k] if k in place else place[(n, k)] for k in tab[n]]
            tabbin += struct.pack('>32I', *offs)
            # the check: the image rebuilt from the cart files as the console would see them
            if cart == 14:
                pg = n + 1 if n < 5 else 6
                cartview = files['PAGE0.BIN'] + files['PAGE%d.BIN' % pg]
            else:
                cartview = bytearray(0x400000)
                cartview[ROM:ROM + len(files['RESIDENT.BIN'])] = files['RESIDENT.BIN']
                cartview[PAGE:PAGE + len(files['STG%d.PAK' % n])] = files['STG%d.PAK' % n]
            re = b''.join(bytes(REG) if o == ZERO else bytes(cartview[o:o + REG]) for o in offs)
            same = re == imgs[n][0].tobytes()
            ok &= same
            log.append('cart %d MB: image %d rebuilt from the cart %s' % (cart, n, 'EQUAL' if same else 'DIFFERENT'))
        open(d + '/actab.bin', 'wb').write(tabbin)
    open(out + '/layout.txt', 'w').write('\n'.join(log) + '\n')
    print('\n'.join(log))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
