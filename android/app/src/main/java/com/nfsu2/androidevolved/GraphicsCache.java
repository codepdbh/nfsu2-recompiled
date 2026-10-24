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
        new AlertDialog.Builder(activity).setTitle(R.string.graphics_cache_reset_title)
                .setMessage(R.string.graphics_cache_reset_message)
                .setNegativeButton(R.string.common_cancel, null)
                .setPositiveButton(R.string.graphics_cache_reset_action, (dialog, which) -> reset(activity)).show();
    }

    private static void reset(Activity activity) {
        try {
            ActivityManager manager = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
            if (manager == null) throw new IllegalStateException(activity.getString(R.string.graphics_cache_process_error));
            List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
            if (running != null) for (ActivityManager.RunningAppProcessInfo process : running)
                if (process.uid == Process.myUid() && process.processName.equals(activity.getPackageName() + ":game"))
                    Process.killProcess(process.pid);
            File cache = new File(activity.getCacheDir(), DIRECTORY);
            if (cache.exists()) {
                File backup = new File(activity.getCacheDir(), DIRECTORY + "-reset-" + System.currentTimeMillis());
                if (!cache.isDirectory() || !cache.renameTo(backup))
                    throw new IllegalStateException(activity.getString(R.string.graphics_cache_reset_error));
            }
            new AlertDialog.Builder(activity).setTitle(R.string.graphics_cache_ready_title)
                    .setMessage(R.string.graphics_cache_ready_message)
                    .setPositiveButton(R.string.common_ok, null).show();
        } catch (RuntimeException error) {
            new AlertDialog.Builder(activity).setTitle(R.string.graphics_cache_title).setMessage(error.getMessage())
                    .setPositiveButton(R.string.common_ok, null).show();
        }
    }
}
