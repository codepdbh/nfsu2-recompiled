package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.os.Build;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Which Vulkan driver suits this phone's GPU, for the launcher's driver dialog. Imported drivers are
 * GPU-specific: Turnip (Mesa) only runs on Adreno, PanVK (Mesa) only on Mali, and there is no
 * replacement driver for PowerVR or Samsung Xclipse. Reports showed both mismatches and players on
 * MediaTek and Unisoc phones who did not know which driver to try.
 */
final class GpuAdvice {
    private GpuAdvice() {}

    /** GL_RENDERER, e.g. "Mali-G57 MC2", "Adreno (TM) 619", "PowerVR B-Series BXM-8-256"; cached per system build. */
    static String renderer(Context context) {
        SharedPreferences prefs = GameOptions.prefs(context);
        String key = "gpu_renderer_" + Integer.toHexString(Build.FINGERPRINT.hashCode());
        String cached = prefs.getString(key, null);
        if (cached != null) return cached;
        String name = "";
        try {
            EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            int[] version = new int[2];
            // No eglTerminate: the display is shared with the UI's renderer in this process.
            if (display != EGL14.EGL_NO_DISPLAY && EGL14.eglInitialize(display, version, 0, version, 1)) {
                int[] attributes = {EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                        EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT, EGL14.EGL_NONE};
                EGLConfig[] configs = new EGLConfig[1];
                int[] count = new int[1];
                if (EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) && count[0] > 0) {
                    EGLContext gl = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                            new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
                    EGLSurface surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                            new int[] {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
                    EGLContext previousContext = EGL14.eglGetCurrentContext();
                    EGLSurface previousDraw = EGL14.eglGetCurrentSurface(EGL14.EGL_DRAW);
                    EGLSurface previousRead = EGL14.eglGetCurrentSurface(EGL14.EGL_READ);
                    if (gl != EGL14.EGL_NO_CONTEXT && surface != EGL14.EGL_NO_SURFACE
                            && EGL14.eglMakeCurrent(display, surface, surface, gl)) {
                        String value = GLES20.glGetString(GLES20.GL_RENDERER);
                        if (value != null) name = value.trim();
                        EGL14.eglMakeCurrent(display, previousDraw, previousRead, previousContext);
                    }
                    if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
                    if (gl != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, gl);
                }
            }
        } catch (RuntimeException ignored) {
            // No GPU name: the advice falls back to the system driver.
        }
        if (!name.isEmpty()) prefs.edit().putString(key, name).apply();
        return name;
    }

    enum Family { ADRENO_TURNIP, ADRENO_SYSTEM, MALI_VALHALL, MALI_OLDER, POWERVR, XCLIPSE, OTHER }

    static Family family(String renderer) {
        String gpu = renderer.toLowerCase(Locale.ROOT);
        Matcher adreno = Pattern.compile("adreno[^0-9]*([0-9]{3})").matcher(gpu);
        if (adreno.find()) {
            int model = Integer.parseInt(adreno.group(1));
            return model >= 610 && model < 800 ? Family.ADRENO_TURNIP : Family.ADRENO_SYSTEM;
        }
        Matcher mali = Pattern.compile("mali-g([0-9]+)").matcher(gpu);
        if (mali.find()) {
            int model = Integer.parseInt(mali.group(1));
            // Valhall and newer: G57, G68, G77, G78 and every G3xx-G9xx. Bifrost: G31-G52, G71, G72, G76.
            boolean valhall = model == 57 || model == 68 || model == 77 || model == 78 || model >= 300;
            return valhall ? Family.MALI_VALHALL : Family.MALI_OLDER;
        }
        if (gpu.contains("mali")) return Family.MALI_OLDER;
        if (gpu.contains("powervr") || gpu.contains("img ") || gpu.contains("bxm") || gpu.contains("ge83")) return Family.POWERVR;
        if (gpu.contains("xclipse")) return Family.XCLIPSE;
        return Family.OTHER;
    }

    /** One sentence in Spanish (the launchers translate it): which driver to use on this GPU. */
    static String advice(Family family) {
        switch (family) {
            case ADRENO_TURNIP:
                return "Recomendado para tu Snapdragon: Turnip (Mesa) para Adreno 6xx/7xx si el driver del sistema va lento o con errores gráficos. PanVK no sirve para Adreno.";
            case ADRENO_SYSTEM:
                return "Recomendado: el driver del sistema. Turnip para esta Adreno es experimental.";
            case MALI_VALHALL:
                return "Recomendado para tu GPU Mali: PanVK (Mesa) si el driver del sistema va lento o con errores gráficos. Turnip no sirve para Mali.";
            case MALI_OLDER:
                return "Recomendado: el driver del sistema. PanVK en Mali-G52/G72/G76 o más antiguas es experimental, y Turnip no sirve para Mali.";
            case POWERVR:
                return "Recomendado: el driver del sistema. No hay un driver alternativo estable para PowerVR; Turnip y PanVK no funcionan en esta GPU.";
            case XCLIPSE:
                return "Recomendado: el driver del sistema. Turnip y PanVK no funcionan en las GPU Xclipse de Samsung.";
            default:
                return "Recomendado: el driver del sistema.";
        }
    }

    /** True when the imported driver is built for another GPU family (Turnip on Mali, PanVK on Adreno). */
    static boolean mismatched(Family family, String driverName) {
        String name = driverName.toLowerCase(Locale.ROOT);
        boolean turnip = name.contains("turnip") || name.contains("freedreno") || name.contains("adreno");
        boolean panvk = name.contains("panvk") || name.contains("panfrost") || name.contains("mali");
        boolean adreno = family == Family.ADRENO_TURNIP || family == Family.ADRENO_SYSTEM;
        boolean maliGpu = family == Family.MALI_VALHALL || family == Family.MALI_OLDER;
        if (family == Family.OTHER) return false;
        return (turnip && !adreno) || (panvk && !maliGpu);
    }
}
