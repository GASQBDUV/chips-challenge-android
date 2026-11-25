#ifndef CC_AUDIO_H
#define CC_AUDIO_H
#define SND_N 10   /* must equal SND_COUNT in cc_app.h */
int  audio_init(const char *datadir);   /* >=0 number of WAVs loaded */
void audio_play(int id);
void audio_music(int track);            /* 0 = none, 1 = CHIP01.MID, 2 = CHIP02.MID */
void audio_enable(int sfx_on);
void audio_set_music_mode(int m);       /* 0 off, 1 FM (OPL3), 2 Android MIDI (Java plays; native silent unless audio_android_failed) */
void audio_android_failed(int f);
int  audio_fm_available(void);
int  audio_render_midi_wav(const char *mid, const char *wav);
#endif
