package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Launcher options. GameActivity reads them when the game process starts and hands them to the native
 * runtime before the guest boots; changing one takes effect on the next JUGAR.
 */
final class GameOptions {
    static final class Option {
        final String key, title, titleEn, defaultValue;
        final String[] values, labels, labelsEn;
        Option(String key, String title, String titleEn, String defaultValue, String[] values, String[] labels, String[] labelsEn) {
            this.key = key; this.title = title; this.titleEn = titleEn; this.defaultValue = defaultValue;
            this.values = values; this.labels = labels; this.labelsEn = labelsEn;
        }
        String title(Context context) { return english(context) ? titleEn : title; }
        String[] labels(Context context) { return english(context) ? labelsEn : labels; }
        String label(Context context, String value) {
            for (int i = 0; i < values.length; i++) if (values[i].equals(value)) return labels(context)[i];
            return value;
        }
    }

    static final String RESOLUTION = "resolution", FPS = "fps", WIDESCREEN = "widescreen", LATENCY = "latency",
            TEXTURES = "textures", MINIMAP = "minimap", LANGUAGE = "language";

    static final Option[] ALL = {
            new Option(RESOLUTION, "Resolución interna", "Internal resolution", "75",
                    new String[] {"100", "75", "50", "720"},
                    new String[] {"Nativa · máxima calidad", "75% · equilibrada", "50% · máximo rendimiento",
                            "1280×720 · 16:9"},
                    new String[] {"Native · best quality", "75% · balanced", "50% · best performance",
                            "1280×720 · 16:9"}),
            new Option(FPS, "Límite de FPS", "FPS limit", "120",
                    new String[] {"30", "60", "90", "120", "0"},
                    new String[] {"30 FPS · ahorra batería", "60 FPS", "90 FPS", "120 FPS", "Sin límite"},
                    new String[] {"30 FPS · saves battery", "60 FPS", "90 FPS", "120 FPS", "Unlimited"}),
            new Option(WIDESCREEN, "Pantalla panorámica", "Widescreen", "on",
                    new String[] {"on", "off"},
                    new String[] {"HUD y cámara corregidos", "Original 4:3 estirado"},
                    new String[] {"Fixed HUD and camera", "Original 4:3 stretched"}),
            new Option(MINIMAP, "Minimapa", "Minimap", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Arriba a la izquierda", "Abajo a la izquierda (original)"},
                    new String[] {"Top left", "Bottom left (original)"}),
            new Option(LATENCY, "Sincronía CPU/GPU", "CPU/GPU sync", "2",
                    new String[] {"2", "1"},
                    new String[] {"Rendimiento · CPU y GPU en paralelo", "Baja latencia · menos FPS"},
                    new String[] {"Performance · CPU and GPU in parallel", "Low latency · fewer FPS"}),
            new Option(TEXTURES, "Texturas comprimidas", "Compressed textures", "auto",
                    new String[] {"auto", "cpu"},
                    new String[] {"Automático · GPU si las soporta", "Convertir en CPU · compatibilidad"},
                    new String[] {"Automatic · GPU if supported", "Convert on CPU · compatibility"}),
    };

    private static final String PREFS = "nfsu2_game";

    private GameOptions() {}

    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static Option find(String key) {
        for (Option o : ALL) if (o.key.equals(key)) return o;
        throw new IllegalArgumentException(key);
    }

    static String get(Context context, String key) {
        Option o = find(key);
        String value = prefs(context).getString(key, o.defaultValue);
        for (String allowed : o.values) if (allowed.equals(value)) return value;
        return o.defaultValue;
    }

    static void set(Context context, String key, String value) {
        prefs(context).edit().putString(key, value).apply();
    }

    static final String ONLINE_SERVER = "online_server", DEFAULT_ONLINE_SERVER = "eahub.eu";

    /** EA Nation replacement server: the game's *.ea.com hosts resolve to this name or IPv4 address. */
    static String onlineServer(Context context) {
        String value = prefs(context).getString(ONLINE_SERVER, DEFAULT_ONLINE_SERVER);
        return validServer(value) ? value : DEFAULT_ONLINE_SERVER;
    }

    static boolean validServer(String value) {
        return value != null && value.length() <= 253 && value.matches("[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*");
    }

    static final String UI_LANGUAGE = "ui_language";

    /** Launcher text language: "es" or "en"; defaults to Spanish on Spanish devices, English elsewhere. */
    static boolean english(Context context) {
        String fallback = java.util.Locale.getDefault().getLanguage().equals("es") ? "es" : "en";
        return prefs(context).getString(UI_LANGUAGE, fallback).equals("en");
    }

    /** Spanish or English text, following the launcher language. */
    static String tr(Context context, String es, String en) { return english(context) ? en : es; }

    /** The game's language folder name (LANGUAGES/<file>.bin) chosen in the launcher. */
    static String language(Context context) {
        return prefs(context).getString(LANGUAGE, "Spanish");
    }

    /** Render size for the selected resolution on a display of width × height (landscape). */
    static int[] renderSize(Context context, int width, int height) {
        String value = get(context, RESOLUTION);
        if (value.equals("720")) return new int[] {1280, 720};
        float scale = Integer.parseInt(value) / 100f;
        return new int[] {Math.round(width * scale / 2f) * 2, Math.round(height * scale / 2f) * 2};
    }
}
