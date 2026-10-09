package com.nfsu2.androidevolved;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Process;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Crash detection and tester reports (after the MW port). Reports contain this app's own log, the
 * device and the launcher options; never game files or saves. Nothing is uploaded automatically:
 * the player saves the ZIP and attaches it to the GitHub issue the launcher opens.
 */
final class Diagnostics {
    static final String ISSUES = "https://github.com/codepdbh/nfsu2-recompiled/issues/new";
    private static final int LIMIT = 4 * 1024 * 1024;
    private static final String PREFS = "nfsu2_diagnostics";

    /** A crash of the :game process the player has not been asked about yet, or null. */
    static String unreportedCrash(Context context) {
        if (Build.VERSION.SDK_INT < 30) return null;
        SharedPreferences prefs = context.getSharedPreferences(PREFS, 0);
        long seen = prefs.getLong("crash_seen", 0);
        ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (manager == null) return null;
        try {
            for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 8)) {
                if (exit.getTimestamp() <= seen || !exit.getProcessName().endsWith(":game")) continue;
                int reason = exit.getReason();
                if (reason == ApplicationExitInfo.REASON_CRASH || reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                        || reason == ApplicationExitInfo.REASON_ANR) {
                    return (reason == ApplicationExitInfo.REASON_ANR ? "El juego dejó de responder" : "El juego se cerró inesperadamente")
                            + (exit.getDescription() == null ? "" : " (" + exit.getDescription() + ")");
                }
            }
        } catch (RuntimeException ignored) { }
        return null;
    }

    static void acknowledgeCrashes(Context context) {
        context.getSharedPreferences(PREFS, 0).edit().putLong("crash_seen", System.currentTimeMillis()).apply();
    }

    static String summary(Context context) {
        GpuDrivers.Driver driver = GpuDrivers.selected(context);
        return "NFSU2 Android Evolved " + ReleaseUpdates.installed(context)
                + "\nTeléfono: " + Build.MANUFACTURER + " " + Build.MODEL
                + "\nAndroid: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")"
                + "\nSoC: " + (Build.VERSION.SDK_INT >= 31 ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL : Build.HARDWARE)
                + "\nDriver Vulkan: " + (driver == null ? "Sistema" : driver.name) + "\n";
    }

    static File createReport(Context context) throws Exception {
        File directory = new File(context.getCacheDir(), "reports");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("No se pudo crear el informe.");
        File[] old = directory.listFiles();
        if (old != null) for (File file : old)
            if (file.isFile() && file.lastModified() < System.currentTimeMillis() - TimeUnit.DAYS.toMillis(7)) file.delete();
        String date = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        File report = new File(directory, "NFSU2-diagnostico-" + date + ".zip");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(report))) {
            StringBuilder info = new StringBuilder(summary(context));
            info.append("Fecha: ").append(new Date()).append("\nABI: ").append(Arrays.toString(Build.SUPPORTED_ABIS))
                    .append("\n\nOpciones:\n");
            for (GameOptions.Option option : GameOptions.ALL)
                info.append(option.key).append('=').append(GameOptions.get(context, option.key)).append('\n');
            info.append("language=").append(GameOptions.language(context)).append('\n');
            add(zip, "informe.txt", info.toString().getBytes(StandardCharsets.UTF_8));
            add(zip, "logcat.txt", logcat(0));
            ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            if (Build.VERSION.SDK_INT >= 30 && manager != null) addExitInfo(zip, manager, context);
        } catch (Exception error) {
            report.delete();
            throw error;
        }
        return report;
    }

    /** The last lines of this app's log, for the issue body. */
    static String logTail(int lines) {
        try { return new String(logcat(lines), StandardCharsets.UTF_8); }
        catch (IOException error) { return error.toString(); }
    }

    static Uri issueUri(Context context, String crash, String zipName) {
        String tail = logTail(60);
        if (tail.length() > 5000) tail = tail.substring(tail.length() - 5000);
        String body = "### Qué ocurrió\n" + (crash == null ? "" : crash + "\n") + "\n### Pasos para reproducirlo\n\n"
                + "### Dispositivo\n" + summary(context)
                + "\n### Registro completo\nAdjunta aquí el archivo **" + zipName + "** que acabas de guardar.\n"
                + "\n### Últimas líneas del registro\n```\n" + tail + "\n```\n";
        return Uri.parse(ISSUES).buildUpon()
                .appendQueryParameter("title", "[Android] " + Build.MODEL + ": " + (crash == null ? "" : "cierre inesperado"))
                .appendQueryParameter("body", body).build();
    }

    private static void add(ZipOutputStream zip, String name, byte[] data) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(data);
        zip.closeEntry();
    }

    private static byte[] readLimited(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[16384];
        while (output.size() < LIMIT) {
            int count = input.read(buffer, 0, Math.min(buffer.length, LIMIT - output.size()));
            if (count < 0) break;
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    /** This app's main and crash log buffers (all processes of our uid); lines = 0 for the whole buffer. */
    private static byte[] logcat(int lines) throws IOException {
        java.lang.Process process = null;
        try {
            List<String> command = new java.util.ArrayList<>(Arrays.asList("logcat", "-d", "-b", "main", "-b", "crash",
                    "-v", "threadtime", "--uid=" + Process.myUid()));
            if (lines > 0) { command.add("-t"); command.add(String.valueOf(lines)); }
            process = new ProcessBuilder(command).redirectErrorStream(true).start();
            java.lang.Process active = process;
            Thread timeout = new Thread(() -> {
                try { if (!active.waitFor(5, TimeUnit.SECONDS)) active.destroyForcibly(); }
                catch (InterruptedException ignored) { active.destroyForcibly(); Thread.currentThread().interrupt(); }
            }, "ReportLogcatTimeout");
            timeout.setDaemon(true);
            timeout.start();
            try (InputStream input = process.getInputStream()) { return readLimited(input); }
        } finally { if (process != null) process.destroy(); }
    }

    private static void addExitInfo(ZipOutputStream zip, ActivityManager manager, Context context) throws IOException {
        StringBuilder info = new StringBuilder();
        try {
            boolean traceAdded = false;
            for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 5)) {
                info.append(new Date(exit.getTimestamp())).append(' ').append(exit.getProcessName())
                        .append(" reason=").append(exit.getReason()).append(" status=").append(exit.getStatus())
                        .append(" description=").append(exit.getDescription()).append('\n');
                if (!traceAdded && (exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE
                        || exit.getReason() == ApplicationExitInfo.REASON_ANR)) {
                    try (InputStream trace = exit.getTraceInputStream()) {
                        if (trace != null) {
                            add(zip, exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE ? "native-crash.pb" : "anr-trace.txt",
                                    readLimited(trace));
                            traceAdded = true;
                        }
                    } catch (IOException error) { info.append("Traza no disponible: ").append(error).append('\n'); }
                }
            }
        } catch (RuntimeException error) { info.append(error); }
        add(zip, "cierres.txt", info.toString().getBytes(StandardCharsets.UTF_8));
    }
}
