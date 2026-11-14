/* game_files.h - Sapphire CD32X's files for the engine disc (docs/67; Gate of Thunder's goteng/game_files.h, the BootCamp's
 * stack/subcpu/game_files.h for this game: the build copies it there). The disc's files are made in the work folder - game
 * data, never in a repository. With the packed launcher (tools/sap_pack.py) the program reads none of them: the disc
 * carries the music; the files are the stack's minimum.
 *
 * (from the BootCamp's:) The SH2 opens a file with CMD 38 (COMM2 = file number, the length comes back in COMM8) and
 * copies it 32 KB at a time into the cart with CMD 46. The file number decides the name on the disc AND where in the
 * cart it lands (cart slot x 128 KB):
 *   20            GAME_ROM_NAME      the 32X ROM -> cart 0 (CDLOADER loads and starts it)
 *   100 + n       game_data_files[n] -> cart GAME_DATA_SLOT x 128 KB
 *   200 + n       game_data_files[n] -> cart GAME_RESIDENT_SLOT x 128 KB
 */
#ifndef GAME_FILES_H
#define GAME_FILES_H

#define GAME_ROM_NAME       "GAME.32X"
#define GAME_BRAM_NAME      "SAPPHIRE_CD"   /* Sega CD backup RAM file name: exactly 11 characters, A-Z 0-9 _ */

#define GAME_DATA_BASE      100
#define GAME_DATA_SLOT      16          /* 16 x 128 KB = cart 0x200000 */
#define GAME_RESIDENT_BASE  200
#define GAME_RESIDENT_SLOT  4           /*  4 x 128 KB = cart 0x080000 */
#define GAME_MOVIE_BASE     300

static const char *const game_data_files[] = {
    "LEVEL00.BIN",                      /* file 100: the engine build's empty blocks (the Makefile makes it) */
    "SAPFWTAB.BIN",                     /* file 201: the CD unit's load-port image (tools/sap_files.py) - resident */
};

static const char *const game_movie_files[] = {
    "INTRO.ROQ",                        /* file 300 (not used) */
};

#endif
