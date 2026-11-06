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
        new AlertDialog.Builder(activity).setTitle(GameOptions.tr(activity, "Reiniciar caché gráfica", "Reset graphics cache"))
                .setMessage(GameOptions.tr(activity, "Se cerrará la sesión actual del juego. Los shaders se volverán a compilar al jugar; las partidas y los ajustes se conservan.",
                        "The current game session will close. Shaders will be rebuilt when you play; saves and settings are kept."))
                .setNegativeButton(GameOptions.tr(activity, "Cancelar", "Cancel"), null)
                .setPositiveButton(GameOptions.tr(activity, "Reiniciar", "Reset"), (dialog, which) -> reset(activity)).show();
    }

    private static void reset(Activity activity) {
        try {
            ActivityManager manager = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
            if (manager == null) throw new IllegalStateException(GameOptions.tr(activity, "No se pudo comprobar el proceso del juego.", "Could not check the game process."));
            List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
            if (running != null) for (ActivityManager.RunningAppProcessInfo process : running)
                if (process.uid == Process.myUid() && process.processName.equals(activity.getPackageName() + ":game"))
                    Process.killProcess(process.pid);
            File cache = new File(activity.getCacheDir(), DIRECTORY);
            if (cache.exists()) {
                File backup = new File(activity.getCacheDir(), DIRECTORY + "-reset-" + System.currentTimeMillis());
                if (!cache.isDirectory() || !cache.renameTo(backup))
                    throw new IllegalStateException(GameOptions.tr(activity, "No se pudo reiniciar la caché gráfica.", "Could not reset the graphics cache."));
            }
            new AlertDialog.Builder(activity).setTitle(GameOptions.tr(activity, "Caché gráfica preparada", "Graphics cache ready"))
                    .setMessage(GameOptions.tr(activity, "El próximo arranque generará una caché nueva. La anterior queda respaldada en la caché privada de la app.",
                            "The next launch will build a new cache. The old one is backed up in the app's private cache."))
                    .setPositiveButton(GameOptions.tr(activity, "Aceptar", "OK"), null).show();
        } catch (RuntimeException error) {
            new AlertDialog.Builder(activity).setTitle(GameOptions.tr(activity, "Caché gráfica", "Graphics cache")).setMessage(error.getMessage())
                    .setPositiveButton(GameOptions.tr(activity, "Aceptar", "OK"), null).show();
        }
    }
}
