#include "cc_engine.h"
#include "tile_tt.h"
#include <string.h>

static const int DX[4] = {0, -1, 0, 1}, DY[4] = {-1, 0, 1, 0};
static int dirof(int dx, int dy) { return dy < 0 ? 0 : dx < 0 ? 1 : dy > 0 ? 2 : dx > 0 ? 3 : -1; }
#define IS_CHIPT(t) (((t) >= 0x6C && (t) <= 0x6F) || ((t) >= 0x3C && (t) <= 0x3F))
#define IS_MON(t) ((t) >= 0x40 && (t) <= 0x63)
#define IS_FORCE(t) ((t) == 0x0D || (t) == 0x12 || (t) == 0x13 || (t) == 0x14 || (t) == 0x32)
#define IS_CORNER(t) ((t) >= 0x1A && (t) <= 0x1D)
#define IS_SLIPPERY(t) ((t) == 0x0C || IS_FORCE(t) || IS_CORNER(t) || (t) == 0x29)

static int rnd(Game *g) { g->rng = g->rng * 0x343FDu + 0x269EC3u; return (g->rng >> 16) & 0x7FFF; }

int game_chips_left(const Game *g) { return g->chips_need > 0 ? g->chips_need : 0; }
int game_seconds_left(const Game *g) { return g->notime ? -1 : g->time_left; }
int game_on_hint(const Game *g) { return g->lo[g->chipy * 32 + g->chipx] == T_HINT; }
int game_chip_dir(const Game *g) { return g->up[g->chipy * 32 + g->chipx] & 3; }

static void kill_chip(Game *g, int why) {
    if (g->state != ST_PLAY) return;
    g->state = ST_DEAD; g->death = why; g->events |= EV_DIE;
}

/* ---- geometry (RULES.md sec 1, thin_wall_pass 1010:1934) ---- */
static int thin_pass(int t, int dx, int dy, int entering) {
    int d = dirof(dx, dy);
    if (entering) switch (t) {
        case 6: return d != S_; case 7: return d != E_; case 8: return d != N_; case 9: return d != W_;
        case 0x30: return d != N_ && d != W_;
        case 0x1A: return d != S_ && d != E_; case 0x1B: return d != S_ && d != W_;
        case 0x1C: return d != N_ && d != W_; case 0x1D: return d != N_ && d != E_;
    } else switch (t) {
        case 6: return d != N_; case 7: return d != W_; case 8: return d != S_; case 9: return d != E_;
        case 0x30: return d != S_ && d != E_;
    }
    return 1;
}
static int is_thinlike(int t) { return (t >= 6 && t <= 9) || t == 0x30 || IS_CORNER(t); }

static void compute_slide_dir(Game *g, int t, int *dx, int *dy) {
    int d = dirof(*dx, *dy), nd = -1;
    switch (t) {
    case 0x0D: nd = S_; break; case 0x12: nd = N_; break; case 0x13: nd = E_; break; case 0x14: nd = W_; break;
    case 0x32: { int r = rnd(g) % 4; nd = r == 0 ? N_ : r == 1 ? S_ : r == 2 ? W_ : E_; } break;
    case 0x1A: nd = d == W_ ? S_ : d == N_ ? E_ : (d ^ 2); break;
    case 0x1B: nd = d == N_ ? W_ : d == E_ ? S_ : (d ^ 2); break;
    case 0x1C: nd = d == S_ ? W_ : d == E_ ? N_ : (d ^ 2); break;
    case 0x1D: nd = d == W_ ? N_ : d == S_ ? E_ : (d ^ 2); break;
    default: return;
    }
    if (d < 0 && nd < 0) return;
    *dx = DX[nd]; *dy = DY[nd];
}

/* ---- traps, buttons ---- */
static int trap_idx_closed(Game *g, int x, int y, int *found) {
    int cl = 0; *found = 0;
    for (int i = 0; i < g->lev->ntrap; i++) if (g->lev->trap[i].x2 == x && g->lev->trap[i].y2 == y) { *found = 1; if (g->trapflag[i]) cl = 1; }
    return cl;
}
static int trap_blocks_leave(Game *g, int x, int y) { int f; int cl = trap_idx_closed(g, x, y, &f); return !f || cl; }
static void trap_recalc(Game *g, int x, int y, int ex, int ey) {
    int held = 0;
    for (int i = 0; i < g->lev->ntrap; i++) { const Link *l = &g->lev->trap[i]; if (l->x2 != x || l->y2 != y) continue;
        if (l->x1 == ex && l->y1 == ey) continue;
        int b = l->y1 * 32 + l->x1; if (b >= 0 && b < 1024 && g->up[b] != T_BTN_BROWN) held = 1; }
    for (int i = 0; i < g->lev->ntrap; i++) if (g->lev->trap[i].x2 == x && g->lev->trap[i].y2 == y) g->trapflag[i] = held ? 0 : 1;
}
static void brown_press(Game *g, int x, int y) {
    for (int i = 0; i < g->lev->ntrap; i++) if (g->lev->trap[i].x1 == x && g->lev->trap[i].y1 == y) {
        int tx = g->lev->trap[i].x2, ty = g->lev->trap[i].y2;
        for (int j = 0; j < g->lev->ntrap; j++) if (g->lev->trap[j].x2 == tx && g->lev->trap[j].y2 == ty) g->trapflag[j] = 0;
    }
}

