/* Sound effects (the original WAV files, loaded at runtime) and a small software MIDI synthesiser for CHIP01/02.MID. */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cc_audio.h"
#include "cc_opl.h"

#define RATE 22050
#define NVOICE 8

typedef struct { Sint16 *d; int n; } Clip;
typedef struct { Clip *c; int pos; } Voice;

static Clip sfx[SND_N];
static Clip music[2], fm[2];
static int music_mode = 1, android_failed = 0;   /* 0 off, 1 FM (OPL3), 2 Android MIDI (played by Java; native silent unless it failed) */
static Voice voices[NVOICE];
static int cur_music = -1, music_pos = 0, enabled_sfx = 1;
static SDL_AudioDeviceID dev;

static const char *SFX_NAMES[SND_N] = { "BLIP2.WAV", "BUMMER.WAV", "CLICK3.WAV", "DITTY1.WAV", "DOOR.WAV", "OOF3.WAV", "POP2.WAV", "STRIKE.WAV", "TELEPORT.WAV", "WATER2.WAV" };

static int load_wav(const char *path, Clip *c) {
    SDL_AudioSpec spec; Uint8 *buf; Uint32 len;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) return -1;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, RATE) < 0) { SDL_FreeWAV(buf); return -2; }
    cvt.len = (int)len; cvt.buf = SDL_malloc(len * cvt.len_mult);
    memcpy(cvt.buf, buf, len); SDL_FreeWAV(buf);
    if (cvt.needed && SDL_ConvertAudio(&cvt)) { SDL_free(cvt.buf); return -3; }
    c->d = (Sint16 *)cvt.buf; c->n = cvt.len_cvt / 2; return 0;
}

/* ------------------------------------------------------------ MIDI */
typedef struct { unsigned tick; int kind, ch, a, b; } Ev;   /* kind: 0 on, 1 off, 2 program, 3 tempo */
static int evcmp(const void *x, const void *y) { const Ev *a = x, *b = y; return a->tick < b->tick ? -1 : a->tick > b->tick ? 1 : (a->kind == 1 && b->kind != 1) ? -1 : (b->kind == 1 && a->kind != 1) ? 1 : 0; }

typedef struct { double t0, t1; int key, vel, ch, prog; } Note;

static void synth_note(float *buf, int total, const Note *n) {
    int s0 = (int)(n->t0 * RATE), s1 = (int)(n->t1 * RATE);
    if (s0 >= total) return;
    double f = 440.0 * pow(2.0, (n->key - 69) / 12.0), vel = n->vel / 127.0;
    int prog = n->prog, drum = n->ch == 9;
    int tail = drum ? RATE / 8 : RATE / 15;
    if (s1 + tail > total) tail = total - s1 > 0 ? total - s1 : 0;
    unsigned noise = 12345 + n->key * 7919;
    for (int i = s0; i < s1 + tail && i < total; i++) {
        double t = (double)(i - s0) / RATE, ts = (double)(i - s1) / RATE, env, v;
        if (drum) {
            double k = n->key; t = (double)(i - s0) / RATE; noise = noise * 1664525u + 1013904223u; double nz = ((noise >> 16) & 0x7fff) / 16384.0 - 1.0;
            if (k == 35 || k == 36) { v = sin(6.2832 * (50 * t + 90 * (1 - exp(-t * 30)) / 30.0)) * exp(-t * 14); }
            else if (k == 38 || k == 40) v = nz * exp(-t * 22) * 0.8 + sin(6.2832 * 190 * t) * exp(-t * 30) * 0.4;
            else if (k == 42 || k == 44) v = nz * exp(-t * 70) * 0.5;
            else if (k == 46) v = nz * exp(-t * 25) * 0.5;
            else v = nz * exp(-t * 18) * 0.5;
            buf[i] += (float)(v * vel * 0.5); continue;
        }
        double a = t < 0.01 ? t / 0.01 : 1.0;
        double ph = fmod(f * t, 1.0);
        if (prog < 8) { v = (ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph) * 0.8 + sin(6.2832 * 2 * f * t) * 0.2; env = exp(-t * 2.5); }
        else if (prog < 16) { v = sin(6.2832 * f * t) + 0.4 * sin(6.2832 * 3 * f * t); env = exp(-t * 4); }
        else if (prog < 24) { v = (ph < 0.5 ? 1 : -1) * 0.5 + sin(6.2832 * f * t) * 0.5; env = 1; }
        else if (prog < 40) { v = 2 * ph - 1; env = exp(-t * (prog >= 32 ? 3.0 : 4.5)); if (prog >= 32) v = (ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph); }
        else if (prog < 56) { v = (2 * ph - 1) * 0.6; env = t < 0.06 ? t / 0.06 : 1; a = 1; }
        else if (prog < 80) { v = (ph < 0.35 ? 1 : -1) * 0.6; env = 1; }
        else { v = ph < 0.5 ? 1 : -1; env = 0.8; if (prog >= 88) env = exp(-t * 1.2) * 0.8 + 0.2; }
        double rel = i >= s1 ? exp(-ts * 60) : 1.0;
        buf[i] += (float)(v * env * a * rel * vel * 0.22);
    }
}

