#include "cc_app.h"
#include "font_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------ drawing primitives */
static uint32_t *g_fb; static int g_w = SCR_W, g_h = SCR_H;
static void px_put(App *a, int x, int y, uint32_t c) { (void)a; if ((unsigned)x < (unsigned)g_w && (unsigned)y < (unsigned)g_h) g_fb[y * g_w + x] = c; }
static void fill(App *a, int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px_put(a, i, j, c);
}
static void frame(App *a, int x, int y, int w, int h, uint32_t hi, uint32_t lo) {
    fill(a, x, y, w, 1, hi); fill(a, x, y, 1, h, hi); fill(a, x, y + h - 1, w, 1, lo); fill(a, x + w - 1, y, 1, h, lo);
}
static void blit(App *a, const Img *im, int sx, int sy, int w, int h, int dx, int dy, int keyed) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        uint32_t c = im->px[(sy + j) * im->w + sx + i];
        if (keyed && !(c >> 24)) continue;
        px_put(a, dx + i, dy + j, c | 0xFF000000u);
    }
}
static int text_w(const char *s) { int w = 0; for (; *s; s++) if (*s >= 32 && *s < 127) w += font_w[*s - 32]; return w; }
static void text(App *a, int x, int y, const char *s, uint32_t col) {
    for (; *s; s++) {
        if (*s < 32 || *s > 126) continue;
        int g = *s - 32, w = font_w[g]; const unsigned char *d = font_d;
        for (int k = 0; k < g; k++) d += font_w[k] * FONT_H;
        for (int j = 0; j < FONT_H; j++) for (int i = 0; i < w; i++) {
            int al = d[j * w + i]; if (!al) continue;
            int X = x + i, Y = y + j; if ((unsigned)X >= (unsigned)g_w || (unsigned)Y >= (unsigned)g_h) continue;
            uint32_t o = g_fb[Y * g_w + X], r = 0xFF000000u;
            for (int sh = 0; sh < 24; sh += 8) r |= ((((o >> sh) & 255) * (255 - al) + ((col >> sh) & 255) * al) / 255) << sh;
            g_fb[Y * g_w + X] = r;
        }
        x += w;
    }
}
static void text_c(App *a, int cx, int y, const char *s, uint32_t col) { text(a, cx - text_w(s) / 2, y, s, col); }
/* word wrap; returns y after last line */
static int text_wrap(App *a, int x, int y, int maxw, const char *s, uint32_t col) {
    char line[200] = ""; int n = 0; const char *p = s;
    while (*p) {
        const char *e = p; while (*e && *e != ' ') e++;
        char word[100]; int wl = (int)(e - p); if (wl > 99) wl = 99; memcpy(word, p, wl); word[wl] = 0;
        char t[300]; snprintf(t, sizeof t, "%s%s%s", line, n ? " " : "", word);
        if (n && text_w(t) > maxw) { line[n] = 0; text(a, x, y, line, col); y += FONT_H + 1; snprintf(line, sizeof line, "%s", word); n = (int)strlen(line); }
        else { snprintf(line, sizeof line, "%s", t); n = (int)strlen(line); }
        p = e; while (*p == ' ') p++;
    }
    if (n) { text(a, x, y, line, col); y += FONT_H + 1; }
    return y;
}

/* ------------------------------------------------------------ tiles */
static void tile(App *a, int col, int row, int dx, int dy, int keyed) { blit(a, &a->art.tiles, col * 32, row * 32, 32, 32, dx, dy, keyed); }
static void sprite(App *a, int col, int row, int dx, int dy) { tile(a, col + 3, row, dx, dy, 1); }