static int move_block(Game *g, int x, int y, int dx, int dy, int mk);
static int mon_check(Game *g, int tile, int x, int y, int dx, int dy, int *kind);

static void cr_add(Game *g, int tile, int x, int y) {
    if (g->ncr >= CRMAX) return;
    int d = tile & 3; Cr *c = &g->cr[g->ncr++]; c->tile = tile; c->x = x; c->y = y; c->dx = DX[d]; c->dy = DY[d]; c->flag = 0;
}

static void red_clone(Game *g, int bx, int by) {
    for (int i = 0; i < g->lev->nclone; i++) {
        const Link *l = &g->lev->clone[i]; if (l->x1 != bx || l->y1 != by) continue;
        int m = l->y2 * 32 + l->x2; if (m < 0 || m > 1023) continue;
        int t = g->up[m];
        if (IS_MON(t)) {
            int d = t & 3, nx = l->x2 + DX[d], ny = l->y2 + DY[d], k;
            if (nx < 0 || nx > 31 || ny < 0 || ny > 31) continue;
            int dup = 0; for (int j = 0; j < g->ncr; j++) if (g->cr[j].x == l->x2 && g->cr[j].y == l->y2) dup = 1;
            if (dup) continue;
            if (!mon_check(g, t, nx, ny, DX[d], DY[d], &k) && g->up[ny * 32 + nx] != t) continue;
            cr_add(g, t, l->x2, l->y2);
        } else if (t >= 0x0E && t <= 0x11) {
            int d = t - 0x0E; g->up[m] = T_BLOCK;
            move_block(g, l->x2, l->y2, DX[d], DY[d], 1);
            g->up[m] = t;
        }
    }
}

static void press_button(Game *g, int btn, int x, int y, int bychip) {
    switch (btn) {
    case T_BTN_GREEN: for (int i = 0; i < g->ntog; i++) { int c = g->tog[i];
            if (g->up[c] == 0x25) g->up[c] = 0x26; else if (g->up[c] == 0x26) g->up[c] = 0x25;
            if (g->lo[c] == 0x25) g->lo[c] = 0x26; else if (g->lo[c] == 0x26) g->lo[c] = 0x25; } break;
    case T_BTN_RED: red_clone(g, x, y); if (bychip) g->events |= EV_BUTTON; break;
    case T_BTN_BROWN: brown_press(g, x, y); break;
    case T_BTN_BLUE:
        for (int i = 0; i < g->ncr; i++) { Cr *c = &g->cr[i]; if (c->tile >= 0x4C && c->tile <= 0x4F) {
            int d = (c->tile & 3) ^ 2; c->tile = 0x4C + d; c->dx = DX[d]; c->dy = DY[d]; if (g->up[c->y * 32 + c->x] >= 0x4C && g->up[c->y * 32 + c->x] <= 0x4F) g->up[c->y * 32 + c->x] = c->tile; } }
        if (bychip) g->events |= EV_BUTTON; break;
    }
}

/* ---- teleport ---- */
static int tele_find(Game *g, int mode, int x, int y, int dx, int dy) {
    int cur = y * 32 + x, i = -1;
    for (int k = 0; k < g->ntele; k++) if (g->tele[k] == cur) i = k;
    if (i < 0) return -1;
    for (int k = 1; k < g->ntele; k++) {
        int c = g->tele[((i - k) % g->ntele + g->ntele) % g->ntele];
        if (g->up[c] != T_TELEPORT) continue;
        int ex = (c & 31) + dx, ey = (c >> 5) + dy; if (ex < 0 || ex > 31 || ey < 0 || ey > 31) continue;
        int e = ey * 32 + ex, kind;
        if (g->lo[e] == 0x31) continue;
        if (g->up[e] == T_BLOCK) { if (mode == 1) continue; if (!move_block(g, ex, ey, dx, dy, 1)) continue; }
        if (mode == 0) { int t = g->up[e], cls = TT[t][0]; if (IS_MON(t) || IS_CHIPT(t)) continue; if (!cls) continue; if (t == 0x25 || t == 0x1F || t == 0x2C || t == 0x31) continue;
            if (cls == 2 && TT[t][1] == 3) continue; if (is_thinlike(t) && !thin_pass(t, dx, dy, 1)) continue; (void)kind; }
        else if (mode == 1) { int t = g->up[e]; if (!TT[t][2] || (is_thinlike(t) && !thin_pass(t, dx, dy, 1))) continue; }
        else { if (!mon_check(g, T_BUG, ex, ey, dx, dy, &kind)) continue; }
        return c;
    }
    return -1;
}

