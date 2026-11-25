/* Rule unit tests on synthetic levels (no game files needed), expectations taken from re/RULES.md. */
#include "cc_engine.h"
#include <stdio.h>
#include <string.h>

static Level L; static Game G; static int fails, total;
#define CHECK(c, msg) do { total++; if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static void fresh(void) {
    memset(&L, 0, sizeof L); L.num = 1; L.time = 100;
    for (int i = 0; i < 32; i++) { L.up[i] = 1; L.up[31 * 32 + i] = 1; L.up[i * 32] = 1; L.up[i * 32 + 31] = 1; }
}
static void put(int x, int y, int code) { L.up[y * 32 + x] = code; }
static void putlo(int x, int y, int code) { L.lo[y * 32 + x] = code; }
static void mon(int x, int y) { L.monx[L.nmon] = x; L.mony[L.nmon++] = y; }
static void begin(int cx, int cy) { put(cx, cy, 0x6E); game_start(&G, &L, 1, 0); }
static void ticks(int n) { for (int i = 0; i < n; i++) game_tick(&G); }
/* one Chip step: key-down, then two ticks (B flag clears on the next even tick) */
static void mv(int d) { game_input(&G, d); ticks(2); }
static int cx(void) { return G.chipx; } static int cy(void) { return G.chipy; }
static int at(int x, int y) { return G.up[y * 32 + x]; }
static int lo(int x, int y) { return G.lo[y * 32 + x]; }
static int mx(int i) { return G.cr[i].x; } static int my(int i) { return G.cr[i].y; }

