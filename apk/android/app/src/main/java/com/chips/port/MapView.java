package com.chips.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

/** Second-screen panel: whole 32x32 level, Chip highlighted, chips left, time, keys/boots and the level hint. */
public class MapView extends View {
    private final byte[] st = new byte[1280];
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    private final Runnable tick = new Runnable() { public void run() { invalidate(); postDelayed(this, 100); } };
    private static final int[] COL = new int[32];
    static {
        java.util.Arrays.fill(COL, 0xFFC8C8C8);
        COL[1] = 0xFF505050; COL[2] = 0xFF2060E0; COL[3] = 0xFFF06010; COL[4] = 0xFF20D040; COL[5] = 0xFFB0B0B0; COL[6] = 0xFF80E0F0; COL[7] = 0xFF208040;
        COL[8] = 0xFF2040FF; COL[9] = 0xFFE02020; COL[10] = 0xFF20C020; COL[11] = 0xFFE0D020; COL[12] = 0xFFF0F060; COL[13] = 0xFF703000; COL[14] = 0xFF9040C0;
        COL[15] = 0xFFC08030; COL[16] = 0xFF909090; COL[17] = 0xFF30B0F0; COL[18] = 0xFF906030; COL[19] = 0xFFFF2020; COL[20] = 0xFFFFFFFF; COL[21] = 0xFF909090;
        COL[22] = 0xFF604020; COL[23] = 0xFFA08050; COL[24] = 0xFF707070;
    }
    private static final int[] KEYC = { 0xFF2040FF, 0xFFE02020, 0xFF20C020, 0xFFE0D020 };

    public MapView(Context c) { super(c); setBackgroundColor(0xFF101820); }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); post(tick); }
    @Override protected void onDetachedFromWindow() { removeCallbacks(tick); super.onDetachedFromWindow(); }

    private static String cstr(byte[] b, int off, int max) {
        int n = 0; while (n < max && b[off + n] != 0) n++;
        return new String(b, off, n);
    }

    @Override protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight();
        boolean ok = false;
        try { ok = ChipsActivity.nativeState(st) != 0; } catch (Throwable t) { }
        p.setStyle(Paint.Style.FILL);
        if (!ok) { p.setColor(0xFFAAAAAA); p.setTextSize(h / 14f); c.drawText("Chip's Challenge", w / 20f, h / 2f, p); return; }
        boolean land = w >= h;
        float mapSz = land ? Math.min(h * 0.96f, w * 0.62f) : Math.min(w * 0.96f, h * 0.55f);
        float mx = land ? h * 0.02f : (w - mapSz) / 2f, my = land ? (h - mapSz) / 2f : h * 0.02f;
        float cs = mapSz / 32f;
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            int k = st[20 + y * 32 + x] & 255;
            p.setColor(k < COL.length ? COL[k] : 0xFFC8C8C8);
            r.set(mx + x * cs, my + y * cs, mx + (x + 1) * cs, my + (y + 1) * cs);
            if (k == 12 || k == 19 || k == 20 || k == 18 || (k >= 8 && k <= 11)) {
                p.setColor(0xFFC8C8C8); c.drawRect(r, p); p.setColor(COL[k]); r.inset(cs * 0.15f, cs * 0.15f); c.drawOval(r, p);
            } else c.drawRect(r, p);
        }
        int cx = st[7] & 255, cy = st[8] & 255;
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(Math.max(2f, cs * 0.35f)); p.setColor(0xFFFFFF00);
        c.drawCircle(mx + (cx + 0.5f) * cs, my + (cy + 0.5f) * cs, cs * 1.6f, p);
        p.setStyle(Paint.Style.FILL);
        float ix = land ? mx + mapSz + w * 0.03f : w * 0.04f, iy = land ? h * 0.08f : my + mapSz + h * 0.04f;
        float iw = land ? w - ix - w * 0.02f : w * 0.92f, ts = land ? h / 11f : h / 26f;
        int level = (st[1] & 255) | ((st[2] & 255) << 8), chips = (st[3] & 255) | ((st[4] & 255) << 8), secs = (st[5] & 255) | ((st[6] & 255) << 8);
        p.setColor(0xFFFFFFFF); p.setTextSize(ts); c.drawText("Level " + level, ix, iy, p);
        p.setTextSize(ts * 0.6f); p.setColor(0xFFB0C0D0); c.drawText(cstr(st, 1044, 63), ix, iy + ts * 0.75f, p);
        float y2 = iy + ts * 2.0f; p.setTextSize(ts); p.setColor(0xFFFFE040);
        c.drawText("Chips " + chips, ix, y2, p);
        p.setColor(secs != 0xFFFF && secs <= 15 ? 0xFFFF5050 : 0xFFFFE040);
        c.drawText(secs == 0xFFFF ? "Time --" : "Time " + secs, ix + iw * 0.5f, y2, p);
        float sq = ts * 0.9f, y3 = y2 + ts * 0.5f;
        for (int i = 0; i < 4; i++) {
            float x0 = ix + i * (sq * 1.25f);
            p.setColor(0xFF303840); r.set(x0, y3, x0 + sq, y3 + sq); c.drawRect(r, p);
            if ((st[10 + i] & 255) > 0) { p.setColor(KEYC[i]); r.inset(sq * 0.2f, sq * 0.2f); c.drawOval(r, p); }
        }
        String[] bn = { "F", "B", "S", "G" };
        for (int i = 0; i < 4; i++) {
            float x0 = ix + (4 + i) * (sq * 1.25f) + sq * 0.5f;
            p.setColor(0xFF303840); r.set(x0, y3, x0 + sq, y3 + sq); c.drawRect(r, p);
            if (st[14 + i] != 0) { p.setColor(0xFFFFFFFF); p.setTextSize(sq * 0.7f); c.drawText(bn[i], x0 + sq * 0.25f, y3 + sq * 0.75f, p); }
        }
        String hint = cstr(st, 1108, 159);
        int state = st[18];
        if (state == 2) { p.setColor(0xFFFF6060); p.setTextSize(ts * 0.7f); c.drawText("Chip is dead - press A", ix, y3 + sq * 2.1f, p); }
        else if (state == 1) { p.setColor(0xFF60FF60); p.setTextSize(ts * 0.7f); c.drawText("Level complete!", ix, y3 + sq * 2.1f, p); }
        else if (hint.length() > 0) {
            p.setColor(0xFFE0E0E0); p.setTextSize(ts * 0.5f);
            float y4 = y3 + sq * 2.0f, lh = ts * 0.62f; StringBuilder line = new StringBuilder();
            for (String word : hint.split(" ")) {
                if (p.measureText(line + " " + word) > iw && line.length() > 0) { c.drawText(line.toString(), ix, y4, p); y4 += lh; line.setLength(0); }
                if (line.length() > 0) line.append(' ');
                line.append(word);
            }
            if (line.length() > 0) c.drawText(line.toString(), ix, y4, p);
        }
    }
}
