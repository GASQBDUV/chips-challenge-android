/* Chip's Challenge engine following the rules recovered from CHIPS.EXE (see re/RULES.md; docs/RULES_IMPL.md).
   Model: two 32x32 layers (up = visible object, lo = tile underneath), table driven legality, 110 ms game tick. */
#ifndef CC_ENGINE_H
#define CC_ENGINE_H
#include "cc_data.h"

enum { N_ = 0, W_ = 1, S_ = 2, E_ = 3 };   /* tile&3 direction order */

enum {
    T_EMPTY=0, T_WALL=1, T_ICCHIP=2, T_WATER=3, T_FIRE=4, T_HIDDEN_PERM=5, T_THIN_N=6, T_THIN_W=7, T_THIN_S=8, T_THIN_E=9,
    T_BLOCK=0x0A, T_DIRT=0x0B, T_ICE=0x0C, T_FORCE_S=0x0D, T_FORCE_N=0x12, T_FORCE_E=0x13, T_FORCE_W=0x14,
    T_EXIT=0x15, T_DOOR_B=0x16, T_DOOR_R=0x17, T_DOOR_G=0x18, T_DOOR_Y=0x19,
    T_ICE_NW=0x1A, T_ICE_NE=0x1B, T_ICE_SE=0x1C, T_ICE_SW=0x1D, T_BLUE_FAKE=0x1E, T_BLUE_REAL=0x1F,
    T_THIEF=0x21, T_SOCKET=0x22, T_BTN_GREEN=0x23, T_BTN_RED=0x24, T_TOGGLE_CLOSED=0x25, T_TOGGLE_OPEN=0x26,
    T_BTN_BROWN=0x27, T_BTN_BLUE=0x28, T_TELEPORT=0x29, T_BOMB=0x2A, T_TRAP=0x2B, T_HIDDEN_TEMP=0x2C,
    T_GRAVEL=0x2D, T_POPUP=0x2E, T_HINT=0x2F, T_THIN_SE=0x30, T_CLONER=0x31, T_FORCE_RAND=0x32,
    T_CHIP_DROWN=0x33, T_CHIP_BURN=0x34, T_CHIP_BOMB=0x35, T_CHIP_SWIM=0x3C,
    T_BUG=0x40, T_FIREBALL=0x44, T_BALL=0x48, T_TANK=0x4C, T_GLIDER=0x50, T_TEETH=0x54, T_WALKER=0x58, T_BLOB=0x5C, T_PARAMECIUM=0x60,
    T_KEY_B=0x64, T_KEY_R=0x65, T_KEY_G=0x66, T_KEY_Y=0x67,
    T_BOOT_WATER=0x68, T_BOOT_FIRE=0x69, T_BOOT_ICE=0x6A, T_BOOT_FORCE=0x6B, T_CHIP=0x6C
};

enum { ST_PLAY=0, ST_WON=1, ST_DEAD=2 };
/* death codes = RULES.md death_state */
enum { D_NONE=0, D_BURN=1, D_DROWN=2, D_BOMB=3, D_BLOCK=4, D_MONSTER=5, D_TIME=6 };

/* sound events (bit mask) */
enum { EV_CHIP=1, EV_ITEM=2, EV_DOOR=4, EV_BUMP=8, EV_DIE=16, EV_WIN=32, EV_TELEPORT=64, EV_SPLASH=128, EV_BOMB=256, EV_BUTTON=512, EV_SOCKET=1024, EV_THIEF=2048, EV_TICK=4096, EV_TIMEOUT=8192 };

typedef struct { int tile, x, y, dx, dy, flag; } Cr;   /* creature list entry (flag=1: sliding) */
#define CRMAX 256
#define SLIPMAX 128

typedef struct {
    const Level *lev;
    unsigned char up[1024], lo[1024];
    Cr cr[CRMAX]; int ncr;
    Cr slip[SLIPMAX]; int nslip;           /* flag 0 = block, 1 = creature */
    int tog[1024], ntog, tele[64], ntele;  /* toggle wall cells, teleport cells (row-major) */
    int trapflag[32];                      /* per trap link: 1 closed, 0 open */
    int chipx, chipy;
    int A, B, C, pdx, pdy, sdx, sdy, idle; /* chip_sliding, moved-this-tick, key pending, pending dir, slide dir */
    int chips_need, time_left, notime;
    int keys[4], boots[4];
    int state, death, events, moves;
    unsigned tick, rng;
} Game;

/* tick0: value of the global tick counter (never reset in the original; pass the running count). */
void game_start(Game *g, const Level *lev, unsigned seed, unsigned tick0);
/* player direction input (key-down or per-tick while held): dir N_..E_ */
void game_input(Game *g, int dir);
/* one 110 ms game tick */
void game_tick(Game *g);
int  game_chips_left(const Game *g);
int  game_seconds_left(const Game *g);
int  game_on_hint(const Game *g);
int  game_chip_dir(const Game *g);
#endif