static const struct { unsigned char t, c, r; } TERMAP[] = {
    {T_EMPTY,0,0},{T_WALL,1,0},{T_ICCHIP,2,0},{T_WATER,3,0},{T_FIRE,4,0},{T_HIDDEN_PERM,0,0},{T_THIN_N,6,0},{T_THIN_W,7,0},{T_THIN_S,8,0},{T_THIN_E,9,0},
    {T_DIRT,11,0},{T_ICE,12,0},{T_FORCE_S,13,0},{T_FORCE_N,2,1},{T_FORCE_E,3,1},{T_FORCE_W,4,1},{T_EXIT,5,1},
    {T_DOOR_B,6,1},{T_DOOR_R,7,1},{T_DOOR_G,8,1},{T_DOOR_Y,9,1},{0x1A,10,1},{0x1B,11,1},{0x1C,12,1},{0x1D,13,1},
    {T_BLUE_FAKE,14,1},{T_BLUE_REAL,15,1},{T_THIEF,1,2},{T_SOCKET,2,2},{T_BTN_GREEN,3,2},{T_BTN_RED,4,2},{T_TOGGLE_CLOSED,5,2},{T_TOGGLE_OPEN,6,2},
    {T_BTN_BROWN,7,2},{T_BTN_BLUE,8,2},{T_TELEPORT,9,2},{T_BOMB,10,2},{T_TRAP,11,2},{T_HIDDEN_TEMP,0,0},{T_GRAVEL,13,2},{T_POPUP,14,2},{T_HINT,15,2},
    {T_THIN_SE,0,3},{T_CLONER,1,3},{T_FORCE_RAND,2,3},
};
static const unsigned char CRSPR[9][2] = { {4,0}, {4,4}, {4,8}, {4,12}, {5,0}, {5,4}, {5,8}, {5,12}, {6,0} };   /* bug fireball ball tank glider teeth walker blob paramecium: (col,row0) */
#define IS_CHIPT(t) (((t) >= 0x6C && (t) <= 0x6F) || ((t) >= 0x3C && (t) <= 0x3F))

static void draw_base(App *a, int t, int dx, int dy) {
    int drawn = 0;
    for (unsigned i = 0; i < sizeof TERMAP / sizeof TERMAP[0]; i++) if (TERMAP[i].t == t) { tile(a, TERMAP[i].r, TERMAP[i].c, dx, dy, 0); drawn = 1; break; }
    if (!drawn) tile(a, 0, 0, dx, dy, 0);
    if (t >= T_KEY_B && t <= T_KEY_Y) sprite(a, 6, 4 + t - T_KEY_B, dx, dy);
    else if (t >= T_BOOT_WATER && t <= T_BOOT_FORCE) sprite(a, 6, 8 + t - T_BOOT_WATER, dx, dy);
}

static void draw_cell(App *a, int cx, int cy, int dx, int dy) {
    Game *g = &a->g; int i = cy * 32 + cx, u = g->up[i], l = g->lo[i];
    int over = (u >= 0x40 && u <= 0x63) || u == 0x0A || (u >= 0x0E && u <= 0x11) || IS_CHIPT(u) || (u >= 0x33 && u <= 0x35);
    draw_base(a, over ? l : u, dx, dy);
    if (!over) return;
    if (u == 0x0A || (u >= 0x0E && u <= 0x11)) tile(a, 3, 1, dx, dy, 0);
    else if (u >= 0x40 && u <= 0x63) sprite(a, CRSPR[(u - 0x40) / 4][0], CRSPR[(u - 0x40) / 4][1] + (u & 3), dx, dy);
    else if (u >= 0x6C && u <= 0x6F) sprite(a, 6, 12 + (u & 3), dx, dy);
    else if (u >= 0x3C && u <= 0x3F) tile(a, 3, 12 + (u & 3), dx, dy, 0);
    else tile(a, 3, 3 + (u - 0x33), dx, dy, 0);
}

static void digits(App *a, int x, int y, int val, int ndig, int green) {
    char s[8]; if (val < 0) snprintf(s, sizeof s, "---"); else snprintf(s, sizeof s, "%*d", ndig, val);
    int base = green ? 12 : 0;
    for (int i = 0; i < ndig; i++) {
        int f = 1;                       /* blank */
        if (s[i] == '-') f = 0; else if (s[i] >= '0' && s[i] <= '9') f = 11 - (s[i] - '0');
        blit(a, &a->art.digits, 0, (base + f) * 23, 17, 23, x + i * 17, y, 0);
    }
}