/* ---- blocks ---- */
static int slip_find(Game *g, int flag, int x, int y) { for (int i = 0; i < g->nslip; i++) if (g->slip[i].flag == flag && g->slip[i].x == x && g->slip[i].y == y) return i; return -1; }
static void slip_remove(Game *g, int i) { if (i >= 0 && i < g->nslip) { g->slip[i] = g->slip[g->nslip - 1]; g->nslip--; } }
static void slip_add(Game *g, int flag, int tile, int x, int y, int dx, int dy) {
    if (g->nslip >= SLIPMAX) return;
    Cr *s = &g->slip[g->nslip++]; s->flag = flag; s->tile = tile; s->x = x; s->y = y; s->dx = dx; s->dy = dy;
}

static int block_check(Game *g, int x, int y, int dx, int dy, int *kind) {
    int i = y * 32 + x; if (g->lo[i] == 0x31) return 0;
    int t = g->up[i]; if (IS_CHIPT(t)) { *kind = 1; return 1; }
    int cls = TT[t][2]; *kind = TT[t][3]; if (!cls) return 0;
    if (cls == 2 && is_thinlike(t) && !thin_pass(t, dx, dy, 1)) return 0;
    return 1;
}

static void src_cleanup(Game *g, int s) { if (g->lo[s] != 0x31) { g->up[s] = g->lo[s]; g->lo[s] = 0; } }

static int move_block(Game *g, int x, int y, int dx, int dy, int mk) {
    int nx = x + dx, ny = y + dy; if (nx < 0 || nx > 31 || ny < 0 || ny > 31) return 0;
    int s = y * 32 + x, e = ny * 32 + nx, kind;
    if (g->lo[s] == T_TRAP && trap_blocks_leave(g, x, y)) return 0;
    if (is_thinlike(g->lo[s]) && !thin_pass(g->lo[s], dx, dy, 0)) return 0;
    if (!block_check(g, nx, ny, dx, dy, &kind)) return 0;
    int old = g->up[e];
    switch (kind) {
    case 2: g->up[e] = 0x0B; g->events |= EV_SPLASH; src_cleanup(g, s); { int k = slip_find(g, 0, x, y); slip_remove(g, k); } return 1;
    case 5: g->up[e] = 0; g->events |= EV_BOMB; src_cleanup(g, s); { int k = slip_find(g, 0, x, y); slip_remove(g, k); } return 1;
    default: break;
    }
    g->lo[e] = old; g->up[e] = T_BLOCK; src_cleanup(g, s);
    { int k = slip_find(g, 0, x, y); if (k >= 0) { g->slip[k].x = nx; g->slip[k].y = ny; } }
    if (IS_CHIPT(old)) { kill_chip(g, D_BLOCK); return 1; }
    if (old == T_BTN_GREEN || old == T_BTN_RED || old == T_BTN_BROWN || old == T_BTN_BLUE) press_button(g, old, nx, ny, 0);
    if (kind == 6) trap_recalc(g, nx, ny, x, y);
    if (kind == 7) {
        int c = tele_find(g, 1, nx, ny, dx, dy);
        if (c >= 0) { g->up[e] = T_TELEPORT; g->lo[e] = 0; g->up[c] = T_BLOCK; g->lo[c] = T_TELEPORT; nx = c & 31; ny = c >> 5; g->events |= EV_TELEPORT;
            int k = slip_find(g, 0, x, y); if (k >= 0) { g->slip[k].x = nx; g->slip[k].y = ny; } }
        if (mk && slip_find(g, 0, nx, ny) < 0) slip_add(g, 0, T_BLOCK, nx, ny, dx, dy);
    } else if (kind == 4 && mk && slip_find(g, 0, nx, ny) < 0) {
        int sx = dx, sy = dy; compute_slide_dir(g, g->lo[ny * 32 + nx], &sx, &sy); slip_add(g, 0, T_BLOCK, nx, ny, sx, sy);
    }
    return 1;
}

