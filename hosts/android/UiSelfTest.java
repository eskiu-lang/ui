package org.eskiu.ui;

import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

// The test harness's runs: the self test (an intent with selftest) and the conformance scene
// (an intent with conformance), each writing its result to a JSON file in the app's files.
final class UiSelfTest {
    private final UiActivity activity;
    UiSelfTest(UiActivity activity) { this.activity = activity; }

    // The counter: 100 presses of Increment batched into one commit on the next frame.
    void start() {
        Button button = findButton("Increment");
        UiActivity.require(button != null, "increment button");
        for (int i = 0; i < 100; i++) button.performClick();
        // The queued frame commits before this callback on the next frame.
        Choreographer.getInstance().postFrameCallback(time -> finishSelfTest());
    }

    private Button findButton(String title) {
        for (View view : activity.nodes.values()) if (view instanceof Button && ((Button)view).getText().toString().equals(title)) return (Button)view;
        return null;
    }

    private void finishSelfTest() {
        boolean found = false;
        for (View view : activity.nodes.values()) {
            if (view instanceof TextView && ((TextView)view).getText().toString().equals("Count: 100")) {
                found = view.getWidth() > 0 && view.getHeight() > 0;
            }
        }
        UiActivity.require(found && activity.commits == 2 && activity.frames == 1, "counter, layout or frame batching failed");
        UiActivity.require(activity.root.getPaddingTop() > 0, "root below the status bar");
        runTodoTest();
    }

    // Type a task into the TODO's field, press Add, and look for the new row.
    private void runTodoTest() {
        activity.nativeStart(1);
        activity.root.postDelayed(() -> {
            EditText field = null;
            for (View view : activity.nodes.values()) if (view instanceof EditText) field = (EditText)view;
            UiActivity.require(field != null, "todo field");
            UiActivity.require("New task".contentEquals(field.getContentDescription()) && "New task".contentEquals(field.getHint()),
                "field label and hint");
            field.setText("Write the docs");
            final EditText typed = field;
            activity.root.postDelayed(() -> {
                Button add = findButton("Add");
                UiActivity.require(add != null, "add button");
                add.performClick();
                activity.root.postDelayed(() -> {
                    boolean added = false;
                    for (View view : activity.nodes.values()) {
                        if (view instanceof TextView && !(view instanceof EditText) && !(view instanceof Button)
                            && ((TextView)view).getText().toString().equals("Write the docs")) added = true;
                    }
                    UiActivity.require(added && typed.getText().length() == 0, "typed task added and draft cleared");
                    ImageView logo = null;
                    for (View view : activity.nodes.values()) if (view instanceof ImageView) logo = (ImageView)view;
                    UiActivity.require(logo != null && logo.getDrawable() != null && "Eskiu".contentEquals(logo.getContentDescription())
                        && logo.getWidth() == Math.round(48 * activity.density()), "logo image, alt text and size");
                    writeResult();
                }, 300);
            }, 300);
        }, 300);
    }

    private static String roleOf(View view) {
        if (view instanceof Switch) return "switch";
        if (view instanceof android.widget.SeekBar) return "slider";
        if (view instanceof Button) return "button";
        if (view instanceof ProgressBar) return "progressbar";
        if (view instanceof ViewGroup && view.isClickable() && !(view instanceof ScrollView)) return "button";
        if (view instanceof EditText) return "textfield";
        if (view instanceof ImageView) return "image";
        if (view instanceof ScrollView) return "scroll";
        if (view instanceof TextView) return "text";
        return "group";
    }

