package org.eskiu.ui;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.view.View;
import java.nio.charset.StandardCharsets;

// The core's commands: alerts, URLs, the keyboard, sharing, the clipboard, haptics,
// announcements, fetches and the splash.
final class UiCommands {
    private final UiActivity activity;
    UiCommands(UiActivity activity) { this.activity = activity; }

    // The alert on screen and how to answer it (also used by the conformance run).
    AlertDialog shownAlert;
    java.util.function.IntConsumer answerAlert;

    // Commands: 1 an alert (title, message, buttons; up to three, as Android shows them),
    // 2 open a URL.
    void command(int request, int command, String[] strings) {
        UiActivity a = activity;
        if (command == 13) { a.access.setWords(strings); return; }
        if (command == 11 && strings.length >= 1) {
            if (strings[0].equals("1")) a.splashHeld = a.splash != null;
            else a.hideSplash();
            return;
        }
        if (command == 1 && strings.length >= 2) {
            final boolean[] answered = {false};
            java.util.function.IntConsumer answer = index -> {
                if (answered[0]) return;
                answered[0] = true;
                shownAlert = null; answerAlert = null;
                a.nativeAlert(request, index);
            };
            AlertDialog.Builder builder = new AlertDialog.Builder(a).setTitle(strings[0]).setMessage(strings[1]);
            int buttons = Math.min(3, strings.length - 2);
            if (buttons >= 1) builder.setPositiveButton(strings[2 + buttons - 1], (d, w) -> answer.accept(buttons - 1));
            if (buttons >= 2) builder.setNegativeButton(strings[2], (d, w) -> answer.accept(0));
            if (buttons == 3) builder.setNeutralButton(strings[3], (d, w) -> answer.accept(1));
            builder.setOnCancelListener(d -> answer.accept(-1));
            shownAlert = builder.show();
            answerAlert = answer;
        } else if (command == 2 && strings.length >= 1) {
            // ui_open_settings: the app's page in the system settings.
            Intent intent = strings[0].equals("app-settings:")
                ? new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + a.getPackageName()))
                : new Intent(Intent.ACTION_VIEW, Uri.parse(strings[0]));
            try { a.startActivity(intent); }
            catch (Exception error) { /* no app handles it */ }
        } else if (command == 4) {
            View focused = a.getCurrentFocus();
            android.view.inputmethod.InputMethodManager input =
                (android.view.inputmethod.InputMethodManager)a.getSystemService(UiActivity.INPUT_METHOD_SERVICE);
            if (input != null) input.hideSoftInputFromWindow(a.root.getWindowToken(), 0);
            if (focused != null) focused.clearFocus();
        } else if (command == 5 && strings.length >= 2) {
            String text = strings[1].isEmpty() ? strings[0] : (strings[0].isEmpty() ? strings[1] : strings[0] + "\n" + strings[1]);
            Intent send = new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, text);
            try { a.startActivity(Intent.createChooser(send, null)); } catch (Exception error) { /* nothing shares */ }
        } else if (command == 6 && strings.length >= 1) {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager)a.getSystemService(UiActivity.CLIPBOARD_SERVICE);
            if (clipboard != null) clipboard.setPrimaryClip(android.content.ClipData.newPlainText("text", strings[0]));
        } else if (command == 7) {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager)a.getSystemService(UiActivity.CLIPBOARD_SERVICE);
            CharSequence text = null;
            if (clipboard != null && clipboard.getPrimaryClip() != null && clipboard.getPrimaryClip().getItemCount() > 0) {
                text = clipboard.getPrimaryClip().getItemAt(0).coerceToText(a);
            }
            a.nativeAnswer(request, text == null ? "" : text.toString());
        } else if (command == 8 && strings.length >= 1) {
            // Selection, light, medium, heavy, success, warning, error.
            int[] feedback = {android.view.HapticFeedbackConstants.CLOCK_TICK, android.view.HapticFeedbackConstants.VIRTUAL_KEY,
                android.view.HapticFeedbackConstants.KEYBOARD_TAP, android.view.HapticFeedbackConstants.LONG_PRESS,
                Build.VERSION.SDK_INT >= 30 ? android.view.HapticFeedbackConstants.CONFIRM : android.view.HapticFeedbackConstants.VIRTUAL_KEY,
                Build.VERSION.SDK_INT >= 30 ? android.view.HapticFeedbackConstants.REJECT : android.view.HapticFeedbackConstants.LONG_PRESS,
                Build.VERSION.SDK_INT >= 30 ? android.view.HapticFeedbackConstants.REJECT : android.view.HapticFeedbackConstants.LONG_PRESS};
            int kind = Math.max(0, Math.min(6, Integer.parseInt(strings[0])));
            a.root.performHapticFeedback(feedback[kind]);
        } else if (command == 9 && strings.length >= 1) {
            a.root.announceForAccessibility(strings[0]);
        } else if (command == 10 && strings.length >= 6) {
            fetch(request, strings);
        }
    }

    // A fetch through HttpURLConnection on its own thread; the answer comes back on the UI thread.
    private void fetch(int request, String[] strings) {
        new Thread(() -> {
            int status = 0;
            String body;
            StringBuilder headers = new StringBuilder();
            try {
                java.net.HttpURLConnection connection = (java.net.HttpURLConnection)new java.net.URL(strings[0]).openConnection();
                connection.setRequestMethod(strings[1]);
                for (String line : strings[2].split("\n")) {
                    int colon = line.indexOf(':');
                    if (colon > 0) connection.setRequestProperty(line.substring(0, colon).trim(), line.substring(colon + 1).trim());
                }
                int timeout = Integer.parseInt(strings[4]);
                if (timeout > 0) { connection.setConnectTimeout(timeout); connection.setReadTimeout(timeout); }
                if (strings[5].equals("1")) {
                    connection.setDoOutput(true);
                    connection.getOutputStream().write(strings[3].getBytes(StandardCharsets.UTF_8));
                }
                status = connection.getResponseCode();
                java.io.InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
                // FetchRequest.save_to: a 2xx body goes to the file whole (a partial file first).
                String saveTo = strings.length >= 7 ? strings[6] : "";
                boolean saving = !saveTo.isEmpty() && status >= 200 && status < 300;
                java.io.File partial = new java.io.File(saveTo + ".part");
                java.io.OutputStream out = saving ? new java.io.FileOutputStream(partial) : new java.io.ByteArrayOutputStream();
                if (stream != null) {
                    byte[] buffer = new byte[16384];
                    for (int n; (n = stream.read(buffer)) > 0; ) out.write(buffer, 0, n);
                    stream.close();
                }
                out.close();
                if (saving && !partial.renameTo(new java.io.File(saveTo))) throw new java.io.IOException("cannot write " + saveTo);
                body = saving ? "" : new String(((java.io.ByteArrayOutputStream)out).toByteArray(), StandardCharsets.UTF_8);
                for (java.util.Map.Entry<String, java.util.List<String>> header : connection.getHeaderFields().entrySet()) {
                    if (header.getKey() != null) headers.append(header.getKey()).append(": ").append(String.join(", ", header.getValue())).append('\n');
                }
                connection.disconnect();
            } catch (Exception error) {
                status = 0;
                body = error.toString();
            }
            final int code = status;
            final String text = body;
            UiActivity.mainThread.post(() -> { if (activity.active) activity.nativeFetch(request, code, text, headers.toString()); });
        }).start();
    }
}
