package com.nfsu2.androidevolved;

import android.content.Context;
import android.net.Uri;
import android.os.Build;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/**
 * User-provided adrenotools ZIPs: Turnip, PanVK or other Android ARM64 Vulkan drivers (from the MW port).
 * The app ships no driver; an imported one is loaded by adrenotools in the game and probe processes only.
 */
final class GpuDrivers {
    static final class Driver {
        final String id, name, library;
        final File directory;
        Driver(String id, String name, File directory, String library) {
            this.id = id; this.name = name; this.directory = directory; this.library = library;
        }
    }
    private static File root(Context context) {
        File root = new File(context.getFilesDir(), "gpu_drivers"); root.mkdirs(); return root;
    }
    static List<Driver> list(Context context) {
        List<Driver> result = new ArrayList<>();
        File[] dirs = root(context).listFiles(File::isDirectory);
        if (dirs != null) for (File dir : dirs) {
            try {
                if (dir.getName().startsWith(".")) continue;
                JSONObject meta = new JSONObject(new String(Files.readAllBytes(new File(dir, "driver.json").toPath()), StandardCharsets.UTF_8));
                String library = meta.getString("libraryName");
                if (!library.matches("[A-Za-z0-9_.+-]+\\.so")) continue;
                File loadDir = new File(dir, meta.getString("folder"));
                if (!loadDir.getCanonicalPath().startsWith(dir.getCanonicalPath() + "/")) continue;
                if (new File(loadDir, library).isFile())
                    result.add(new Driver(dir.getName(), meta.getString("name"), loadDir, library));
            } catch (Exception ignored) { }
        }
        result.sort(Comparator.comparing(d -> d.name.toLowerCase(Locale.ROOT)));
        return result;
    }
    static String selectedId(Context context) {
        return GameOptions.prefs(context).getString("gpu_driver", "");
    }
    static Driver selected(Context context) {
        String id = selectedId(context);
        for (Driver driver : list(context)) if (driver.id.equals(id)) return driver;
        return null;
    }
    static void select(Context context, String id) {
        GameOptions.prefs(context).edit().putString("gpu_driver", id).apply();
    }
    static Driver importZip(Context context, Uri uri) throws Exception {
        File stage = new File(root(context), ".import-" + UUID.randomUUID());
        if (!stage.mkdirs()) throw new IOException("No se pudo crear la carpeta del driver.");
        try {
            InputStream input = context.getContentResolver().openInputStream(uri);
            if (input == null) throw new IOException("No se pudo abrir el ZIP.");
            DriverArchive.extract(input, stage);
            File packageRoot = findMeta(stage);
            if (packageRoot == null) throw new IOException("Falta meta.json en el ZIP del driver.");
            JSONObject meta = new JSONObject(new String(Files.readAllBytes(new File(packageRoot, "meta.json").toPath()), StandardCharsets.UTF_8));
            String library = meta.getString("libraryName");
            if (!library.matches("[A-Za-z0-9_.+-]+\\.so")) throw new IOException("libraryName no es válido.");
            if (meta.optInt("minApi", 26) > Build.VERSION.SDK_INT)
                throw new IOException("Este driver necesita una versión más reciente de Android.");
            DriverArchive.validateLibrary(new File(packageRoot, library));
            String name = meta.optString("name", library);
            if (name.trim().isEmpty() || name.length() > 120) throw new IOException("Nombre de driver no válido.");
            // Keep dependencies alongside the main library, including ZIPs with a wrapper directory.
            String relative = "payload";
            File installed = new File(root(context), UUID.randomUUID().toString());
            if (!installed.mkdirs()) throw new IOException("No se pudo instalar el driver.");
            try {
                if (!packageRoot.renameTo(new File(installed, relative))) throw new IOException("No se pudo instalar el paquete.");
                JSONObject record = new JSONObject().put("name", name).put("libraryName", library).put("folder", relative);
                Files.write(new File(installed, "driver.json").toPath(), record.toString().getBytes(StandardCharsets.UTF_8));
                readOnly(new File(installed, relative));
            } catch (Exception error) { DriverArchive.delete(installed); throw error; }
            return new Driver(installed.getName(), name, new File(installed, relative), library);
        } finally {
            if (stage.exists()) DriverArchive.delete(stage);
        }
    }
    private static File findMeta(File root) throws IOException {
        if (new File(root, "meta.json").isFile()) return root;
        File found = null;
        File[] children = root.listFiles(File::isDirectory);
        if (children != null) for (File child : children) {
            File candidate = findMeta(child);
            if (candidate != null) {
                if (found != null) throw new IOException("Hay varios meta.json; el paquete es ambiguo.");
                found = candidate;
            }
        }
        return found;
    }
    private static void readOnly(File file) throws IOException {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) readOnly(child);
        else if (!file.setReadOnly()) throw new IOException("No se pudo proteger la biblioteca del driver.");
    }
    static void remove(Context context, Driver driver) throws IOException {
        File target = new File(root(context), driver.id);
        if (!target.getCanonicalFile().getParentFile().equals(root(context).getCanonicalFile()))
            throw new IOException("Ruta de driver no válida.");
        DriverArchive.delete(target);
        if (selectedId(context).equals(driver.id)) select(context, "");
    }
    static String errorMessage(Context context, Throwable error) {
        String message = error.getMessage();
        if (message == null) return error.toString();
        if (message.equals("No se pudo crear la carpeta del driver.")) return context.getString(R.string.driver_error_create_folder);
        if (message.equals("No se pudo abrir el ZIP.")) return context.getString(R.string.driver_error_open_zip);
        if (message.equals("Falta meta.json en el ZIP del driver.")) return context.getString(R.string.driver_error_missing_meta);
        if (message.equals("libraryName no es válido.")) return context.getString(R.string.driver_error_invalid_library_name);
        if (message.equals("Este driver necesita una versión más reciente de Android.")) return context.getString(R.string.driver_error_android_too_old);
        if (message.equals("Nombre de driver no válido.")) return context.getString(R.string.driver_error_invalid_name);
        if (message.equals("No se pudo instalar el driver.")) return context.getString(R.string.driver_error_install);
        if (message.equals("No se pudo instalar el paquete.")) return context.getString(R.string.driver_error_install_package);
        if (message.equals("Hay varios meta.json; el paquete es ambiguo.")) return context.getString(R.string.driver_error_multiple_meta);
        if (message.equals("No se pudo proteger la biblioteca del driver.")) return context.getString(R.string.driver_error_protect_library);
        if (message.equals("Ruta de driver no válida.")) return context.getString(R.string.driver_error_invalid_path);
        if (message.equals("El ZIP contiene demasiados archivos.")) return context.getString(R.string.driver_error_zip_too_many_files);
        if (message.equals("El ZIP contiene rutas no válidas o repetidas.")) return context.getString(R.string.driver_error_zip_invalid_paths);
        if (message.equals("No se pudo crear la carpeta.")) return context.getString(R.string.driver_error_create_archive_folder);
        if (message.equals("El paquete de driver es demasiado grande.")) return context.getString(R.string.driver_error_package_too_large);
        if (message.equals("El driver debe ser una biblioteca Android ARM64 (.so), no de PC ni de 32 bits.")) return context.getString(R.string.driver_error_not_arm64);
        if (message.equals("Enlace inesperado en el paquete.")) return context.getString(R.string.driver_error_unexpected_link);
        String deletePrefix = "No se pudo eliminar ";
        if (message.startsWith(deletePrefix)) return context.getString(R.string.driver_error_delete, message.substring(deletePrefix.length()));
        return message;
    }

    /** {hooks dir, temp dir, driver dir, library}; empty driver fields select the system driver. */
    static String[] loaderArguments(Context context) {
        Driver driver = android.os.Process.is64Bit() ? selected(context) : null;
        return new String[] {context.getApplicationInfo().nativeLibraryDir, context.getCacheDir().getAbsolutePath(),
                driver == null ? "" : driver.directory.getAbsolutePath() + "/", driver == null ? "" : driver.library};
    }
}