#define VX 91
#define VY 36
#define PX 395
#define PY 30

static uint32_t C_WHITE = 0xFFFFFFFFu, C_BLACK = 0xFF000000u, C_YEL = 0xFFFFFF40u, C_GRAY = 0xFFC0C0C0u;

static void box(App *a, int x, int y, int w, int h) {
    fill(a, x, y, w, h, 0xFFC0C0C0u); frame(a, x, y, w, h, 0xFFFFFFFFu, 0xFF404040u); frame(a, x + 1, y + 1, w - 2, h - 2, 0xFFDFDFDFu, 0xFF808080u);
}

static const char *DEATHMSG[] = { "", "Ooops! Don't step in the fire without fire boots!", "Ooops! Chip can't swim without flippers!",
    "Ooops! Don't touch the bombs!", "Ooops! Watch out for moving blocks!", "Ooops! Look out for creatures!", "Ooops! Out of time!" };

static void draw_touch(App *a) {
    uint32_t col = 0xFF9090A0u;
    /* d-pad */
    int cx = 45, cy = 235;
    fill(a, cx - 14, cy - 44, 28, 88, 0xFF304050u); fill(a, cx - 44, cy - 14, 88, 28, 0xFF304050u);
    text_c(a, cx, cy - 42, "^", col); text_c(a, cx, cy + 28, "v", col); text(a, cx - 40, cy - 8, "<", col); text(a, cx + 34, cy - 8, ">", col);
    struct { int x, y; const char *s; } b[] = { {596, 250, "A"}, {560, 280, "B"}, {20, 40, "L"}, {590, 40, "R"}, {590, 100, "Menu"} };
    for (int i = 0; i < 5; i++) {
        int w = i == 4 ? 44 : 34;
        fill(a, b[i].x - 17 + (i == 4 ? -5 : 0), b[i].y - 16 + (i == 4 ? 4 : 0), w, 32, 0xFF304050u);
        text_c(a, b[i].x + (i == 4 ? 0 : 0), b[i].y - 8 + (i == 4 ? 4 : 0), b[i].s, col);
    }
}

uint32_t app_touch_mask(float lx, float ly) {
    uint32_t m = 0;
    float dx = lx - 45, dy = ly - 235;
    if (dx * dx + dy * dy < 60 * 60 && dx * dx + dy * dy > 6 * 6) {
        float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax > ay * 0.6f) m |= dx < 0 ? B_LEFT : B_RIGHT;
        if (ay > ax * 0.6f) m |= dy < 0 ? B_UP : B_DOWN;
    }
    if (lx > 575 && lx < 620 && ly > 225 && ly < 275) m |= B_A;
    if (lx > 538 && lx < 580 && ly > 262 && ly < 305) m |= B_B;
    if (lx < 55 && ly < 70) m |= B_L;
    if (lx > 570 && ly < 70) m |= B_R;
    if (lx > 560 && ly > 88 && ly < 130) m |= B_START;
    return m;
}

void app_render_full(App *a, uint32_t *out) {   /* debug: whole 32x32 map at 32px into a 1024x1024 buffer */
    g_fb = out; g_w = g_h = 1024;
    for (int j = 0; j < 32; j++) for (int i = 0; i < 32; i++) draw_cell(a, i, j, i * 32, j * 32);
}

