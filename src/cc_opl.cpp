// General MIDI -> OPL3 sequencer. Emulator: ymfm (BSD-3-Clause, third_party/ymfm). Patch bank: hand-authored for this project
// (see docs/MUSIC.md); no third-party instrument banks are used.
#include "cc_opl.h"
#include "ymfm_opl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const double OPL_RATE = 49716.0;   // 14.318 MHz / 288

struct Op { uint8_t r20, r40, r60, r80, rE0; };
struct Patch { Op m, c; uint8_t fbcn; int8_t tune; };   // fbcn = feedback<<1 | connection(1 = additive)

// reg20: AM VIB EGT(sustain) KSR MULT   reg40: KSL<<6 | TL   reg60: AR<<4 | DR   reg80: SL<<4 | RR   rE0: waveform
#define OPR(am, vib, egt, ksr, mult, ksl, tl, ar, dr, sl, rr, wave) \
    { (uint8_t)((am) << 7 | (vib) << 6 | (egt) << 5 | (ksr) << 4 | (mult)), (uint8_t)((ksl) << 6 | (tl)), (uint8_t)((ar) << 4 | (dr)), (uint8_t)((sl) << 4 | (rr)), (uint8_t)(wave) }

// ---- hand-authored patches (2-operator). Values chosen from FM synthesis principles, not copied from any bank. ----
const Patch P_PIANO   = { OPR(0,0,0,1,1, 1,26, 15,5,6,6, 0), OPR(0,0,0,1,1, 0,0, 15,5,7,6, 0), 1 << 1, 0 };
const Patch P_BELL    = { OPR(0,0,0,0,3, 0,18, 15,4,6,5, 0), OPR(0,0,0,0,1, 0,0, 15,3,8,5, 0), 0 << 1, 0 };
const Patch P_ORGAN   = { OPR(0,0,1,0,1, 0,20, 15,0,0,5, 0), OPR(0,0,1,0,2, 0,10, 15,0,0,5, 0), (2 << 1) | 1, 0 };
const Patch P_PLUCK   = { OPR(0,0,0,1,1, 1,24, 15,7,7,7, 0), OPR(0,0,0,1,1, 0,0, 15,4,5,6, 0), 2 << 1, 0 };
const Patch P_BASS    = { OPR(0,0,0,0,1, 1,30, 15,7,7,8, 0), OPR(0,0,1,0,1, 0,0, 15,6,3,7, 0), 3 << 1, 0 };   // fretless/finger bass
const Patch P_STRINGS = { OPR(0,1,1,0,1, 0,28, 8,2,2,5, 0), OPR(0,1,1,0,1, 0,2, 8,2,1,5, 0), 1 << 1, 0 };
const Patch P_TROMBONE= { OPR(0,0,1,0,1, 0,17, 8,4,3,6, 0), OPR(0,0,1,0,1, 0,0, 9,3,2,6, 0), 5 << 1, 0 };
const Patch P_SAX     = { OPR(0,0,1,0,1, 0,22, 11,3,3,6, 0), OPR(0,1,1,0,1, 0,0, 11,3,2,6, 0), 4 << 1, 0 };   // baritone sax: reed, slight vibrato
const Patch P_ENGHORN = { OPR(0,0,1,0,2, 0,21, 10,3,3,6, 0), OPR(0,1,1,0,1, 0,0, 10,3,2,6, 0), 3 << 1, 0 };    // double reed
const Patch P_FLUTE   = { OPR(0,0,1,0,1, 0,34, 10,2,2,6, 0), OPR(0,1,1,0,1, 0,0, 10,2,1,6, 0), 1 << 1, 0 };
const Patch P_LEAD    = { OPR(0,0,1,0,1, 0,16, 13,2,2,6, 0), OPR(0,0,1,0,1, 0,0, 13,2,1,6, 1), 7 << 1, 0 };
const Patch P_PAD     = { OPR(0,1,1,0,2, 0,30, 6,2,2,7, 0), OPR(0,1,1,0,1, 0,4, 6,2,1,7, 0), (1 << 1), 0 };
const Patch P_NOISE   = { OPR(0,0,0,0,15, 0,8, 15,6,15,8, 0), OPR(0,0,0,0,7, 0,0, 15,6,15,8, 2), 7 << 1, 0 };
// percussion (fixed-pitch, very short): kick, snare, hat/cymbal, tom
const Patch D_KICK    = { OPR(0,0,0,0,1, 0,16, 15,9,15,8, 0), OPR(0,0,0,0,1, 0,0, 15,8,15,8, 0), 6 << 1, 0 };
const Patch D_SNARE   = { OPR(0,0,0,0,15, 0,10, 15,10,15,9, 0), OPR(0,0,0,0,3, 0,0, 15,9,15,9, 2), 7 << 1, 0 };
const Patch D_HAT     = { OPR(0,0,0,0,15, 0,6, 15,12,15,12, 0), OPR(0,0,0,0,13, 0,0, 15,12,15,12, 2), 7 << 1, 0 };
const Patch D_TOM     = { OPR(0,0,0,0,1, 0,18, 15,8,15,8, 0), OPR(0,0,0,0,1, 0,0, 15,7,15,8, 0), 4 << 1, 0 };

