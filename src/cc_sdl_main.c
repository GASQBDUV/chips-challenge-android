/* SDL2 front end (desktop + Android). Android: argv[1] = app files dir, argv[2] = game data dir. Desktop: argv[1] = data dir. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cc_app.h"
#include "cc_audio.h"
#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "ChipsPort", __VA_ARGS__)
#else
#define LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

static App app;
static unsigned char g_state[STATE_BYTES];
static uint32_t g_panel[PAN_W * PAN_H];
static volatile Uint32 g_panel_req;      /* tick of the last nativePanel() call: the panel is only rendered while a second screen asks for it */
static SDL_mutex *g_mu;

#ifdef __ANDROID__
JNIEXPORT void JNICALL Java_com_chips_port_ChipsActivity_nativeMusicStatus(JNIEnv *env, jclass cls, jint ok) { (void)env; (void)cls; audio_android_failed(!ok); LOG("Android MIDI status ok=%d", (int)ok); }
/* Java side: static native int nativeState(byte[] out) */
JNIEXPORT jint JNICALL Java_com_chips_port_ChipsActivity_nativeState(JNIEnv *env, jclass cls, jbyteArray arr) {
    (void)cls; if (!g_mu) return 0;
    jsize n = (*env)->GetArrayLength(env, arr); if (n > STATE_BYTES) n = STATE_BYTES;
    SDL_LockMutex(g_mu); (*env)->SetByteArrayRegion(env, arr, 0, n, (const jbyte *)g_state); SDL_UnlockMutex(g_mu);
    return g_state[0];
}
/* Java side: static native int nativePanel(int[] out): the second-screen picture (PAN_W x PAN_H ARGB) */
JNIEXPORT jint JNICALL Java_com_chips_port_ChipsActivity_nativePanel(JNIEnv *env, jclass cls, jintArray arr) {
    (void)cls; g_panel_req = SDL_GetTicks() | 1; if (!g_mu || !g_state[0]) return 0;
    jsize n = (*env)->GetArrayLength(env, arr); if (n > PAN_W * PAN_H) n = PAN_W * PAN_H;
    SDL_LockMutex(g_mu); (*env)->SetIntArrayRegion(env, arr, 0, n, (const jint *)g_panel); SDL_UnlockMutex(g_mu);
    return 1;
}
#endif

typedef struct { SDL_FingerID id; float x, y; int on; } Finger;
static Finger fingers[10];
static float win_to_logical(SDL_Renderer *r, float nx, float ny, float *ly) {
    int w, h; SDL_GetRendererOutputSize(r, &w, &h);
    float lx, lyy; SDL_RenderWindowToLogical(r, (int)(nx * w), (int)(ny * h), &lx, &lyy); *ly = lyy; return lx;
}

