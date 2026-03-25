package com.ekernel.manager;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.InputStreamReader;

public class MainActivity extends Activity {

    private WebView webView;
    private static final String ASSET_PREFIX = "file:///android_asset/";
    private static final String CONFIG_DIR = "/data/ekernel/config";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        webView = new WebView(this);
        setContentView(webView);

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);

        webView.setWebViewClient(new WebViewClient() {
            private boolean shouldBlockUrl(String url) {
                return url == null ||
                        (!url.startsWith(ASSET_PREFIX) && !"about:blank".equals(url));
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return shouldBlockUrl(url);
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                String url = request != null && request.getUrl() != null
                        ? request.getUrl().toString() : null;
                return shouldBlockUrl(url);
            }
        });
        webView.addJavascriptInterface(new EKernelBridge(), "EKernelBridge");
        webView.loadUrl(ASSET_PREFIX + "index.html");
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    private class EKernelBridge {

        private boolean isSafeValue(String value) {
            return value != null && value.indexOf('\0') < 0;
        }

        private String normalizeNodePath(String path) {
            if (path == null || path.indexOf('\0') >= 0 || path.indexOf('\n') >= 0
                    || path.indexOf('\r') >= 0 || path.contains("..")) {
                return null;
            }

            if (path.startsWith("/proc/") || path.startsWith("/sys/")
                    || path.startsWith(CONFIG_DIR + "/")) {
                return path;
            }

            return null;
        }

        private String normalizeConfigKey(String key) {
            if (key == null || key.isEmpty()) {
                return null;
            }

            for (int i = 0; i < key.length(); i++) {
                char ch = key.charAt(i);
                if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z')
                        && (ch < '0' || ch > '9') && ch != '_' && ch != '-') {
                    return null;
                }
            }

            return key;
        }

        private String shellQuote(String value) {
            return "'" + value.replace("'", "'\"'\"'") + "'";
        }

        private String runSuForOutput(String command) throws Exception {
            Process process = Runtime.getRuntime().exec("su");
            DataOutputStream os = new DataOutputStream(process.getOutputStream());
            os.writeBytes(command);
            os.writeBytes("\nexit\n");
            os.flush();

            BufferedReader reader = new BufferedReader(
                    new InputStreamReader(process.getInputStream()));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line).append("\n");
            }
            reader.close();

            process.waitFor();
            return sb.toString().trim();
        }

        private boolean runSuCommand(String command) throws Exception {
            Process process = Runtime.getRuntime().exec("su");
            DataOutputStream os = new DataOutputStream(process.getOutputStream());
            os.writeBytes(command);
            os.writeBytes("\nexit\n");
            os.flush();
            process.waitFor();
            return process.exitValue() == 0;
        }

        private boolean writeGlob(String pattern, String value) throws Exception {
            String quotedValue = shellQuote(value);
            String command = "ok=1\n" +
                    "matched=0\n" +
                    "for node in " + pattern + "; do\n" +
                    "  if [ -f \"$node\" ]; then\n" +
                    "    matched=1\n" +
                    "    printf '%s\\n' " + quotedValue + " > \"$node\" || ok=0\n" +
                    "  fi\n" +
                    "done\n" +
                    "[ \"$matched\" -eq 1 ] && [ \"$ok\" -eq 1 ]";
            return runSuCommand(command);
        }

        @JavascriptInterface
        public String readNode(String path) {
            String safePath = normalizeNodePath(path);
            if (safePath == null) {
                return "error: invalid path";
            }

            try {
                return runSuForOutput("cat -- " + shellQuote(safePath));
            } catch (Exception e) {
                return "error: " + e.getMessage();
            }
        }

        @JavascriptInterface
        public boolean writeNode(String path, String value) {
            String safePath = normalizeNodePath(path);
            if (safePath == null || !isSafeValue(value)) {
                return false;
            }

            try {
                return runSuCommand("printf '%s\\n' " + shellQuote(value)
                        + " > " + shellQuote(safePath));
            } catch (Exception e) {
                return false;
            }
        }

        @JavascriptInterface
        public String readConfig(String key) {
            String safeKey = normalizeConfigKey(key);
            if (safeKey == null) {
                return "";
            }

            return readNode(CONFIG_DIR + "/" + safeKey);
        }

        @JavascriptInterface
        public boolean writeConfig(String key, String value) {
            String safeKey = normalizeConfigKey(key);
            if (safeKey == null || !isSafeValue(value)) {
                return false;
            }

            try {
                return runSuCommand("mkdir -p " + shellQuote(CONFIG_DIR) + "\n"
                        + "printf '%s\\n' " + shellQuote(value) + " > "
                        + shellQuote(CONFIG_DIR + "/" + safeKey));
            } catch (Exception e) {
                return false;
            }
        }

        @JavascriptInterface
        public boolean applyTuning(String key, String value) {
            String safeKey = normalizeConfigKey(key);
            if (safeKey == null || !isSafeValue(value)) {
                return false;
            }

            try {
                switch (safeKey) {
                    case "cpu_governor":
                        return writeGlob(
                                "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor",
                                value);
                    case "io_scheduler":
                        return writeGlob(
                                "/sys/block/sd*/queue/scheduler /sys/block/mmcblk*/queue/scheduler",
                                value);
                    case "read_ahead_kb":
                        return writeGlob(
                                "/sys/block/sd*/queue/read_ahead_kb /sys/block/mmcblk*/queue/read_ahead_kb",
                                value);
                    case "swappiness":
                        return writeNode("/proc/sys/vm/swappiness", value);
                    case "dirty_ratio":
                        return writeNode("/proc/sys/vm/dirty_ratio", value);
                    case "dirty_background_ratio":
                        return writeNode("/proc/sys/vm/dirty_background_ratio", value);
                    case "vfs_cache_pressure":
                        return writeNode("/proc/sys/vm/vfs_cache_pressure", value);
                    case "tcp_congestion":
                        return writeNode("/proc/sys/net/ipv4/tcp_congestion_control", value);
                    case "thermal_profile":
                        return writeNode("/proc/ekernel/thermal_profile", value);
                    case "store_mode":
                        return writeNode("/sys/class/power_supply/battery/store_mode", value);
                    default:
                        return false;
                }
            } catch (Exception e) {
                return false;
            }
        }

        @JavascriptInterface
        public String getKernelVersion() {
            return readNode("/proc/version");
        }

        @JavascriptInterface
        public String getThermalStats() {
            return readNode("/proc/ekernel/thermal_stats");
        }

        @JavascriptInterface
        public void reboot() {
            try {
                Process p = Runtime.getRuntime().exec("su");
                DataOutputStream os = new DataOutputStream(p.getOutputStream());
                os.writeBytes("reboot\n");
                os.flush();
            } catch (Exception e) {
                // ignore
            }
        }

        @JavascriptInterface
        public void openElbSettings() {
            android.content.Intent intent = new android.content.Intent(MainActivity.this, ElbActivity.class);
            startActivity(intent);
        }
    }
}
