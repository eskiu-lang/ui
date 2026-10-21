package org.eskiu.ui.prototype;

import android.app.Activity;
import android.os.Bundle;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
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
    private native void nativeTick();
    private native void nativeLayout(float width, float height);
    private native void nativeStop();
    private final HashMap<Integer, View> nodes = new HashMap<>();
    private final HashMap<Integer, Integer> generations = new HashMap<>();
    private YogaGroup root;
    private boolean active;
    private boolean pending;
    private boolean testing;
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
        YogaGroup() { super(UiActivity.this); }
        @Override protected void onMeasure(int width, int height) {
            setMeasuredDimension(MeasureSpec.getSize(width), MeasureSpec.getSize(height));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            if (this == root && active) nativeLayout((r - l) / density(), (b - t) / density());
        }
    }
    private float density() { return getResources().getDisplayMetrics().density; }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        root = new YogaGroup();
        setContentView(root);
        testing = getIntent().getBooleanExtra("selftest", false);
        root.post(() -> {
            active = true;
            nativeStart(getIntent().getIntExtra("example", 0));
            if (testing) {
                Button button = findButton("Increment");
                require(button != null, "increment button");
                for (int i = 0; i < 100; i++) button.performClick();
                // The queued frame commits before this callback on the next frame.
                Choreographer.getInstance().postFrameCallback(time -> finishSelfTest());
            }
        });
    }
    @Override protected void onDestroy() {
        if (active) { nativeStop(); active = false; }
        super.onDestroy();
    }
    public void schedule() {
        if (!pending && active) { pending = true; Choreographer.getInstance().postFrameCallback(frame); }
    }
    public void cancel() { Choreographer.getInstance().removeFrameCallback(frame); pending = false; }
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
            } else view = new YogaGroup();
            view.setId(View.generateViewId());
            nodes.put(id, view); generations.put(id, generation);
            return;
        }
        View view = nodes.get(id);
        if (view == null) { require(op == 6, "unknown node " + id); return; }
        if (op == 3) ((TextView)view).setText(text);
        else if (op == 4 || op == 5) {
            ViewGroup group = nodes.containsKey(parent) ? (ViewGroup)nodes.get(parent) : root;
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
        view.layout(l, t, l + w, t + h);
    }
    public float width() { return root.getWidth() / density(); }
    public float height() { return root.getHeight() / density(); }
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
        String result = "{\"status\":\"PASS\",\"session\":\"" + getIntent().getStringExtra("session") + "\",\"events\":100,\"commits\":" + commits +
            ",\"frames\":" + frames + ",\"frameWorkMs\":" + lastFrameMs + ",\"renderer\":\"android.view + JNI + Yoga\"}";
        try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "selftest.json"))) {
            out.write(result.getBytes(StandardCharsets.UTF_8));
        } catch (Exception error) { throw new RuntimeException(error); }
    }
}