void app_render(App *a) {
    Game *g = &a->g; const Level *L = g->lev;
    g_fb = a->fb; g_w = SCR_W; g_h = SCR_H;
    for (int i = 0; i < SCR_W * SCR_H; i++) a->fb[i] = 0xFF006000u;
    /* circuit board backdrop */
    for (int y = 0; y < SCR_H; y++) for (int x = 0; x < SCR_W; x++) {
        uint32_t c = a->art.circuit.px[(y % a->art.circuit.h) * a->art.circuit.w + (x % a->art.circuit.w)];
        a->fb[y * SCR_W + x] = c | 0xFF000000u;
    }
    for (int i = 0; i < SCR_W * SCR_H; i++) { uint32_t c = a->fb[i]; a->fb[i] = 0xFF000000u | (((c >> 16 & 255) / 2) << 16) | (((c >> 8 & 255) / 2) << 8) | ((c & 255) / 2); }
    fill(a, VX - 4, VY - 4, 296, 296, 0xFF000000u);
    /* viewport */
    int vx = g->chipx - 4, vy = g->chipy - 4;
    if (vx < 0) vx = 0; if (vx > 23) vx = 23; if (vy < 0) vy = 0; if (vy > 23) vy = 23;
    for (int j = 0; j < 9; j++) for (int i = 0; i < 9; i++) draw_cell(a, vx + i, vy + j, VX + i * 32, VY + j * 32);
    /* side panel */
    blit(a, &a->art.panel, 0, 0, a->art.panel.w, a->art.panel.h, PX, PY, 0);
    digits(a, PX + 46, PY + 37, L->num, 3, 0);
    digits(a, PX + 46, PY + 99, game_seconds_left(g), 3, g->notime ? 0 : (game_seconds_left(g) <= 15));
    digits(a, PX + 46, PY + 189, game_chips_left(g), 3, game_chips_left(g) == 0);
    for (int i = 0; i < 4; i++) {
        if (g->keys[i] > 0) { tile(a, 0, 0, PX + 13 + i * 32, PY + 226, 0); sprite(a, 6, 4 + i, PX + 13 + i * 32, PY + 226);
            if (g->keys[i] > 1) { char s[8]; snprintf(s, sizeof s, "%d", g->keys[i]); text(a, PX + 13 + i * 32 + 22, PY + 226 + 17, s, C_BLACK); } }
        else { tile(a, 0, 0, PX + 13 + i * 32, PY + 226, 0); }
        tile(a, 0, 0, PX + 13 + i * 32, PY + 258, 0);
        if (g->boots[i]) sprite(a, 6, 8 + i, PX + 13 + i * 32, PY + 258);
    }
    /* hint overlay in panel */
    if ((game_on_hint(g) || a->show_hint) && L->hint[0] && a->mode == M_PLAY) {
        box(a, PX + 2, PY + 70, 150, 156);
        text(a, PX + 8, PY + 76, "HINT", 0xFFA00000u);
        text_wrap(a, PX + 8, PY + 94, 138, L->hint, C_BLACK);
    }
    /* status line under viewport */
    { char s[100]; snprintf(s, sizeof s, "%s", L->title); text_c(a, VX + 144, VY + 292, s, C_WHITE); }
    /* mode overlays */
    if (a->mode == M_INTRO) {
        box(a, VX + 24, VY + 70, 240, 140);
        char s[64]; snprintf(s, sizeof s, "Level %d", L->num);
        text_c(a, VX + 144, VY + 84, s, 0xFF000080u); text_c(a, VX + 144, VY + 108, L->title, C_BLACK);
        snprintf(s, sizeof s, "Password: %s", L->pass); text_c(a, VX + 144, VY + 134, s, C_BLACK);
        if (L->time) { snprintf(s, sizeof s, "Time %d   Chips %d", L->time, L->chips); } else snprintf(s, sizeof s, "No time limit   Chips %d", L->chips);
        text_c(a, VX + 144, VY + 156, s, 0xFF404040u);
        if (a->best_time[a->level_idx]) { snprintf(s, sizeof s, "Best: %d s left  score %d", a->best_time[a->level_idx], a->best_score[a->level_idx]); text_c(a, VX + 144, VY + 174, s, 0xFF006000u); }
        text_c(a, VX + 144, VY + 192, "Move to start", 0xFF800000u);
    } else if (a->mode == M_DEAD) {
        box(a, VX + 14, VY + 100, 260, 78);
        text_c(a, VX + 144, VY + 112, DEATHMSG[g->death], 0xFF800000u);
        text_c(a, VX + 144, VY + 140, "Press A / tap to try again", C_BLACK);
    } else if (a->mode == M_WON) {
        box(a, VX + 14, VY + 70, 260, 140);
        char s[80]; text_c(a, VX + 144, VY + 82, "Level complete!", 0xFF006000u);
        snprintf(s, sizeof s, "Level bonus %d + time bonus %d", a->last_bonus, g->notime ? 0 : game_seconds_left(g) * 10);
        text_c(a, VX + 144, VY + 108, s, C_BLACK);
        snprintf(s, sizeof s, "Score %d", a->score); text_c(a, VX + 144, VY + 132, s, 0xFF000080u);
        text_c(a, VX + 144, VY + 170, "Press A for the next level", 0xFF800000u);
    } else if (a->mode == M_MENU) {
        box(a, VX + 4, VY + 8, 280, 272);
        text_c(a, VX + 144, VY + 14, "MENU", 0xFF000080u);
        char s[100]; const char *it[8];
        char lv[80]; snprintf(lv, sizeof lv, "< Level %d: %s >", a->dat.lev[a->menu_level].num, a->dat.lev[a->menu_level].title);
        char pw[24]; snprintf(pw, sizeof pw, "Password: [%c%c%c%c] (%d/4)", a->pass_in[0], a->pass_in[1], a->pass_in[2], a->pass_in[3], a->pass_cur + 1);
        it[0] = "Resume"; it[1] = "Restart level"; it[2] = lv; it[3] = "Go to selected level"; it[4] = pw;
        it[5] = a->sound_on ? "Sound: on" : "Sound: off"; it[6] = a->music_mode == 1 ? "Music: FM (OPL3)" : a->music_mode == 2 ? "Music: Android MIDI" : "Music: Off"; it[7] = "Quit";
        for (int i = 0; i < 8; i++) {
            if (i == a->menu_sel) fill(a, VX + 12, VY + 36 + i * 24, 264, 20, 0xFF000080u);
            text_c(a, VX + 144, VY + 38 + i * 24, it[i], i == a->menu_sel ? C_WHITE : C_BLACK);
        }
        snprintf(s, sizeof s, "Unlocked up to level %d", a->dat.lev[a->top_idx].num); text_c(a, VX + 144, VY + 232, s, 0xFF404040u);
        text_c(a, VX + 144, VY + 250, a->menu_msg, 0xFF800000u);
    }
    draw_touch(a);
}