/* ---- Chip ---- */
static int boots_protect(Game *g, int t) {
    if (t == 3) return g->boots[0]; if (t == 4) return g->boots[1];
    if (t == 0x0C || IS_CORNER(t)) return g->boots[2];
    if (t == 0x0D || t == 0x12 || t == 0x13 || t == 0x14 || t == 0x32) return g->boots[3];
    return 0;
}

/* returns 1 if Chip may enter (x,y); *kind = landing kind. commit applies consumption/reveal side effects. */
static int chip_can_enter(Game *g, int x, int y, int dx, int dy, int commit, int *kind) {
    int i = y * 32 + x; if (g->lo[i] == 0x31) return 0;
    int t = g->up[i], k;
    if (IS_MON(t) || t == T_BLOCK) {      /* creature/block: judge the tile beneath; Chip dies */
        int lt = g->lo[i]; if (t == T_BLOCK) return 0;
        int c0 = TT[lt][0]; if (!c0) return 0;
        *kind = 2; return 1;
    }
    int cls = TT[t][0]; k = TT[t][1]; if (!cls) return 0;
    if (cls == 2) {
        if (is_thinlike(t)) { if (!thin_pass(t, dx, dy, 1)) return 0; if (IS_CORNER(t)) k = boots_protect(g, t) ? 4 : 5; }
        else switch (t) {
        case 0x16: case 0x17: case 0x18: case 0x19: if (g->keys[t - 0x16] <= 0) return 0;
            if (commit) { if (t != 0x18) g->keys[t - 0x16]--; g->events |= EV_DOOR; } break;
        case 3: case 4: case 0x0C: case 0x0D: case 0x12: case 0x13: case 0x14: case 0x32: if (boots_protect(g, t)) k = 4; break;
        case 0x1E: if (commit) g->up[i] = 0; break;
        case 0x1F: case 0x2C: if (commit) { g->up[i] = 1; g->events |= EV_BUMP; } return 0;
        case 0x21: if (commit) { memset(g->boots, 0, sizeof g->boots); g->events |= EV_THIEF; } break;
        case 0x22: if (g->chips_need > 0) return 0; if (commit) g->events |= EV_SOCKET; break;
        case 0x2E: if (commit) g->up[i] = 1; break;      /* wall appears; Chip stands in it */
        default: break;
        }
    }
    *kind = k; return 1;
}

static void chip_face(Game *g, int d) {
    int i = g->chipy * 32 + g->chipx; if (!IS_CHIPT(g->up[i])) return;
    g->up[i] = (g->lo[i] == 3 ? 0x3C : 0x6C) + d;
}

