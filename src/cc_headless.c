/* Headless driver: renders frames of the real app to BMP. Usage:
   cc_headless DATADIR OUTPREFIX LEVELINDEX "script"
   script chars: u d l r = hold direction for one move (2 ticks); . = idle 4 ticks; a/b/s = press A/B/Start (1 tick);
   '@' = dump frame; 'w' = idle 20 ticks; 'z' = idle 1 tick  */
#include "cc_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(App *a, const char *pre, int n) {
    app_render(a);
    char p[600]; snprintf(p, sizeof p, "%s_%02d.bmp", pre, n);
    FILE *f = fopen(p, "wb"); if (!f) return;
    int w = SCR_W, h = SCR_H; unsigned hdr[14] = {0};
    unsigned char b[54] = {'B','M'}; unsigned sz = 54 + w * h * 4;
    memcpy(b + 2, &sz, 4); unsigned off = 54; memcpy(b + 10, &off, 4); unsigned hs = 40; memcpy(b + 14, &hs, 4);
    memcpy(b + 18, &w, 4); memcpy(b + 22, &h, 4); b[26] = 1; b[28] = 32; (void)hdr;
    fwrite(b, 1, 54, f);
    for (int y = h - 1; y >= 0; y--) fwrite(a->fb + y * w, 4, w, f);
    fclose(f); printf("dumped %s  mode=%d state=%d chips_left=%d pos=%d,%d secs=%d\n", p, a->mode, a->g.state, game_chips_left(&a->g), a->g.cr[0].x, a->g.cr[0].y, game_seconds_left(&a->g));
}
int main(int argc, char **argv) {
    if (argc < 5) return 2;
    static App a; if (app_init(&a, argv[1], "") ) { fprintf(stderr, "init failed\n"); return 1; }
    app_start_level(&a, atoi(argv[3]));
    int n = 0;
    for (const char *s = argv[4]; *s; s++) {
        uint32_t m = 0; int ticks = 2;
        switch (*s) {
        case 'u': m = B_UP; break; case 'd': m = B_DOWN; break; case 'l': m = B_LEFT; break; case 'r': m = B_RIGHT; break;
        case '.': break; case 'w': ticks = 9; break; case 'z': ticks = 1; break;
        case 'a': m = B_A; ticks = 1; break; case 'b': m = B_B; ticks = 1; break; case 's': m = B_START; ticks = 1; break;
        case '@': dump(&a, argv[2], n++); continue;
        case 'M': { static uint32_t big[1024 * 1024]; app_render_full(&a, big); char p[600]; snprintf(p, sizeof p, "%s_map.bmp", argv[2]); FILE *f = fopen(p, "wb");
            int w = 1024, h = 1024; unsigned char b[54] = {'B','M'}; unsigned sz = 54 + w * h * 4, off = 54, hs = 40; memcpy(b + 2, &sz, 4); memcpy(b + 10, &off, 4); memcpy(b + 14, &hs, 4); memcpy(b + 18, &w, 4); memcpy(b + 22, &h, 4); b[26] = 1; b[28] = 32;
            fwrite(b, 1, 54, f); for (int y = h - 1; y >= 0; y--) fwrite(big + y * w, 4, w, f); fclose(f); } continue;
        default: continue;
        }
        for (int t = 0; t < ticks; t++) app_step(&a, m);
        if (ticks == 1 && m) app_step(&a, 0);
    }
    { unsigned char st[STATE_BYTES]; app_pack_state(&a, st, sizeof st);
      printf("state: v%d level %d chips %d secs %d chip %d,%d mode %d title '%s' hint '%.40s'\n", st[0], st[1] | st[2] << 8, st[3] | st[4] << 8, st[5] | st[6] << 8, st[7], st[8], st[9], st + 1044, st + 1108);
      for (int y = 8; y < 20; y++) { for (int x = 8; x < 26; x++) printf("%c", " #~^X*i>cK?@!MP"[st[20 + y * 32 + x] % 15]); printf("\n"); } }
    return 0;
}