static int load_midi(const char *path, Clip *c) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return -2; } fclose(f);
    if (n < 14 || memcmp(b, "MThd", 4)) { free(b); return -3; }
    int ntrk = b[10] << 8 | b[11], div = b[12] << 8 | b[13]; if (div <= 0 || (div & 0x8000)) div = 240;
    Ev *ev = malloc(sizeof(Ev) * 200000); int ne = 0;
    long p = 14;
    for (int t = 0; t < ntrk && p + 8 <= n; t++) {
        long len = (long)b[p+4] << 24 | b[p+5] << 16 | b[p+6] << 8 | b[p+7]; long q = p + 8, e = q + len; if (e > n) e = n; p = e;
        unsigned tick = 0; int run = 0;
        while (q < e) {
            unsigned dt = 0; int cnt = 0; while (q < e && cnt++ < 4) { int x = b[q++]; dt = dt << 7 | (x & 127); if (!(x & 128)) break; }
            tick += dt; if (q >= e) break;
            int st = b[q];
            if (st == 0xFF) { int ty = b[q+1]; long l = 0; long qq = q + 2; int cn = 0; while (qq < e && cn++ < 4) { int x = b[qq++]; l = l << 7 | (x & 127); if (!(x & 128)) break; }
                if (ty == 0x51 && l >= 3 && ne < 199990) { ev[ne++] = (Ev){ tick, 3, 0, b[qq] << 16 | b[qq+1] << 8 | b[qq+2], 0 }; }
                q = qq + l; if (ty == 0x2F) break; continue; }
            if (st == 0xF0 || st == 0xF7) { long l = 0; long qq = q + 1; int cn = 0; while (qq < e && cn++ < 4) { int x = b[qq++]; l = l << 7 | (x & 127); if (!(x & 128)) break; } q = qq + l; continue; }
            if (st & 0x80) { run = st; q++; } else st = run;
            int ty = st & 0xF0, ch = st & 15;
            if (ty == 0xC0 || ty == 0xD0) { int a = b[q++]; if (ty == 0xC0 && ne < 199990) ev[ne++] = (Ev){ tick, 2, ch, a, 0 }; }
            else { int a = b[q], v = b[q+1]; q += 2;
                if (ne < 199990) { if (ty == 0x90 && v) ev[ne++] = (Ev){ tick, 0, ch, a, v }; else if (ty == 0x80 || ty == 0x90) ev[ne++] = (Ev){ tick, 1, ch, a, 0 }; } }
        }
    }
    free(b);
    qsort(ev, ne, sizeof(Ev), evcmp);
    Note *notes = malloc(sizeof(Note) * 40000); int nn = 0;
    int prog[16] = {0}; int open_[16][128]; memset(open_, -1, sizeof open_);
    double sec = 0; unsigned lt = 0; int tempo = 500000;
    for (int i = 0; i < ne; i++) {
        sec += (double)(ev[i].tick - lt) * tempo / 1e6 / div; lt = ev[i].tick;
        if (ev[i].kind == 3) tempo = ev[i].a;
        else if (ev[i].kind == 2) prog[ev[i].ch] = ev[i].a;
        else if (ev[i].kind == 0 && nn < 39990) { open_[ev[i].ch][ev[i].a] = nn; notes[nn++] = (Note){ sec, sec + 30, ev[i].a, ev[i].b, ev[i].ch, prog[ev[i].ch] }; }
        else if (ev[i].kind == 1) { int k = open_[ev[i].ch][ev[i].a]; if (k >= 0) { notes[k].t1 = sec; open_[ev[i].ch][ev[i].a] = -1; } }
    }
    double endsec = sec + 0.5; if (endsec < 1) endsec = 1;
    for (int i = 0; i < nn; i++) if (notes[i].t1 > endsec) notes[i].t1 = endsec;
    int total = (int)(endsec * RATE); float *fb = calloc(total, sizeof(float));
    for (int i = 0; i < nn; i++) synth_note(fb, total, &notes[i]);
    float pk = 0.01f; for (int i = 0; i < total; i++) if (fabsf(fb[i]) > pk) pk = fabsf(fb[i]);
    c->d = malloc(total * 2); c->n = total; float g = 0.55f / pk; if (g > 1.5f) g = 1.5f;
    for (int i = 0; i < total; i++) c->d[i] = (Sint16)(fb[i] * g * 32767.0f);
    free(fb); free(notes); free(ev); return 0;
}

