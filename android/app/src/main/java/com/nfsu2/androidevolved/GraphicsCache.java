package com.nfsu2.androidevolved;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.os.Process;
import java.io.File;
import java.util.List;

/** Only disposable graphics caches in this application's private cache directory. */
final class GraphicsCache {
    // Keep in sync with configurePipelineCache() in main.cpp. Epoch 2 excludes
    // caches from the previous DXVK preparation; those files remain untouched.
    static final String DIRECTORY = "dxvk-v2";

    static void prompt(Activity activity) {
        new AlertDialog.Builder(activity).setTitle("Reset graphics cache")
                .setMessage("The current game session will close. Shaders will recompile when playing; saves and settings are kept.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Reset", (dialog, which) -> reset(activity)).show();
    }

    private static void reset(Activity activity) {
        try {
            ActivityManager manager = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
            if (manager == null) throw new IllegalStateException("Could not check game process.");
            List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
            if (running != null) for (ActivityManager.RunningAppProcessInfo process : running)
                if (process.uid == Process.myUid() && process.processName.equals(activity.getPackageName() + ":game"))
                    Process.killProcess(process.pid);
            File cache = new File(activity.getCacheDir(), DIRECTORY);
            if (cache.exists()) {
                File backup = new File(activity.getCacheDir(), DIRECTORY + "-reset-" + System.currentTimeMillis());
                if (!cache.isDirectory() || !cache.renameTo(backup))
                    throw new IllegalStateException("Could not reset graphics cache.");
            }
            new AlertDialog.Builder(activity).setTitle("Graphics cache ready")
                    .setMessage("The next launch will generate a fresh cache. The previous cache is backed up in the app private cache.")
                    .setPositiveButton("OK", null).show();
        } catch (RuntimeException error) {
            new AlertDialog.Builder(activity).setTitle("Graphics cache").setMessage(error.getMessage())
                    .setPositiveButton("OK", null).show();
        }
    }
}