/* ------------------------------------------------------------ save / flow */
void app_save(const App *a) {
    if (!a->save_path[0]) return;
    FILE *f = fopen(a->save_path, "w"); if (!f) return;
    fprintf(f, "top %d\nscore %d\nsound %d\nmusicmode %d\n", a->top_idx, a->score, a->sound_on, a->music_mode);
    for (int i = 0; i < a->dat.n; i++) if (a->best_time[i]) fprintf(f, "b %d %d %d\n", i, a->best_time[i], a->best_score[i]);
    fclose(f);
}
static void app_load_save(App *a) {
    FILE *f = a->save_path[0] ? fopen(a->save_path, "r") : NULL; if (!f) return;
    char k[16]; int x, y, z;
    while (fscanf(f, "%15s", k) == 1) {
        if (!strcmp(k, "top") && fscanf(f, "%d", &x) == 1) { if (x >= 0 && x < a->dat.n) a->top_idx = x; }
        else if (!strcmp(k, "score") && fscanf(f, "%d", &x) == 1) a->score = x;
        else if (!strcmp(k, "sound") && fscanf(f, "%d", &x) == 1) a->sound_on = x;
        else if (!strcmp(k, "music") && fscanf(f, "%d", &x) == 1) a->music_mode = x ? 1 : 0;
        else if (!strcmp(k, "musicmode") && fscanf(f, "%d", &x) == 1) a->music_mode = (x >= 0 && x <= 2) ? x : 1;
        else if (!strcmp(k, "b") && fscanf(f, "%d %d %d", &x, &y, &z) == 3) { if (x >= 0 && x < a->dat.n) { a->best_time[x] = y; a->best_score[x] = z; } }
        else break;
    }
    fclose(f);
}