static int chip_move(Game *g, int dx, int dy, int user, int bump) {
    if (g->state != ST_PLAY) return 0;
    if (user) { if (g->B) { if (!g->C) { g->C = 1; g->pdx = dx; g->pdy = dy; } return 0; } g->B = 1; g->C = 0; }
    g->idle = 0;
    int d = dirof(dx, dy), x = g->chipx, y = g->chipy, nx = x + dx, ny = y + dy, s = y * 32 + x;
    if (g->A && user) {
        int lt = g->lo[s];
        if (lt == 0x0C || IS_CORNER(lt) || lt == 0x29) return 0;
        if (IS_FORCE(lt) && dx == g->sdx && dy == g->sdy) return 0;
    }
    if (d >= 0) chip_face(g, d);
    if (nx < 0 || nx > 31 || ny < 0 || ny > 31) { if (bump) g->events |= EV_BUMP; return 0; }
    if (g->lo[s] == T_TRAP && trap_blocks_leave(g, x, y)) { if (bump) g->events |= EV_BUMP; return 0; }
    if (is_thinlike(g->lo[s]) && !thin_pass(g->lo[s], dx, dy, 0)) { if (bump) g->events |= EV_BUMP; return 0; }
    int e = ny * 32 + nx;
    if (g->up[e] == T_BLOCK) {
        int k = slip_find(g, 0, nx, ny);
        if (k >= 0) { int sd = dirof(g->slip[k].dx, g->slip[k].dy); if (sd == d || sd == (d ^ 2)) { if (bump) g->events |= EV_BUMP; return 0; } }
        if (!move_block(g, nx, ny, dx, dy, 1)) { if (bump) g->events |= EV_BUMP; return 0; }
        if (g->state != ST_PLAY) return 1;
    }
    int kind = 0;
    if (!chip_can_enter(g, nx, ny, dx, dy, 1, &kind)) { if (bump) g->events |= EV_BUMP; return 0; }
    int old = g->up[e];
    g->up[s] = g->lo[s]; g->lo[s] = 0;
    g->chipx = nx; g->chipy = ny; g->moves++;
    int oldlo = g->lo[e]; g->lo[e] = 0;
    int swim = 0;
    switch (kind) {
    case 1:
        if (old == 2) { if (g->chips_need > 0) g->chips_need--; g->events |= EV_CHIP; }
        else if (old >= 0x64 && old <= 0x67) { g->keys[old - 0x64]++; g->events |= EV_ITEM; }
        else if (old >= 0x68 && old <= 0x6B) { g->boots[old - 0x68] = 1; g->events |= EV_ITEM; }
        break;
    case 2: {
        int why = old == 0x2A ? D_BOMB : old == 3 ? D_DROWN : old == 4 ? D_BURN : D_MONSTER;
        g->lo[e] = IS_MON(old) ? oldlo : old;
        g->up[e] = why == D_DROWN ? 0x33 : why == D_BURN ? 0x34 : why == D_BOMB ? 0x35 : 0x6C + d;
        if (why == D_BOMB) { g->events |= EV_BOMB; } else if (why == D_DROWN) g->events |= EV_SPLASH;
        g->A = 0; kill_chip(g, why); return 1; }
    case 3: break;
    case 4: case 5: case 6: case 7: g->lo[e] = old; break;
    }
    if (kind == 4 || kind == 5 || kind == 6 || kind == 7) g->lo[e] = old;
    int lo = g->lo[e];
    swim = lo == 3;
    g->up[e] = (swim ? 0x3C : 0x6C) + d;
    g->A = 0;
    if (kind == 5) { g->A = 1; g->sdx = dx; g->sdy = dy; compute_slide_dir(g, lo, &g->sdx, &g->sdy); }
    if (kind == 4 && (lo == T_BTN_GREEN || lo == T_BTN_RED || lo == T_BTN_BROWN || lo == T_BTN_BLUE)) press_button(g, lo, nx, ny, 1);
    if (kind == 6) trap_recalc(g, nx, ny, x, y);
    if (kind == 7) {
        g->A = 1; g->sdx = dx; g->sdy = dy;
        int c = tele_find(g, 0, nx, ny, dx, dy);
        if (c >= 0) { g->up[e] = T_TELEPORT; g->lo[e] = 0; g->chipx = c & 31; g->chipy = c >> 5; g->up[c] = 0x6C + d; g->lo[c] = T_TELEPORT; g->events |= EV_TELEPORT; }
    }
    if (g->lo[g->chipy * 32 + g->chipx] == T_EXIT) { g->state = ST_WON; g->events |= EV_WIN; }
    return 1;
}

/* ---- monsters ---- */
static int mon_check(Game *g, int tile, int x, int y, int dx, int dy, int *kind) {
    int i = y * 32 + x; if (g->lo[i] == 0x31) return 0;
    int t = g->up[i]; if (IS_CHIPT(t)) t = g->lo[i];
    int cls = TT[t][4], k = TT[t][5]; if (!cls) return 0;
    if (cls == 2) {
        if (is_thinlike(t)) { if (!thin_pass(t, dx, dy, 1)) return 0; }
        else if (t == 3) { if (tile >= 0x50 && tile <= 0x53) k = 1; }
        else if (t == 4) { if (tile >= 0x40 && tile <= 0x43) return 0; if (tile >= 0x58 && tile <= 0x5B) return 0; if (tile >= 0x44 && tile <= 0x47) k = 1; }
    }
    *kind = k; return 1;
}

static void mon_remove(Game *g, int i) { for (int j = i; j < g->ncr - 1; j++) g->cr[j] = g->cr[j + 1]; g->ncr--; }

/* 0 blocked, 1 moved, 2 died */
static int monster_move(Game *g, int i, int dx, int dy, int *slipped) {
    Cr *c = &g->cr[i]; *slipped = 0;
    if (!dx && !dy) return 0;
    int x = c->x, y = c->y, nx = x + dx, ny = y + dy; if (nx < 0 || nx > 31 || ny < 0 || ny > 31) return 0;
    int s = y * 32 + x, e = ny * 32 + nx, kind;
    if (g->lo[s] == T_TRAP && trap_blocks_leave(g, x, y)) return 0;
    if (is_thinlike(g->lo[s]) && !thin_pass(g->lo[s], dx, dy, 0)) return 0;
    if (!mon_check(g, c->tile, nx, ny, dx, dy, &kind)) return 0;
    int old = g->up[e], d = dirof(dx, dy), nt = (c->tile & 0xFC) + d;
    if (kind == 2) { src_cleanup(g, s); return 2; }
    if (kind == 5) { g->up[e] = 0; g->events |= EV_BOMB; src_cleanup(g, s); return 2; }
    g->lo[e] = old; g->up[e] = nt; src_cleanup(g, s);
    c->x = nx; c->y = ny; c->tile = nt; c->dx = dx; c->dy = dy;
    int lo = g->lo[e];
    if (lo == T_BTN_GREEN || lo == T_BTN_RED || lo == T_BTN_BROWN || lo == T_BTN_BLUE) press_button(g, lo, nx, ny, 0);
    if (kind == 6) trap_recalc(g, nx, ny, x, y);
    if (kind == 7) {
        int cc = tele_find(g, 2, nx, ny, dx, dy);
        if (cc >= 0) { g->up[e] = T_TELEPORT; g->lo[e] = 0; g->up[cc] = nt; g->lo[cc] = T_TELEPORT; c->x = cc & 31; c->y = cc >> 5; }
        *slipped = 1;
    } else if (kind == 4) *slipped = 1;
    if (IS_CHIPT(g->lo[c->y * 32 + c->x])) kill_chip(g, D_MONSTER);
    return 1;
}

