# Music

Menu entry "Music": FM (OPL3) / Android MIDI / Off (left/right or A cycles; saved as `musicmode` in the settings file; old `music 0/1` saves still load).

* FM (OPL3): `src/cc_opl.cpp` reads CHIP01/02.MID at start, sequences it (tempo, program change, notes, CC7/CC11, 18 voices, channel 10 drums, voice stealing, pitch bend not needed: the files have none) and renders it offline through the ymfm YMF262 core to a looped clip (22050 Hz mono).
* Android MIDI: `ChipsActivity.java` plays `CHIP0n.MID` from the data dir with `android.media.MediaPlayer`, looped; it follows the native state (bytes 1040-1042: mode, track, menu-open ducking), pauses in `onPause`, switches file when the level changes. If it throws, `nativeMusicStatus(0)` makes the engine use FM instead.
* Fallback: the old additive synth is used if the OPL render fails.

## Licenses
* ymfm (Aaron Giles, BSD-3-Clause), sources and LICENSE in `third_party/ymfm` (commit in COMMIT.txt). Only the OPL/PCM/ADPCM parts are built.
* Patch bank: hand-authored in `cc_opl.cpp` (about 17 patches). No GENMIDI/OP2/third-party bank used.
* Files use programs 0, 19, 35, 57, 67, 69 (CHIP01: 0/67/57; CHIP02: 35/19/69); no drums, no pitch bend. Each part is duplicated on channels 1-3 and 13-15; only one copy is played.

## Verify
`bash build_desktop.sh` then `build/opl2wav orig/CHIP01.MID out.wav`.
