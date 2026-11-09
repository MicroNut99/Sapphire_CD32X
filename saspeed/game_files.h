/* game_files.h - YOUR GAME'S FILES (CD32X BootCamp) - Sapphire CD32X's (saspeed/; cd32x/apply.sh copies it in)
 *
 * The only Sub-CPU file a game edits.  The SH2 opens a file with CMD 38 (COMM2 = file number,
 * the length comes back in COMM8) and copies it 32 KB at a time into the RAM cart with CMD 46
 * (COMM2 = chunk number).  The file number decides the name on the disc AND where in the
 * cart it lands (cart slot x 128 KB):
 *
 *   0             IMAGE.RAW          the boot picture (CDLOADER shows it while the ROM loads)
 *   20            GAME_ROM_NAME      your 32X ROM -> cart 0 (CDLOADER loads and starts it)
 *   100 + n       game_data_files[n] -> cart GAME_DATA_SLOT x 128 KB      ("level" slot)
 *   200 + n       game_data_files[n] -> cart GAME_RESIDENT_SLOT x 128 KB  ("resident" slot)
 *   300 + n       game_movie_files[n] -> cart GAME_DATA_SLOT x 128 KB
 *   any other n   IMAGEn.RAW         -> cart n x 128 KB   (16 is reserved: KOBOGFX.BIN)
 *
 * Names are ISO-9660 (8.3, capitals).  build.sh copies everything in your game's "cd" folder
 * onto the disc.  A listed file that is not on the disc opens with length 0.
 *
 * The default cart layout (4 MB cart, see docs/04_MEMORY_MAPS.md):
 *   0x000000 - 0x07FFFF   the game ROM (keep it under 512 KB, or move the resident slot)
 *   0x080000 - 0x1FFFFF   resident data (slot 4: loaded once, kept across power-off)
 *   0x200000 - 0x3EFFFF   the level slot (slot 16: up to 1984 KB per file)
 *   0x3F0000 - 0x3FFFFF   the save area (CMD 58 + 61, 16 pieces of 4 KB)
 */
#ifndef GAME_FILES_H
#define GAME_FILES_H

#define GAME_ROM_NAME       "GAME.32X"
#define GAME_BRAM_NAME      "BOOTCAMP___"   /* Sega CD backup RAM file name: exactly 11 characters, A-Z 0-9 _ */

#define GAME_DATA_BASE      100
#define GAME_DATA_SLOT      16          /* 16 x 128 KB = cart 0x200000 */
#define GAME_RESIDENT_BASE  200
#define GAME_RESIDENT_SLOT  4           /*  4 x 128 KB = cart 0x080000 */
#define GAME_MOVIE_BASE     300

static const char *const game_data_files[] = {
    "LEVEL00.BIN",                      /* hello: file 100 (level slot) / 200 (resident slot) */
    "LEVEL01.BIN",
    "DXTRACK.TRK",                      /* the PC Engine data track: file 102 (command 91, CDRING; DraculaX docs/69) */
    "LEVEL03.BIN",                      /* Sapphire docs/39: the Arcade Card's first 1 MB (file 103, into page 6 with CMD 68);
                                           its second 1 MB is IMAGE24.RAW (file 24: cart slot 24, 1 MB into window 1) */
    "C00.BIN",                          /* docs/39: the blocks' combinations, file 104 + i (tools/m3_combo.py) */
    "C01.BIN",
    "C02.BIN",
    "C03.BIN",
    "C04.BIN",
    "C05.BIN",
    "C06.BIN",
    "C07.BIN",
    "C08.BIN",
    "C09.BIN",
    "C10.BIN",
    "C11.BIN",
    "C12.BIN",
    "C13.BIN",
    "C14.BIN",
    "C15.BIN",
};

static const char *const game_movie_files[] = {
    "INTRO.ROQ",                        /* file 300 (not used by hello) */
};

#endif