const Patch *program_patch(int p) {
    switch (p) {          // specific instruments used by CHIP01/CHIP02 first
    case 0: return &P_PIANO; case 19: return &P_ORGAN; case 35: return &P_BASS; case 57: return &P_TROMBONE;
    case 67: return &P_SAX; case 69: return &P_ENGHORN;
    }
    switch (p / 8) {      // GM families
    case 0: return &P_PIANO; case 1: return &P_BELL; case 2: return &P_ORGAN; case 3: return &P_PLUCK; case 4: return &P_BASS;
    case 5: case 6: return &P_STRINGS; case 7: return &P_TROMBONE; case 8: return &P_SAX; case 9: return &P_FLUTE;
    case 10: return &P_LEAD; case 11: case 12: return &P_PAD; case 13: return &P_PLUCK; case 14: return &P_BELL; default: return &P_NOISE;
    }
}
const Patch *drum_patch(int key, int *fixed_note) {
    if (key == 35 || key == 36) { *fixed_note = 36; return &D_KICK; }
    if (key == 38 || key == 40 || key == 37 || key == 39) { *fixed_note = 62; return &D_SNARE; }
    if (key >= 41 && key <= 50 && key != 42 && key != 44 && key != 46) { *fixed_note = 40 + (key - 41) * 2; return &D_TOM; }
    *fixed_note = 84; return &D_HAT;
}

struct Ev { uint32_t tick; int kind, ch, a, b; };   // kind 0 on, 1 off, 2 program, 3 tempo, 4 cc
struct Voice { int ch = -1, key = -1; const Patch *p = nullptr; int vel = 0; uint64_t age = 0; bool on = false; };

class Opl {
public:
    ymfm::ymfm_interface intf; ymfm::ymf262 chip;
    Opl() : chip(intf) { chip.reset(); w(1, 0x05, 1); w(0, 0x04, 0); w(0, 0x08, 0); w(0, 0xBD, 0); }
    void w(int bank, int reg, int val) { chip.write(bank ? 2 : 0, (uint8_t)reg); chip.write(bank ? 3 : 1, (uint8_t)val); }
};

