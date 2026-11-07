/* cdapi.c - the CD32X services (see cdapi.h).  CD32X BootCamp.
 *
 * Every error is tagged BC-Ex in this file (grep "BC-E" to find where it is raised) and listed with
 * its causes in docs/09_ERROR_CODES.md.  The error keeps a copy of the COMM registers taken the moment
 * it happened, so the error box can show what the Sub-CPU had answered.
 */
#include "32x.h"
#include "cdapi.h"

#define COMM8_32   (*(volatile uint32_t *)0x20004028)   /* COMM8 + COMM10 as one long */
#define COMM12_16  (*(volatile uint16_t *)0x2000402C)   /* 16 bit: COMM14 is the Sub-CPU's heartbeat */
#define COMM_WORD(n) (((volatile uint16_t *)0x20004020)[n])

extern volatile uint32_t gTicks;            /* the game's vertical blank counter (hello: main.c) */
#define CD_TIMEOUT_TICKS  600               /* 10 s (NTSC): a chunk with read-back takes well under 1 s */

static void cd_pause(void)                  /* rule: never hammer a COMM register */
{
    for (volatile int k = 0; k < 64; k++) { }
}

CdError cd_error;
static uint16_t last_status;                /* COMM12 is also the reply of CMD 46 and 61: put it back after */
static int cur_file;                        /* the file being opened / loaded (for the error box) */
static int no_answer;                       /* 1 once the Sub-CPU has failed to answer (BC-E4): final */

static void fail(uint16_t code, const char *tag, uint16_t cmd, uint16_t arg)
{
    /* the FIRST error is the one that explains the rest: keep it.  Except a Sub-CPU that stops answering
       (BC-E4): that explains more than any earlier error, so it replaces one (and is never replaced). */
    if (cd_error.code && !(code == CD_ERR_NO_ANSWER && cd_error.code != CD_ERR_NO_ANSWER))
        return;
    cd_error.code = code; cd_error.tag = tag; cd_error.cmd = cmd; cd_error.arg = arg;
    cd_error.file = (uint16_t)cur_file;
    for (int i = 0; i < 8; i++)             /* COMM0 ... COMM14, as they were when it failed */
        cd_error.comm[i] = COMM_WORD(i);
    cd_error.ticks = gTicks;
    last_status = code;                     /* the error stays in COMM12 until cd_status() posts again */
    COMM12_16 = code;
}

void cd_error_clear(void)
{
    cd_error.code = 0;
}

int cd_cmd(uint16_t cmd)
{
    /* bounded by real time (the vertical blank counter), not by a loop count: a wait that crosses
       three processors costs far more per pass than it looks (OpenLara addendum, rule 6).  The
       loop count (2,000,000 short pauses: some tens of seconds) is the backstop for a game whose
       vertical blank counter is not running. */
    uint32_t t0 = gTicks;
    /* once the Sub-CPU has failed to answer (BC-E4), later waits are short (0.1 s): the game keeps
       running and the error box shows, instead of one frame every 10 s */
    uint32_t limit = no_answer ? 6 : CD_TIMEOUT_TICKS;
    MARS_SYS_COMM0 = cmd;
    for (int n = 0; n < 2000000 && MARS_SYS_COMM0 != 0 && gTicks - t0 < limit; n++)
        cd_pause();
    if (MARS_SYS_COMM0 == 0)
        return 1;
    no_answer = 1;
    fail(CD_ERR_NO_ANSWER, "BC-E4 CDAPI.C CD_CMD", (uint16_t)(cmd & 0xFF), MARS_SYS_COMM2);   /* BC-E4 */
    return 0;
}

void cd_status(uint16_t code)
{
    last_status = code;
    COMM12_16 = code;
}

static int keep_status(int result)
{
    COMM12_16 = last_status;
    return result;
}

uint32_t cd_open(int file)
{
    cur_file = file;
    MARS_SYS_COMM2 = (uint16_t)file;
    if (!cd_cmd(CD_CMD_OPEN_FILE))
        return 0;
    int32_t len = (int32_t)COMM8_32;
    if (len <= 0)
    {
        fail(CD_ERR_NO_FILE, "BC-E2 CDAPI.C CD_OPEN", CD_CMD_OPEN_FILE, (uint16_t)file);   /* BC-E2 */
        return 0;
    }
    return (uint32_t)len;
}