    // The conformance scene: frames relative to the scene in points, roles, then the
    // handler log after two taps and typing "abc" (tools/conformance.py compares them).
    void runConformance() {
        View suite = null;
        for (View view : activity.nodes.values()) if ("Suite".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) suite = view;
        int[] base = new int[2];
        suite.getLocationInWindow(base);
        StringBuilder frames = new StringBuilder(), roles = new StringBuilder();
        Button tap = null;
        EditText field = null;
        for (View view : activity.nodes.values()) {
            String label = view.getContentDescription() != null ? view.getContentDescription().toString()
                : view instanceof Button ? ((Button)view).getText().toString() : "";
            if (label.isEmpty()) continue;
            int[] at = new int[2];
            view.getLocationInWindow(at);
            float d = activity.density();
            if (frames.length() > 0) { frames.append(','); roles.append(','); }
            frames.append('"').append(label).append("\":[").append(round1((at[0] - base[0]) / d)).append(',')
                .append(round1((at[1] - base[1]) / d)).append(',').append(round1(view.getWidth() / d)).append(',')
                .append(round1(view.getHeight() / d)).append(']');
            roles.append('"').append(label).append("\":\"").append(roleOf(view)).append('"');
            if (label.equals("Tap")) tap = (Button)view;
            if (label.equals("Field")) field = (EditText)view;
        }
        // What was drawn: colors as rrggbbaa, sizes in points.
        View painted = null, press = null;
        TextView title = null;
        Switch toggle = null;
        android.widget.SeekBar volume = null;
        View moved = null;
        for (View view : activity.nodes.values()) {
            if ("Volume".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) volume = (android.widget.SeekBar)view;
            if ("Moved".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) moved = view;
            String label = view.getContentDescription() == null ? "" : view.getContentDescription().toString();
            if (label.equals("Painted")) painted = view;
            if (label.equals("Title")) title = (TextView)view;
            if (label.equals("Press")) press = view;
            if (label.equals("Toggle")) toggle = (Switch)view;
        }
        float d = activity.density();
        GradientDrawable paint = activity.props.paints.get(activity.idOf(painted));
        int[] border = activity.props.borders.get(activity.idOf(painted));
        String styles = "{\"Painted\":{\"background\":\"" + hex(paint.getColor().getDefaultColor()) +
            "\",\"radius\":" + round1(paint.getCornerRadius() / d) + ",\"border\":" + border[0] +
            ",\"borderColor\":\"" + hex(UiProps.argb(border[1])) + "\",\"opacity\":" + round1(painted.getAlpha()) +
            "},\"Title\":{\"color\":\"" + hex(title.getCurrentTextColor()) + "\",\"fontSize\":" +
            round1(title.getTextSize() / d) + ",\"fontWeight\":" + (Build.VERSION.SDK_INT >= 28 ? title.getTypeface().getWeight() : 0) +
            "},\"Moved\":{\"translateX\":" + round1(moved.getTranslationX() / d) + ",\"translateY\":" + round1(moved.getTranslationY() / d) +
            ",\"scale\":" + round1(moved.getScaleX()) + ",\"rotate\":" + round1(moved.getRotation()) + "}}";
        tap.performClick();
        tap.performClick();
        field.setText("abc");
        press.performClick();
        toggle.setChecked(true);
        // A key press is a user's move (setProgress is not): 30 + 40.
        volume.setKeyProgressIncrement(40);
        volume.onKeyDown(android.view.KeyEvent.KEYCODE_DPAD_RIGHT,
            new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_DPAD_RIGHT));
        findButton("Copy").performClick();
        final String result = "{\"status\":\"DONE\",\"session\":\"" + activity.getIntent().getStringExtra("session") +
            "\",\"frames\":{" + frames + "},\"roles\":{" + roles + "},\"styles\":" + styles;
        final View suiteView = suite;
        final String[] modal = {"null"};
        final boolean[] refreshSeen = {false, false};
        final UiCommands commands = activity.commands;
        steps(300,
            // The alert: answer its second button.
            () -> findButton("Ask").performClick(),
            () -> { if (commands.shownAlert != null) { commands.shownAlert.dismiss(); commands.answerAlert.accept(1); } findButton("Open sheet").performClick(); },
            // The modal: measure it, close it from inside.
            () -> { modal[0] = modalReport(); findButton("Close").performClick(); },
            // Back with the modal open closes it (Android only).
            () -> findButton("Open sheet").performClick(),
            activity::onBackPressed,
            // Pull the feed at its top with real touches; "Loaded" hides the indicator.
            () -> {
                View feed = labeled("Feed");
                long now = android.os.SystemClock.uptimeMillis();
                float x = feed.getWidth() / 2f, y = 20 * activity.density();
                feed.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now, android.view.MotionEvent.ACTION_DOWN, x, y, 0));
                for (int i = 1; i <= 5; i++) {
                    feed.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + i * 16,
                        android.view.MotionEvent.ACTION_MOVE, x, y + i * 30 * activity.density(), 0));
                }
                feed.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + 100, android.view.MotionEvent.ACTION_UP,
                    x, y + 150 * activity.density(), 0));
            },
            () -> { refreshSeen[0] = refreshShown(); findButton("Loaded").performClick(); },
            () -> refreshSeen[1] = refreshShown(),
            // Scroll the feed: rows near 2000 points mount, rows from the top go.
            () -> {
                for (View view : activity.nodes.values()) {
                    if ("Feed".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) {
                        view.scrollTo(0, Math.round(2000 * activity.density()));
                    }
                }
            },
            () -> {},
            () -> {
                float dd = activity.density();
                int[] origin = new int[2];
                suiteView.getLocationInWindow(origin);
                StringBuilder scrolled = new StringBuilder();
                for (View view : activity.nodes.values()) {
                    String label = view.getContentDescription() == null ? "" : view.getContentDescription().toString();
                    if (!(label.startsWith("Row ") || label.equals("Feed"))) continue;
                    int[] at = new int[2];
                    view.getLocationInWindow(at);
                    if (scrolled.length() > 0) scrolled.append(',');
                    scrolled.append('"').append(label).append("\":[").append(round1((at[0] - origin[0]) / dd)).append(',')
                        .append(round1((at[1] - origin[1]) / dd)).append(',').append(round1(view.getWidth() / dd)).append(',')
                        .append(round1(view.getHeight() / dd)).append(']');
                }
                String json = result + ",\"scrolled\":{" + scrolled + "},\"modal\":" + modal[0] +
                    ",\"refresh\":[" + refreshSeen[0] + "," + refreshSeen[1] + "]" +
                    ",\"log\":\"" + activity.nativeConformanceLog() + "\"}";
                try (FileOutputStream out = new FileOutputStream(new File(activity.getFilesDir(), "conformance.json"))) {
                    out.write(json.getBytes(StandardCharsets.UTF_8));
                } catch (Exception error) { throw new RuntimeException(error); }
            });
    }

    private View labeled(String label) {
        for (View view : activity.nodes.values()) {
            if (label.contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) return view;
        }
        return null;
    }
    private boolean refreshShown() {
        for (java.util.Map.Entry<Integer, ProgressBar> entry : activity.props.refreshers.entrySet()) {
            if (entry.getValue().getVisibility() == View.VISIBLE) return true;
        }
        return false;
    }

    // Run each step `delay` ms after the previous one.
    private void steps(int delay, Runnable... list) { step(0, delay, list); }
    private void step(int index, int delay, Runnable[] list) {
        if (index >= list.length) return;
        activity.root.postDelayed(() -> { list[index].run(); step(index + 1, delay, list); }, delay);
    }

    // The open modal's layer and its sheet, in points in the window, and the window size.
    private String modalReport() {
        View layer = null, sheet = null;
        for (java.util.Map.Entry<Integer, View> entry : activity.nodes.entrySet()) {
            if (activity.tree.modalIds.contains(entry.getKey())) layer = entry.getValue();
            if ("Sheet".contentEquals(entry.getValue().getContentDescription() == null ? "" : entry.getValue().getContentDescription())) sheet = entry.getValue();
        }
        if (layer == null || sheet == null) return "null";
        float d = activity.density();
        int[] a = new int[2], b = new int[2];
        layer.getLocationInWindow(a);
        sheet.getLocationInWindow(b);
        View window = activity.getWindow().getDecorView();
        return "{\"cover\":[" + round1(a[0] / d) + "," + round1(a[1] / d) + "," + round1(layer.getWidth() / d) + "," +
            round1(layer.getHeight() / d) + "],\"window\":[" + round1(window.getWidth() / d) + "," +
            round1(window.getHeight() / d) + "],\"sheet\":[" + round1(b[0] / d) + "," + round1(b[1] / d) + "," +
            round1(sheet.getWidth() / d) + "," + round1(sheet.getHeight() / d) + "]}";
    }
    private static float round1(float value) { return Math.round(value * 10) / 10f; }
    private static String hex(int argb) {
        return String.format("%02x%02x%02x%02x", (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, (argb >>> 24));
    }

    private void writeResult() {
        String result = "{\"status\":\"PASS\",\"session\":\"" + activity.getIntent().getStringExtra("session") + "\",\"events\":100,\"commits\":" + activity.commits +
            ",\"frames\":" + activity.frames + ",\"frameWorkMs\":" + activity.lastFrameMs + ",\"todo\":\"PASS\",\"renderer\":\"android.view + JNI + Yoga\"}";
        try (FileOutputStream out = new FileOutputStream(new File(activity.getFilesDir(), "selftest.json"))) {
            out.write(result.getBytes(StandardCharsets.UTF_8));
        } catch (Exception error) { throw new RuntimeException(error); }
    }
}
