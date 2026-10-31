package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Launcher options. GameActivity reads them when the game process starts and hands them to the native
 * runtime before the guest boots; changing one takes effect on the next JUGAR.
 */
final class GameOptions {
    static final class Option {
        final String key, title, defaultValue;
        final String[] values, labels;
        Option(String key, String title, String defaultValue, String[] values, String[] labels) {
            this.key = key; this.title = title; this.defaultValue = defaultValue; this.values = values; this.labels = labels;
        }
        String label(String value) {
            for (int i = 0; i < values.length; i++) if (values[i].equals(value)) return labels[i];
            return value;
        }
    }

    static final String RESOLUTION = "resolution", FPS = "fps", WIDESCREEN = "widescreen", LATENCY = "latency",
            TEXTURES = "textures", MINIMAP = "minimap", LANGUAGE = "language", REGION = "region", FPS_OVERLAY = "fps_overlay", TACHOMETER = "tachometer", PAD_LAYOUT = "pad_layout";

    static final Option[] ALL = {
            new Option(RESOLUTION, "Resolución interna", "75",
                    new String[] {"100", "75", "50", "720"},
                    new String[] {"Nativa · máxima calidad", "75% · equilibrada", "50% · máximo rendimiento",
                            "1280×720 · 16:9"}),
            new Option(FPS, "Límite de FPS", "120",
                    new String[] {"30", "60", "90", "120", "0"},
                    new String[] {"30 FPS · ahorra batería", "60 FPS", "90 FPS", "120 FPS", "Sin límite"}),
            new Option(WIDESCREEN, "Pantalla panorámica", "on",
                    new String[] {"on", "off"},
                    new String[] {"HUD y cámara corregidos", "Original 4:3 estirado"}),
            new Option(MINIMAP, "Minimapa", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Arriba a la izquierda", "Abajo a la izquierda (original)"}),
            new Option(TACHOMETER, "Cuentarrevoluciones", "bottom",
                    new String[] {"bottom", "raised", "middle"},
                    new String[] {"Abajo a la derecha (original)", "Un poco más arriba", "A media altura"}),
            new Option(PAD_LAYOUT, "Controles del mando", "port",
                    new String[] {"port", "console"},
                    new String[] {"Los del port", "Como en consola · cruceta: mapa, SMS, estadísticas"}),
            new Option(LATENCY, "Sincronía CPU/GPU", "2",
                    new String[] {"2", "1"},
                    new String[] {"Rendimiento · CPU y GPU en paralelo", "Baja latencia · menos FPS"}),
            new Option(TEXTURES, "Texturas comprimidas", "auto",
                    new String[] {"auto", "cpu"},
                    new String[] {"Automático · GPU si las soporta", "Convertir en CPU · compatibilidad"}),
            new Option(FPS_OVERLAY, "Contador de FPS", "off",
                    new String[] {"off", "fps", "full"},
                    new String[] {"Desactivado", "FPS", "FPS y ms por frame"}),
            // The documented SPEED2.EXE always behaves as the North American release (SKU 0):
            // GLOBALTEXTURESNA.BIN, US ads. Europe (SKU 1, PAL) is what the European discs ship.
            new Option(REGION, "Región del juego", "auto",
                    new String[] {"auto", "eu", "na"},
                    new String[] {"La del ejecutable · Norteamérica", "Europa · PAL (datos europeos)", "Norteamérica"}),
    };

    /** 0 original, 1 raised, 2 mid-screen (native nfs_widescreen_set_tachometer_offset). */
    static int tachometerPosition(Context context) {
        switch (get(context, TACHOMETER)) {
            case "middle": return 2;
            case "raised": return 1;
            default: return 0;
        }
    }

    /** -1 keeps the executable's region, 0 North America, 1 Europe (native setGuestRegion). */
    static int regionCode(Context context) {
        switch (get(context, REGION)) {
            case "eu": return 1;
            case "na": return 0;
            default: return -1;
        }
    }

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
