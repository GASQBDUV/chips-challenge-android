/* General MIDI -> OPL3 (YMF262, emulated by ymfm, BSD-3) sequencer with a hand-authored patch bank. Offline renderer. */
#ifndef CC_OPL_H
#define CC_OPL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    double seconds;          /* rendered length */
    int peak_raw, clipped_raw;/* mono peak and count of |sample|>32767 BEFORE normalisation */
    double gain;             /* normalisation gain applied (<=1 means attenuation) */
    int peak_out; double rms_dbfs;
    int notes, max_voices, steals;
} OplStats;
/* Renders path (SMF) to mono s16 at out_rate. *out is malloc'ed (free() it). Returns 0 on success. */
int opl_render_midi(const char *path, int16_t **out, int *n, int out_rate, OplStats *st);
#ifdef __cplusplus
}
#endif
#endif