int main(void) {
    /* Chip movement + two layers */
    fresh(); begin(5, 5); mv(E_); CHECK(cx() == 6 && cy() == 5, "move east"); CHECK(at(5, 5) == 0 && at(6, 5) == 0x6F, "upper layer follows Chip, facing E");
    for (int i = 0; i < 6; i++) mv(N_); CHECK(cy() == 1, "stops at wall");
    fresh(); begin(5, 5); game_input(&G, E_); game_input(&G, E_); CHECK(cx() == 6 && G.C == 1, "one-slot key queue (B gating)"); ticks(2); CHECK(cx() == 6, "queue not served while B set (tick 1,2)"); ticks(1); CHECK(cx() == 7, "queued move executes on tick 3 (odd tick, B clear)");
    /* water, fire */
    fresh(); put(6, 5, T_WATER); begin(5, 5); mv(E_); CHECK(G.state == ST_DEAD && G.death == D_DROWN && at(6, 5) == 0x33, "drown (death 2, sprite 33)");
    fresh(); put(6, 5, T_WATER); put(4, 5, T_BOOT_WATER); begin(5, 5); mv(W_); mv(E_); mv(E_); CHECK(G.state == ST_PLAY && cx() == 6 && at(6, 5) == 0x3F, "flippers: swim (tile 3C+dir)");
    fresh(); put(6, 5, T_FIRE); begin(5, 5); mv(E_); CHECK(G.state == ST_DEAD && G.death == D_BURN, "burn (death 1)");
    fresh(); put(6, 5, T_FIRE); put(4, 5, T_BOOT_FIRE); begin(5, 5); mv(W_); mv(E_); mv(E_); CHECK(G.state == ST_PLAY && cx() == 6 && lo(6, 5) == T_FIRE, "fire boots; fire stays under Chip");
    /* doors: blue/red/yellow consumed, green not */
    fresh(); put(6, 5, T_DOOR_R); begin(5, 5); mv(E_); CHECK(cx() == 5, "door blocks without key");
    fresh(); put(6, 5, T_DOOR_R); put(4, 5, T_KEY_R); begin(5, 5); mv(W_); CHECK(G.keys[1] == 1, "pick key"); mv(E_); mv(E_); CHECK(cx() == 6 && G.keys[1] == 0, "red door consumes key");
    fresh(); put(6, 5, T_DOOR_G); put(4, 5, T_KEY_G); begin(5, 5); mv(W_); mv(E_); mv(E_); CHECK(cx() == 6 && G.keys[2] == 1, "green key NOT consumed");
    /* socket */
    fresh(); L.chips = 1; put(6, 5, T_SOCKET); put(4, 5, T_ICCHIP); begin(5, 5); mv(E_); CHECK(cx() == 5, "socket blocked");
    mv(W_); CHECK(game_chips_left(&G) == 0, "chip collected"); mv(E_); mv(E_); CHECK(cx() == 6, "socket opens");
    /* blocks */
    fresh(); put(6, 5, T_BLOCK); begin(5, 5); mv(E_); CHECK(cx() == 6 && at(7, 5) == T_BLOCK, "push block");
    fresh(); put(6, 5, T_BLOCK); put(7, 5, T_WALL); begin(5, 5); mv(E_); CHECK(cx() == 5 && at(6, 5) == T_BLOCK, "block against wall");
    fresh(); put(6, 5, T_BLOCK); put(7, 5, T_WATER); begin(5, 5); mv(E_); CHECK(at(7, 5) == T_DIRT, "block into water -> dirt (block consumed)");
    fresh(); put(6, 5, T_BLOCK); put(7, 5, T_BOMB); begin(5, 5); mv(E_); CHECK(at(7, 5) == 0 && at(6, 5) == 0x6F, "block + bomb vanish");
    fresh(); put(6, 5, T_BLOCK); put(7, 5, T_BLOCK); begin(5, 5); mv(E_); CHECK(cx() == 5, "two blocks cannot be pushed");
    fresh(); put(6, 5, T_BLOCK); put(7, 5, T_FIRE); begin(5, 5); mv(E_); CHECK(at(7, 5) == T_BLOCK && lo(7, 5) == T_FIRE, "block lands on fire (kind 1)");
    /* special wall tiles */
    fresh(); put(6, 5, T_THIN_W); begin(5, 5); mv(E_); CHECK(cx() == 5, "thin wall W blocks entering moving E");
    fresh(); put(6, 5, T_THIN_N); begin(5, 5); mv(E_); CHECK(cx() == 6, "thin wall N: E-W passes"); mv(N_); CHECK(cy() == 5, "thin wall N blocks leaving N");
    fresh(); put(6, 5, T_BLUE_FAKE); begin(5, 5); mv(E_); CHECK(cx() == 6 && lo(6, 5) == 0, "fake blue wall: opens");
    fresh(); put(6, 5, T_BLUE_REAL); begin(5, 5); mv(E_); CHECK(cx() == 5 && at(6, 5) == T_WALL, "real blue wall becomes wall, Chip blocked");
    fresh(); put(6, 5, T_HIDDEN_TEMP); begin(5, 5); mv(E_); CHECK(cx() == 5 && at(6, 5) == T_WALL, "0x2C appears as wall, blocked");
    fresh(); put(6, 5, T_HIDDEN_PERM); begin(5, 5); mv(E_); CHECK(cx() == 5 && at(6, 5) == T_HIDDEN_PERM, "0x05 never appears");
    fresh(); put(6, 5, T_DIRT); begin(5, 5); mv(E_); CHECK(cx() == 6, "dirt cleared");
    fresh(); put(6, 5, T_POPUP); begin(5, 5); mv(E_); CHECK(cx() == 6 && lo(6, 5) == T_WALL, "pass-once wall: wall behind Chip"); mv(W_); mv(E_); CHECK(cx() == 5, "pass-once wall is solid afterwards");
    fresh(); put(6, 5, T_THIEF); put(4, 5, T_BOOT_FIRE); put(3, 5, T_KEY_B); begin(5, 5); mv(W_); mv(W_); mv(E_); mv(E_); mv(E_); CHECK(G.boots[1] == 0 && G.keys[0] == 1, "thief steals boots, keeps keys");
    fresh(); put(6, 5, T_BOMB); begin(5, 5); mv(E_); CHECK(G.state == ST_DEAD && G.death == D_BOMB, "bomb death 3");
    fresh(); put(6, 5, T_EXIT); begin(5, 5); mv(E_); CHECK(G.state == ST_WON && lo(6, 5) == T_EXIT, "exit wins (no chip check)");
    /* ice: forced move every tick, bounce, skates, corner */
    fresh(); for (int x = 6; x <= 9; x++) put(x, 5, T_ICE); begin(5, 5); mv(E_); CHECK(G.A == 1, "on ice -> sliding"); ticks(2); CHECK(cx() == 10 && G.A == 0, "slides 1 tile/tick, stops on first non-ice floor");
    fresh(); for (int x = 6; x <= 9; x++) put(x, 5, T_ICE); put(10, 5, T_WALL); begin(5, 5); mv(E_); ticks(10); CHECK(G.state == ST_PLAY && cx() == 5 && G.A == 0, "bounces off wall on ice, ends on floor behind");
    fresh(); for (int x = 6; x <= 9; x++) put(x, 5, T_ICE); put(4, 5, T_BOOT_ICE); begin(5, 5); mv(W_); mv(E_); mv(E_); ticks(4); CHECK(cx() == 6 && G.A == 0, "skates: no slide");
    fresh(); put(6, 5, T_ICE); put(7, 5, T_ICE_NE); put(7, 6, T_ICE); put(7, 7, T_ICE); begin(5, 5); mv(E_); ticks(6); CHECK(cx() == 7 && cy() >= 6, "ice corner redirects (1B: moving E -> S)");
    /* force floors */
    fresh(); for (int x = 6; x <= 9; x++) put(x, 5, T_FORCE_E); put(10, 5, T_WALL); begin(5, 5); mv(E_); ticks(8); CHECK(cx() == 9, "force floor carries Chip");
    fresh(); put(6, 5, T_FORCE_E); put(7, 5, T_FORCE_E); put(8, 5, T_FORCE_E); begin(5, 5); mv(E_); mv(S_); CHECK(cy() == 6, "player may steer off a force floor");
    fresh(); put(6, 5, T_FORCE_RAND); put(4, 5, T_BOOT_FORCE); begin(5, 5); mv(W_); mv(E_); mv(E_); CHECK(cx() == 6 && G.A == 0, "suction boots protect");
    /* teleport */
    fresh(); put(6, 5, T_TELEPORT); put(10, 10, T_TELEPORT); begin(5, 5); mv(E_); ticks(3); CHECK(cx() == 11 && cy() == 10, "teleport: exit beyond destination (reverse scan)");
    /* clone machine: creature on machine is not added at load; red button adds one */
    fresh(); put(10, 5, 0x4B); putlo(10, 5, T_CLONER); put(5, 6, T_BTN_RED); L.nclone = 1; L.clone[0] = (Link){5, 6, 10, 5, 0}; mon(10, 5);
    begin(5, 5); CHECK(G.ncr == 0, "creature on clone machine is not added at load"); mv(S_); ticks(4); { int n = 0; for (int i = 0; i < G.ncr; i++) if (G.cr[i].tile == 0x4B) n++; CHECK(n == 1 && at(10, 5) == 0x4B, "clone: one active clone, machine keeps tile"); }
    /* trap */
    fresh(); put(6, 5, T_TRAP); put(3, 5, T_BTN_BROWN); L.ntrap = 1; L.trap[0] = (Link){3, 5, 6, 5, 0}; begin(5, 5); mv(E_); CHECK(cx() == 6 && G.trapflag[0] == 1, "entering a trap closes it"); mv(E_); CHECK(cx() == 6, "closed trap holds Chip");
    fresh(); put(6, 5, T_TRAP); put(3, 5, T_BTN_BROWN); L.ntrap = 1; L.trap[0] = (Link){3, 5, 6, 5, 1}; begin(2, 5); mv(E_); CHECK(G.trapflag[0] == 0, "brown button opens trap (flag 0)");
    /* toggle walls */
    fresh(); put(6, 5, T_TOGGLE_CLOSED); put(4, 5, T_BTN_GREEN); begin(5, 5); mv(E_); CHECK(cx() == 5, "closed toggle wall blocks"); mv(W_); mv(E_); mv(E_); CHECK(cx() == 6, "green button opens it");
    /* timer */
    fresh(); L.time = 5; begin(5, 5); ticks(9); CHECK(G.time_left == 5, "no decrement before tick 10"); ticks(1); CHECK(G.time_left == 4, "decrement at tick 10");
    fresh(); L.time = 1; begin(5, 5); ticks(10); CHECK(G.state == ST_DEAD && G.death == D_TIME, "time out (death 6)");
    fresh(); L.time = 5; put(5, 5, 0x6E); game_start(&G, &L, 1, 7); ticks(3); CHECK(G.time_left == 4, "global tick phase carries over levels (first second shorter)");
    fresh(); L.time = 0; begin(5, 5); ticks(100); CHECK(G.state == ST_PLAY && G.notime, "untimed level never expires");

    /* ---- creatures ---- */
    fresh(); for (int i = 4; i <= 8; i++) { put(i, 4, 1); put(i, 8, 1); put(4, i, 1); put(8, i, 1); } put(5, 5, 0x43); mon(5, 5); begin(20, 20);
    ticks(1); CHECK(mx(0) == 5, "creature does not act on odd tick"); ticks(1); CHECK(mx(0) == 6 && my(0) == 5, "creature acts on even ticks");
    { int ex[6][2] = {{7,5},{7,6},{7,7},{6,7},{5,7},{5,6}}, ok = 1; for (int k = 0; k < 6; k++) { ticks(2); if (mx(0) != ex[k][0] || my(0) != ex[k][1]) ok = 0; } CHECK(ok, "bug: left-hand wall following (L,S,R,B)"); }
    fresh(); for (int i = 4; i <= 8; i++) { put(i, 4, 1); put(i, 8, 1); put(4, i, 1); put(8, i, 1); } put(5, 5, 0x61); mon(5, 5); begin(20, 20);
    { int ex[4][2] = {{5,6},{5,7},{6,7},{7,7}}, ok = 1; for (int k = 0; k < 4; k++) { ticks(2); if (mx(0) != ex[k][0] || my(0) != ex[k][1]) ok = 0; } CHECK(ok, "paramecium: right-hand wall following"); }
    fresh(); put(10, 10, 0x53); put(13, 10, 1); mon(10, 10); begin(20, 20); ticks(4); CHECK(mx(0) == 12, "glider straight"); ticks(2); CHECK(mx(0) == 12 && my(0) == 9 && G.cr[0].tile == 0x50, "glider: straight, then LEFT");
    fresh(); put(10, 10, 0x47); put(13, 10, 1); mon(10, 10); begin(20, 20); ticks(6); CHECK(mx(0) == 12 && my(0) == 11 && G.cr[0].tile == 0x46, "fireball: straight, then RIGHT");
    fresh(); put(10, 10, 0x4B); put(13, 10, 1); mon(10, 10); begin(20, 20); ticks(6); CHECK(mx(0) == 11 && G.cr[0].tile == 0x49, "ball: straight, then back");
    fresh(); put(10, 10, 0x4F); put(13, 10, 1); mon(10, 10); begin(20, 20); ticks(10); CHECK(mx(0) == 12 && G.cr[0].dx == 0 && G.cr[0].dy == 0, "tank: stops permanently when blocked");
    fresh(); put(10, 10, 0x4F); put(13, 10, 1); put(5, 6, T_BTN_BLUE); mon(10, 10); begin(5, 5); ticks(10); mv(S_); CHECK(G.cr[0].dx == -1, "blue button reverses (revives) tank");
    fresh(); put(11, 10, T_FIRE); put(10, 10, 0x47); mon(10, 10); begin(20, 20); ticks(2); CHECK(mx(0) == 11, "fireball enters fire");
    fresh(); put(11, 10, T_FIRE); put(10, 10, 0x43); mon(10, 10); begin(20, 20); ticks(2); CHECK(mx(0) != 11, "bug blocked by fire");
    fresh(); put(11, 10, T_WATER); put(10, 10, 0x4B); mon(10, 10); begin(20, 20); ticks(2); CHECK(G.ncr == 0 && at(11, 10) == T_WATER, "ball dies in water");
    fresh(); put(11, 10, T_WATER); put(10, 10, 0x53); mon(10, 10); begin(20, 20); ticks(2); CHECK(G.ncr == 1 && mx(0) == 11, "glider crosses water");
    fresh(); put(11, 10, T_BOMB); put(10, 10, 0x4B); mon(10, 10); begin(20, 20); ticks(2); CHECK(G.ncr == 0 && at(11, 10) == 0, "creature + bomb vanish");
    fresh(); put(8, 5, 0x4D); mon(8, 5); begin(5, 5); ticks(8); CHECK(G.state == ST_DEAD && G.death == D_MONSTER, "tank kills Chip (death 5)");
    fresh(); put(7, 5, 0x4F); begin(5, 5); ticks(8); CHECK(G.state == ST_PLAY && at(7, 5) == 0x4F, "creatures NOT listed in field 10 are inert");
    fresh(); put(12, 5, 0x57); mon(12, 5); begin(5, 5); ticks(3); CHECK(mx(0) == 12, "teeth idle before tick 4"); ticks(1); CHECK(mx(0) == 11, "teeth move on tick%4==0 toward Chip");
    fresh(); put(12, 8, 0x5F); mon(12, 8); begin(5, 5); ticks(3); CHECK(mx(0) == 12 && my(0) == 8, "blob waits for tick%4");
    fresh(); put(6, 5, T_BLOCK); for (int x = 7; x <= 10; x++) put(x, 5, T_ICE); put(11, 5, T_WALL); begin(5, 5); mv(E_); CHECK(G.nslip == 1, "pushed block onto ice gets a slip entry"); ticks(1); CHECK(at(9, 5) == T_BLOCK || at(8, 5) == T_BLOCK || at(10, 5) == T_BLOCK, "block slides 1 tile/tick");

    printf("%d/%d checks passed\n", total - fails, total);
    return fails != 0;
}