int cd_load(int file)
{
    cd_music_stop();                        /* rule: no CD data reads while CD audio plays */
    uint32_t len = cd_open(file);
    if (len == 0)
        return keep_status(0);
    uint32_t chunks = (len + 32767) >> 15;
    for (uint32_t c = 0; c < chunks; c++)
    {
        COMM8_32 = len;                     /* CMD 46 reads the file length from COMM8 */
        MARS_SYS_COMM2 = (uint16_t)c;       /* bit 15 clear: the Sub-CPU reads it back and compares */
        COMM12_16 = 0xFFFF;
        if (!cd_cmd(CD_CMD_CHUNK_CART))
            return keep_status(0);
        if (COMM12_16 != 0)
        {
            fail(CD_ERR_CHUNK, "BC-E3 CDAPI.C CD_LOAD", CD_CMD_CHUNK_CART, (uint16_t)c);   /* BC-E3 */
            return keep_status(0);
        }
    }
    if (cd_error.code == CD_ERR_NO_FILE || cd_error.code == CD_ERR_CHUNK)
        cd_error_clear();                   /* a load worked again */
    return keep_status(1);                  /* read it at CART_SLOT(slot) cache-through, or clear the cache */
}

uint16_t cd_pad(int sfx, int volume)
{
    MARS_SYS_COMM2 = (uint16_t)volume;
    if (!cd_cmd((uint16_t)(CD_CMD_PAD | (sfx << 8))))
        return 0;
    uint16_t pad = MARS_SYS_COMM8;
    if ((pad & SEGA_CTRL_TYPE) == SEGA_CTRL_NONE)
        return 0;                           /* no pad plugged in */
    return pad;
}

void cd_music_play(int track, int once)
{
    MARS_SYS_COMM2 = (uint16_t)(track | (once ? 0x8000 : 0));
    cd_cmd(CD_CMD_PLAY_TRACK);
}

void cd_music_stop(void)
{
    MARS_SYS_COMM2 = 0;
    cd_cmd(CD_CMD_STOP_MUSIC);
}

void cd_genesis_display(int on)
{
    MARS_SYS_COMM2 = (uint16_t)(on ? 1 : 0);
    cd_cmd(CD_CMD_GENESIS);
}

int cart_save(int piece, const uint16_t *words, int count)
{
    for (int i = 0; i < count; i++)         /* the Sub-CPU's 4 KB buffer, one word at a time */
    {
        MARS_SYS_COMM2 = (uint16_t)i;
        MARS_SYS_COMM8 = words[i];
        if (!cd_cmd(CD_CMD_BUF_PUT))
            return 0;
    }
    MARS_SYS_COMM2 = (uint16_t)piece;
    COMM12_16 = 0;
    if (!cd_cmd(CD_CMD_SAVE_PIECE))
        return keep_status(0);
    if (COMM12_16 != 1)                     /* 1: the 68000 wrote it and read it back */
    {
        fail(CD_ERR_SAVE, "BC-E1 CDAPI.C CART_SAVE", CD_CMD_SAVE_PIECE, (uint16_t)piece);   /* BC-E1 */
        return keep_status(0);
    }
    if (cd_error.code == CD_ERR_SAVE)
        cd_error_clear();
    return keep_status(1);
}

int cd_sfx_load(int pack)
{
    MARS_SYS_COMM2 = (uint16_t)pack;        /* SFXnn.BIN -> the PCM chip */
    COMM12_16 = 0;
    if (!cd_cmd(CD_CMD_LOAD_SFX))
        return keep_status(0);
    int n = COMM12_16;                      /* the number of effects, 0 = failed */
    keep_status(0);
    return n;
}

int bram_load(void)
{
    COMM12_16 = 0;
    if (!cd_cmd(CD_CMD_BRAM_LOAD))
        return keep_status(0);
    int ok = COMM12_16 == 1;                /* 0: no such file in the backup RAM (yet) */
    return keep_status(ok);
}

uint16_t bram_get(int index)
{
    MARS_SYS_COMM2 = (uint16_t)index;       /* word 0-2047 of the Sub-CPU's 4 KB buffer */
    if (!cd_cmd(CD_CMD_BUF_GET))
        return 0;
    return (uint16_t)MARS_SYS_COMM8;
}

int bram_save(const uint16_t *words, int count)
{
    for (int i = 0; i < count; i++)
    {
        MARS_SYS_COMM2 = (uint16_t)i;
        MARS_SYS_COMM8 = words[i];
        if (!cd_cmd(CD_CMD_BUF_PUT))
            return 0;
    }
    COMM12_16 = 0;
    if (!cd_cmd(CD_CMD_BRAM_SAVE))
        return keep_status(0);
    int ok = COMM12_16 == 1;
    return keep_status(ok);
}
