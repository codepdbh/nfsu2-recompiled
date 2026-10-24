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
 * device and the launcher options; never game files or saves. Nothing is uploaded automatically.
 * GitHub cannot take an attachment through the new-issue link, so the issue body carries a compact
 * technical summary of the game log, and the full ZIP is saved to Download/NFSU2 for the player to
 * attach.
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
                    return context.getString(reason == ApplicationExitInfo.REASON_ANR ? R.string.crash_reason_anr : R.string.crash_reason_unexpected)
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
        return context.getString(R.string.app_name) + " " + ReleaseUpdates.installed(context)
                + "\n" + context.getString(R.string.diagnostics_summary_apk_abi, Process.is64Bit() ? "arm64-v8a" : "armeabi-v7a")
                + "\n" + context.getString(R.string.diagnostics_summary_phone, Build.MANUFACTURER, Build.MODEL)
                + "\n" + context.getString(R.string.diagnostics_summary_android, Build.VERSION.RELEASE, Build.VERSION.SDK_INT)
                + "\n" + context.getString(R.string.diagnostics_summary_soc, Build.VERSION.SDK_INT >= 31 ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL : Build.HARDWARE)
                + "\n" + context.getString(R.string.diagnostics_summary_vulkan_driver, driver == null ? context.getString(R.string.system_driver) : driver.name) + "\n";
    }

    static File createReport(Context context) throws Exception {
        File directory = new File(context.getCacheDir(), "reports");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException(context.getString(R.string.diagnostics_create_report_failed));
        File[] old = directory.listFiles();
        if (old != null) for (File file : old)
            if (file.isFile() && file.lastModified() < System.currentTimeMillis() - TimeUnit.DAYS.toMillis(7)) file.delete();
        String date = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        File report = new File(directory, "NFSU2-diagnostico-" + date + ".zip");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(report))) {
            StringBuilder info = new StringBuilder(summary(context));
            info.append(context.getString(R.string.diagnostics_date, new Date())).append("\n")
                    .append(context.getString(R.string.diagnostics_abi, Arrays.toString(Build.SUPPORTED_ABIS)))
                    .append("\n\n").append(context.getString(R.string.diagnostics_options)).append("\n");
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

    /** The last game lines (runtime, DXVK, crashes) of this app's log, for the issue body. */
    static String logTail(int lines) {
        try { return new String(logcat(lines, "NFSU2:I", "DXVK:W", "AndroidRuntime:E", "DEBUG:F", "libc:F", "*:S"), StandardCharsets.UTF_8); }
        catch (IOException error) { return error.toString(); }
    }

    // Lines that explain a stop, a stall or a missing piece of the Windows layer.
    private static final java.util.regex.Pattern KEY_LINE = java.util.regex.Pattern.compile(
            "stopped|boundary|pendiente|unresolved|stall|  thread |FATAL|Fatal|SIGSEGV|SIGBUS|abort|DeadSystem|E DXVK|MessageBox"
            // Native crash frames inside our libraries (symbolized later with symbols-vX.Y.Z).
            + "|#[01][0-9] pc [0-9a-f]+ .*libnfsu2");
    // Per-frame or per-file chatter that crowds out the useful part of the tail.
    private static final java.util.regex.Pattern NOISE = java.util.regex.Pattern.compile(
            "Guest file |touch DIK|Keyboard buffered|D3D9 COM|Guest timer|Guest import|Guest LoadLibrary d3d9|VirtualProtect|presentation");
    // GitHub refuses the new-issue link past roughly 8 KB once it is URL-encoded.
    private static final int URL_BUDGET = 7500;

    /** "20:30:40.892 W text" from a threadtime line, to save room in the link. */
    private static String compact(String line) {
        String[] parts = line.trim().split("\\s+", 6);
        if (parts.length < 6) return line;
        String message = parts[5];
        int colon = message.indexOf(": ");
        return parts[1] + " " + parts[4] + " " + (colon >= 0 ? message.substring(colon + 2) : message);
    }

    static Uri issueUri(Context context, String crash, String zipLocation) {
        List<String> key = new java.util.ArrayList<>(), tail = new java.util.ArrayList<>();
        String lastFrame = null;
        for (String line : logTail(0).split("\n")) {
            if (line.contains("presentation")) lastFrame = compact(line);
            if (KEY_LINE.matcher(line).find()) key.add(compact(line));
            else if (line.contains(" NFSU2 ") && !NOISE.matcher(line).find()) tail.add(compact(line));
        }
        if (key.size() > 35) key = key.subList(key.size() - 35, key.size());
        if (tail.size() > 60) tail = tail.subList(tail.size() - 60, tail.size());
        String title = "[Android] " + Build.MODEL + (crash == null ? "" : ": " + context.getString(R.string.issue_unexpected_close));
        while (true) {
            String body = context.getString(R.string.issue_what_happened) + "\n" + (crash == null ? "" : crash + "\n")
                    + "\n" + context.getString(R.string.issue_steps_to_reproduce) + "\n\n"
                    + context.getString(R.string.issue_device) + "\n" + summary(context)
                    + "\n" + context.getString(R.string.issue_full_log, zipLocation) + "\n"
                    + "\n" + context.getString(R.string.issue_technical_summary) + "\n```\n" + String.join("\n", key)
                    + (lastFrame == null ? "" : "\n" + context.getString(R.string.issue_last_frame, lastFrame))
                    + "\n" + context.getString(R.string.issue_last_game_lines) + "\n" + String.join("\n", tail) + "\n```\n";
            Uri uri = Uri.parse(ISSUES).buildUpon().appendQueryParameter("title", title).appendQueryParameter("body", body).build();
            if (uri.toString().length() <= URL_BUDGET || (tail.isEmpty() && key.size() <= 1)) return uri;
            if (!tail.isEmpty()) tail = tail.subList(Math.min(tail.size(), 5), tail.size());
            else key = key.subList(1, key.size());
        }
    }

    /** Copies the report to Download/NFSU2 (MediaStore: an app needs no permission for its own files). */
    static String saveToDownloads(Context context, File report) throws IOException {
        android.content.ContentValues values = new android.content.ContentValues();
        values.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, report.getName());
        values.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "application/zip");
        values.put(android.provider.MediaStore.MediaColumns.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/NFSU2");
        android.content.ContentResolver resolver = context.getContentResolver();
        Uri target = resolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (target == null) throw new IOException(context.getString(R.string.diagnostics_create_download_failed));
        try (InputStream input = new java.io.FileInputStream(report);
             java.io.OutputStream output = resolver.openOutputStream(target, "w")) {
            if (output == null) throw new IOException(context.getString(R.string.diagnostics_write_download_failed));
            byte[] buffer = new byte[65536];
            for (int n; (n = input.read(buffer)) != -1;) output.write(buffer, 0, n);
        } catch (IOException error) {
            resolver.delete(target, null, null);
            throw error;
        }
        return context.getString(R.string.diagnostics_download_location, report.getName());
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
    private static byte[] logcat(int lines, String... filters) throws IOException {
        java.lang.Process process = null;
        try {
            List<String> command = new java.util.ArrayList<>(Arrays.asList("logcat", "-d", "-b", "main", "-b", "crash",
                    "-v", "threadtime", "--uid=" + Process.myUid()));
            if (lines > 0) { command.add("-t"); command.add(String.valueOf(lines)); }
            command.addAll(Arrays.asList(filters));
            // Android 11's logcat has no --uid (the report came back empty); an app only reads
            // its own uid's log there anyway.
            if (Build.VERSION.SDK_INT < 31) command.remove("--uid=" + Process.myUid());
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
