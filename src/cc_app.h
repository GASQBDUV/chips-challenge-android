/* Front-end independent application layer: game flow, menu, save, software renderer (640x360 ARGB framebuffer). */
#ifndef CC_APP_H
#define CC_APP_H
#include "cc_engine.h"

#define SCR_W 640
#define SCR_H 360
#define PAN_W 640      /* second-screen panel framebuffer (4:3) */
#define PAN_H 480

enum { B_UP=1, B_DOWN=2, B_LEFT=4, B_RIGHT=8, B_A=16, B_B=32, B_L=64, B_R=128, B_START=256, B_RESTART=512 };
enum { M_INTRO=0, M_PLAY, M_DEAD, M_WON, M_MENU };

/* sound ids */
enum { SND_BLIP, SND_BUMMER, SND_CLICK, SND_DITTY, SND_DOOR, SND_OOF, SND_POP, SND_STRIKE, SND_TELEPORT, SND_WATER, SND_COUNT };

typedef struct {
    Dat dat; Art art; Game g;
    int mode, level_idx, top_idx;            /* level_idx: index into dat.lev; top_idx: highest unlocked index */
    int show_hint, intro_wait, endtimer;
    int menu_sel, menu_level, pass_cur; char pass_in[5]; char menu_msg[64];
    int sound_on, music_mode, cur_track;   /* music_mode: 0 off, 1 FM (OPL3), 2 Android MIDI */
    int gtick, tries, level_idx_prev;
    int score, last_bonus, best_time[MAXLEV], best_score[MAXLEV];
    uint32_t prev_held; int bump_cd; int blink;
    uint32_t snd_mask;                       /* sounds requested during the last step; cleared by the caller */
    char save_path[512];
    int want_quit;
    int touch_mode;                          /* on-screen controls: 0 off, 1 on, 2 auto (hidden once a gamepad/keyboard is used, back on touch) */
    int hw_seen;                             /* set by the front end when physical input is used; cleared by a touch */
    uint32_t fb[SCR_W * SCR_H];
    uint32_t pfb[PAN_W * PAN_H];
} App;

int  app_init(App *a, const char *datadir, const char *savepath);   /* 0 ok */
void app_free(App *a);
void app_start_level(App *a, int idx);
void app_step(App *a, uint32_t held);          /* one 1/20 s tick */
void app_render(App *a);
void app_render_panel(App *a);                 /* second-screen panel (real tiles + real info panel) into a->pfb */
void app_render_full(App *a, uint32_t *out);   /* debug */                      /* draws into a->fb */
int  app_pack_state(const App *a, unsigned char *out, int cap);   /* for the second screen; returns bytes */
uint32_t app_touch_mask(const App *a, float lx, float ly);  /* touch point (logical coords) -> button bit */
void app_save(const App *a);
#define STATE_BYTES 1280
#endif
