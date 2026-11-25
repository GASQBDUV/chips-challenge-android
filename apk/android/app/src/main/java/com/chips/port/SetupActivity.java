package com.chips.port;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * First-run setup. The APK contains no game files: the user's own copy of CHIPS.DAT, CHIPS.EXE and the WAV/MID files
 * is read from files/gamedata (copied here via the folder picker) or from /sdcard/Chips/data.
 * If the data is already present the game starts immediately.
 */
public class SetupActivity extends Activity {
    private static final int REQ_TREE = 1, REQ_PERM = 2;
    private TextView status;

    private static boolean ready(File d) {
        return d.isDirectory() && (new File(d, "CHIPS.DAT").isFile() || new File(d, "chips.dat").isFile())
            && (new File(d, "CHIPS.EXE").isFile() || new File(d, "chips.exe").isFile());
    }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (!getIntent().getBooleanExtra("setup", false) && ready(ChipsActivity.dataDir(this))) { launch(); return; }
        LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); l.setGravity(Gravity.CENTER); l.setPadding(48, 24, 48, 24);
        TextView t = new TextView(this); t.setTextSize(18);
        t.setText("Chip's Challenge needs your own game files: CHIPS.DAT, CHIPS.EXE (and optionally the .WAV and .MID files) from the original Windows 3.1 version.\n\n"
            + "Put them in /sdcard/Chips/data, or pick the folder that holds them and they will be copied into the app.");
        l.addView(t);
        Button pick = new Button(this); pick.setText("Choose folder with the game files (copy)");
        pick.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TREE));
        l.addView(pick);
        Button sd = new Button(this); sd.setText("Use /sdcard/Chips/data");
        sd.setOnClickListener(v -> {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) requestPermissions(new String[] { android.Manifest.permission.READ_EXTERNAL_STORAGE }, REQ_PERM);
            else checkSd();
        });
        l.addView(sd);
        status = new TextView(this); status.setTextSize(16); l.addView(status);
        setContentView(l);
    }

    private void checkSd() {
        if (ready(new File("/sdcard/Chips/data"))) launch(); else status.setText("CHIPS.DAT / CHIPS.EXE not found in /sdcard/Chips/data");
    }
    @Override public void onRequestPermissionsResult(int rc, String[] p, int[] r) { if (rc == REQ_PERM) checkSd(); }

    private void launch() { startActivity(new Intent(this, ChipsActivity.class)); finish(); }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        if (req != REQ_TREE || res != RESULT_OK || data == null) return;
        final Uri tree = data.getData();
        status.setText("Copying...");
        new Thread(() -> {
            int n = 0; String err = null;
            try { n = copyTree(tree); } catch (Exception e) { err = e.toString(); }
            final int cnt = n; final String er = err;
            runOnUiThread(() -> {
                if (ready(new File(getFilesDir(), "gamedata"))) launch();
                else status.setText("Copied " + cnt + " files, but CHIPS.DAT / CHIPS.EXE are missing" + (er != null ? " (" + er + ")" : ""));
            });
        }).start();
    }

    private int copyTree(Uri tree) throws Exception {
        File out = new File(getFilesDir(), "gamedata"); out.mkdirs();
        String docId = DocumentsContract.getTreeDocumentId(tree);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        int n = 0;
        try (android.database.Cursor c = getContentResolver().query(children, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String name = c.getString(1), up = name.toUpperCase();
                if (!(up.endsWith(".DAT") || up.endsWith(".EXE") || up.endsWith(".WAV") || up.endsWith(".MID"))) continue;
                Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0));
                try (InputStream in = getContentResolver().openInputStream(doc); OutputStream os = new java.io.FileOutputStream(new File(out, name))) {
                    byte[] buf = new byte[65536]; int r; while ((r = in.read(buf)) > 0) os.write(buf, 0, r);
                }
                n++;
            }
        }
        return n;
    }
}