static void left_of(int dx, int dy, int *lx, int *ly) { int d = dirof(dx, dy); if (d < 0) { *lx = *ly = 0; return; } d = (d + 1) & 3; *lx = DX[d]; *ly = DY[d]; }
static void right_of(int dx, int dy, int *lx, int *ly) { int d = dirof(dx, dy); if (d < 0) { *lx = *ly = 0; return; } d = (d + 3) & 3; *lx = DX[d]; *ly = DY[d]; }

/* try move; handles slide-entry creation. returns 0/1/2 */
static int mtry(Game *g, int i, int dx, int dy) {
    int sl; int r = monster_move(g, i, dx, dy, &sl);
    if (r == 1 && sl) {
        Cr *c = &g->cr[i]; int lo = g->lo[c->y * 32 + c->x]; int sx = dx, sy = dy; compute_slide_dir(g, lo, &sx, &sy);
        if (IS_SLIPPERY(lo)) { c->flag = 1; slip_add(g, 1, c->tile, c->x, c->y, sx, sy); }
    }
    return r;
}

static void monsters_tick(Game *g, int fourth) {
    int n = g->ncr;
    for (int i = 0; i < n; i++) {
        Cr *c = &g->cr[i]; if (c->flag) continue;
        int t = c->tile, dx = c->dx, dy = c->dy, r = 0, lx, ly, rx, ry;
        int onspecial = g->lo[c->y * 32 + c->x] == T_TRAP || g->lo[c->y * 32 + c->x] == 0x31;
        left_of(dx, dy, &lx, &ly); right_of(dx, dy, &rx, &ry);
        int moved = 0;
        if (t >= 0x40 && t <= 0x43) {
            if (onspecial) r = mtry(g, i, dx, dy);
            else if ((r = mtry(g, i, lx, ly)) == 0 && (r = mtry(g, i, dx, dy)) == 0 && (r = mtry(g, i, rx, ry)) == 0) r = mtry(g, i, -dx, -dy);
        } else if (t >= 0x44 && t <= 0x47) {
            if (onspecial) r = mtry(g, i, dx, dy);
            else if ((r = mtry(g, i, dx, dy)) == 0 && (r = mtry(g, i, rx, ry)) == 0 && (r = mtry(g, i, lx, ly)) == 0) r = mtry(g, i, -dx, -dy);
        } else if (t >= 0x48 && t <= 0x4B) {
            if ((r = mtry(g, i, dx, dy)) == 0 && !onspecial) r = mtry(g, i, -dx, -dy);
        } else if (t >= 0x4C && t <= 0x4F) {
            r = mtry(g, i, dx, dy);
            if (r == 0) { int f; int cl = g->lo[c->y * 32 + c->x] == T_TRAP ? trap_idx_closed(g, c->x, c->y, &f) : 0; if (!cl) { c->dx = 0; c->dy = 0; } }
        } else if (t >= 0x50 && t <= 0x53) {
            if (onspecial) r = mtry(g, i, dx, dy);
            else if ((r = mtry(g, i, dx, dy)) == 0 && (r = mtry(g, i, lx, ly)) == 0 && (r = mtry(g, i, rx, ry)) == 0) r = mtry(g, i, -dx, -dy);
        } else if (t >= 0x60 && t <= 0x63) {
            if (onspecial) r = mtry(g, i, dx, dy);
            else if ((r = mtry(g, i, rx, ry)) == 0 && (r = mtry(g, i, dx, dy)) == 0 && (r = mtry(g, i, lx, ly)) == 0) r = mtry(g, i, -dx, -dy);
        } else if (t >= 0x54 && t <= 0x57) {
            if (!fourth) continue;
            int cx = g->chipx - c->x, cy = g->chipy - c->y, ax = cx < 0 ? -cx : cx, ay = cy < 0 ? -cy : cy;
            int pdx = 0, pdy = 0, odx = 0, ody = 0;
            if (ay < ax) { pdx = cx < 0 ? -1 : 1; if (cy) ody = cy < 0 ? -1 : 1; } else { pdy = cy <= 0 ? -1 : 1; if (cx) odx = cx < 0 ? -1 : 1; }
            r = mtry(g, i, pdx, pdy);
            if (r == 0 && !onspecial && (odx || ody)) r = mtry(g, i, odx, ody);
            if (r == 0) { int d = dirof(pdx, pdy); c->tile = 0x54 + d; g->up[c->y * 32 + c->x] = c->tile; c->dx = pdx; c->dy = pdy; }
        } else if (t >= 0x58 && t <= 0x5B) {
            r = mtry(g, i, dx, dy);
            if (r == 0 && !onspecial) { int tried = 0, tries = 0;
                while (tried != 7 && tries++ < 40) { int k = rnd(g) % 3; if (tried & (1 << k)) continue; tried |= 1 << k;
                    r = k == 0 ? mtry(g, i, lx, ly) : k == 1 ? mtry(g, i, rx, ry) : mtry(g, i, -dx, -dy); if (r) break; } }
        } else if (t >= 0x5C && t <= 0x5F) {
            if (!fourth) continue;
            int ddx, ddy; do { ddx = rnd(g) % 3 - 1; ddy = rnd(g) % 3 - 1; } while ((ddx != 0) + (ddy != 0) != 1);
            r = mtry(g, i, ddx, ddy);
            if (r == 0 && !onspecial) { int l2x, l2y, r2x, r2y; left_of(ddx, ddy, &l2x, &l2y); right_of(ddx, ddy, &r2x, &r2y); int tried = 0, tries = 0;
                while (tried != 7 && tries++ < 40) { int k = rnd(g) % 3; if (tried & (1 << k)) continue; tried |= 1 << k;
                    r = k == 0 ? mtry(g, i, l2x, l2y) : k == 1 ? mtry(g, i, r2x, r2y) : mtry(g, i, -ddx, -ddy); if (r) break; } }
        } else continue;
        (void)moved;
        if (r == 2) { mon_remove(g, i); i--; n--; }
        if (g->state != ST_PLAY) return;
    }
}

