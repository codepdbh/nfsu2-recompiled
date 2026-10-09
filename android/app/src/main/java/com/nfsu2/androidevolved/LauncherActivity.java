package com.nfsu2.androidevolved;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Launcher in the style of the MW and Carbon ports: game files and JUGAR on the left, options on the
 * right (Vulkan driver, resolution, frame cap, widescreen, compatibility).
 */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_DRIVER_ZIP = 45, REQUEST_DRIVER_PROBE = 46, REQUEST_SAVE_REPORT = 47;
    // NFSU2 green.
    private static final int ACCENT = 0xFF8EE03A;
    /** {label, guest language name, LANGUAGES/<file>.bin}. */
    private static final String[][] LANGUAGES = {
        {"Español", "Spanish", "Spanish"}, {"English", "English UK", "English"},
        {"Français", "French", "French"}, {"Deutsch", "German", "German"},
        {"Italiano", "Italian", "Italian"}, {"Nederlands", "Dutch", "Dutch"},
        {"Svenska", "Swedish", "Swedish"}, {"Dansk", "Danish", "Danish"},
        {"日本語", "Japanese", "Japanese"}, {"한국어", "Korean", "Korean"},
        {"繁體中文", "Chinese (Traditional)", "Chinese"}, {"ไทย", "Thai", "Thailand"}
    };

    private LinearLayout checksList, optionsList;
    private TextView status;
    private Button play, access;
    private boolean playAfterProbe, checkingUpdates, resumed;
    private File pendingReport;
    private String pendingCrash;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        setContentView(buildScreen());
        checkUpdates(false);
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        String crash = Diagnostics.unreportedCrash(this);
        if (crash != null) {
            Diagnostics.acknowledgeCrashes(this);
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("El juego se cerró").setMessage(crash + ".\n\n¿Quieres enviar el registro completo a GitHub para que se pueda corregir?")
                    .setPositiveButton("Reportar en GitHub", (dialog, which) -> prepareReport(crash))
                    .setNegativeButton("Ahora no", null).show();
        }
    }

    @Override protected void onPause() {
        resumed = false;
        super.onPause();
    }

    // ---- Screen ------------------------------------------------------------------------------------------

    private View buildScreen() {
        FrameLayout screen = new FrameLayout(this);
        screen.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[] {0xFF060D08, 0xFF0B2210, 0xFF060D08}));
        LinearLayout columns = new LinearLayout(this);
        columns.setPadding(dp(24), dp(10), dp(24), dp(10));
        screen.addView(columns, new FrameLayout.LayoutParams(-1, -1));

        // Left: logo, game files and JUGAR.
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setGravity(Gravity.CENTER_HORIZONTAL);
        ScrollView leftScroll = new ScrollView(this);
        leftScroll.setFillViewport(true);
        leftScroll.addView(left);
        columns.addView(leftScroll, new LinearLayout.LayoutParams(0, -1, 1.1f));

        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.icono);
        logo.setAdjustViewBounds(true);
        header.addView(logo, new LinearLayout.LayoutParams(dp(76), dp(76)));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        titles.setPadding(dp(14), 0, 0, 0);
        titles.addView(label("Need for Speed Underground 2", 18, Color.WHITE, true));
        titles.addView(label("PC · recompilado de forma nativa para Android", 12, 0x99FFFFFF, false));
        header.addView(titles);
        left.addView(header, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout card = card();
        LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(-1, -2);
        cardParams.topMargin = dp(8);
        left.addView(card, cardParams);
        card.addView(label("ARCHIVOS DEL JUEGO", 12, ACCENT, true));
        status = label("", 13, 0xDDFFFFFF, false);
        status.setPadding(0, dp(4), 0, dp(6));
        card.addView(status);
        checksList = new LinearLayout(this);
        checksList.setOrientation(LinearLayout.VERTICAL);
        card.addView(checksList);

        play = actionButton("JUGAR", true);
        play.setOnClickListener(view -> play());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(-1, dp(54));
        playParams.topMargin = dp(8);
        left.addView(play, playParams);

        access = actionButton("Permitir acceso a memoria interna/nfsu2", false);
        access.setOnClickListener(view -> requestAccess());
        LinearLayout.LayoutParams accessParams = new LinearLayout.LayoutParams(-1, dp(46));
        accessParams.topMargin = dp(10);
        left.addView(access, accessParams);

        // Right: options.
        LinearLayout right = card();
        LinearLayout.LayoutParams rightParams = new LinearLayout.LayoutParams(0, -1, 1f);
        rightParams.leftMargin = dp(24);
        columns.addView(right, rightParams);
        right.addView(label("OPCIONES", 12, ACCENT, true));
        TextView note = label("Se aplican al pulsar JUGAR.", 12, 0x88FFFFFF, false);
        note.setPadding(0, dp(2), 0, dp(4));
        right.addView(note);
        ScrollView scroll = new ScrollView(this);
        optionsList = new LinearLayout(this);
        optionsList.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(optionsList);
        right.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));
        return screen;
    }

    private void refresh() {
        boolean allowed = allowed();
        File root = gameRoot();
        checksList.removeAllViews();
        boolean exe = allowed && new File(root, "SPEED2.EXE").isFile();
        addCheck("SPEED2.EXE", exe, true);
        StringBuilder missing = new StringBuilder();
        for (String folder : new String[] {"GLOBAL", "TRACKS", "CARS", "FRONTEND", "LANGUAGES"})
            if (!(allowed && new File(root, folder).isDirectory())) missing.append(missing.length() == 0 ? "" : ", ").append(folder);
        addCheck(missing.length() == 0 ? "Carpetas del juego" : "Faltan carpetas: " + missing, missing.length() == 0, true);
        addCheck("HUD panorámico (scripts/…WidescreenFix.dat)",
                allowed && new File(root, "scripts/NFSUnderground2.WidescreenFix.dat").isFile(), false);
        boolean ready = exe && !languages().isEmpty();
        status.setText(!allowed ? "Permite el acceso a archivos para leer memoria interna/nfsu2."
                : ready ? "Memoria interna/nfsu2" : "Copia los archivos de tu juego en memoria interna/nfsu2.");
        play.setEnabled(ready);
        play.setAlpha(ready ? 1f : .45f);
        access.setVisibility(allowed ? View.GONE : View.VISIBLE);
        refreshOptions();
    }

    private void refreshOptions() {
        optionsList.removeAllViews();
        GpuDrivers.Driver driver = GpuDrivers.selected(this);
        // Custom drivers (libadrenotools) exist only in the 64-bit build.
        if (android.os.Process.is64Bit())
            optionsList.addView(optionRow("Driver Vulkan", driver == null ? "Del sistema" : driver.name, this::chooseDriver));
        optionsList.addView(optionRow("Probar driver", "GPU y funciones Vulkan", () -> probeDriver(false)));
        optionsList.addView(optionRow("Idioma", languageLabel(), this::chooseLanguage));
        for (GameOptions.Option option : GameOptions.ALL)
            optionsList.addView(optionRow(option.title, option.label(GameOptions.get(this, option.key)), () -> chooseOption(option)));
        optionsList.addView(optionRow("Buscar actualizaciones",
                checkingUpdates ? "Comprobando GitHub…" : "Versión " + ReleaseUpdates.installed(this), () -> checkUpdates(true)));
        optionsList.addView(optionRow("Reportar un problema", "Log completo a GitHub", () -> prepareReport(null)));
    }

    // ---- Options ---------------------------------------------------------------------------------------

    private List<String[]> languages() {
        List<String[]> found = new ArrayList<>();
        File root = gameRoot();
        for (String[] language : LANGUAGES)
            if (new File(root, "LANGUAGES/" + language[2] + ".bin").isFile()) found.add(language);
        return found;
    }

    private String languageLabel() {
        String current = GameOptions.language(this);
        for (String[] language : LANGUAGES) if (language[1].equals(current)) return language[0];
        return current;
    }

    private void chooseLanguage() {
        List<String[]> found = allowed() ? languages() : new ArrayList<>();
        if (found.isEmpty()) { message("Idioma", "No se encontraron idiomas en memoria interna/nfsu2/LANGUAGES."); return; }
        String[] names = new String[found.size()];
        int checked = 0;
        for (int i = 0; i < names.length; i++) {
            names[i] = found.get(i)[0];
            if (found.get(i)[1].equals(GameOptions.language(this))) checked = i;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Idioma")
                .setSingleChoiceItems(names, checked, (dialog, which) -> {
                    GameOptions.prefs(this).edit().putString(GameOptions.LANGUAGE, found.get(which)[1]).apply();
                    dialog.dismiss(); refreshOptions();
                }).setNegativeButton("Cancelar", null).show();
    }

    private void chooseOption(GameOptions.Option option) {
        String current = GameOptions.get(this, option.key);
        int checked = 0;
        for (int i = 0; i < option.values.length; i++) if (option.values[i].equals(current)) checked = i;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle(option.title)
                .setSingleChoiceItems(option.labels, checked, (dialog, which) -> {
                    GameOptions.set(this, option.key, option.values[which]);
                    dialog.dismiss(); refreshOptions();
                }).setNegativeButton("Cancelar", null).show();
    }

    private void chooseDriver() {
        List<GpuDrivers.Driver> drivers = GpuDrivers.list(this);
        String[] names = new String[drivers.size() + 1];
        names[0] = "Driver del sistema";
        int selected = 0;
        for (int i = 0; i < drivers.size(); ++i) {
            names[i + 1] = drivers.get(i).name;
            if (drivers.get(i).id.equals(GpuDrivers.selectedId(this))) selected = i + 1;
        }
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Driver Vulkan")
                .setSingleChoiceItems(names, selected, (dialog, which) -> {
                    GpuDrivers.select(this, which == 0 ? "" : drivers.get(which - 1).id);
                    refreshOptions(); dialog.dismiss();
                })
                .setPositiveButton("Importar ZIP", (dialog, which) -> startActivityForResult(
                        new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),
                        REQUEST_DRIVER_ZIP))
                .setNeutralButton("Eliminar seleccionado", (dialog, which) -> {
                    GpuDrivers.Driver driver = GpuDrivers.selected(this);
                    if (driver == null) return;
                    try { GpuDrivers.remove(this, driver); refreshOptions(); }
                    catch (java.io.IOException error) { message("Driver Vulkan", error.getMessage()); }
                })
                .setNegativeButton("Volver", null).show();
    }

    private void probeDriver(boolean thenPlay) {
        playAfterProbe = thenPlay;
        play.setEnabled(false);
        startActivityForResult(new Intent(this, GpuProbeActivity.class), REQUEST_DRIVER_PROBE);
    }

    private void showDriverReport(String report) {
        play.setEnabled(true);
        try {
            org.json.JSONObject gpu = new org.json.JSONObject(report);
            boolean failed = gpu.optBoolean("driverLoadFailed") || gpu.has("probeError");
            if (playAfterProbe && !failed && gpu.optBoolean("compatible")) { playAfterProbe = false; launch(); return; }
            playAfterProbe = false;
            String text = failed ? "No se pudo usar el driver: " + gpu.optString("probeError", "error al iniciar Vulkan.")
                    : "GPU: " + gpu.optString("gpu") + "\nVulkan: " + gpu.optString("vulkan")
                      + "\nDriver: " + gpu.optString("driverName", "—") + " " + gpu.optString("driverInfo", "")
                      + "\nTexturas BC: " + (gpu.optBoolean("bcTextures") ? "en la GPU" : "se convierten en la CPU")
                      + "\n" + (gpu.optBoolean("compatible") ? "Compatible con el juego."
                              : "Faltan funciones: " + gpu.optJSONArray("missing"));
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Prueba del driver Vulkan").setMessage(text).setPositiveButton("Aceptar", null)
                    .setNeutralButton("Usar driver del sistema", (dialog, which) -> { GpuDrivers.select(this, ""); refreshOptions(); })
                    .show();
        } catch (Exception error) { message("Prueba del driver Vulkan", "Diagnóstico no válido: " + error.getMessage()); }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_SAVE_REPORT) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) finishReport(data.getData());
            else pendingReport = null;
            return;
        }
        if (requestCode == REQUEST_DRIVER_PROBE) {
            showDriverReport(resultCode == RESULT_OK && data != null ? data.getStringExtra("gpu_report")
                    : "{\"probeError\":\"La prueba se cerró o fue cancelada.\",\"driverLoadFailed\":true}");
        } else if (requestCode == REQUEST_DRIVER_ZIP && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri zip = data.getData();
            status.setText("Importando driver Vulkan…");
            worker.execute(() -> {
                try {
                    GpuDrivers.Driver driver = GpuDrivers.importZip(this, zip);
                    main.post(() -> {
                        GpuDrivers.select(this, driver.id); refresh();
                        status.setText("Driver importado: " + driver.name + ". Usa Probar driver para comprobarlo.");
                    });
                } catch (Exception error) {
                    main.post(() -> { refresh(); message("Driver Vulkan", "No se pudo importar el driver: " + error.getMessage()); });
                }
            });
        }
    }

    // ---- Updates and reports ---------------------------------------------------------------------------

    private void checkUpdates(boolean manual) {
        if (checkingUpdates) return;
        checkingUpdates = true;
        if (optionsList != null) refreshOptions();
        ReleaseUpdates.check(this, manual, result -> {
            checkingUpdates = false;
            if (isFinishing() || isDestroyed()) return;
            refreshOptions();
            if (result.error != null) { if (manual) message("Actualizaciones", result.error); return; }
            if (!ReleaseVersion.newer(result.tag, ReleaseUpdates.installed(this))) {
                if (manual) message("Actualizaciones", "Tienes la versión " + ReleaseUpdates.installed(this)
                        + ". No hay una versión más reciente en GitHub.");
                return;
            }
            android.content.SharedPreferences updates = ReleaseUpdates.prefs(this);
            if (!manual && result.tag.equals(updates.getString("notified_tag", ""))) return;
            updates.edit().putString("notified_tag", result.tag).apply();
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Nueva versión disponible: " + result.tag)
                    .setMessage("Puedes descargarla desde GitHub o seguir jugando con esta versión. Instala el APK encima "
                            + "del actual para conservar tus partidas y ajustes.")
                    .setPositiveButton("Actualizar", (dialog, which) -> openUrl(Uri.parse(result.url)))
                    .setNegativeButton("Más tarde", null).show();
        });
    }

    /** Builds the report ZIP, lets the player save it, then opens a pre-filled GitHub issue. */
    private void prepareReport(String crash) {
        status.setText("Preparando el registro…");
        worker.execute(() -> {
            try {
                File report = Diagnostics.createReport(this);
                main.post(() -> {
                    refresh();
                    pendingReport = report; pendingCrash = crash;
                    new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle("Reportar en GitHub")
                            .setMessage("1. Guarda el ZIP con el registro completo.\n2. Se abrirá el issue en GitHub ya rellenado: "
                                    + "describe qué pasó y adjunta el ZIP.")
                            .setPositiveButton("Guardar y abrir GitHub", (dialog, which) -> startActivityForResult(
                                    new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                                            .setType("application/zip").putExtra(Intent.EXTRA_TITLE, report.getName()),
                                    REQUEST_SAVE_REPORT))
                            .setNegativeButton("Cancelar", (dialog, which) -> pendingReport = null).show();
                });
            } catch (Exception error) {
                main.post(() -> { refresh(); message("Reportar un problema", "No se pudo preparar el registro: " + error.getMessage()); });
            }
        });
    }

    private void finishReport(Uri destination) {
        File report = pendingReport;
        String crash = pendingCrash;
        pendingReport = null;
        if (report == null || !report.isFile()) { message("Reportar un problema", "El registro ya no está disponible."); return; }
        worker.execute(() -> {
            try (java.io.InputStream input = new java.io.FileInputStream(report);
                 java.io.OutputStream output = getContentResolver().openOutputStream(destination, "w")) {
                if (output == null) throw new java.io.IOException("No se pudo abrir el destino.");
                byte[] buffer = new byte[65536];
                for (int n; (n = input.read(buffer)) != -1;) output.write(buffer, 0, n);
                Uri issue = Diagnostics.issueUri(this, crash, report.getName());
                main.post(() -> openUrl(issue));
            } catch (Exception error) {
                main.post(() -> message("Reportar un problema", "No se pudo guardar el ZIP: " + error.getMessage()));
            }
        });
    }

    private void openUrl(Uri uri) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, uri)); }
        catch (android.content.ActivityNotFoundException error) { message("GitHub", "No hay un navegador para abrir " + uri); }
    }

    // ---- Launch ----------------------------------------------------------------------------------------

    private void play() {
        if (android.os.Process.is64Bit() && GpuDrivers.selected(this) != null) probeDriver(true);
        else launch();
    }

    private void launch() {
        // Each launch uses a fresh guest machine in the dedicated :game process.
        android.app.ActivityManager manager = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        List<android.app.ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
        if (processes != null) for (android.app.ActivityManager.RunningAppProcessInfo process : processes)
            if (process.uid == android.os.Process.myUid() && process.processName.equals(getPackageName() + ":game"))
                android.os.Process.killProcess(process.pid);
        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int[] size = GameOptions.renderSize(this, Math.max(metrics.widthPixels, metrics.heightPixels),
                Math.min(metrics.widthPixels, metrics.heightPixels));
        String[] loader = GpuDrivers.loaderArguments(this);
        startActivity(new Intent(this, GameActivity.class)
                .putExtra("language", GameOptions.language(this))
                .putExtra("renderWidth", size[0]).putExtra("renderHeight", size[1])
                .putExtra("frameCap", Integer.parseInt(GameOptions.get(this, GameOptions.FPS)))
                .putExtra("widescreen", GameOptions.get(this, GameOptions.WIDESCREEN).equals("on"))
                .putExtra("minimapTop", GameOptions.get(this, GameOptions.MINIMAP).equals("top"))
                .putExtra("backBuffers", Integer.parseInt(GameOptions.get(this, GameOptions.LATENCY)))
                .putExtra("cpuTextures", GameOptions.get(this, GameOptions.TEXTURES).equals("cpu"))
                .putExtra("driverLoader", loader));
    }

    // ---- Storage ---------------------------------------------------------------------------------------

    private static File gameRoot() { return new File(Environment.getExternalStorageDirectory(), "nfsu2"); }

    private boolean allowed() {
        return Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
            : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30)
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        else requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
    }

    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        refresh();
    }

    @Override protected void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }

    // ---- Widgets ---------------------------------------------------------------------------------------

    private void addCheck(String name, boolean ok, boolean required) {
        TextView row = label((ok ? "✓  " : required ? "✗  " : "!  ") + name + (ok || required ? "" : " · opcional"), 13,
                ok ? 0xFF7CD992 : required ? 0xFFFF6B6B : 0xFFFFC857, false);
        row.setPadding(0, dp(2), 0, dp(2));
        checksList.addView(row);
    }

    private View optionRow(String title, String value, Runnable onClick) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(12), dp(8), dp(12), dp(8));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0x168EE03A);
        bg.setCornerRadius(dp(10));
        row.setBackground(bg);
        row.setClickable(true);
        row.setOnClickListener(v -> onClick.run());
        row.addView(label(title, 14, Color.WHITE, false), new LinearLayout.LayoutParams(0, -2, 1f));
        TextView current = label(value + "  ›", 13, ACCENT, true);
        current.setGravity(Gravity.END);
        row.addView(current, new LinearLayout.LayoutParams(0, -2, 1f));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(5);
        row.setLayoutParams(lp);
        return row;
    }

    private LinearLayout card() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(12), dp(16), dp(12));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xCC0E1A12);
        bg.setCornerRadius(dp(16));
        bg.setStroke(dp(1), 0x448EE03A);
        card.setBackground(bg);
        return card;
    }

    private Button actionButton(String text, boolean primary) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(primary ? 0xFF0A1C05 : Color.WHITE);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, primary ? 22 : 15);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setLetterSpacing(primary ? .12f : 0f);
        GradientDrawable bg = primary
                ? new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] {0xFFB8F25A, 0xFF3FB82A})
                : new GradientDrawable();
        if (!primary) { bg.setColor(0x22FFFFFF); bg.setStroke(dp(1), 0x55FFFFFF); }
        bg.setCornerRadius(dp(14));
        b.setBackground(bg);
        b.setStateListAnimator(null);
        return b;
    }

    private TextView label(String text, int sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) { t.setTypeface(Typeface.DEFAULT_BOLD); t.setLetterSpacing(.06f); }
        return t;
    }

    private void message(String title, String text) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(title).setMessage(text).setPositiveButton("Aceptar", null).show();
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
