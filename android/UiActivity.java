package org.eskiu.ui.prototype;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.Button;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.HashMap;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

// Native Android views. The same Eskiu components drive this and the other hosts.
public final class UiActivity extends Activity {
    static { System.loadLibrary("eskiu_ui"); }
    private native void nativeStart(int example);
    private native void nativeEvent(int id, int generation);
    private native void nativeText(int id, int generation, String text);
    private native void nativeFocus(int id, int generation, int focused);
    private native void nativeTick();
    private native void nativeLayout(float width, float height);
    private native void nativeStop();
    private native int nativeEdgeToEdge();
    private native void nativeInsets(int top, int right, int bottom, int left, int keyboard);
    private native void nativeActive(int active);
    private native String nativeConformanceLog();
    private final HashMap<Integer, View> nodes = new HashMap<>();
    private final HashMap<Integer, Integer> generations = new HashMap<>();
    private YogaGroup root;
    private boolean active;
    private boolean pending;
    private boolean testing;
    // The insets last applied, reported again when the app starts.
    private int[] lastInsets;
    private void reportInsets() {
        if (lastInsets != null) nativeInsets(lastInsets[0], lastInsets[1], lastInsets[2], lastInsets[3], lastInsets[4]);
    }
    // Set while a commit writes an EditText, so the change does not echo back as an edit.
    private boolean applying;
    private int commits;
    private int frames;
    private double lastFrameMs;
    private final Choreographer.FrameCallback frame = time -> {
        pending = false;
        if (!active) return;
        long start = System.nanoTime();
        nativeTick();
        lastFrameMs = (System.nanoTime() - start) / 1e6;
        frames++;
    };

