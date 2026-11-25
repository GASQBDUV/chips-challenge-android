package com.chips.port;

import org.libsdl.app.SDLActivity;
import java.io.File;

/** SDL game activity. The second display (if any) shows a Presentation with the full-level minimap, fed by nativeState(). */
public class ChipsActivity extends SDLActivity {
    static native int nativeState(byte[] out);   // implemented in libmain.so (src/cc_sdl_main.c)
    static native void nativeMusicStatus(int ok); // tells the engine whether Android MIDI playback works (0 -> engine falls back to FM)

    // ---- Android built-in MIDI player (music mode 2): driven by the native state (bytes 1040..1042) ----
    private android.media.MediaPlayer mp;
    private int curTrack = -1;
    private boolean midiFailed, activityPaused;
    private final byte[] mst = new byte[1280];
    private final android.os.Handler mh = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable musicTick = new Runnable() { public void run() { try { musicStep(); } catch (Throwable t) { failMidi(t); } mh.postDelayed(this, 250); } };

    private void failMidi(Throwable t) {
        android.util.Log.e("ChipsPort", "android midi: " + t);
        if (!midiFailed) { midiFailed = true; nativeMusicStatus(0); }
        stopMp();
    }
    private void stopMp() { try { if (mp != null) { mp.release(); } } catch (Throwable t) { } mp = null; curTrack = -1; }

    private void musicStep() throws Exception {
        if (midiFailed || nativeState(mst) == 0) return;
        int mode = mst[1040], track = mst[1041], duck = mst[1042];
        if (mode != 2 || track < 1 || activityPaused) { if (mp != null && (mode != 2 || activityPaused)) { if (mode != 2) stopMp(); else if (mp.isPlaying()) mp.pause(); } return; }
        if (mp == null || track != curTrack) {        // start / level change (odd and even levels alternate CHIP01 / CHIP02)
            stopMp();
            File f = new File(dataDir(this), "CHIP0" + track + ".MID");
            if (!f.isFile()) f = new File(dataDir(this), "chip0" + track + ".mid");
            if (!f.isFile()) throw new java.io.FileNotFoundException(f.getPath());
            mp = new android.media.MediaPlayer();
            mp.setDataSource(f.getAbsolutePath()); mp.setLooping(true); mp.prepare(); mp.start();
            curTrack = track; nativeMusicStatus(1);
        }
        float vol = duck != 0 ? 0.35f : 1.0f;          // menu open: duck the music
        mp.setVolume(vol, vol);
        if (!mp.isPlaying()) mp.start();
    }

    private android.app.Presentation panel;

    @Override protected void onCreate(android.os.Bundle b) {
        super.onCreate(b);
        mh.postDelayed(musicTick, 1000);
        try { startPanel(); } catch (Throwable t) { android.util.Log.e("ChipsPort", "panel: " + t); }
    }

    private void startPanel() {
        android.hardware.display.DisplayManager dm = (android.hardware.display.DisplayManager) getSystemService(DISPLAY_SERVICE);
        android.view.Display[] ds = dm.getDisplays(android.hardware.display.DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (ds.length == 0) { android.util.Log.i("ChipsPort", "panel: no second display"); return; }
        panel = new android.app.Presentation(this, ds[0]);
        panel.setContentView(new MapView(this));
        panel.getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        panel.show();
        android.util.Log.i("ChipsPort", "panel shown on display " + ds[0].getDisplayId());
    }

    @Override protected void onPause() { activityPaused = true; try { if (mp != null && mp.isPlaying()) mp.pause(); } catch (Throwable t) { } super.onPause(); }
    @Override protected void onResume() { activityPaused = false; super.onResume(); }

    @Override protected void onDestroy() { mh.removeCallbacks(musicTick); stopMp(); try { if (panel != null) panel.dismiss(); } catch (Throwable t) { } super.onDestroy(); }

    /** Data dir: app-private copy first, then /sdcard/Chips/data. */
    static File dataDir(android.content.Context c) {
        File a = new File(c.getFilesDir(), "gamedata");
        if (new File(a, "CHIPS.DAT").isFile() || new File(a, "chips.dat").isFile()) return a;
        return new File("/sdcard/Chips/data");
    }

    @Override protected String[] getLibraries() { return new String[] { "SDL2", "main" }; }

    // argv[1] = app files dir (save file), argv[2] = game data dir
    @Override protected String[] getArguments() {
        return new String[] { getFilesDir().getAbsolutePath(), dataDir(this).getAbsolutePath() };
    }
}
