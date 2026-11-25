#include "cc_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp(const char *p, long *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t)*n) { fclose(f); free(b); return NULL; } fclose(f); return b;
}
static int u16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int unrle(const unsigned char *s, int n, unsigned char *out) {
    int o = 0, i = 0;
    while (i < n && o < 1024) {
        if (s[i] == 0xFF && i + 2 < n) { int c = s[i+1]; while (c-- && o < 1024) out[o++] = s[i+2]; i += 3; }
        else out[o++] = s[i++];
    }
    return o;
}

int dat_load(Dat *d, const char *path) {
    long n; unsigned char *b = slurp(path, &n); if (!b) return -1;
    if (n < 6 || (u32(b) & 0xFFFEFFFF) != 0x0002AAAC) { free(b); return -2; }
    int cnt = u16(b + 4); d->lev = calloc(cnt, sizeof(Level)); d->n = 0;
    long p = 6;
    for (int i = 0; i < cnt && p + 2 <= n; i++) {
        int sz = u16(b + p); const unsigned char *r = b + p + 2; p += 2 + sz; if (p > n) break;
        Level *L = &d->lev[d->n]; memset(L, 0, sizeof *L);
        L->num = u16(r); L->time = u16(r + 2); L->chips = u16(r + 4);
        int q = 8, l1 = u16(r + q); unrle(r + q + 2, l1, L->up); q += 2 + l1;
        int l2 = u16(r + q); unrle(r + q + 2, l2, L->lo); q += 2 + l2;
        int ex = u16(r + q); q += 2; int e = q + ex; if (e > sz) e = sz;
        while (q + 2 <= e) {
            int t = r[q], l = r[q+1]; const unsigned char *f = r + q + 2; q += 2 + l; if (q > e) break;
            switch (t) {
            case 3: snprintf(L->title, sizeof L->title, "%.*s", l, (const char*)f); break;
            case 6: { int k; for (k = 0; k < l && k < 15 && f[k]; k++) L->pass[k] = f[k] ^ 0x99; L->pass[k] = 0; } break;
            case 7: snprintf(L->hint, sizeof L->hint, "%.*s", l, (const char*)f); break;
            case 4: for (int k = 0; k + 10 <= l && L->ntrap < 32; k += 10) { Link *x = &L->trap[L->ntrap++];
                    x->x1 = u16(f+k); x->y1 = u16(f+k+2); x->x2 = u16(f+k+4); x->y2 = u16(f+k+6); x->f = u16(f+k+8); } break;
            case 5: for (int k = 0; k + 8 <= l && L->nclone < 32; k += 8) { Link *x = &L->clone[L->nclone++];
                    x->x1 = u16(f+k); x->y1 = u16(f+k+2); x->x2 = u16(f+k+4); x->y2 = u16(f+k+6); } break;
            case 10: for (int k = 0; k + 2 <= l && L->nmon < 128; k += 2) { L->monx[L->nmon] = f[k]; L->mony[L->nmon++] = f[k+1]; } break;
            }
        }
        d->n++;
    }
    free(b); return d->n > 0 ? 0 : -3;
}
void dat_free(Dat *d) { free(d->lev); d->lev = NULL; d->n = 0; }

/* ---- NE resources + DIB (uncompressed / RLE4 / RLE8) ---- */
static int find_res(const unsigned char *b, long n, int type, int id, const unsigned char **out, int *len) {
    if (n < 0x40 || b[0] != 'M') return 0;
    uint32_t ne = u32(b + 0x3c); if (ne + 0x40 > (uint32_t)n || b[ne] != 'N') return 0;
    uint32_t p = ne + u16(b + ne + 0x24); int shift = u16(b + p); p += 2;
    while (p + 8 < (uint32_t)n) {
        int tid = u16(b + p); if (!tid) break; int cnt = u16(b + p + 2); p += 8;
        for (int i = 0; i < cnt; i++, p += 12) {
            int rid = u16(b + p + 6);
            if ((tid & 0x7fff) == type && (rid & 0x7fff) == id) {
                uint32_t off = (uint32_t)u16(b + p) << shift; int l = u16(b + p + 2) << shift;
                if (off + l > (uint32_t)n) l = n - off;
                *out = b + off; *len = l; return 1;
            }
        }
    }
    return 0;
}

