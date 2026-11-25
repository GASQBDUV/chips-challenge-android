/* Chip's Challenge (Windows 3.1) data loaders: CHIPS.DAT levels and CHIPS.EXE bitmap resources.
   Everything is read at runtime from the user's own files; nothing is embedded. */
#ifndef CC_DATA_H
#define CC_DATA_H
#include <stdint.h>

#define MAXLEV 160
typedef struct { int x1,y1,x2,y2,f; } Link;   /* button -> trap/clone machine (coords) */
typedef struct {
    int num, time, chips;
    unsigned char up[1024], lo[1024];
    char title[80], pass[16], hint[160];
    int nmon; int monx[128], mony[128];
    int ntrap; Link trap[32];
    int nclone; Link clone[32];
} Level;

typedef struct { int n; Level *lev; } Dat;
int dat_load(Dat *d, const char *path);          /* 0 ok */
void dat_free(Dat *d);

/* 32-bit ARGB bitmaps */
typedef struct { int w, h; uint32_t *px; } Img;
typedef struct {
    Img tiles;     /* 416x512 sheet; sprite area (cols 7-9) gets alpha from the mask (cols 10-12) */
    Img panel;     /* 154x300 side panel */
    Img digits;    /* 17x552 : 24 frames of 23px (12 yellow, 12 green) */
    Img splash, circuit;
    int ok;
} Art;
int art_load(Art *a, const char *exe_path);      /* 0 ok */
void art_free(Art *a);
#endif