static uint32_t kbd_mask(void) {
    const Uint8 *k = SDL_GetKeyboardState(NULL); uint32_t m = 0;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) m |= B_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) m |= B_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) m |= B_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) m |= B_RIGHT;
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_Z]) m |= B_A;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_BACKSPACE]) m |= B_B;
    if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_PAGEUP]) m |= B_L;
    if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_PAGEDOWN]) m |= B_R;
    if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_TAB]) m |= B_START;
    if (k[SDL_SCANCODE_R] || k[SDL_SCANCODE_F5]) m |= B_RESTART;
    return m;
}
static uint32_t pad_mask(SDL_GameController *c) {
    if (!c) return 0; uint32_t m = 0;
#define BT(b) SDL_GameControllerGetButton(c, b)
    if (BT(SDL_CONTROLLER_BUTTON_DPAD_UP)) m |= B_UP; if (BT(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) m |= B_DOWN;
    if (BT(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) m |= B_LEFT; if (BT(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= B_RIGHT;
    if (BT(SDL_CONTROLLER_BUTTON_A)) m |= B_A; if (BT(SDL_CONTROLLER_BUTTON_B)) m |= B_B;
    if (BT(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) m |= B_L; if (BT(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= B_R;
    if (BT(SDL_CONTROLLER_BUTTON_START)) m |= B_START;
    if (BT(SDL_CONTROLLER_BUTTON_BACK)) m |= B_RESTART;
    int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (abs(lx) > 12000 || abs(ly) > 12000) { if (abs(lx) > abs(ly)) m |= lx < 0 ? B_LEFT : B_RIGHT; else m |= ly < 0 ? B_UP : B_DOWN; }
    return m;
}

int main(int argc, char **argv) {
    char datadir[600] = "orig", savepath[700] = "cc_save.txt";
#ifdef __ANDROID__
    if (argc > 2) { snprintf(datadir, sizeof datadir, "%s", argv[2]); snprintf(savepath, sizeof savepath, "%s/cc_save.txt", argv[1]); }
#else
    if (argc > 1) snprintf(datadir, sizeof datadir, "%s", argv[1]);
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER)) { LOG("SDL_Init: %s", SDL_GetError()); return 1; }
    g_mu = SDL_CreateMutex();
    if (app_init(&app, datadir, savepath)) { LOG("cannot load game data from %s", datadir); SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chip's Challenge", "CHIPS.DAT / CHIPS.EXE not found in the data folder.", NULL); return 1; }
    LOG("loaded %d levels from %s", app.dat.n, datadir);
    int ac = audio_init(datadir); LOG("audio: %d wavs", ac);
    Uint32 wf = SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
    wf = SDL_WINDOW_FULLSCREEN;
#endif
    SDL_Window *win = SDL_CreateWindow("Chip's Challenge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, wf);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(ren, SCR_W, SCR_H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    if (pad) app.hw_seen = 1;
#ifndef __ANDROID__
    audio_android_failed(1);          /* no Android MediaPlayer on desktop: mode 2 falls back to FM */
#endif
    Uint32 last = SDL_GetTicks(); int acc = 0, run = 1, frame = 0;
    int mouse_down = 0; float mx = 0, my = 0; int exit_after = getenv("CC_EXIT_AFTER") ? atoi(getenv("CC_EXIT_AFTER")) : 0;
    while (run && !app.want_quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: run = 0; break;
            case SDL_CONTROLLERDEVICEADDED: if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED: if (pad && !SDL_GameControllerGetAttached(pad)) { SDL_GameControllerClose(pad); pad = NULL; } break;
            case SDL_FINGERDOWN: case SDL_FINGERMOTION: {
                if (e.type == SDL_FINGERDOWN) app.hw_seen = 0;     /* touching the screen brings the on-screen controls back in Auto mode */
                int k = -1; for (int i = 0; i < 10; i++) if (fingers[i].on && fingers[i].id == e.tfinger.fingerId) k = i;
                if (k < 0) for (int i = 0; i < 10; i++) if (!fingers[i].on) { k = i; break; }
                if (k >= 0) { fingers[k].on = 1; fingers[k].id = e.tfinger.fingerId; fingers[k].x = e.tfinger.x; fingers[k].y = e.tfinger.y; }
                break; }
            case SDL_FINGERUP: for (int i = 0; i < 10; i++) if (fingers[i].on && fingers[i].id == e.tfinger.fingerId) fingers[i].on = 0; break;
            case SDL_MOUSEBUTTONDOWN: if (e.button.which != SDL_TOUCH_MOUSEID) { mouse_down = 1; } break;
            case SDL_MOUSEBUTTONUP: mouse_down = 0; break;
            case SDL_MOUSEMOTION: break;
            case SDL_APP_WILLENTERBACKGROUND: app_save(&app); break;
            case SDL_KEYDOWN: if (e.key.keysym.sym == SDLK_AC_BACK) { app.mode = M_MENU; } break;
            }
        }
        uint32_t m = kbd_mask() | pad_mask(pad);
        if (m) app.hw_seen = 1;                                         /* physical keys / gamepad in use: Auto hides the touch overlay */
        for (int i = 0; i < 10; i++) if (fingers[i].on) { float ly; float lx = win_to_logical(ren, fingers[i].x, fingers[i].y, &ly); m |= app_touch_mask(&app, lx, ly); }
        if (mouse_down) { int px, py; SDL_GetMouseState(&px, &py); float lx, ly; SDL_RenderWindowToLogical(ren, px, py, &lx, &ly); m |= app_touch_mask(&app, lx, ly); (void)mx; (void)my; }
        Uint32 now = SDL_GetTicks(); acc += (int)(now - last); last = now; if (acc > 500) acc = 500;
        while (acc >= 110) {
            acc -= 110;
            app_step(&app, m);
            if (app.snd_mask) { for (int i = 0; i < SND_COUNT; i++) if (app.snd_mask & (1u << i)) audio_play(i); app.snd_mask = 0; }
        }
        audio_enable(app.sound_on); audio_set_music_mode(app.music_mode); audio_music(app.music_mode ? app.cur_track : 0);
        app_render(&app);
        if ((frame++ & 1) == 0) { SDL_LockMutex(g_mu); app_pack_state(&app, g_state, STATE_BYTES);
            if (g_panel_req && SDL_GetTicks() - g_panel_req < 1500) { app_render_panel(&app); memcpy(g_panel, app.pfb, sizeof g_panel); }
            SDL_UnlockMutex(g_mu); }
        SDL_UpdateTexture(tex, NULL, app.fb, SCR_W * 4);
        SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, NULL, NULL); SDL_RenderPresent(ren);
        SDL_Delay(2);
        if (exit_after && frame >= exit_after) break;
    }
    app_save(&app);
    return 0;
}