const int MODOFF[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

struct Synth {
    Opl opl; Voice v[18]; uint64_t clock = 0;
    int prog[16] = {0}, vol[16], expr[16];
    int maxv = 0, steals = 0, notes = 0;
    Synth() { for (int i = 0; i < 16; i++) { vol[i] = 100; expr[i] = 127; } }

    static int attn(int vel, int vol, int ex) {      // TL steps (0.75 dB each) for a combined 0..127 level product
        double s = (vel / 127.0) * (vol / 127.0) * (ex / 127.0) * 0.5; if (s < 0.002) return 63;   // 0.5 = master headroom (-6 dB) so ymfm never clamps
        int a = (int)std::lround(-8.0 * std::log2(s)); return a > 63 ? 63 : a;
    }
    void set_tl(int c, int att, bool voice_on_patch = true) {
        (void)voice_on_patch;
        const Patch *p = v[c].p; int bank = c / 9, ci = c % 9, mo = MODOFF[ci], co = mo + 3;
        int ctl = std::min(63, (p->c.r40 & 63) + att);
        opl.w(bank, 0x40 + co, (p->c.r40 & 0xC0) | ctl);
        int vb = std::max(0, 127 - v[c].vel); int mtl = (p->m.r40 & 63) + vb * 5 / 127;      // brighter when played harder
        if (p->fbcn & 1) mtl += att;                                                         // additive: both operators carry sound
        opl.w(bank, 0x40 + mo, (p->m.r40 & 0xC0) | std::min(63, mtl));
    }
    void key_on(int c, int ch, int key, const Patch *p, int vel, int note_for_pitch) {
        int bank = c / 9, ci = c % 9, mo = MODOFF[ci], co = mo + 3;
        double f = 440.0 * std::pow(2.0, (note_for_pitch + p->tune - 69) / 12.0);
        int block = 0; double fn = 0;
        for (block = 0; block < 8; block++) { fn = f * std::pow(2.0, 20 - block) / OPL_RATE; if (fn < 1024) break; }
        if (block > 7) { block = 7; fn = 1023; }
        int fnum = (int)std::lround(fn); if (fnum > 1023) fnum = 1023;
        opl.w(bank, 0xB0 + ci, 0);                                  // key off while reprogramming
        opl.w(bank, 0x20 + mo, p->m.r20); opl.w(bank, 0x20 + co, p->c.r20);
        opl.w(bank, 0x60 + mo, p->m.r60); opl.w(bank, 0x60 + co, p->c.r60);
        opl.w(bank, 0x80 + mo, p->m.r80); opl.w(bank, 0x80 + co, p->c.r80);
        opl.w(bank, 0xE0 + mo, p->m.rE0); opl.w(bank, 0xE0 + co, p->c.rE0);
        opl.w(bank, 0xC0 + ci, (p->fbcn & 0x0F) | 0x30);            // feedback/connection + both output channels
        v[c].ch = ch; v[c].key = key; v[c].p = p; v[c].vel = vel; v[c].age = ++clock; v[c].on = true;
        set_tl(c, attn(vel, vol[ch & 15], expr[ch & 15]));
        opl.w(bank, 0xA0 + ci, fnum & 255);
        lastb[c] = 0x20 | (block << 2) | (fnum >> 8);
        opl.w(bank, 0xB0 + ci, lastb[c]);
        if (ch == 9) v[c].on = false;       // percussion is never key-offed: let the voice be reused
    }
    int lastb[18] = {0};
    void key_off(int c) { int bank = c / 9, ci = c % 9; v[c].on = false; v[c].age = ++clock; opl.w(bank, 0xB0 + ci, lastb[c] & ~0x20); }
    void note_on(int ch, int key, int vel) {
        const Patch *p; int pitch = key;
        if (ch == 9) { int fx; p = drum_patch(key, &fx); pitch = fx; } else p = program_patch(prog[ch]);
        // retrigger same key on same channel
        int c = -1;
        for (int i = 0; i < 18; i++) if (v[i].on && v[i].ch == ch && v[i].key == key) c = i;
        if (c < 0) for (int i = 0; i < 18; i++) if (!v[i].on && v[i].age == 0) { c = i; break; }
        if (c < 0) { uint64_t best = ~0ull; for (int i = 0; i < 18; i++) if (!v[i].on && v[i].age < best) { best = v[i].age; c = i; } }   // oldest released
        if (c < 0) { uint64_t best = ~0ull; for (int i = 0; i < 18; i++) if (v[i].age < best) { best = v[i].age; c = i; } steals++; }       // steal oldest sounding
        key_on(c, ch, key, p, vel, pitch);
        int n = 0; for (int i = 0; i < 18; i++) if (v[i].on) n++; maxv = std::max(maxv, n); notes++;
    }
    void note_off(int ch, int key) {
        for (int i = 0; i < 18; i++) if (v[i].on && v[i].ch == ch && v[i].key == key) { if (ch == 9) continue; key_off(i); return; }
    }
    void refresh_volume(int ch) {
        for (int i = 0; i < 18; i++) if (v[i].on && v[i].ch == ch) set_tl(i, attn(v[i].vel, vol[ch], expr[ch]));
    }
};

bool parse_smf(const char *path, std::vector<Ev> &ev, int &div) {
    FILE *f = fopen(path, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> b(n); if (fread(b.data(), 1, n, f) != (size_t)n) { fclose(f); return false; } fclose(f);
    if (n < 14 || memcmp(b.data(), "MThd", 4)) return false;
    int ntrk = b[10] << 8 | b[11]; div = b[12] << 8 | b[13]; if (div <= 0 || (div & 0x8000)) div = 240;
    long p = 14;
    for (int t = 0; t < ntrk && p + 8 <= n; t++) {
        long len = (long)b[p+4] << 24 | b[p+5] << 16 | b[p+6] << 8 | b[p+7]; long q = p + 8, e = std::min(n, q + len); p = e;
        uint32_t tick = 0; int run = 0;
        while (q < e) {
            uint32_t dt = 0; int cnt = 0; while (q < e && cnt++ < 4) { int x = b[q++]; dt = dt << 7 | (x & 127); if (!(x & 128)) break; }
            tick += dt; if (q >= e) break;
            int st = b[q];
            if (st == 0xFF) { int ty = b[q+1]; long l = 0, qq = q + 2; int cn = 0; while (qq < e && cn++ < 4) { int x = b[qq++]; l = l << 7 | (x & 127); if (!(x & 128)) break; }
                if (ty == 0x51 && l >= 3) ev.push_back({tick, 3, 0, b[qq] << 16 | b[qq+1] << 8 | b[qq+2], 0});
                q = qq + l; if (ty == 0x2F) break; continue; }
            if (st == 0xF0 || st == 0xF7) { long l = 0, qq = q + 1; int cn = 0; while (qq < e && cn++ < 4) { int x = b[qq++]; l = l << 7 | (x & 127); if (!(x & 128)) break; } q = qq + l; continue; }
            if (st & 0x80) { run = st; q++; } else st = run;
            int ty = st & 0xF0, ch = st & 15;
            if (ty == 0xC0 || ty == 0xD0) { int a = b[q++]; if (ty == 0xC0) ev.push_back({tick, 2, ch, a, 0}); }
            else { int a = b[q], v = b[q+1]; q += 2;
                if (ty == 0x90 && v) ev.push_back({tick, 0, ch, a, v});
                else if (ty == 0x80 || ty == 0x90) ev.push_back({tick, 1, ch, a, 0});
                else if (ty == 0xB0) ev.push_back({tick, 4, ch, a, v}); }
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev &x, const Ev &y) { return x.tick < y.tick || (x.tick == y.tick && x.kind == 1 && y.kind != 1); });
    return true;
}

}  // namespace

extern "C" int opl_render_midi(const char *path, int16_t **out, int *n_out, int out_rate, OplStats *st) {
    std::vector<Ev> ev; int div = 240; if (!parse_smf(path, ev, div)) return -1;
    // CHIP01/02 carry every part twice (MIDI channels 1-3 "extended" and 13-15 "base level"); play only one copy.
    bool lowc = false, highc = false;
    for (auto &e : ev) if (e.kind == 0) { if (e.ch < 10) lowc = true; else highc = true; }
    bool drop_high = lowc && highc;
    Synth *s = new Synth();
    std::vector<int32_t> mix; mix.reserve((size_t)OPL_RATE * 120);
    double sec = 0, t_samples = 0; uint32_t lt = 0; int tempo = 500000; size_t done = 0;
    auto advance_to = [&](double target) {
        size_t want = (size_t)target; if (want <= done) return;
        size_t cnt = want - done; ymfm::ymf262::output_data buf[256];
        while (cnt) { size_t k = std::min<size_t>(cnt, 256); s->opl.chip.generate(buf, (uint32_t)k);
            for (size_t i = 0; i < k; i++) mix.push_back((buf[i].data[0] + buf[i].data[1]) / 2); cnt -= k; }
        done = want;
    };
    for (auto &e : ev) {
        sec += (double)(e.tick - lt) * tempo / 1e6 / div; lt = e.tick; t_samples = sec * OPL_RATE;
        if (e.kind != 3 && drop_high && e.ch >= 10 && e.ch != 9) continue;
        advance_to(t_samples);
        switch (e.kind) {
        case 3: tempo = e.a; break;
        case 2: s->prog[e.ch] = e.a; break;
        case 4: if (e.a == 7) { s->vol[e.ch] = e.b; s->refresh_volume(e.ch); } else if (e.a == 11) { s->expr[e.ch] = e.b; s->refresh_volume(e.ch); } break;
        case 0: s->note_on(e.ch, e.a, e.b); break;
        case 1: s->note_off(e.ch, e.a); break;
        }
    }
    for (int i = 0; i < 18; i++) if (s->v[i].on) s->key_off(i);
    advance_to(t_samples + OPL_RATE * 0.8);      // release tail
    size_t total = mix.size();
    // peak / clipping before normalisation
    int peak = 0, clipped = 0; for (size_t i = 0; i < total; i++) { int a = std::abs(mix[i]); peak = std::max(peak, a); if (a > 32767) clipped++; }
    double gain = peak > 0 ? std::min(1.0, 0.85 * 32767.0 / peak) : 1.0;
    // if quiet, bring up to 0.5 FS peak
    if (peak > 0 && peak < 0.5 * 32767) gain = 0.5 * 32767.0 / peak;
    // box-filter decimation to out_rate
    double ratio = OPL_RATE / out_rate; int nout = (int)(total / ratio);
    int16_t *o = (int16_t *)malloc(sizeof(int16_t) * nout);
    double sumsq = 0; int pk2 = 0;
    for (int i = 0; i < nout; i++) {
        double a = i * ratio, b2 = a + ratio, acc = 0; size_t i0 = (size_t)a, i1 = (size_t)b2;
        for (size_t k = i0; k <= i1 && k < total; k++) { double lo = std::max(a, (double)k), hi = std::min(b2, (double)k + 1); if (hi > lo) acc += mix[k] * (hi - lo); }
        int v = (int)std::lround(acc / ratio * gain); v = std::max(-32767, std::min(32767, v)); o[i] = (int16_t)v; sumsq += (double)v * v; pk2 = std::max(pk2, std::abs(v));
    }
    if (st) { st->seconds = (double)nout / out_rate; st->peak_raw = peak; st->clipped_raw = clipped; st->gain = gain; st->peak_out = pk2;
              st->rms_dbfs = nout ? 20 * std::log10(std::sqrt(sumsq / nout) / 32768.0 + 1e-12) : -99; st->notes = s->notes; st->max_voices = s->maxv; st->steals = s->steals; }
    delete s; *out = o; *n_out = nout; return 0;
}