    private final class YogaGroup extends ViewGroup {
        // The size Yoga gave a scroll view's content; 0 for every other group.
        int contentWidth, contentHeight;
        YogaGroup() { super(UiActivity.this); }
        @Override protected void onMeasure(int width, int height) {
            if (contentHeight > 0) { setMeasuredDimension(contentWidth, contentHeight); return; }
            setMeasuredDimension(MeasureSpec.getSize(width), MeasureSpec.getSize(height));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            if (this == root && active) nativeLayout(width(), height());
        }
    }
    private float density() { return getResources().getDisplayMetrics().density; }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        root = new YogaGroup();
        setContentView(root);
        // System bars, display cutouts and the keyboard: padding on the root unless the app
        // draws edge to edge; the raw insets go to the app either way.
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            int top, right, bottom, left, keyboard;
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                android.graphics.Insets ime = insets.getInsets(WindowInsets.Type.ime());
                top = bars.top; right = bars.right; bottom = bars.bottom; left = bars.left; keyboard = ime.bottom;
            } else {
                top = insets.getSystemWindowInsetTop(); right = insets.getSystemWindowInsetRight();
                bottom = insets.getSystemWindowInsetBottom(); left = insets.getSystemWindowInsetLeft(); keyboard = 0;
            }
            if (nativeEdgeToEdge() == 0) root.setPadding(left, top, right, Math.max(bottom, keyboard));
            float d = density();
            lastInsets = new int[] {Math.round(top / d), Math.round(right / d), Math.round(bottom / d),
                                    Math.round(left / d), Math.round(keyboard / d)};
            if (active) reportInsets();
            return insets;
        });
        testing = getIntent().getBooleanExtra("selftest", false);
        root.post(() -> {
            active = true;
            boolean conformance = getIntent().getBooleanExtra("conformance", false);
            nativeStart(conformance ? 3 : getIntent().getIntExtra("example", 0));
            reportInsets();
            if (conformance) root.postDelayed(this::runConformance, 400);
            if (testing) {
                Button button = findButton("Increment");
                require(button != null, "increment button");
                for (int i = 0; i < 100; i++) button.performClick();
                // The queued frame commits before this callback on the next frame.
                Choreographer.getInstance().postFrameCallback(time -> finishSelfTest());
            }
        });
    }
    @Override protected void onResume() { super.onResume(); if (active) nativeActive(1); }
    @Override protected void onPause() { if (active) nativeActive(0); super.onPause(); }
    @Override protected void onDestroy() {
        if (active) { nativeStop(); active = false; }
        super.onDestroy();
    }
    public void schedule() {
        if (!pending && active) { pending = true; Choreographer.getInstance().postFrameCallback(frame); }
    }
    public void cancel() { Choreographer.getInstance().removeFrameCallback(frame); pending = false; }
    // A file in the APK's assets, or an http(s) URL fetched off the UI thread.
    private void loadImage(ImageView view, String source) {
        view.setImageDrawable(null);
        if (source.isEmpty()) return;
        if (source.startsWith("http://") || source.startsWith("https://")) {
            new Thread(() -> {
                try (java.io.InputStream in = new java.net.URL(source).openStream()) {
                    Bitmap bitmap = BitmapFactory.decodeStream(in);
                    runOnUiThread(() -> view.setImageBitmap(bitmap));
                } catch (Exception error) { /* a failed load leaves the view empty */ }
            }).start();
            return;
        }
        try (java.io.InputStream in = getAssets().open(source)) {
            view.setImageBitmap(BitmapFactory.decodeStream(in));
        } catch (Exception error) { /* a missing asset leaves the view empty */ }
    }

    private Button findButton(String title) {
        for (View view : nodes.values()) if (view instanceof Button && ((Button)view).getText().toString().equals(title)) return (Button)view;
        return null;
    }
    private void require(boolean ok, String message) { if (!ok) throw new AssertionError(message); }

    public void apply(int op, int id, int generation, int parent, int before, int kind, String text) {
        if (op == 1) {
            View view;
            if (kind == 4) {
                Button button = new Button(this);
                button.setAllCaps(false);
                button.setOnClickListener(v -> nativeEvent(id, generations.get(id)));
                view = button;
            } else if (kind == 3) {
                TextView label = new TextView(this); label.setTextSize(18); view = label;
            } else if (kind == 5) {
                EditText field = new EditText(this);
                field.setSingleLine(true);
                field.setTextSize(18);
                field.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                    @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
                    @Override public void afterTextChanged(Editable text) {
                        if (!applying && active) nativeText(id, generations.get(id), text.toString());
                    }
                });
                field.setOnFocusChangeListener((v, focused) -> {
                    if (active && generations.containsKey(id)) nativeFocus(id, generations.get(id), focused ? 1 : 0);
                });
                view = field;
            } else if (kind == 7) {
                ImageView image = new ImageView(this);
                image.setScaleType(ImageView.ScaleType.FIT_CENTER);
                view = image;
            } else if (kind == 6) {
                // A ScrollView holds one child: the group its children go into.
                ScrollView scroll = new ScrollView(this);
                scroll.addView(new YogaGroup());
                view = scroll;
            } else view = new YogaGroup();
            view.setId(View.generateViewId());
            nodes.put(id, view); generations.put(id, generation);
            return;
        }
        View view = nodes.get(id);
        if (view == null) { require(op == 6, "unknown node " + id); return; }
        if (op == 3 && view instanceof EditText) {
            EditText field = (EditText)view;
            if (!field.getText().toString().equals(text)) {
                applying = true;
                field.setText(text);
                field.setSelection(text.length());
                applying = false;
            }
        } else if (op == 3) ((TextView)view).setText(text);
        else if (op == 9) {
            // String props: 1 label, 2 placeholder.
            if (kind == 1) view.setContentDescription(text.isEmpty() ? null : text);
            else if (kind == 2 && view instanceof EditText) ((EditText)view).setHint(text);
            else if (kind == 3 && view instanceof ImageView) loadImage((ImageView)view, text);
        } else if (op == 2 && kind == 3) {
            if (before != 0) view.requestFocus(); else view.clearFocus();
        }
        else if (op == 4 || op == 5) {
            ViewGroup group = nodes.containsKey(parent) ? (ViewGroup)nodes.get(parent) : root;
            if (group instanceof ScrollView) group = (ViewGroup)group.getChildAt(0);
            if (view.getParent() != null) ((ViewGroup)view.getParent()).removeView(view);
            group.addView(view, before == 0 ? group.getChildCount() : group.indexOfChild(nodes.get(before)));
        } else if (op == 6) {
            if (view.getParent() != null) ((ViewGroup)view.getParent()).removeView(view);
            nodes.remove(id); generations.remove(id);
        } else if (op == 7 || op == 8) view.setEnabled(op == 7);
    }
    public long measure(int id, float width, int wm, float height, int hm) {
        View view = nodes.get(id);
        int wspec = wm == 0 ? View.MeasureSpec.UNSPECIFIED : wm == 1 ? View.MeasureSpec.EXACTLY : View.MeasureSpec.AT_MOST;
        int hspec = hm == 0 ? View.MeasureSpec.UNSPECIFIED : hm == 1 ? View.MeasureSpec.EXACTLY : View.MeasureSpec.AT_MOST;
        view.measure(View.MeasureSpec.makeMeasureSpec(Math.max(0, Math.round(width * density())), wspec),
                     View.MeasureSpec.makeMeasureSpec(Math.max(0, Math.round(height * density())), hspec));
        return ((long)view.getMeasuredWidth() << 32) | (view.getMeasuredHeight() & 0xffffffffL);
    }
    public void place(int id, float x, float y, float width, float height) {
        View view = nodes.get(id);
        int l = Math.round(x * density()), t = Math.round(y * density());
        int w = Math.round(width * density()), h = Math.round(height * density());
        view.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY));
        // The root's own children sit inside its padding.
        if (view.getParent() == root) { l += root.getPaddingLeft(); t += root.getPaddingTop(); }
        view.layout(l, t, l + w, t + h);
    }
    public void content(int id, float width, float height) {
        YogaGroup content = (YogaGroup)((ScrollView)nodes.get(id)).getChildAt(0);
        content.contentWidth = Math.round(width * density());
        content.contentHeight = Math.max(1, Math.round(height * density()));
        content.measure(View.MeasureSpec.makeMeasureSpec(content.contentWidth, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(content.contentHeight, View.MeasureSpec.EXACTLY));
        content.layout(0, 0, content.contentWidth, content.contentHeight);
    }
    public float width() { return (root.getWidth() - root.getPaddingLeft() - root.getPaddingRight()) / density(); }
    public float height() { return (root.getHeight() - root.getPaddingTop() - root.getPaddingBottom()) / density(); }
    public float scale() { return density(); }
    public void committed() { commits++; root.invalidate(); }

    private void finishSelfTest() {
        boolean found = false;
        for (View view : nodes.values()) {
            if (view instanceof TextView && ((TextView)view).getText().toString().equals("Count: 100")) {
                found = view.getWidth() > 0 && view.getHeight() > 0;
            }
        }
        require(found && commits == 2 && frames == 1, "counter, layout or frame batching failed");
        require(root.getPaddingTop() > 0, "root below the status bar");
        runTodoTest();
    }

    // Type a task into the TODO's field, press Add, and look for the new row.
    private void runTodoTest() {
        nativeStart(1);
        root.postDelayed(() -> {
            EditText field = null;
            for (View view : nodes.values()) if (view instanceof EditText) field = (EditText)view;
            require(field != null, "todo field");
            require("New task".contentEquals(field.getContentDescription()) && "New task".contentEquals(field.getHint()),
                "field label and hint");
            field.setText("Write the docs");
            final EditText typed = field;
            root.postDelayed(() -> {
                Button add = findButton("Add");
                require(add != null, "add button");
                add.performClick();
                root.postDelayed(() -> {
                    boolean added = false;
                    for (View view : nodes.values()) {
                        if (view instanceof TextView && !(view instanceof EditText) && !(view instanceof Button)
                            && ((TextView)view).getText().toString().equals("Write the docs")) added = true;
                    }
                    require(added && typed.getText().length() == 0, "typed task added and draft cleared");
                    ImageView logo = null;
                    for (View view : nodes.values()) if (view instanceof ImageView) logo = (ImageView)view;
                    require(logo != null && logo.getDrawable() != null && "Eskiu".contentEquals(logo.getContentDescription())
                        && logo.getWidth() == Math.round(48 * density()), "logo image, alt text and size");
                    writeResult();
                }, 300);
            }, 300);
        }, 300);
    }

    private static String roleOf(View view) {
        if (view instanceof Button) return "button";
        if (view instanceof EditText) return "textfield";
        if (view instanceof ImageView) return "image";
        if (view instanceof ScrollView) return "scroll";
        if (view instanceof TextView) return "text";
        return "group";
    }

    // The conformance scene: frames relative to the scene in points, roles, then the
    // handler log after two taps and typing "abc" (tools/conformance.py compares them).
    private void runConformance() {
        View suite = null;
        for (View view : nodes.values()) if ("Suite".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) suite = view;
        int[] base = new int[2];
        suite.getLocationInWindow(base);
        StringBuilder frames = new StringBuilder(), roles = new StringBuilder();
        Button tap = null;
        EditText field = null;
        for (View view : nodes.values()) {
            String label = view instanceof Button ? ((Button)view).getText().toString()
                : view.getContentDescription() == null ? "" : view.getContentDescription().toString();
            if (label.isEmpty()) continue;
            int[] at = new int[2];
            view.getLocationInWindow(at);
            float d = density();
            if (frames.length() > 0) { frames.append(','); roles.append(','); }
            frames.append('"').append(label).append("\":[").append(round1((at[0] - base[0]) / d)).append(',')
                .append(round1((at[1] - base[1]) / d)).append(',').append(round1(view.getWidth() / d)).append(',')
                .append(round1(view.getHeight() / d)).append(']');
            roles.append('"').append(label).append("\":\"").append(roleOf(view)).append('"');
            if (label.equals("Tap")) tap = (Button)view;
            if (label.equals("Field")) field = (EditText)view;
        }
        tap.performClick();
        tap.performClick();
        field.setText("abc");
        final String result = "{\"status\":\"DONE\",\"session\":\"" + getIntent().getStringExtra("session") +
            "\",\"frames\":{" + frames + "},\"roles\":{" + roles + "}";
        root.postDelayed(() -> {
            String json = result + ",\"log\":\"" + nativeConformanceLog() + "\"}";
            try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "conformance.json"))) {
                out.write(json.getBytes(StandardCharsets.UTF_8));
            } catch (Exception error) { throw new RuntimeException(error); }
        }, 300);
    }
    private static float round1(float value) { return Math.round(value * 10) / 10f; }

    private void writeResult() {
        String result = "{\"status\":\"PASS\",\"session\":\"" + getIntent().getStringExtra("session") + "\",\"events\":100,\"commits\":" + commits +
            ",\"frames\":" + frames + ",\"frameWorkMs\":" + lastFrameMs + ",\"todo\":\"PASS\",\"renderer\":\"android.view + JNI + Yoga\"}";
        try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "selftest.json"))) {
            out.write(result.getBytes(StandardCharsets.UTF_8));
        } catch (Exception error) { throw new RuntimeException(error); }
    }
}