int app_init(App *a, const char *dir, const char *savepath) {
    memset(a, 0, sizeof *a);
    char p[600];
    snprintf(p, sizeof p, "%s/CHIPS.DAT", dir); if (dat_load(&a->dat, p)) { snprintf(p, sizeof p, "%s/chips.dat", dir); if (dat_load(&a->dat, p)) return -1; }
    snprintf(p, sizeof p, "%s/CHIPS.EXE", dir); if (art_load(&a->art, p)) { snprintf(p, sizeof p, "%s/chips.exe", dir); if (art_load(&a->art, p)) return -2; }
    snprintf(a->save_path, sizeof a->save_path, "%s", savepath ? savepath : "");
    a->sound_on = 1; a->music_mode = 1; a->level_idx_prev = -1; strcpy(a->pass_in, "AAAA");
    app_load_save(a);
    app_start_level(a, a->top_idx);
    return 0;
}
void app_free(App *a) { dat_free(&a->dat); art_free(&a->art); }

void app_start_level(App *a, int idx) {
    if (idx < 0) idx = 0; if (idx >= a->dat.n) idx = a->dat.n - 1;
    a->level_idx = idx; a->menu_level = idx;
    if (idx != a->level_idx_prev) a->tries = 0; else a->tries++;
    a->level_idx_prev = idx;
    game_start(&a->g, &a->dat.lev[idx], 0x1234u + (unsigned)idx * 77u + a->gtick, a->gtick);
    a->mode = M_INTRO; a->show_hint = 0;
    a->cur_track = 1 + (a->dat.lev[idx].num & 1);
}

static int dirs_from(uint32_t h, uint32_t pressed, int *last) {
    if (pressed & B_UP) *last = N_; else if (pressed & B_DOWN) *last = S_; else if (pressed & B_LEFT) *last = W_; else if (pressed & B_RIGHT) *last = E_;
    int want[4] = { !!(h & B_UP), !!(h & B_LEFT), !!(h & B_DOWN), !!(h & B_RIGHT) };
    if (*last >= 0 && want[*last]) return *last;
    for (int d = 0; d < 4; d++) if (want[d]) { *last = d; return d; }
    *last = -1; return -1;
}

static void snd(App *a, int id) { if (a->sound_on) a->snd_mask |= 1u << id; }

static void menu_step(App *a, uint32_t pr) {
    if (pr & B_UP) a->menu_sel = (a->menu_sel + 7) % 8;
    if (pr & B_DOWN) a->menu_sel = (a->menu_sel + 1) % 8;
    int mx = a->dat.n;
    if (a->menu_sel == 2) {
        if (pr & B_LEFT) a->menu_level = (a->menu_level + a->top_idx) % (a->top_idx + 1);
        if (pr & B_RIGHT) a->menu_level = (a->menu_level + 1) % (a->top_idx + 1);
    }
    if ((pr & B_L)) { a->menu_level -= 10; if (a->menu_level < 0) a->menu_level = 0; }
    if ((pr & B_R)) { a->menu_level += 10; if (a->menu_level > a->top_idx) a->menu_level = a->top_idx; }
    if (a->menu_sel == 6) {
        if (pr & B_LEFT) { a->music_mode = (a->music_mode + 2) % 3; app_save(a); }
        if (pr & B_RIGHT) { a->music_mode = (a->music_mode + 1) % 3; app_save(a); }
    }
    if (a->menu_sel == 4) {
        char *c = &a->pass_in[a->pass_cur];
        if (pr & B_LEFT) *c = *c <= 'A' ? 'Z' : *c - 1;
        if (pr & B_RIGHT) *c = *c >= 'Z' ? 'A' : *c + 1;
    }
    (void)mx;
    if (pr & B_B) { a->mode = M_PLAY; return; }
    if (pr & (B_A | B_START)) {
        a->menu_msg[0] = 0;
        switch (a->menu_sel) {
        case 0: a->mode = a->g.state == ST_PLAY && a->g.tick > 0 ? M_PLAY : a->mode == M_MENU ? M_PLAY : a->mode; break;
        case 1: app_start_level(a, a->level_idx); break;
        case 3: app_start_level(a, a->menu_level); break;
        case 4:
            if (a->pass_cur < 3) a->pass_cur++;
            else {
                a->pass_cur = 0; int found = -1;
                for (int i = 0; i < a->dat.n; i++) if (!strncasecmp(a->dat.lev[i].pass, a->pass_in, 4)) found = i;
                if (found >= 0) { if (found > a->top_idx) a->top_idx = found; a->menu_level = found; snprintf(a->menu_msg, sizeof a->menu_msg, "Level %d unlocked", a->dat.lev[found].num); app_save(a); }
                else snprintf(a->menu_msg, sizeof a->menu_msg, "Unknown password");
            }
            break;
        case 5: a->sound_on = !a->sound_on; app_save(a); break;
        case 6: a->music_mode = (a->music_mode + 1) % 3; app_save(a); break;
        case 7: a->want_quit = 1; break;
        }
    }
}

