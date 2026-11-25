/* Greedy BFS solver over the real engine: proves levels of the user's CHIPS.DAT are completable with our rules.
   Usage: solve CHIPS.DAT first last [nodecap]. Needs the user's own CHIPS.DAT (not distributed). */
#include "cc_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint64_t hashg(const Game *g) {
    uint64_t h = 1469598103934665603ull;
#define MIX(p, n) do { const unsigned char *b = (const unsigned char *)(p); for (size_t i = 0; i < (n); i++) { h ^= b[i]; h *= 1099511628211ull; } } while (0)
    MIX(g->up, 1024); MIX(g->lo, 1024); MIX(g->keys, sizeof g->keys); MIX(g->boots, sizeof g->boots); MIX(&g->chips_need, 4); MIX(&g->rng, 4);
    int t = g->tick & 1; MIX(&t, 4); MIX(&g->A, 4); MIX(&g->B, 4); MIX(&g->C, 4);
    for (int i = 0; i < g->ncr; i++) MIX(&g->cr[i], sizeof(Cr));
    return h;
}
typedef struct { Game g; int parent; signed char in; } Node;

static int solve_level(const Level *L, int cap, int *nmoves) {
    Game start; game_start(&start, L, 5, 0);
    static Node *q = NULL; static uint64_t *seen = NULL; size_t hs = 1 << 22;
    if (!q) { q = malloc(sizeof(Node) * (size_t)cap); seen = malloc(hs * 8); }
    Game cur = start; int total = 0;
    for (int hop = 0; hop < 400; hop++) {
        memset(seen, 0, hs * 8);
        int qn = 0, qh = 0, found = -1;
        q[qn].g = cur; q[qn].parent = -1; q[qn].in = -1; qn++;
        int base_items = (L->chips - cur.chips_need) + cur.keys[0] + cur.keys[1] + cur.keys[2] + cur.keys[3] + cur.boots[0] + cur.boots[1] + cur.boots[2] + cur.boots[3];
        while (qh < qn && found < 0) {
            int ci = qh++;
            for (int in = -1; in < 4; in++) {
                if (qn >= cap) break;
                Node *n = &q[qn]; n->g = q[ci].g;
                if (in >= 0) game_input(&n->g, in); game_tick(&n->g); game_tick(&n->g);
                if (n->g.state == ST_DEAD) continue;
                if (in >= 0 && n->g.chipx == q[ci].g.chipx && n->g.chipy == q[ci].g.chipy && n->g.chips_need == q[ci].g.chips_need && n->g.state == ST_PLAY) continue;  /* prune bumps */
                uint64_t h = hashg(&n->g) | 1; size_t s = h & (hs - 1);
                int dup = 0; while (seen[s]) { if (seen[s] == h) { dup = 1; break; } s = (s + 1) & (hs - 1); }
                if (dup) continue; seen[s] = h;
                n->parent = ci; n->in = in;
                int items = (L->chips - n->g.chips_need) + n->g.keys[0] + n->g.keys[1] + n->g.keys[2] + n->g.keys[3] + n->g.boots[0] + n->g.boots[1] + n->g.boots[2] + n->g.boots[3];
                if (n->g.state == ST_WON || items > base_items) { found = qn; qn++; break; }
                qn++;
            }
        }
        if (found < 0) return 0;
        int path[2000], pl = 0; for (int k = found; q[k].parent >= 0; k = q[k].parent) path[pl++] = q[k].in;
        total += pl; cur = q[found].g;
        if (cur.state == ST_WON) { *nmoves = total; return 1; }
    }
    return 0;
}
int main(int argc, char **argv) {
    Dat d; if (dat_load(&d, argv[1])) { puts("cannot load dat"); return 2; }
    int a = atoi(argv[2]), b = atoi(argv[3]), cap = argc > 4 ? atoi(argv[4]) : 60000, ok = 0, tot = 0;
    for (int i = a; i <= b && i < d.n; i++) {
        int nm = 0; int r = solve_level(&d.lev[i], cap, &nm); tot++; ok += r;
        printf("level %d (%s): %s %d moves  [monsters %d]\n", d.lev[i].num, d.lev[i].title, r ? "SOLVED" : "not solved", nm, d.lev[i].nmon); fflush(stdout);
    }
    printf("%d/%d solved\n", ok, tot);
}
