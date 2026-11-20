package org.eskiu.ui;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import android.widget.FrameLayout;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.Choreographer;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import java.util.HashMap;

// Native Android views. The same Eskiu components drive this and the other hosts.
// The activity is the lifecycle and the JNI surface host.cpp binds by name: the native
// methods and the methods it calls. The work is in the package's other classes: the view
// tree (UiTree), props (UiProps), commands (UiCommands), the driver (UiDriver) and the
// test harness's runs (UiSelfTest).
public final class UiActivity extends Activity {
    static { System.loadLibrary("eskiu_ui"); }
    native void nativeStart(int example);
    native void nativeEvent(int id, int generation);
    native void nativeText(int id, int generation, String text);
    native void nativeSubmit(int id, int generation, String text);
    native void nativeFocus(int id, int generation, int focused);
    native void nativeTick();
    native void nativeLayout(float width, float height);
    native void nativeStop();
    native int nativeReload(String path);
    native void nativeKeepState();
    native int nativeEdgeToEdge();
    native void nativeInsets(int top, int right, int bottom, int left, int keyboard);
    native void nativeActive(int active);
    native void nativeSystem(int dark, int screenReader, int reduceMotion, int fontScale, String locale, int rtl);
    native String nativeConformanceLog();
    native void nativeToggle(int id, int generation, int on);
    native void nativeScroll(int id, int generation, int offset, int viewport);
    native void nativeAlert(int request, int choice);
    native void nativeRefresh(int id, int generation);
    native int nativeBack();
    native void nativeLink(String url);
    native void nativeWindow(int width, int height, int scale);
    native void nativeModuleEvent(String channel, String payload);
    native void nativeTimer(int token);
    native void nativeFrame(double time);
    native void nativeHover(int id, int generation, int inside);
    native void nativeLongPress(int id, int generation);
    native void nativePan(int id, int generation, int phase, int dx, int dy, int vx, int vy);
    native void nativeValue(int id, int generation, int value);
    native void nativeAnswer(int request, String text);
    native void nativeDataDir(String path);
    native void nativeFetch(int request, int status, String body, String headers);
    native void nativeViewEvent(int id, int generation, String name, String payload);
    native void nativeScrolled();

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
        // Text sizes in SP follow the system's text size: each text view's is set again, and
        // the core lays the tree out again (nativeSystem) when the scale changed.
        if (config.fontScale != fontScale) {
            props.rescaleFonts(nodes, config.fontScale / fontScale);
            fontScale = config.fontScale;
        }
        reportSystem();
    }

    final UiProps props = new UiProps(this);
    final UiAccess access = new UiAccess();
    final UiInput input = new UiInput(this);
    final UiTree tree = new UiTree(this);
    private float fontScale = 1f;
    final UiCommands commands = new UiCommands(this);
    private final UiDriver driver = new UiDriver(this);
    private final UiSelfTest selfTest = new UiSelfTest(this);

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
    // The display clock for use_frame: a frame callback that posts itself again while it is on.
    private boolean clockOn;
    private final Choreographer.FrameCallback clock = new Choreographer.FrameCallback() {
        @Override public void doFrame(long time) {
            if (!clockOn) return;
            Choreographer.getInstance().postFrameCallback(this);
            if (active) nativeFrame(time / 1e9);
        }
    };
    public void frames(boolean on) {
        if (on == clockOn) return;
        clockOn = on;
        if (on) Choreographer.getInstance().postFrameCallback(clock);
        else Choreographer.getInstance().removeFrameCallback(clock);
    }

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
    static final Handler mainThread = new Handler(Looper.getMainLooper());
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
    // Another activity for its result (a picker, the camera app), for native code: `done` gets
    // the result code and the data intent (null when there is none), on the UI thread.
    public interface ActivityResult { void accept(int code, Intent data); }
    private final HashMap<Integer, ActivityResult> resultRequests = new HashMap<>();
    private int nextResultRequest = 1;
    public static void startForResult(Intent intent, ActivityResult done) {
        mainThread.post(() -> {
            UiActivity activity = current;
            if (activity == null) { done.accept(RESULT_CANCELED, null); return; }
            int request = 0x4000 + activity.nextResultRequest++;
            activity.resultRequests.put(request, done);
            try {
                activity.startActivityForResult(intent, request);
            } catch (android.content.ActivityNotFoundException error) {
                activity.resultRequests.remove(request);
                done.accept(RESULT_CANCELED, null);
            }
        });
    }
    @Override protected void onActivityResult(int request, int code, Intent data) {
        super.onActivityResult(request, code, data);
        ActivityResult done = resultRequests.remove(request);
        if (done != null) done.accept(code, data);
    }
    // The intents that open the activity, for native code: `listener` gets the one it started
    // with (when the app starts, after the native setup) and each one after it.
    private static final java.util.ArrayList<java.util.function.Consumer<Intent>> intentListeners = new java.util.ArrayList<>();
    public static void onIntent(java.util.function.Consumer<Intent> listener) {
        intentListeners.add(listener);
        if (current != null && current.active) listener.accept(current.getIntent());
    }
    private void deliverIntent(Intent intent) {
        for (java.util.function.Consumer<Intent> listener : new java.util.ArrayList<>(intentListeners)) listener.accept(intent);
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        java.util.function.Consumer<Boolean> done = permissionRequests.remove(request);
        if (done == null) return;
        boolean all = results.length > 0;
        for (int result : results) if (result != PackageManager.PERMISSION_GRANTED) all = false;
        done.accept(all);
    }
    final HashMap<Integer, View> nativeViews = new HashMap<>();
    final HashMap<Integer, String> nativeTypes = new HashMap<>();
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
    void mountNative(int id, FrameLayout box, String type) {
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
    // Per node: its view and the generation its events carry.
    final HashMap<Integer, View> nodes = new HashMap<>();
    final HashMap<Integer, Integer> generations = new HashMap<>();
    UiYogaGroup root;
    boolean active;
    private boolean pending;
    private boolean testing;
    // The insets last applied, reported again when the app starts.
    private int[] lastInsets;
    private void reportInsets() {
        if (lastInsets != null) nativeInsets(lastInsets[0], lastInsets[1], lastInsets[2], lastInsets[3], lastInsets[4]);
    }
    // Set while a commit writes an EditText, so the change does not echo back as an edit.
    boolean applying;
    int commits;
    int frames;
    double lastFrameMs;
    private final Choreographer.FrameCallback frame = time -> {
        pending = false;
        if (!active) return;
        long start = System.nanoTime();
        nativeTick();
        lastFrameMs = (System.nanoTime() - start) / 1e6;
        frames++;
    };

    float density() { return getResources().getDisplayMetrics().density; }
    static void require(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    int idOf(View view) {
        for (java.util.Map.Entry<Integer, View> entry : nodes.entrySet()) if (entry.getValue() == view) return entry.getKey();
        return -1;
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        fontScale = getResources().getConfiguration().fontScale;
        // A reload build (ui serve) loads the app's code from the library ui-reload names,
        // before anything here calls into it.
        String core = reloadPath();
        if (core != null) nativeReload(core);
        root = new UiYogaGroup(this);
        setContentView(root);
        showSplash();
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
            deliverIntent(getIntent());
            if (conformance) root.postDelayed(selfTest::runConformance, 400);
            if (testing) selfTest.start();
        });
    }
    // `ui serve`: files/ui-reload holds the path of the app's newest library (the tool writes
    // it through run-as), null when there is none.
    private String reloadPath() {
        java.io.File file = new java.io.File(getFilesDir(), "ui-reload");
        if (!file.exists()) return null;
        try (java.io.FileInputStream in = new java.io.FileInputStream(file)) {
            byte[] bytes = new byte[(int)file.length()];
            int got = 0;
            while (got < bytes.length) {
                int n = in.read(bytes, got, bytes.length - got);
                if (n < 0) break;
                got += n;
            }
            return new String(bytes, 0, got, "UTF-8").trim();
        } catch (java.io.IOException e) {
            return null;
        }
    }
    // The driver's host side (UiDriver), called from the native code of a dev build only.
    public String driverTap(int id) { return driver.tap(id); }
    public String driverType(int id, String text) { return driver.type(id, text); }
    public String driverScroll(int id, int dy) { return driver.scroll(id, dy); }
    public String driverLayout() { return driver.layout(); }
    public void driverClock(boolean owned) { driver.clock(owned); }
    public long driverMemory() { return driver.memory(); }
    // A new build of the app's code (an intent with ui_reload from `ui serve`): the old core
    // keeps its routes and signals and stops, the views, timers and per-node state it left go,
    // and the new one starts on what the old one kept.
    private void restartCore(String path) {
        if (active) { nativeKeepState(); nativeStop(); active = false; }
        driverClock(false);
        cancel();
        for (Runnable tick : timers.values()) mainThread.removeCallbacks(tick);
        timers.clear();
        frames(false);
        if (commands.shownAlert != null) { commands.shownAlert.dismiss(); commands.shownAlert = null; }
        for (View view : nodes.values()) {
            if (view.getParent() instanceof ViewGroup) ((ViewGroup)view.getParent()).removeView(view);
        }
        root.removeAllViews();
        nodes.clear(); generations.clear(); props.clear(); access.clear(); input.clear();
        nativeViews.clear(); nativeTypes.clear(); tree.scrollViewports.clear(); tree.modalIds.clear();
        if (nativeReload(path) == 0) return;
        active = true;
        nativeDataDir(getFilesDir().getPath());
        nativeStart(0);
        root.requestApplyInsets();
        reportInsets();
        reportSystem();
        root.requestLayout();
        android.util.Log.i("ui", "restarted on " + new java.io.File(path).getName());
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String reload = intent.getStringExtra("ui_reload");
        if (reload != null) { restartCore(reload); return; }
        setIntent(intent);
        if (active) deliverIntent(intent);
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

    // A commit's operations, Yoga's measure and place, and a scroll view's content (UiTree).
    public void apply(int op, int id, int generation, int parent, int before, int kind, String text) {
        tree.apply(op, id, generation, parent, before, kind, text);
    }
    public long measure(int id, float width, int wm, float height, int hm) { return tree.measure(id, width, wm, height, hm); }
    public void place(int id, float x, float y, float width, float height) { tree.place(id, x, y, width, height); }
    public void content(int id, float width, float height) { tree.content(id, width, height); }
    public void command(int request, int command, String[] strings) { commands.command(request, command, strings); }
    public float screenWidth() { return findViewById(android.R.id.content).getWidth() / density(); }
    public float screenHeight() { return findViewById(android.R.id.content).getHeight() / density(); }
    public float width() { return (root.getWidth() - root.getPaddingLeft() - root.getPaddingRight()) / density(); }
    public float height() { return (root.getHeight() - root.getPaddingTop() - root.getPaddingBottom()) / density(); }
    public float scale() { return density(); }
    // ui_asset_path: an asset copied out of the APK into the cache, once per install of the app
    // (the copy is made again when the app was updated since); null when there is no such asset.
    public String assetPath(String name) {
        java.io.File file = new java.io.File(new java.io.File(getCacheDir(), "ui-assets"), name);
        long installed = 0;
        try { installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime; }
        catch (PackageManager.NameNotFoundException e) {}
        if (file.isFile() && file.lastModified() >= installed) return file.getPath();
        java.io.File partial = new java.io.File(file.getPath() + ".part");
        try (java.io.InputStream in = getAssets().open(name)) {
            file.getParentFile().mkdirs();
            try (java.io.OutputStream out = new java.io.FileOutputStream(partial)) {
                byte[] buffer = new byte[65536];
                for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
            }
            if (!partial.renameTo(file)) return null;
            return file.getPath();
        } catch (java.io.IOException e) {
            partial.delete();
            return null;
        }
    }
    public void committed() {
        commits++; root.invalidate();
        if (splash != null && !splashHeld) hideSplash();
    }
    // The splash (ui.toml [splash]: the activity's org.eskiu.ui.splash color and the assets'
    // ui-splash.png) over the app until its first commit, or until ui_splash_hide() when the app
    // held it. The theme's window background shows the same before the first frame.
    FrameLayout splash;
    boolean splashHeld;
    private void showSplash() {
        // aapt keeps a "#rrggbb" value as a color (an Integer), not a string.
        Object color = null;
        try {
            ActivityInfo info = getPackageManager().getActivityInfo(getComponentName(), PackageManager.GET_META_DATA);
            color = info.metaData == null ? null : info.metaData.get("org.eskiu.ui.splash");
        } catch (PackageManager.NameNotFoundException e) {}
        splash = new FrameLayout(this);
        if (color instanceof Integer) splash.setBackgroundColor((Integer)color);
        else if (color instanceof String) splash.setBackgroundColor(android.graphics.Color.parseColor((String)color));
        else {
            android.content.res.TypedArray a = getTheme().obtainStyledAttributes(new int[] {android.R.attr.colorBackground});
            splash.setBackgroundColor(a.getColor(0, 0xffffffff));
            a.recycle();
        }
        try (java.io.InputStream in = getAssets().open("ui-splash.png")) {
            // A third of its pixels in dp, as the launch screen's xxhdpi bitmap.
            android.graphics.BitmapFactory.Options options = new android.graphics.BitmapFactory.Options();
            options.inDensity = android.util.DisplayMetrics.DENSITY_XXHIGH;
            options.inTargetDensity = getResources().getDisplayMetrics().densityDpi;
            ImageView image = new ImageView(this);
            image.setImageBitmap(android.graphics.BitmapFactory.decodeStream(in, null, options));
            splash.addView(image, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT, android.view.Gravity.CENTER));
        } catch (java.io.IOException e) {}
        ((FrameLayout)findViewById(android.R.id.content)).addView(splash,
            new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }
    void hideSplash() {
        FrameLayout view = splash;
        splash = null;
        splashHeld = false;
        if (view == null) return;
        // The theme's splash window background goes back to the platform's.
        android.content.res.TypedArray a = getTheme().obtainStyledAttributes(new int[] {android.R.attr.colorBackground});
        getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(a.getColor(0, 0xffffffff)));
        a.recycle();
        view.animate().alpha(0).setDuration(200).withEndAction(() -> {
            if (view.getParent() != null) ((ViewGroup)view.getParent()).removeView(view);
        });
    }
}
