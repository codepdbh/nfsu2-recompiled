package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Launcher options. GameActivity reads them when the game process starts and hands them to the native
 * runtime before the guest boots; changing one takes effect on the next PLAY.
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
            TEXTURES = "textures", MINIMAP = "minimap", LANGUAGE = "language";

    static final Option[] ALL = {
            new Option(RESOLUTION, "Внутреннее разрешение", "75",
                    new String[] {"100", "75", "50", "720"},
                    new String[] {"Нативное · максимальное качество", "75% · сбалансированно", "50% · максимальная производительность",
                            "1280×720 · 16:9"}),
            new Option(FPS, "Лимит FPS", "120",
                    new String[] {"30", "60", "90", "120", "0"},
                    new String[] {"30 FPS · экономия батареи", "60 FPS", "90 FPS", "120 FPS", "Без ограничения"}),
            new Option(WIDESCREEN, "Широкий экран", "on",
                    new String[] {"on", "off"},
                    new String[] {"HUD и камера скорректированы", "Оригинальный формат 4:3, растянуто"}),
            new Option(MINIMAP, "Миникарта", "top",
                    new String[] {"top", "bottom"},
                    new String[] {"Слева сверху", "Слева снизу (оригинал)"}),
            new Option(LATENCY, "Синхронизация CPU/GPU", "2",
                    new String[] {"2", "1"},
                    new String[] {"Производительность · CPU и GPU работают параллельно", "Низкая задержка · ниже FPS"}),
            new Option(TEXTURES, "Сжатые текстуры", "auto",
                    new String[] {"auto", "cpu"},
                    new String[] {"Автоматически · GPU при поддержке", "Конвертация на CPU · совместимость"}),
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

    /** The game's language folder name (LANGUAGES/<file>.bin) chosen in the launcher. */
    static String language(Context context) {
        return prefs(context).getString(LANGUAGE, "English UK");
    }

    /** Render size for the selected resolution on a display of width × height (landscape). */
    static int[] renderSize(Context context, int width, int height) {
        String value = get(context, RESOLUTION);
        if (value.equals("720")) return new int[] {1280, 720};
        float scale = Integer.parseInt(value) / 100f;
        return new int[] {Math.round(width * scale / 2f) * 2, Math.round(height * scale / 2f) * 2};
    }
}
