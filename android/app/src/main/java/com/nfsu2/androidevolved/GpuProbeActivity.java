package com.nfsu2.androidevolved;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.widget.TextView;

/**
 * Fresh process for each driver test (from the MW port): adrenotools' loader hooks cannot be switched
 * in a running process, and a broken driver must not take the launcher down with it.
 */
public final class GpuProbeActivity extends Activity {
    static { System.loadLibrary("nfsu2_diagnostics"); }
    private static native String nativeGpuReport(String hooks, String temp, String directory, String library);
    private boolean finished;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView status = new TextView(this);
        status.setText("Comprobando el driver Vulkan…");
        status.setTextColor(0xFFFFFFFF); status.setTextSize(18);
        status.setGravity(android.view.Gravity.CENTER); status.setBackgroundColor(0xFF0B0F14);
        setContentView(status);
        Handler main = new Handler(Looper.getMainLooper());
        main.postDelayed(() -> finishReport("{\"probeError\":\"La prueba del driver excedió 20 segundos\",\"driverLoadFailed\":true}"), 20000);
        String[] loader = GpuDrivers.loaderArguments(this);
        new Thread(() -> {
            String result;
            try { result = nativeGpuReport(loader[0], loader[1], loader[2], loader[3]); }
            catch (Exception | LinkageError error) {
                result = "{\"probeError\":\"No se pudo ejecutar la prueba: " + error.getClass().getSimpleName() + "\",\"driverLoadFailed\":true}";
            }
            String report = result;
            main.post(() -> finishReport(report));
        }, "VulkanDriverProbe").start();
    }

    private void finishReport(String report) {
        if (finished) return;
        finished = true;
        setResult(RESULT_OK, new Intent().putExtra("gpu_report", report));
        finish();
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        // Vulkan handles and injected namespace hooks are owned by this disposable process.
        Process.killProcess(Process.myPid());
    }
}