static int dib_decode(const unsigned char *d, int len, Img *im) {
    int hs = u32(d), w = (int)u32(d + 4), h = (int)u32(d + 8), bpp = u16(d + 14), comp = u32(d + 16);
    int nc = u32(d + 32); if (!nc && bpp <= 8) nc = 1 << bpp;
    uint32_t pal[256]; for (int i = 0; i < nc && i < 256; i++) pal[i] = 0xFF000000u | d[hs+i*4+2] << 16 | d[hs+i*4+1] << 8 | d[hs+i*4];
    const unsigned char *s = d + hs + nc * 4, *end = d + len;
    unsigned char *idx = calloc(w, h);
    if (comp == 0) {
        int stride = ((w * bpp + 31) / 32) * 4;
        for (int y = 0; y < h; y++) { const unsigned char *r = s + (size_t)y * stride; if (r + stride > end) break;
            for (int x = 0; x < w; x++) idx[(h-1-y)*w + x] = bpp == 8 ? r[x] : bpp == 4 ? (r[x/2] >> (x&1 ? 0 : 4)) & 15 : (r[x/8] >> (7 - (x&7))) & 1; }
    } else if (comp == 1 || comp == 2) {
        int x = 0, y = 0;
        while (s + 1 < end && y < h) {
            int c = *s++, v = *s++;
            if (c) { for (int k = 0; k < c; k++) { int val = comp == 2 ? ((k & 1) ? v & 15 : v >> 4) : v; if (x < w) idx[(h-1-y)*w + x] = val; x++; } }
            else if (v == 0) { x = 0; y++; }
            else if (v == 1) break;
            else if (v == 2) { x += s[0]; y += s[1]; s += 2; }
            else { int nb = comp == 2 ? (v + 1) / 2 : v;
                for (int k = 0; k < v; k++) { int val = comp == 2 ? ((k & 1) ? s[k/2] & 15 : s[k/2] >> 4) : s[k]; if (x < w) idx[(h-1-y)*w + x] = val; x++; }
                s += nb; if (nb & 1) s++; }
        }
    } else { free(idx); return -1; }
    im->w = w; im->h = h; im->px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) im->px[i] = pal[idx[i] & 255];
    free(idx); return 0;
}

static void load_one(const unsigned char *b, long n, int id, Img *im, int *err) {
    const unsigned char *d; int l; memset(im, 0, sizeof *im);
    if (!find_res(b, n, 2, id, &d, &l) || dib_decode(d, l, im)) *err = 1;
}

int art_load(Art *a, const char *exe) {
    long n; unsigned char *b = slurp(exe, &n); if (!b) return -1;
    int err = 0; memset(a, 0, sizeof *a);
    load_one(b, n, 308, &a->tiles, &err);
    load_one(b, n, 344, &a->panel, &err);
    load_one(b, n, 200, &a->digits, &err);
    load_one(b, n, 352, &a->splash, &err);
    load_one(b, n, 333, &a->circuit, &err);
    free(b);
    if (err) { art_free(a); return -2; }
    /* sprite alpha: cols 7..9 colour on white; mask in cols 10..12 (white = sprite) */
    Img *t = &a->tiles;
    for (int y = 0; y < t->h; y++)
        for (int x = 7 * 32; x < 10 * 32; x++) {
            uint32_t m = t->px[y * t->w + x + 96];
            if ((m & 0xFF) < 128) t->px[y * t->w + x] &= 0x00FFFFFF;
        }
    a->ok = 1; return 0;
}
void art_free(Art *a) { free(a->tiles.px); free(a->panel.px); free(a->digits.px); free(a->splash.px); free(a->circuit.px); memset(a, 0, sizeof *a); }