static void cb(void *u, Uint8 *out, int len) {
    (void)u; Sint16 *o = (Sint16 *)out; int n = len / 2;
    for (int i = 0; i < n; i++) {
        int s = 0;
        if (cur_music >= 0 && (music_mode == 1 || (music_mode == 2 && android_failed))) {
            const Clip *mc = fm[cur_music].n ? &fm[cur_music] : &music[cur_music];
            if (mc->n) { s += mc->d[music_pos] * 8 / 10; if (++music_pos >= mc->n) music_pos = 0; }
        }
        for (int v = 0; v < NVOICE; v++) if (voices[v].c) { s += voices[v].c->d[voices[v].pos++]; if (voices[v].pos >= voices[v].c->n) voices[v].c = NULL; }
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        o[i] = (Sint16)s;
    }
}

int audio_init(const char *dir) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    char p[600]; int ok = 0;
    for (int i = 0; i < SND_N; i++) { snprintf(p, sizeof p, "%s/%s", dir, SFX_NAMES[i]); if (!load_wav(p, &sfx[i])) ok++; }
    for (int i = 0; i < 2; i++) { snprintf(p, sizeof p, "%s/CHIP0%d.MID", dir, i + 1); if (load_midi(p, &music[i])) { snprintf(p, sizeof p, "%s/chip0%d.mid", dir, i + 1); load_midi(p, &music[i]); } }
    for (int i = 0; i < 2; i++) {   /* OPL3 render of the same MIDI (offline, ymfm) */
        int16_t *d = NULL; int n = 0; OplStats st; snprintf(p, sizeof p, "%s/CHIP0%d.MID", dir, i + 1);
        if (opl_render_midi(p, &d, &n, RATE, &st)) { snprintf(p, sizeof p, "%s/chip0%d.mid", dir, i + 1); if (opl_render_midi(p, &d, &n, RATE, &st)) continue; }
        fm[i].d = d; fm[i].n = n; SDL_Log("OPL3 render CHIP0%d: %.1f s peak %d rms %.1f dBFS", i + 1, st.seconds, st.peak_out, st.rms_dbfs);
    }
    SDL_AudioSpec w, h; SDL_zero(w); w.freq = RATE; w.format = AUDIO_S16SYS; w.channels = 1; w.samples = 1024; w.callback = cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &w, &h, 0); if (!dev) return -2;
    SDL_PauseAudioDevice(dev, 0);
    return ok;
}
void audio_play(int id) { if (!dev || id < 0 || id >= SND_N || !sfx[id].d) return; SDL_LockAudioDevice(dev);
    for (int v = 0; v < NVOICE; v++) if (!voices[v].c) { voices[v].c = &sfx[id]; voices[v].pos = 0; break; } SDL_UnlockAudioDevice(dev); }
void audio_music(int track) { if (!dev) return; SDL_LockAudioDevice(dev); if (track != cur_music + 1) { cur_music = track - 1; music_pos = 0; } SDL_UnlockAudioDevice(dev); }
void audio_enable(int sfx_on) { enabled_sfx = sfx_on; }
void audio_set_music_mode(int m) { if (dev) SDL_LockAudioDevice(dev); music_mode = m; if (dev) SDL_UnlockAudioDevice(dev); }
void audio_android_failed(int f) { android_failed = f; }
int audio_fm_available(void) { return fm[0].n > 0; }
int audio_music_samples(int t) { return music[t].n; }
/* offline helper for tests: renders a MIDI to a mono 16-bit WAV */
int audio_render_midi_wav(const char *mid, const char *wav) {
    Clip c = {0}; if (load_midi(mid, &c)) return -1;
    FILE *f = fopen(wav, "wb"); if (!f) return -2;
    unsigned dl = c.n * 2, rl = 36 + dl, sr = RATE, br = RATE * 2; unsigned short fmt = 1, ch = 1, ba = 2, bps = 16; unsigned fl = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&rl, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f); fwrite(c.d, 2, c.n, f); fclose(f);
    printf("rendered %s: %.1f s\n", mid, (double)c.n / RATE); return 0;
}
