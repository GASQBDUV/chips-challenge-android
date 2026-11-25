// Renders a MIDI file through the OPL3 sequencer to a mono 16-bit WAV and prints level statistics.
#include "cc_opl.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    int16_t *d; int n; OplStats st; int rate = 22050;
    if (opl_render_midi(argv[1], &d, &n, rate, &st)) { puts("render failed"); return 1; }
    FILE *f = fopen(argv[2], "wb"); unsigned dl = n * 2, rl = 36 + dl, sr = rate, br = rate * 2; unsigned short fmt = 1, ch = 1, ba = 2, bps = 16; unsigned fl = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&rl, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f); fwrite(d, 2, n, f); fclose(f);
    printf("%s: %.1f s, notes %d, max voices %d, steals %d, raw peak %d (clipped samples %d), gain %.3f, out peak %d, rms %.1f dBFS\n", argv[1], st.seconds, st.notes, st.max_voices, st.steals, st.peak_raw, st.clipped_raw, st.gain, st.peak_out, st.rms_dbfs);
    return 0;
}
