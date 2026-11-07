/* cdapi.h - the CD32X services for a game's master SH2 (CD32X BootCamp)
 *
 * Everything the 32X cannot do itself goes to the Sega CD's Sub-CPU (stack/subcpu/main.c) as a
 * command: the master SH2 writes the arguments to COMM2 / COMM8, the command number to COMM0,
 * and waits until the Sub-CPU sets COMM0 back to 0.  Only the MASTER SH2 sends commands.
 * Full table: docs/03_COMMANDS.md.  Status codes for debugging go to COMM12 (cd_status).
 */
#ifndef CDAPI_H
#define CDAPI_H
#include <stdint.h>

/* the command numbers of this stack (OpenLara CD32X path) */
#define CD_CMD_OPEN_FILE    38      /* COMM2 = file number            -> COMM8L = length (0 = not found) */
#define CD_CMD_PLAY_TRACK   45      /* COMM2 = track (| 0x8000: once) -> CD audio */
#define CD_CMD_CHUNK_CART   46      /* COMM2 = chunk (| 0x8000: no read-back), COMM8L = file length -> COMM12 = 0 ok */
#define CD_CMD_PAD          48      /* COMM0 high byte = PCM effect (0 none), COMM2 = its volume -> COMM8 = pad bits */
#define CD_CMD_GENESIS      52      /* COMM2 = 1 / 0: the Genesis display on / off */
#define CD_CMD_LOAD_SFX     53      /* COMM2 = n: SFXnn.BIN -> PCM chip      -> COMM12 = number of effects */
#define CD_CMD_STOP_MUSIC   54
#define CD_CMD_BRAM_LOAD    56      /* Sega CD backup RAM file -> the 4 KB buffer -> COMM12 = 1 ok */
#define CD_CMD_BUF_GET      57      /* COMM2 = word index            -> COMM8 = word of the 4 KB buffer */
#define CD_CMD_BUF_PUT      58      /* COMM2 = word index, COMM8 = word -> into the 4 KB buffer */
#define CD_CMD_BRAM_SAVE    59      /* the 4 KB buffer -> Sega CD backup RAM file -> COMM12 = 1 ok */
#define CD_CMD_SAVE_PIECE   61      /* COMM2 = piece 0-15: the 4 KB buffer -> cart 0x3F0000 + piece x 4 KB -> COMM12 = 1 ok */

#define CART_BASE           0x22000000u   /* the RAM cart as the SH2 reads it, cache-through */
#define CART_SLOT(n)        (CART_BASE + ((uint32_t)(n) << 17))   /* slot n x 128 KB */
#define CART_SAVE(piece)    (CART_BASE + 0x3F0000u + ((uint32_t)(piece) << 12))

/* error codes: posted in COMM12 and kept in cd_error (show them on screen: hello's error box).
   Each one is raised at one place in cdapi.c, tagged BC-E1 ... BC-E4.
   The full list, with causes and fixes: docs/09_ERROR_CODES.md */
#define CD_ERR_SAVE         0x4EE1  /* BC-E1  CMD 61: the cart save piece was not written / read back (arg = piece) */
#define CD_ERR_NO_FILE      0x4EE2  /* BC-E2  CMD 38: the file is not on the disc (arg = file number) */
#define CD_ERR_CHUNK        0x4EE3  /* BC-E3  CMD 46: a chunk did not reach the cart (arg = chunk, file = file number) */
#define CD_ERR_NO_ANSWER    0x4EE4  /* BC-E4  any command: the Sub-CPU did not answer within 10 s (cmd = the command) */

typedef struct {
    uint16_t code;                  /* CD_ERR_..., 0 = no error */
    const char *tag;                /* "BC-Ex FILE FUNCTION": where it was raised */
    uint16_t cmd, arg, file;        /* the command, its COMM2 argument, the file being loaded */
    uint16_t comm[8];               /* COMM0, COMM2 ... COMM14 at the moment it failed */
    uint32_t ticks;                 /* the vertical blank count then */
} CdError;
extern CdError cd_error;                                /* the FIRST error (a later BC-E4 replaces it); 0 = none.
                                                           BC-E4 is final: after it every wait is 0.1 s and
                                                           nothing clears it - reboot */
void     cd_error_clear(void);

int      cd_cmd(uint16_t cmd);                          /* 1 = answered, 0 = no answer within 10 s (0.1 s after a BC-E4).
                                                           Needs the game's vertical blank counter:
                                                           extern volatile uint32_t gTicks */
void     cd_status(uint16_t code);                      /* COMM12: shown as STATE: in the diagnostics */
uint32_t cd_open(int file);                             /* length, 0 = not on the disc */
int      cd_load(int file);                             /* open + every chunk into its cart slot; 1 = ok */
uint16_t cd_pad(int sfx, int volume);                   /* pad 1 (SEGA_CTRL_*), and play PCM effect sfx (0 = none) */
void     cd_music_play(int track, int once);
void     cd_music_stop(void);
void     cd_genesis_display(int on);
int      cart_save(int piece, const uint16_t *words, int count);   /* words -> buffer -> cart save piece; 1 = ok.
                                                           The whole 4 KB buffer is written: words past
                                                           count are whatever was in the buffer before */
int      cd_sfx_load(int pack);                         /* CMD 53: SFXnn.BIN -> PCM chip; number of effects, 0 = failed */
int      bram_load(void);                               /* CMD 56: the backup RAM file -> buffer; 1 = ok, 0 = none */
uint16_t bram_get(int index);                           /* CMD 57: word 0-2047 of the buffer */
int      bram_save(const uint16_t *words, int count);   /* CMD 58 + 59: words -> buffer -> backup RAM file; 1 = ok */

#endif