static void slip_tick(Game *g) {
    for (int j = 0; j < g->nslip; j++) {
        Cr *s = &g->slip[j];
        if (s->flag == 0) {
            if (g->up[s->y * 32 + s->x] != T_BLOCK) { slip_remove(g, j); j--; continue; }
            int x = s->x, y = s->y, dx = s->dx, dy = s->dy;
            if (!move_block(g, x, y, dx, dy, 0)) {
                int lo = g->lo[y * 32 + x];
                if (lo == 0x0C || IS_CORNER(lo)) { s = &g->slip[j]; s->dx = -dx; s->dy = -dy; compute_slide_dir(g, lo, &s->dx, &s->dy); if (!move_block(g, x, y, s->dx, s->dy, 0)) { slip_remove(g, j); j--; } }
                else { slip_remove(g, j); j--; }
                continue;
            }
            int k = -1; for (int q = 0; q < g->nslip; q++) if (g->slip[q].flag == 0 && g->slip[q].x == x + dx && g->slip[q].y == y + dy) k = q;
            if (k < 0) { j--; continue; }   /* block vanished (water/bomb): entry already dropped */
            s = &g->slip[k]; { int lo = g->lo[s->y * 32 + s->x]; if (!IS_SLIPPERY(lo)) { slip_remove(g, k); if (k <= j) j--; } else compute_slide_dir(g, lo, &s->dx, &s->dy); }
        } else {
            int ci = -1; for (int q = 0; q < g->ncr; q++) if (g->cr[q].x == s->x && g->cr[q].y == s->y) ci = q;
            if (ci < 0) { slip_remove(g, j); j--; continue; }
            int dx = s->dx, dy = s->dy, sl, r = monster_move(g, ci, dx, dy, &sl);
            if (r == 0) {
                int lo = g->lo[s->y * 32 + s->x];
                if (lo == 0x0C || IS_CORNER(lo)) { s->dx = -dx; s->dy = -dy; compute_slide_dir(g, lo, &s->dx, &s->dy); r = monster_move(g, ci, s->dx, s->dy, &sl); }
            }
            if (r == 0) { g->cr[ci].flag = 0; slip_remove(g, j); j--; continue; }
            if (r == 2) { mon_remove(g, ci); slip_remove(g, j); j--; continue; }
            Cr *c = &g->cr[ci]; s->x = c->x; s->y = c->y; int lo = g->lo[c->y * 32 + c->x];
            if (!IS_SLIPPERY(lo)) { c->flag = 0; slip_remove(g, j); j--; } else compute_slide_dir(g, lo, &s->dx, &s->dy);
        }
        if (g->state != ST_PLAY) return;
    }
}