void app_step(App *a, uint32_t held) {
    static int lastdir = -1;
    uint32_t pr = held & ~a->prev_held; a->prev_held = held; a->blink++;
    if (a->bump_cd > 0) a->bump_cd--;
    int d = dirs_from(held, pr, &lastdir);
    Game *g = &a->g;
    if (a->mode == M_MENU) { menu_step(a, pr); return; }
    if (pr & B_START) { a->mode = M_MENU; a->menu_sel = 0; a->menu_level = a->level_idx; a->menu_msg[0] = 0; return; }
    if (pr & B_L) { if (a->level_idx > 0) app_start_level(a, a->level_idx - 1); return; }
    if (pr & B_R) { if (a->level_idx < a->top_idx) app_start_level(a, a->level_idx + 1); return; }
    switch (a->mode) {
    case M_INTRO: if (d >= 0 || (pr & B_A)) a->mode = M_PLAY; else break; /* fallthrough */
    case M_PLAY:
        if (pr & B_B) { app_start_level(a, a->level_idx); return; }
        if (pr & B_A) a->show_hint = !a->show_hint;
        g->events = 0;
        if (d >= 0) game_input(g, d);          /* key-down semantics: one request per tick while held (B/C gating inside) */
        game_tick(g); a->gtick = g->tick;
        if (g->events & EV_CHIP) snd(a, SND_CLICK);
        if (g->events & EV_ITEM) snd(a, SND_BLIP);
        if (g->events & EV_DOOR) snd(a, SND_DOOR);
        if (g->events & EV_SOCKET) snd(a, SND_DOOR);
        if (g->events & EV_THIEF) snd(a, SND_STRIKE);
        if ((g->events & EV_BUMP) && a->bump_cd == 0) { snd(a, SND_OOF); a->bump_cd = 3; }
        if (g->events & EV_TELEPORT) snd(a, SND_TELEPORT);
        if (g->events & EV_SPLASH) snd(a, SND_WATER);
        if (g->events & EV_BOMB) snd(a, SND_STRIKE);
        if (g->events & EV_BUTTON) snd(a, SND_POP);
        if (g->events & EV_TICK) snd(a, SND_CLICK);
        if (g->events & EV_TIMEOUT) snd(a, SND_BUMMER);
        if (g->events & EV_DIE) { snd(a, SND_BUMMER); a->mode = M_DEAD; a->endtimer = 6; }
        if (g->events & EV_WIN) {
            snd(a, SND_DITTY); a->mode = M_WON; a->endtimer = 6;
            int secs = g->notime ? 0 : game_seconds_left(g), lb = g->lev->num * 500;
            for (int k = 0; k < a->tries; k++) { lb = lb * 4 / 5; if (lb < 500) break; }
            a->last_bonus = lb; int sc = secs * 10 + lb, i = a->level_idx;
            if (sc > a->best_score[i]) { a->score += sc - a->best_score[i]; a->best_score[i] = sc; a->best_time[i] = secs; }
            if (i + 1 < a->dat.n && i + 1 > a->top_idx) a->top_idx = i + 1;
            app_save(a);
        }
        break;
    case M_DEAD: if (a->endtimer > 0) a->endtimer--; else if (pr & (B_A | B_B) || d >= 0) app_start_level(a, a->level_idx); break;
    case M_WON: if (a->endtimer > 0) a->endtimer--; else if (pr & B_A) app_start_level(a, a->level_idx + 1 < a->dat.n ? a->level_idx + 1 : a->level_idx); break;
    }
}

