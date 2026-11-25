package com.chips.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.View;

/** Second-screen panel: shows the picture rendered by the native engine (real tiles of the whole level, the real info panel, hint). */
public class PanelView extends View {
    static final int W = 640, H = 480;
    private final int[] px = new int[W * H];
    private final Bitmap bmp = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
    private final Paint p = new Paint();          // no filtering: keeps the pixel look
    private final Rect src = new Rect(0, 0, W, H), dst = new Rect();
    private boolean have;
    private final Runnable tick = new Runnable() {
        public void run() {
            try { if (ChipsActivity.nativePanel(px) != 0) { bmp.setPixels(px, 0, W, 0, 0, W, H); have = true; invalidate(); } } catch (Throwable t) { }
            postDelayed(this, 100);
        }
    };

    public PanelView(Context c) { super(c); setBackgroundColor(0xFF000000); p.setFilterBitmap(false); }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); post(tick); }
    @Override protected void onDetachedFromWindow() { removeCallbacks(tick); super.onDetachedFromWindow(); }

    @Override protected void onDraw(Canvas c) {
        if (!have) return;
        int vw = getWidth(), vh = getHeight(); if (vw == 0 || vh == 0) return;
        float s = Math.min((float) vw / W, (float) vh / H);       // largest scale that keeps 4:3
        int dw = Math.round(W * s), dh = Math.round(H * s), x = (vw - dw) / 2, y = (vh - dh) / 2;
        dst.set(x, y, x + dw, y + dh);
        c.drawBitmap(bmp, src, dst, p);
    }
}