/* ---- tick (RULES.md sec 3) ---- */
void game_input(Game *g, int dir) { if (dir >= 0 && dir < 4) chip_move(g, DX[dir], DY[dir], 1, 1); }

void game_tick(Game *g) {
    if (g->state != ST_PLAY) return;
    unsigned tick = ++g->tick;
    if ((tick & 1) == 0) {
        if (!g->A) {
            if (!g->B) {
                if (!g->C) { if (++g->idle > 1) { int i = g->chipy * 32 + g->chipx; if (IS_CHIPT(g->up[i])) g->up[i] = g->lo[i] == 3 ? 0x3E : 0x6E; } }
                else { chip_move(g, g->pdx, g->pdy, 1, 1); g->C = 0; }
            } else g->B = 0;
        }
        if (g->ncr > 0 && g->state == ST_PLAY) monsters_tick(g, (tick & 3) == 0);
    } else if (g->C && !g->A && !g->B) { chip_move(g, g->pdx, g->pdy, 1, 1); g->C = 0; }
    if (g->A && g->state == ST_PLAY) {
        g->idle = 0;
        int r = chip_move(g, g->sdx, g->sdy, 0, 1);
        if (!r && g->A && g->state == ST_PLAY) {
            g->sdx = -g->sdx; g->sdy = -g->sdy; compute_slide_dir(g, g->lo[g->chipy * 32 + g->chipx], &g->sdx, &g->sdy);
            r = chip_move(g, g->sdx, g->sdy, 0, 1);
            if (!r) { g->sdx = -g->sdx; g->sdy = -g->sdy; compute_slide_dir(g, g->lo[g->chipy * 32 + g->chipx], &g->sdx, &g->sdy); }
        }
        if (g->B) g->B = 0;
        else if (g->C) { if (g->state == ST_PLAY && (!g->A || g->pdx != g->sdx || g->pdy != g->sdy)) chip_move(g, g->pdx, g->pdy, 1, 1); g->C = 0; }
    }
    if (g->state == ST_PLAY) slip_tick(g);
    if (g->state == ST_PLAY && !g->notime && g->time_left > 0 && tick != 0 && tick % 10 == 0) {
        g->time_left--; if (g->time_left < 16) g->events |= EV_TICK;
        if (g->time_left == 0) { g->events |= EV_TIMEOUT; kill_chip(g, D_TIME); }
    }
}

/* ---- level setup (RULES.md sec 2) ---- */
void game_start(Game *g, const Level *L, unsigned seed, unsigned tick0) {
    memset(g, 0, sizeof *g);
    g->lev = L; g->chips_need = L->chips; g->notime = L->time == 0; g->time_left = L->time; g->rng = seed; g->tick = tick0;
    memcpy(g->up, L->up, 1024); memcpy(g->lo, L->lo, 1024);
    for (int i = 0; i < L->ntrap && i < 32; i++) g->trapflag[i] = L->trap[i].f ? 1 : 0;
    for (int i = 0; i < L->nmon; i++) {
        int x = L->monx[i], y = L->mony[i], idx = y * 32 + x; if (x > 31 || y > 31) continue;
        int t = g->up[idx];
        if (IS_MON(t) && g->lo[idx] != 0x31) {
            cr_add(g, t, x, y);
            if (g->lo[idx] == T_TRAP) trap_recalc(g, x, y, -1, -1);
        }
    }
    int cx = -1, cy = -1;
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        int i = y * 32 + x, t = g->up[i]; if (IS_MON(t)) t = g->lo[i];
        if ((t == 0x25 || t == 0x26) && g->ntog < 1024) g->tog[g->ntog++] = i;
        if (t == 0x29 && g->ntele < 64) g->tele[g->ntele++] = i;
        if (g->up[i] >= 0x6C && g->up[i] <= 0x6F) { cx = x; cy = y; }
    }
    if (cx < 0) { cx = cy = 1; g->up[33] = g->lo[33]; g->lo[33] = 0; g->up[33] = 0x6E; }
    g->chipx = cx; g->chipy = cy;
    if (!IS_CHIPT(g->up[cy * 32 + cx])) g->up[cy * 32 + cx] = 0x6E;
    g->idle = 0;
}