/* ------------------------------------------------------------ second-screen state */
static int cellclass(const Game *g, int i) {
    int t = g->up[i]; if ((t >= 0x40 && t <= 0x63) || t == 0x0A || (t >= 0x0E && t <= 0x11) || (t >= 0x33 && t <= 0x3F) || (t >= 0x6C && t <= 0x6F)) t = g->lo[i];
    switch (t) {
    case T_WALL: case T_HIDDEN_PERM: case T_BLUE_REAL: case T_TOGGLE_CLOSED: return 1;
    case T_WATER: return 2; case T_FIRE: return 3; case T_EXIT: return 4; case T_ICCHIP: return 5; case T_ICE: case 0x1A: case 0x1B: case 0x1C: case 0x1D: return 6;
    case T_FORCE_N: case T_FORCE_S: case T_FORCE_E: case T_FORCE_W: case T_FORCE_RAND: return 7;
    case T_DOOR_B: case T_DOOR_R: case T_DOOR_G: case T_DOOR_Y: return 8 + t - T_DOOR_B;
    case T_BOMB: case T_THIEF: return 13; case T_TELEPORT: return 14;
    case T_BTN_GREEN: case T_BTN_RED: case T_BTN_BROWN: case T_BTN_BLUE: return 15;
    case T_SOCKET: return 16; case T_HINT: return 17; case T_TRAP: return 22; case T_DIRT: case T_GRAVEL: case T_POPUP: return 23;
    case T_THIN_N: case T_THIN_W: case T_THIN_S: case T_THIN_E: case T_THIN_SE: return 21; case T_CLONER: return 24;
    }
    if ((t >= T_KEY_B && t <= T_BOOT_FORCE)) return 12;
    return 0;
}
int app_pack_state(const App *a, unsigned char *o, int cap) {
    if (cap < STATE_BYTES) return 0;
    const Game *g = &a->g; memset(o, 0, STATE_BYTES);
    o[0] = 1; o[1] = g->lev->num & 255; o[2] = g->lev->num >> 8;
    int cl = game_chips_left(g); o[3] = cl & 255; o[4] = cl >> 8;
    int s = game_seconds_left(g); if (s < 0) s = 0xFFFF; o[5] = s & 255; o[6] = s >> 8;
    o[7] = g->chipx; o[8] = g->chipy; o[9] = a->mode;
    for (int i = 0; i < 4; i++) { o[10 + i] = g->keys[i]; o[14 + i] = g->boots[i]; }
    o[18] = g->state; o[19] = g->death;
    o[1040] = a->music_mode; o[1041] = a->cur_track; o[1042] = a->mode == M_MENU;   /* music control for the Java side (Android MIDI mode) */
    for (int i = 0; i < 1024; i++) {
        int c = cellclass(g, i), u = g->up[i];
        if (u == 0x0A || (u >= 0x0E && u <= 0x11)) c = 18;
        else if (u >= 0x40 && u <= 0x63) c = 19;
        else if (IS_CHIPT(u)) c = 20;
        o[20 + i] = c;
    }
    snprintf((char *)o + 1044, 64, "%s", g->lev->title);
    snprintf((char *)o + 1108, 160, "%s", g->lev->hint);
    return STATE_BYTES;
}
