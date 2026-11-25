set -e
NDK=${ANDROID_HOME:-$HOME/android-sdk}/ndk/27.3.13750724
BIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
CC=$BIN/aarch64-linux-android26-clang
CXX=$BIN/aarch64-linux-android26-clang++
H=/mnt/c/Claude/chips
J=$H/apk/android/app/src/main/jniLibs/arm64-v8a
O=/root/andr/chips_obj
mkdir -p $J $O
CF="-O2 -fPIC -Wno-unused-result -I$H/apk/sdl/include -I$H/src"
for f in cc_sdl_main cc_app cc_engine cc_data cc_audio; do $CC $CF -c $H/src/$f.c -o $O/$f.o; done
XF="-O2 -fPIC -std=c++14 -fno-exceptions -fno-rtti -Wno-unused -I$H/src -I$H/third_party/ymfm"
$CXX $XF -c $H/src/cc_opl.cpp -o $O/cc_opl.o
for f in ymfm_opl ymfm_adpcm ymfm_pcm; do $CXX $XF -c $H/third_party/ymfm/$f.cpp -o $O/$f.o; done
$CXX -shared -Wl,-z,max-page-size=16384 -static-libstdc++ $O/*.o -o $J/libmain.so -L$H/apk/sdl -lSDL2 -llog -lm
cp $H/apk/sdl/libSDL2.so $J/
ls -l $J
