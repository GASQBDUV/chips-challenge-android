set -e
cd /mnt/c/Claude/chips
mkdir -p build build/obj
W="-O1 -g -Wall -Wno-unused-result -Wno-misleading-indentation -Wno-format-truncation -Isrc"
gcc $W src/cc_headless.c src/cc_app.c src/cc_engine.c src/cc_data.c -o build/cc_headless
gcc $W test/test_rules.c src/cc_engine.c src/cc_data.c -o build/test_rules
gcc -O2 -Isrc test/solve.c src/cc_engine.c src/cc_data.c -o build/solve
Y=third_party/ymfm
XW="-O2 -std=c++14 -Wno-unused -Isrc -I$Y"
for f in src/cc_opl $Y/ymfm_opl $Y/ymfm_adpcm $Y/ymfm_pcm; do
  g++ $XW -c $f.cpp -o build/obj/$(basename $f).o
done
gcc $W $(sdl2-config --cflags) -c src/cc_sdl_main.c -o build/obj/sdlmain.o
gcc $W $(sdl2-config --cflags) -c src/cc_audio.c -o build/obj/audio.o
gcc $W -c src/cc_app.c -o build/obj/app.o
gcc $W -c src/cc_engine.c -o build/obj/engine.o
gcc $W -c src/cc_data.c -o build/obj/data.o
OPLO="build/obj/cc_opl.o build/obj/ymfm_opl.o build/obj/ymfm_adpcm.o build/obj/ymfm_pcm.o"
g++ build/obj/sdlmain.o build/obj/audio.o build/obj/app.o build/obj/engine.o build/obj/data.o $OPLO -o build/chips_sdl $(sdl2-config --libs) -lm
g++ $XW test/opl2wav.cpp $OPLO -o build/opl2wav
gcc $W $(sdl2-config --cflags) -c test/midi2wav.c -o build/obj/m2w.o
g++ build/obj/m2w.o build/obj/audio.o $OPLO -o build/midi2wav $(sdl2-config --libs) -lm
echo built
