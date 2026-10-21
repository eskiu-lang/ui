package org.eskiu.ui;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Looper;
import android.net.Uri;
import android.os.Build;
import android.view.WindowInsetsController;
import android.widget.FrameLayout;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.Button;
import android.graphics.Bitmap;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.widget.ProgressBar;
import android.widget.Switch;
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
    private native void nativeSystem(int dark, int screenReader, int reduceMotion, int fontScale, String locale, int rtl);
    // The system's appearance, accessibility settings and language, to the core.
    private void reportSystem() {
        if (!active) return;
        android.content.res.Configuration config = getResources().getConfiguration();
        boolean dark = (config.uiMode & android.content.res.Configuration.UI_MODE_NIGHT_MASK) == android.content.res.Configuration.UI_MODE_NIGHT_YES;
        android.view.accessibility.AccessibilityManager a11y = (android.view.accessibility.AccessibilityManager)getSystemService(ACCESSIBILITY_SERVICE);
        boolean reader = a11y != null && a11y.isTouchExplorationEnabled();
        float animations = android.provider.Settings.Global.getFloat(getContentResolver(), android.provider.Settings.Global.ANIMATOR_DURATION_SCALE, 1f);
        java.util.Locale locale = config.getLocales().get(0);
        boolean rtl = android.text.TextUtils.getLayoutDirectionFromLocale(locale) == View.LAYOUT_DIRECTION_RTL;
        nativeSystem(dark ? 1 : 0, reader ? 1 : 0, animations == 0f ? 1 : 0, Math.round(config.fontScale * 100),
                     locale.toLanguageTag(), rtl ? 1 : 0);
    }
    // Dark mode, the language, the text size and rotation do not recreate the activity: the
    // app keeps its state and hears about the change.
    @Override public void onConfigurationChanged(android.content.res.Configuration config) {
        super.onConfigurationChanged(config);
        reportSystem();
    }
    private native String nativeConformanceLog();
    private native void nativeToggle(int id, int generation, int on);
    private native void nativeScroll(int id, int generation, int offset, int viewport);
    private native void nativeAlert(int request, int choice);
    private native void nativeRefresh(int id, int generation);
    // Pull to refresh per scroll: its indicator, over the top of the content.
    private final HashMap<Integer, ProgressBar> refreshers = new HashMap<>();
    private native int nativeBack();
    private native void nativeLink(String url);
    // The alert on screen and how to answer it (also used by the conformance run).
    private AlertDialog shownAlert;
    private final java.util.HashSet<Integer> modalIds = new java.util.HashSet<>();
    private java.util.function.IntConsumer answerAlert;
    // Scroll views whose offset the core wants (virtual lists), and the viewport last reported.
    private final HashMap<Integer, Integer> scrollViewports = new HashMap<>();
    private native void nativeWindow(int width, int height, int scale);
    private native void nativeModuleEvent(String channel, String payload);
    private native void nativeTimer(int token);
    private native void nativeHover(int id, int generation, int inside);
    private native void nativePan(int id, int generation, int phase, int dx, int dy, int vx, int vy);
    // Drags past the touch slop, in screen coordinates (the view itself may move), with their
    // velocity; a touch that does not move is still a click. A group takes the drag from a
    // child that would consume it (a pressable inside), once it passes the slop.
    private final class PanTracker {
        final int id;
        final float[] start = new float[2];
        boolean down, panning;
        android.view.VelocityTracker velocity;
        final int slop = android.view.ViewConfiguration.get(UiActivity.this).getScaledTouchSlop();
        PanTracker(int id) { this.id = id; }
        void track(android.view.MotionEvent e) {
            android.view.MotionEvent screen = android.view.MotionEvent.obtain(e);
            screen.setLocation(e.getRawX(), e.getRawY());
            if (e.getActionMasked() == android.view.MotionEvent.ACTION_DOWN) {
                start[0] = e.getRawX(); start[1] = e.getRawY(); down = true; panning = false;
                if (velocity != null) velocity.recycle();
                velocity = android.view.VelocityTracker.obtain();
            }
            if (velocity != null) velocity.addMovement(screen);
            screen.recycle();
        }
        boolean past(android.view.MotionEvent e) { return down && Math.hypot(e.getRawX() - start[0], e.getRawY() - start[1]) >= slop; }
        // A group's interception: true once the drag passes the slop.
        boolean intercept(android.view.MotionEvent e) {
            track(e);
            int action = e.getActionMasked();
            if (action == android.view.MotionEvent.ACTION_UP || action == android.view.MotionEvent.ACTION_CANCEL) { down = false; return false; }
            return action == android.view.MotionEvent.ACTION_MOVE && past(e);
        }
        boolean touch(View v, android.view.MotionEvent e) {
            if (e.getActionMasked() != android.view.MotionEvent.ACTION_DOWN || !down) track(e);
            float d = density();
            int action = e.getActionMasked();
            int dx = Math.round((e.getRawX() - start[0]) / d), dy = Math.round((e.getRawY() - start[1]) / d);
            if (!active || !generations.containsKey(id)) return true;
            int generation = generations.get(id);
            if (action == android.view.MotionEvent.ACTION_MOVE) {
                if (!panning && !past(e)) return true;
                if (!panning) {
                    panning = true;
                    if (v.getParent() != null) v.getParent().requestDisallowInterceptTouchEvent(true);
                    nativePan(id, generation, 0, 0, 0, 0, 0);
                }
                nativePan(id, generation, 1, dx, dy, 0, 0);
            } else if (action == android.view.MotionEvent.ACTION_UP || action == android.view.MotionEvent.ACTION_CANCEL) {
                int vx = 0, vy = 0;
                if (velocity != null) {
                    velocity.computeCurrentVelocity(1000);
                    vx = Math.round(velocity.getXVelocity() / d); vy = Math.round(velocity.getYVelocity() / d);
                    velocity.recycle(); velocity = null;
                }
                if (panning) nativePan(id, generation, action == android.view.MotionEvent.ACTION_UP ? 2 : 3, dx, dy, vx, vy);
                else if (action == android.view.MotionEvent.ACTION_UP) v.performClick();
                panning = false; down = false;
            }
            return true;
        }
    }
    private final HashMap<Integer, PanTracker> pans = new HashMap<>();
    private void listenPan(int id, View view, boolean on) {
        if (!on) { pans.remove(id); view.setOnTouchListener(null); return; }
        PanTracker tracker = new PanTracker(id);
        pans.put(id, tracker);
        view.setOnTouchListener(tracker::touch);
    }
    private native void nativeValue(int id, int generation, int value);
    private native void nativeAnswer(int request, String text);
    private native void nativeDataDir(String path);
    private native void nativeFetch(int request, int status, String body, String headers);
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
                java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
                if (stream != null) {
                    byte[] buffer = new byte[16384];
                    for (int n; (n = stream.read(buffer)) > 0; ) bytes.write(buffer, 0, n);
                    stream.close();
                }
                body = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
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
            mainThread.post(() -> { if (active) nativeFetch(request, code, text, headers.toString()); });
        }).start();
    }
    // A slider's range: SeekBar counts from 0, so its progress is the value minus the minimum.
    private final HashMap<Integer, int[]> sliderRanges = new HashMap<>();
    // Each node's transforms (translate x, y in points, scale in percent, rotate in degrees)
    // and transition (ms, Easing).
    private final HashMap<Integer, int[]> transforms = new HashMap<>();
    private final HashMap<Integer, int[]> motions = new HashMap<>();
    private android.animation.TimeInterpolator interpolator(int easing) {
        if (easing == 1) return new android.view.animation.DecelerateInterpolator();
        if (easing == 2) return new android.view.animation.AccelerateInterpolator();
        if (easing == 3) return new android.view.animation.LinearInterpolator();
        if (easing == 4) return new android.view.animation.OvershootInterpolator(1.2f);
        return new android.view.animation.AccelerateDecelerateInterpolator();
    }
    // The node's transition, when it has one and its view is on screen.
    private int[] motionOf(int id, View view) {
        int[] motion = motions.get(id);
        return motion != null && motion[0] > 0 && view.isAttachedToWindow() ? motion : null;
    }
    private void applyTransform(int id, View view) {
        int[] t = transforms.get(id);
        float d = density();
        int[] motion = motionOf(id, view);
        if (motion == null) {
            view.setTranslationX(t[0] * d); view.setTranslationY(t[1] * d);
            view.setScaleX(t[2] / 100f); view.setScaleY(t[2] / 100f); view.setRotation(t[3]);
            return;
        }
        view.animate().translationX(t[0] * d).translationY(t[1] * d).scaleX(t[2] / 100f).scaleY(t[2] / 100f)
            .rotation(t[3]).setDuration(motion[0]).setInterpolator(interpolator(motion[1])).start();
    }
    // Timers for use_interval and use_timeout, on the UI thread, by token.
    private final HashMap<Integer, Runnable> timers = new HashMap<>();
    public void timerStart(int token, int ms, boolean repeat) {
        Runnable[] tick = new Runnable[1];
        tick[0] = () -> {
            if (!timers.containsKey(token)) return;
            if (repeat) mainThread.postDelayed(tick[0], ms); else timers.remove(token);
            if (active) nativeTimer(token);
        };
        timers.put(token, tick[0]);
        mainThread.postDelayed(tick[0], ms);
    }
    // A view's position in the window, in points, packed as x << 32 | y.
    public long windowOf(int id) {
        View view = nodes.get(id);
        int[] at = new int[2];
        if (view != null) view.getLocationInWindow(at);
        float d = density();
        return ((long)Math.round(at[0] / d) << 32) | (Math.round(at[1] / d) & 0xffffffffL);
    }
    public void timerStop(int token) {
        Runnable tick = timers.remove(token);
        if (tick != null) mainThread.removeCallbacks(tick);
    }
    private native void nativeViewEvent(int id, int generation, String name, String payload);

    // Native code (native/android). A view type for NativeView: create makes the view (it
    // fills the space the layout gives the NativeView), set applies a named prop ("" when the
    // app stopped giving it).
    public interface ViewFactory {
        View create(Context context, ViewEvents events);
        void set(View view, String name, String value);
    }
    // What a native view sends back: an event's name and payload (NativeView's on_event).
    public interface ViewEvents { void emit(String name, String payload); }
    private static final HashMap<String, ViewFactory> viewTypes = new HashMap<>();
    private static final Handler mainThread = new Handler(Looper.getMainLooper());
    private static UiActivity current;
    public static void registerView(String type, ViewFactory factory) { viewTypes.put(type, factory); }
    // Post payload on channel to the app's use_native_event handlers; any thread.
    public static void emit(String channel, String payload) {
        String text = payload == null ? "" : payload;
        mainThread.post(() -> { if (current != null && current.active) current.nativeModuleEvent(channel, text); });
    }
    // The running activity, for a Context.
    public static UiActivity current() { return current; }
    // Runtime permissions for native code: `done` gets whether every one was granted, on the
    // UI thread (at once when they already are).
    private final HashMap<Integer, java.util.function.Consumer<Boolean>> permissionRequests = new HashMap<>();
    private int nextPermissionRequest = 1;
    public static void requestPermissions(String[] permissions, java.util.function.Consumer<Boolean> done) {
        mainThread.post(() -> {
            UiActivity activity = current;
            if (activity == null) { done.accept(false); return; }
            boolean all = true;
            for (String permission : permissions) {
                if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) all = false;
            }
            if (all) { done.accept(true); return; }
            int request = activity.nextPermissionRequest++;
            activity.permissionRequests.put(request, done);
            activity.requestPermissions(permissions, request);
        });
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        java.util.function.Consumer<Boolean> done = permissionRequests.remove(request);
        if (done == null) return;
        boolean all = results.length > 0;
        for (int result : results) if (result != PackageManager.PERMISSION_GRANTED) all = false;
        done.accept(all);
    }
    private final HashMap<Integer, View> nativeViews = new HashMap<>();
    private final HashMap<Integer, String> nativeTypes = new HashMap<>();
    // The classes `ui build` lists in the manifest: each one's static setup() runs once.
    private static boolean setUp;
    private void setupNative() {
        if (setUp) return;
        setUp = true;
        try {
            ActivityInfo info = getPackageManager().getActivityInfo(getComponentName(), PackageManager.GET_META_DATA);
            String names = info.metaData == null ? null : info.metaData.getString("org.eskiu.ui.native");
            if (names == null) return;
            for (String name : names.split(",")) {
                if (name.isEmpty()) continue;
                java.lang.reflect.Method setup;
                try { setup = Class.forName(name).getMethod("setup"); } catch (NoSuchMethodException e) { continue; }
                setup.invoke(null);
            }
        } catch (Exception e) { throw new RuntimeException("ui: native setup", e); }
    }
    private void mountNative(int id, FrameLayout box, String type) {
        box.removeAllViews();
        nativeViews.remove(id);
        nativeTypes.put(id, type);
        ViewFactory factory = viewTypes.get(type);
        if (factory == null) { android.util.Log.w("ui", "no native view registered as " + type); return; }
        int generation = generations.get(id);
        View view = factory.create(this, (name, payload) -> {
            Runnable send = () -> { if (active) nativeViewEvent(id, generation, name, payload == null ? "" : payload); };
            if (Looper.myLooper() == Looper.getMainLooper()) send.run(); else mainThread.post(send);
        });
        box.addView(view, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        nativeViews.put(id, view);
    }
    public void named(int id, String name, String value) {
        View view = nativeViews.get(id);
        ViewFactory factory = viewTypes.get(nativeTypes.get(id));
        if (view != null && factory != null) factory.set(view, name, value);
    }
    // Per view: the drawable that paints background, border and radius, and the font.
    private final HashMap<Integer, GradientDrawable> paints = new HashMap<>();
    private final HashMap<Integer, int[]> borders = new HashMap<>();
    private final HashMap<Integer, int[]> fonts = new HashMap<>();
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
        // The node this group draws (-1: the root, a scroll's content).
        int nodeId = -1;
        @Override public boolean onInterceptTouchEvent(android.view.MotionEvent e) {
            PanTracker tracker = pans.get(nodeId);
            return tracker != null && tracker.intercept(e);
        }
        // The size Yoga gave a scroll view's content; 0 for every other group.
        int contentWidth, contentHeight;
        YogaGroup() {
            super(UiActivity.this);
            // Overflow is visible, as in Yoga: an absolute child may draw outside (a radius clips
            // through the outline).
            setClipChildren(false);
            setClipToPadding(false);
        }
        @Override protected void onMeasure(int width, int height) {
            if (contentHeight > 0) { setMeasuredDimension(contentWidth, contentHeight); return; }
            setMeasuredDimension(MeasureSpec.getSize(width), MeasureSpec.getSize(height));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            if (this == root && active) {
                nativeWindow(Math.round((r - l) / density()), Math.round((b - t) / density()), Math.round(density() * 100));
                nativeLayout(width(), height());
            }
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
            else root.setPadding(0, 0, 0, 0);
            float d = density();
            lastInsets = new int[] {Math.round(top / d), Math.round(right / d), Math.round(bottom / d),
                                    Math.round(left / d), Math.round(keyboard / d)};
            if (active) reportInsets();
            return insets;
        });
        current = this;
        setupNative();
        testing = getIntent().getBooleanExtra("selftest", false);
        root.post(() -> {
            active = true;
            boolean conformance = getIntent().getBooleanExtra("conformance", false);
            nativeDataDir(getFilesDir().getPath());
            nativeStart(conformance ? 3 : getIntent().getIntExtra("example", 0));
            // The app sets its edge policy as it starts: apply the insets again under it.
            root.requestApplyInsets();
            reportInsets();
            reportSystem();
            if (getIntent().getData() != null) nativeLink(getIntent().getData().toString());
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
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (active && intent.getData() != null) nativeLink(intent.getData().toString());
    }
    // Back asks the app first: an open modal closes, or a use_back handler takes it.
    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() {
        if (active && nativeBack() != 0) return;
        super.onBackPressed();
    }
    @Override protected void onResume() { super.onResume(); if (active) nativeActive(1); }
    @Override protected void onPause() { if (active) nativeActive(0); super.onPause(); }
    @Override protected void onDestroy() {
        if (active) { nativeStop(); active = false; }
        if (current == this) current = null;
        super.onDestroy();
    }
    public void schedule() {
        if (!pending && active) { pending = true; Choreographer.getInstance().postFrameCallback(frame); }
    }
    public void cancel() { Choreographer.getInstance().removeFrameCallback(frame); pending = false; }
    // An RGBA color from the core as Android's ARGB.
    private static int argb(int rgba) {
        return ((rgba & 0xff) << 24) | ((rgba >>> 24) << 16) | (((rgba >>> 16) & 0xff) << 8) | ((rgba >>> 8) & 0xff);
    }
    private GradientDrawable paintOf(int id, View view) {
        GradientDrawable paint = paints.get(id);
        if (paint == null) {
            paint = new GradientDrawable();
            paint.setColor(0);
            paints.put(id, paint);
            // setBackground resets the padding to the drawable's: keep the view's.
            int l = view.getPaddingLeft(), t = view.getPaddingTop(), r = view.getPaddingRight(), b = view.getPaddingBottom();
            view.setBackground(paint);
            view.setPadding(l, t, r, b);
            view.setClipToOutline(true);
        }
        return paint;
    }
    private void applyFont(int id, View view) {
        if (!(view instanceof TextView)) return;
        int[] font = fonts.getOrDefault(id, new int[] {0, 0});
        TextView text = (TextView)view;
        text.setTextSize(TypedValue.COMPLEX_UNIT_DIP, font[0] > 0 ? font[0] : 18);
        if (Build.VERSION.SDK_INT >= 28) text.setTypeface(Typeface.create(Typeface.DEFAULT, font[1] > 0 ? font[1] : 400, false));
    }
    // Int props (layout ones are applied to Yoga in host.cpp): the code in kind, the value in before.
    // Screens (kind 14) that have been on top: a first activation over another slides in, and
    // a removed top screen slides out.
    private final java.util.HashSet<Integer> shownScreens = new java.util.HashSet<>();
    private final HashMap<Integer, Boolean> activeScreens = new HashMap<>();
    // Screens sliding out: what was inside them stays until they go.
    private final java.util.HashSet<View> leaving = new java.util.HashSet<>();
    private boolean insideLeaving(View view) {
        for (android.view.ViewParent p = view.getParent(); p instanceof View; p = p.getParent()) if (leaving.contains(p)) return true;
        return false;
    }
    private static final int kSlide = 300;

    private void prop(int id, View view, int code, int value) {
        float d = density();
        if (code >= 32 && code <= 34 && view instanceof android.widget.SeekBar) {
            android.widget.SeekBar slider = (android.widget.SeekBar)view;
            int[] range = sliderRanges.get(id);
            int shown = slider.getProgress() + range[0];
            if (code == 33) range[0] = value;
            else if (code == 34) range[1] = value;
            else shown = value;
            slider.setMax(Math.max(0, range[1] - range[0]));
            slider.setProgress(shown - range[0]);
            return;
        }
        if (code == 41) { listenPan(id, view, value != 0); return; }
        if (code == 31) {
            // A pointer (a mouse, a stylus) entering and leaving the view.
            view.setOnHoverListener(value == 0 ? null : (v, e) -> {
                int action = e.getActionMasked();
                if (active && generations.containsKey(id) && (action == android.view.MotionEvent.ACTION_HOVER_ENTER ||
                    action == android.view.MotionEvent.ACTION_HOVER_EXIT)) {
                    nativeHover(id, generations.get(id), action == android.view.MotionEvent.ACTION_HOVER_ENTER ? 1 : 0);
                }
                return false;
            });
            return;
        }
        if (code == 23) {
            // A screen under the top is hidden, after a push's slide it stays under.
            activeScreens.put(id, value != 0);
            if (value != 0) {
                view.setVisibility(View.VISIBLE);
                if (shownScreens.add(id)) {
                    // Placed under its parent after this commit: the slide starts on the next frame.
                    view.post(() -> {
                        ViewGroup parent = (ViewGroup)view.getParent();
                        boolean over = false;
                        for (int i = 0; parent != null && i < parent.getChildCount(); i++) {
                            View other = parent.getChildAt(i);
                            if (other != view && other.getVisibility() == View.VISIBLE && shownScreens.contains(idOf(other))) over = true;
                        }
                        if (!over) return;
                        view.setTranslationX(root.getWidth());
                        view.animate().translationX(0).setDuration(kSlide).start();
                    });
                }
            } else if (shownScreens.contains(id)) {
                view.postDelayed(() -> { if (!activeScreens.getOrDefault(id, false)) view.setVisibility(View.GONE); }, kSlide);
            } else view.setVisibility(View.GONE);
            return;
        }
        if (code == 3) { if (value != 0) view.requestFocus(); else view.clearFocus(); }
        else if (code == 2 && (view instanceof EditText || (view instanceof Button && !(view instanceof Switch)))) {
            int p = Math.round(value * d);
            if (value > 0) view.setPadding(p, p, p, p);
        }
        else if (code == 8) {
            GradientDrawable paint = paintOf(id, view);
            int to = value == 0 ? 0 : argb(value);
            int[] motion = motionOf(id, view);
            android.content.res.ColorStateList from = paint.getColor();
            if (motion == null || from == null) paint.setColor(to);
            else {
                android.animation.ValueAnimator fade = android.animation.ValueAnimator.ofArgb(from.getDefaultColor(), to);
                fade.setDuration(motion[0]);
                fade.setInterpolator(interpolator(motion[1]));
                fade.addUpdateListener(a -> paint.setColor((int)a.getAnimatedValue()));
                fade.start();
            }
        }
        else if (code == 9 && view instanceof TextView) {
            if (value != 0) ((TextView)view).setTextColor(argb(value));
            // A field's placeholder: its text color, faded.
            if (value != 0 && view instanceof EditText) ((EditText)view).setHintTextColor((argb(value) & 0x00ffffff) | 0x80000000);
        } else if (code == 10 || code == 11) {
            int[] font = fonts.computeIfAbsent(id, k -> new int[] {0, 0});
            font[code - 10] = value;
            applyFont(id, view);
        } else if (code == 12 || code == 13) {
            int[] border = borders.computeIfAbsent(id, k -> new int[] {0, 0});
            border[code - 12] = value;
            paintOf(id, view).setStroke(Math.round(border[0] * d), border[1] == 0 ? 0xff000000 : argb(border[1]));
        } else if (code == 14) paintOf(id, view).setCornerRadius(value * d);
        else if (code == 15) {
            int[] motion = motionOf(id, view);
            if (motion == null) view.setAlpha(value / 100f);
            else view.animate().alpha(value / 100f).setDuration(motion[0]).setInterpolator(interpolator(motion[1])).start();
        }
        else if (code >= 35 && code <= 38) {
            transforms.computeIfAbsent(id, k -> new int[] {0, 0, 100, 0})[code - 35] = value;
            applyTransform(id, view);
        }
        else if (code == 39 || code == 40) motions.computeIfAbsent(id, k -> new int[] {0, 0})[code - 39] = value;
        else if (code == 20 || code == 21) applyStatusBar(code, value);
        else if (code == 22 && view instanceof ScrollView) refresh(id, (ScrollView)view, value != 0);
        else if (code == 18 && view instanceof Switch) {
            applying = true; ((Switch)view).setChecked(value != 0); applying = false;
        } else if (code == 19) view.setVisibility(value != 0 ? View.VISIBLE : View.INVISIBLE);
    }

    // Android's framework has no pull to refresh view (SwipeRefreshLayout is androidx): a
    // downward drag past 64 dp at the top of the scroll asks once per gesture; the indicator
    // overlaps the top of the content, as SwipeRefreshLayout's does.
    private void refresh(int id, ScrollView scroll, boolean on) {
        ProgressBar indicator = refreshers.get(id);
        if (indicator == null) {
            indicator = new ProgressBar(this);
            indicator.setIndeterminate(true);
            indicator.setElevation(8 * density());
            indicator.setVisibility(View.INVISIBLE);
            ((ViewGroup)scroll.getChildAt(0)).addView(indicator);
            refreshers.put(id, indicator);
            final float[] start = {0};
            final boolean[] armed = {false};
            scroll.setOnTouchListener((v, event) -> {
                int action = event.getActionMasked();
                if (action == android.view.MotionEvent.ACTION_DOWN) {
                    start[0] = event.getY();
                    armed[0] = scroll.getScrollY() == 0 && refreshers.get(id).getVisibility() != View.VISIBLE;
                } else if (action == android.view.MotionEvent.ACTION_MOVE && armed[0] && scroll.getScrollY() == 0
                           && event.getY() - start[0] > 64 * density()) {
                    armed[0] = false;
                    if (active && generations.containsKey(id)) nativeRefresh(id, generations.get(id));
                }
                return false;
            });
        }
        indicator.setVisibility(on ? View.VISIBLE : View.INVISIBLE);
        layoutRefresh(id, scroll);
    }
    private void layoutRefresh(int id, ScrollView scroll) {
        ProgressBar indicator = refreshers.get(id);
        if (indicator == null) return;
        int size = Math.round(40 * density());
        int left = (scroll.getWidth() - size) / 2, top = Math.round(8 * density());
        indicator.measure(View.MeasureSpec.makeMeasureSpec(size, View.MeasureSpec.EXACTLY),
                          View.MeasureSpec.makeMeasureSpec(size, View.MeasureSpec.EXACTLY));
        indicator.layout(left, top, left + size, top + size);
    }

    private void applyStatusBar(int code, int value) {
        if (Build.VERSION.SDK_INT < 30) return;
        WindowInsetsController bars = getWindow().getInsetsController();
        if (bars == null) return;
        if (code == 20) {
            // BarLight: light content (light icons); BarDark: dark icons; BarDefault: leave it.
            if (value == 1) bars.setSystemBarsAppearance(0, WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
            else if (value == 2) bars.setSystemBarsAppearance(WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS,
                                                              WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
        } else if (value != 0) bars.hide(WindowInsets.Type.statusBars());
        else bars.show(WindowInsets.Type.statusBars());
    }

    // Commands: 1 an alert (title, message, buttons; up to three, as Android shows them),
    // 2 open a URL.
    public void command(int request, int command, String[] strings) {
        if (command == 1 && strings.length >= 2) {
            final boolean[] answered = {false};
            java.util.function.IntConsumer answer = index -> {
                if (answered[0]) return;
                answered[0] = true;
                shownAlert = null; answerAlert = null;
                nativeAlert(request, index);
            };
            AlertDialog.Builder builder = new AlertDialog.Builder(this).setTitle(strings[0]).setMessage(strings[1]);
            int buttons = Math.min(3, strings.length - 2);
            if (buttons >= 1) builder.setPositiveButton(strings[2 + buttons - 1], (d, w) -> answer.accept(buttons - 1));
            if (buttons >= 2) builder.setNegativeButton(strings[2], (d, w) -> answer.accept(0));
            if (buttons == 3) builder.setNeutralButton(strings[3], (d, w) -> answer.accept(1));
            builder.setOnCancelListener(d -> answer.accept(-1));
            shownAlert = builder.show();
            answerAlert = answer;
        } else if (command == 2 && strings.length >= 1) {
            try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(strings[0]))); }
            catch (Exception error) { /* no app handles it */ }
        } else if (command == 4) {
            View focused = getCurrentFocus();
            android.view.inputmethod.InputMethodManager input =
                (android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
            if (input != null) input.hideSoftInputFromWindow(root.getWindowToken(), 0);
            if (focused != null) focused.clearFocus();
        } else if (command == 5 && strings.length >= 2) {
            String text = strings[1].isEmpty() ? strings[0] : (strings[0].isEmpty() ? strings[1] : strings[0] + "\n" + strings[1]);
            Intent send = new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, text);
            try { startActivity(Intent.createChooser(send, null)); } catch (Exception error) { /* nothing shares */ }
        } else if (command == 6 && strings.length >= 1) {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
            if (clipboard != null) clipboard.setPrimaryClip(android.content.ClipData.newPlainText("text", strings[0]));
        } else if (command == 7) {
            android.content.ClipboardManager clipboard = (android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
            CharSequence text = null;
            if (clipboard != null && clipboard.getPrimaryClip() != null && clipboard.getPrimaryClip().getItemCount() > 0) {
                text = clipboard.getPrimaryClip().getItemAt(0).coerceToText(this);
            }
            nativeAnswer(request, text == null ? "" : text.toString());
        } else if (command == 8 && strings.length >= 1) {
            // Selection, light, medium, heavy, success, warning, error.
            int[] feedback = {android.view.HapticFeedbackConstants.CLOCK_TICK, android.view.HapticFeedbackConstants.VIRTUAL_KEY,
                android.view.HapticFeedbackConstants.KEYBOARD_TAP, android.view.HapticFeedbackConstants.LONG_PRESS,
                Build.VERSION.SDK_INT >= 30 ? android.view.HapticFeedbackConstants.CONFIRM : android.view.HapticFeedbackConstants.VIRTUAL_KEY,
                Build.VERSION.SDK_INT >= 30 ? android.view.HapticFeedbackConstants.REJECT : android.view.HapticFeedbackConstants.LONG_PRESS,
                Build.VERSION.SDK_INT >= 30 ? android.view.HapticFeedbackConstants.REJECT : android.view.HapticFeedbackConstants.LONG_PRESS};
            int kind = Math.max(0, Math.min(6, Integer.parseInt(strings[0])));
            root.performHapticFeedback(feedback[kind]);
        } else if (command == 9 && strings.length >= 1) {
            root.announceForAccessibility(strings[0]);
        } else if (command == 10 && strings.length >= 6) {
            fetch(request, strings);
        }
    }
    public float screenWidth() { return findViewById(android.R.id.content).getWidth() / density(); }
    public float screenHeight() { return findViewById(android.R.id.content).getHeight() / density(); }

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
            } else if (kind == 8) {
                YogaGroup pressable = new YogaGroup();
                pressable.setClickable(true);
                pressable.setFocusable(true);
                pressable.setOnClickListener(v -> nativeEvent(id, generations.get(id)));
                view = pressable;
            } else if (kind == 15) {
                android.widget.SeekBar slider = new android.widget.SeekBar(this);
                sliderRanges.put(id, new int[] {0, 100});
                slider.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
                    @Override public void onProgressChanged(android.widget.SeekBar b, int progress, boolean user) {
                        if (user && active && generations.containsKey(id)) nativeValue(id, generations.get(id), progress + sliderRanges.get(id)[0]);
                    }
                    @Override public void onStartTrackingTouch(android.widget.SeekBar b) {}
                    @Override public void onStopTrackingTouch(android.widget.SeekBar b) {}
                });
                view = slider;
            } else if (kind == 9) {
                Switch toggle = new Switch(this);
                toggle.setOnCheckedChangeListener((b, on) -> {
                    if (!applying && active) nativeToggle(id, generations.get(id), on ? 1 : 0);
                });
                view = toggle;
            } else if (kind == 10) {
                ProgressBar spinner = new ProgressBar(this, null, android.R.attr.progressBarStyleSmall);
                spinner.setIndeterminate(true);
                view = spinner;
            } else if (kind == 11) {
                view = new View(this);
                view.setVisibility(View.GONE);
            } else if (kind == 12) {
                YogaGroup modal = new YogaGroup();
                modal.setBackgroundColor(0x66000000);
                modal.setClickable(true);
                // A tap on the backdrop (outside every child) asks it to close.
                modal.setOnTouchListener((v, e) -> {
                    if (e.getActionMasked() != android.view.MotionEvent.ACTION_UP || !active) return false;
                    ViewGroup group = (ViewGroup)v;
                    for (int i = 0; i < group.getChildCount(); i++) {
                        View child = group.getChildAt(i);
                        if (e.getX() >= child.getLeft() && e.getX() < child.getRight() && e.getY() >= child.getTop() &&
                            e.getY() < child.getBottom()) return false;
                    }
                    if (generations.containsKey(id)) nativeEvent(id, generations.get(id));
                    return false;
                });
                modalIds.add(id);
                view = modal;
            } else if (kind == 7) {
                ImageView image = new ImageView(this);
                image.setScaleType(ImageView.ScaleType.FIT_CENTER);
                view = image;
            } else if (kind == 6) {
                // A ScrollView holds one child: the group its children go into.
                ScrollView scroll = new ScrollView(this);
                scroll.addView(new YogaGroup());
                scroll.setOnScrollChangeListener((v, x, y, oldX, oldY) -> reportScroll(id, (ScrollView)v));
                view = scroll;
            } else if (kind == 13) {
                view = new FrameLayout(this);
            } else view = new YogaGroup();
            view.setId(View.generateViewId());
            if (view instanceof YogaGroup) ((YogaGroup)view).nodeId = id;
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
            if (kind == 4 && view instanceof FrameLayout) mountNative(id, (FrameLayout)view, text);
            else if (kind == 1) view.setContentDescription(text.isEmpty() ? null : text);
            else if (kind == 2 && view instanceof EditText) ((EditText)view).setHint(text);
            else if (kind == 3 && view instanceof ImageView) loadImage((ImageView)view, text);
        } else if (op == 2) prop(id, view, kind, before);
        else if (op == 4 || op == 5) {
            if (modalIds.contains(id)) {
                // Over everything, outside the parent's layout.
                if (view.getParent() == null) {
                    ((FrameLayout)findViewById(android.R.id.content)).addView(view,
                        new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
                }
                return;
            }
            ViewGroup group = nodes.containsKey(parent) ? (ViewGroup)nodes.get(parent) : root;
            if (group instanceof ScrollView) group = (ViewGroup)group.getChildAt(0);
            if (view.getParent() != null) ((ViewGroup)view.getParent()).removeView(view);
            group.addView(view, before == 0 ? group.getChildCount() : group.indexOfChild(nodes.get(before)));
        } else if (op == 6) {
            ViewGroup owner = (ViewGroup)view.getParent();
            boolean slide = false;
            if (owner != null && activeScreens.getOrDefault(id, false) && view.getVisibility() == View.VISIBLE) {
                for (int i = 0; i < owner.getChildCount(); i++) {
                    View other = owner.getChildAt(i);
                    if (other != view && shownScreens.contains(idOf(other))) slide = true;
                }
            }
            if (slide) {
                view.bringToFront();
                leaving.add(view);
                view.animate().translationX(root.getWidth()).setDuration(kSlide)
                    .withEndAction(() -> { owner.removeView(view); leaving.remove(view); }).start();
            } else if (owner != null && !insideLeaving(view)) owner.removeView(view);
            activeScreens.remove(id);
            nodes.remove(id); generations.remove(id); paints.remove(id); borders.remove(id); fonts.remove(id);
            modalIds.remove(id); refreshers.remove(id); nativeViews.remove(id); nativeTypes.remove(id);
            shownScreens.remove(id); sliderRanges.remove(id); transforms.remove(id); motions.remove(id); pans.remove(id);
            scrollViewports.remove(id);
        } else if (op == 7 || op == 8) {
            if (view instanceof ScrollView) { if (op == 7) scrollViewports.put(id, -1); else scrollViewports.remove(id); }
            else view.setEnabled(op == 7);
        }
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
    private native void nativeScrolled();
    private void reportScroll(int id, ScrollView view) {
        // What is inside moved in the window: listened nodes report their new positions.
        if (active) nativeScrolled();
        if (!active || !scrollViewports.containsKey(id)) return;
        float d = density();
        int viewport = Math.round(view.getHeight() / d);
        scrollViewports.put(id, viewport);
        nativeScroll(id, generations.get(id), Math.round(view.getScrollY() / d), viewport);
    }
    public void content(int id, float width, float height) {
        YogaGroup content = (YogaGroup)((ScrollView)nodes.get(id)).getChildAt(0);
        content.contentWidth = Math.round(width * density());
        content.contentHeight = Math.max(1, Math.round(height * density()));
        content.measure(View.MeasureSpec.makeMeasureSpec(content.contentWidth, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(content.contentHeight, View.MeasureSpec.EXACTLY));
        content.layout(0, 0, content.contentWidth, content.contentHeight);
        // A virtual list hears its viewport once laid out, and again when it changes size.
        ScrollView scroll = (ScrollView)nodes.get(id);
        layoutRefresh(id, scroll);
        Integer reported = scrollViewports.get(id);
        if (reported != null && reported != Math.round(scroll.getHeight() / density())) reportScroll(id, scroll);
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
    private void runConformance() {
        View suite = null;
        for (View view : nodes.values()) if ("Suite".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) suite = view;
        int[] base = new int[2];
        suite.getLocationInWindow(base);
        StringBuilder frames = new StringBuilder(), roles = new StringBuilder();
        Button tap = null;
        EditText field = null;
        for (View view : nodes.values()) {
            String label = view.getContentDescription() != null ? view.getContentDescription().toString()
                : view instanceof Button ? ((Button)view).getText().toString() : "";
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
        // What was drawn: colors as rrggbbaa, sizes in points.
        View painted = null, press = null;
        TextView title = null;
        Switch toggle = null;
        android.widget.SeekBar volume = null;
        View moved = null;
        for (View view : nodes.values()) {
            if ("Volume".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) volume = (android.widget.SeekBar)view;
            if ("Moved".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) moved = view;
            String label = view.getContentDescription() == null ? "" : view.getContentDescription().toString();
            if (label.equals("Painted")) painted = view;
            if (label.equals("Title")) title = (TextView)view;
            if (label.equals("Press")) press = view;
            if (label.equals("Toggle")) toggle = (Switch)view;
        }
        float d = density();
        GradientDrawable paint = paints.get(idOf(painted));
        int[] border = borders.get(idOf(painted));
        String styles = "{\"Painted\":{\"background\":\"" + hex(paint.getColor().getDefaultColor()) +
            "\",\"radius\":" + round1(paint.getCornerRadius() / d) + ",\"border\":" + border[0] +
            ",\"borderColor\":\"" + hex(argb(border[1])) + "\",\"opacity\":" + round1(painted.getAlpha()) +
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
        final String result = "{\"status\":\"DONE\",\"session\":\"" + getIntent().getStringExtra("session") +
            "\",\"frames\":{" + frames + "},\"roles\":{" + roles + "},\"styles\":" + styles;
        final View suiteView = suite;
        final String[] modal = {"null"};
        final boolean[] refreshSeen = {false, false};
        steps(300,
            // The alert: answer its second button.
            () -> findButton("Ask").performClick(),
            () -> { if (shownAlert != null) { shownAlert.dismiss(); answerAlert.accept(1); } findButton("Open sheet").performClick(); },
            // The modal: measure it, close it from inside.
            () -> { modal[0] = modalReport(); findButton("Close").performClick(); },
            // Back with the modal open closes it (Android only).
            () -> findButton("Open sheet").performClick(),
            this::onBackPressed,
            // Pull the feed at its top with real touches; "Loaded" hides the indicator.
            () -> {
                View feed = labeled("Feed");
                long now = android.os.SystemClock.uptimeMillis();
                float x = feed.getWidth() / 2f, y = 20 * density();
                feed.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now, android.view.MotionEvent.ACTION_DOWN, x, y, 0));
                for (int i = 1; i <= 5; i++) {
                    feed.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + i * 16,
                        android.view.MotionEvent.ACTION_MOVE, x, y + i * 30 * density(), 0));
                }
                feed.dispatchTouchEvent(android.view.MotionEvent.obtain(now, now + 100, android.view.MotionEvent.ACTION_UP,
                    x, y + 150 * density(), 0));
            },
            () -> { refreshSeen[0] = refreshShown(); findButton("Loaded").performClick(); },
            () -> refreshSeen[1] = refreshShown(),
            // Scroll the feed: rows near 2000 points mount, rows from the top go.
            () -> {
                for (View view : nodes.values()) {
                    if ("Feed".contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) {
                        view.scrollTo(0, Math.round(2000 * density()));
                    }
                }
            },
            () -> {},
            () -> {
                float dd = density();
                int[] origin = new int[2];
                suiteView.getLocationInWindow(origin);
                StringBuilder scrolled = new StringBuilder();
                for (View view : nodes.values()) {
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
                    ",\"log\":\"" + nativeConformanceLog() + "\"}";
                try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "conformance.json"))) {
                    out.write(json.getBytes(StandardCharsets.UTF_8));
                } catch (Exception error) { throw new RuntimeException(error); }
            });
    }

    private View labeled(String label) {
        for (View view : nodes.values()) {
            if (label.contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) return view;
        }
        return null;
    }
    private boolean refreshShown() {
        for (java.util.Map.Entry<Integer, ProgressBar> entry : refreshers.entrySet()) {
            if (entry.getValue().getVisibility() == View.VISIBLE) return true;
        }
        return false;
    }

    // Run each step `delay` ms after the previous one.
    private void steps(int delay, Runnable... list) { step(0, delay, list); }
    private void step(int index, int delay, Runnable[] list) {
        if (index >= list.length) return;
        root.postDelayed(() -> { list[index].run(); step(index + 1, delay, list); }, delay);
    }

    // The open modal's layer and its sheet, in points in the window, and the window size.
    private String modalReport() {
        View layer = null, sheet = null;
        for (java.util.Map.Entry<Integer, View> entry : nodes.entrySet()) {
            if (modalIds.contains(entry.getKey())) layer = entry.getValue();
            if ("Sheet".contentEquals(entry.getValue().getContentDescription() == null ? "" : entry.getValue().getContentDescription())) sheet = entry.getValue();
        }
        if (layer == null || sheet == null) return "null";
        float d = density();
        int[] a = new int[2], b = new int[2];
        layer.getLocationInWindow(a);
        sheet.getLocationInWindow(b);
        View window = getWindow().getDecorView();
        return "{\"cover\":[" + round1(a[0] / d) + "," + round1(a[1] / d) + "," + round1(layer.getWidth() / d) + "," +
            round1(layer.getHeight() / d) + "],\"window\":[" + round1(window.getWidth() / d) + "," +
            round1(window.getHeight() / d) + "],\"sheet\":[" + round1(b[0] / d) + "," + round1(b[1] / d) + "," +
            round1(sheet.getWidth() / d) + "," + round1(sheet.getHeight() / d) + "]}";
    }
    private static float round1(float value) { return Math.round(value * 10) / 10f; }
    private static String hex(int argb) {
        return String.format("%02x%02x%02x%02x", (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, (argb >>> 24));
    }
    private int idOf(View view) {
        for (java.util.Map.Entry<Integer, View> entry : nodes.entrySet()) if (entry.getValue() == view) return entry.getKey();
        return -1;
    }

    private void writeResult() {
        String result = "{\"status\":\"PASS\",\"session\":\"" + getIntent().getStringExtra("session") + "\",\"events\":100,\"commits\":" + commits +
            ",\"frames\":" + frames + ",\"frameWorkMs\":" + lastFrameMs + ",\"todo\":\"PASS\",\"renderer\":\"android.view + JNI + Yoga\"}";
        try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "selftest.json"))) {
            out.write(result.getBytes(StandardCharsets.UTF_8));
        } catch (Exception error) { throw new RuntimeException(error); }
    }
}
