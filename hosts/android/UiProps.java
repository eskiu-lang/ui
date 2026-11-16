package org.eskiu.ui;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.util.HashMap;

// Int props on the views (layout ones are applied to Yoga in host.cpp), and what they need per
// node: paint, fonts, transforms and transitions, screens, pull to refresh, images.
final class UiProps {
    private final UiActivity activity;
    UiProps(UiActivity activity) { this.activity = activity; }

    // Pull to refresh per scroll: its indicator, over the top of the content.
    final HashMap<Integer, ProgressBar> refreshers = new HashMap<>();
    final HashMap<Integer, UiPanTracker> pans = new HashMap<>();
    // A slider's range: SeekBar counts from 0, so its progress is the value minus the minimum.
    final HashMap<Integer, int[]> sliderRanges = new HashMap<>();
    // Each node's transforms (translate x, y in points, scale in percent, rotate in degrees)
    // and transition (ms, Easing).
    private final HashMap<Integer, int[]> transforms = new HashMap<>();
    private final HashMap<Integer, int[]> motions = new HashMap<>();
    // Per view: the drawable that paints background, border and radius, and the font.
    final HashMap<Integer, GradientDrawable> paints = new HashMap<>();
    final HashMap<Integer, int[]> borders = new HashMap<>();
    private final HashMap<Integer, int[]> fonts = new HashMap<>();
    // The nodes with a Style.blur, and the translucent fill that stands in for it.
    private final java.util.HashSet<Integer> blurs = new java.util.HashSet<>();
    // Background gradients (Style.gradient) by node: from, to (RGBA) and the angle.
    private final HashMap<Integer, int[]> gradients = new HashMap<>();
    // The last background color (RGBA) by node, a gradient's start when one comes later.
    private final HashMap<Integer, Integer> backgrounds = new HashMap<>();
    // Style.font per node, and each font file loaded once (null: it did not load).
    private final HashMap<Integer, String> fontFiles = new HashMap<>();
    private final HashMap<String, Typeface> typefaces = new HashMap<>();
    // Screens (kind 14) that have been on top: a first activation over another slides in, and
    // a removed top screen slides out.
    final java.util.HashSet<Integer> shownScreens = new java.util.HashSet<>();
    final HashMap<Integer, Boolean> activeScreens = new HashMap<>();
    // Screens sliding out: what was inside them stays until they go.
    final java.util.HashSet<View> leaving = new java.util.HashSet<>();
    static final int kSlide = 300;
    // Images scaled without smoothing (Style.pixelated): their drawables do not filter.
    private final java.util.Set<View> pixelated = java.util.Collections.newSetFromMap(new java.util.WeakHashMap<>());

    // A removed node's state.
    void forget(int id) {
        activeScreens.remove(id);
        paints.remove(id); borders.remove(id); fonts.remove(id); fontFiles.remove(id); blurs.remove(id); gradients.remove(id); backgrounds.remove(id);
        refreshers.remove(id);
        shownScreens.remove(id); sliderRanges.remove(id); transforms.remove(id); motions.remove(id); pans.remove(id);
    }
    // Every node's state, when a new build of the app's code starts.
    void clear() {
        paints.clear(); borders.clear(); fonts.clear(); fontFiles.clear(); blurs.clear(); gradients.clear(); backgrounds.clear();
        pans.clear(); sliderRanges.clear(); transforms.clear();
        motions.clear(); refreshers.clear(); activeScreens.clear(); leaving.clear();
    }

    boolean insideLeaving(View view) {
        for (android.view.ViewParent p = view.getParent(); p instanceof View; p = p.getParent()) if (leaving.contains(p)) return true;
        return false;
    }
    private void listenPan(int id, View view, boolean on) {
        if (!on) { pans.remove(id); view.setOnTouchListener(null); return; }
        UiPanTracker tracker = new UiPanTracker(activity, id);
        pans.put(id, tracker);
        view.setOnTouchListener(tracker::touch);
    }
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
        float d = activity.density();
        int[] motion = motionOf(id, view);
        if (motion == null) {
            view.setTranslationX(t[0] * d); view.setTranslationY(t[1] * d);
            view.setScaleX(t[2] / 100f); view.setScaleY(t[2] / 100f); view.setRotation(t[3]);
            return;
        }
        view.animate().translationX(t[0] * d).translationY(t[1] * d).scaleX(t[2] / 100f).scaleY(t[2] / 100f)
            .rotation(t[3]).setDuration(motion[0]).setInterpolator(interpolator(motion[1])).start();
    }
    // An RGBA color from the core as Android's ARGB.
    static int argb(int rgba) {
        return ((rgba & 0xff) << 24) | ((rgba >>> 24) << 16) | (((rgba >>> 16) & 0xff) << 8) | ((rgba >>> 8) & 0xff);
    }
    private int material() {
        boolean dark = (activity.getResources().getConfiguration().uiMode & android.content.res.Configuration.UI_MODE_NIGHT_MASK)
            == android.content.res.Configuration.UI_MODE_NIGHT_YES;
        return dark ? 0xd91c1c1e : 0xd9f2f2f7;
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
    private Typeface typeface(String file) {
        if (typefaces.containsKey(file)) return typefaces.get(file);
        Typeface face = null;
        try {
            face = file.startsWith("/") ? Typeface.createFromFile(file) : Typeface.createFromAsset(activity.getAssets(), file);
        } catch (RuntimeException e) {
            android.util.Log.w("ui", "font " + file + " did not load");
        }
        typefaces.put(file, face);
        return face;
    }
    // The string prop for a font file ("" for none).
    void fontFile(int id, View view, String file) {
        if (file.isEmpty()) fontFiles.remove(id); else fontFiles.put(id, file);
        applyFont(id, view);
    }
    private void applyFont(int id, View view) {
        if (!(view instanceof TextView)) return;
        int[] font = fonts.getOrDefault(id, new int[] {0, 0});
        TextView text = (TextView)view;
        text.setTextSize(TypedValue.COMPLEX_UNIT_DIP, font[0] > 0 ? font[0] : 18);
        String file = fontFiles.get(id);
        Typeface custom = file != null ? typeface(file) : null;
        if (custom != null) text.setTypeface(custom);
        else if (Build.VERSION.SDK_INT >= 28) text.setTypeface(Typeface.create(Typeface.DEFAULT, font[1] > 0 ? font[1] : 400, false));
        else text.setTypeface(Typeface.DEFAULT);
    }

    // An int prop: the code in kind, the value in before.
    void prop(int id, View view, int code, int value) {
        float d = activity.density();
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
        if (code == 49) {
            // A long press; consumed, so it is not also a click.
            view.setOnLongClickListener(value == 0 ? null : v -> {
                if (activity.active && activity.generations.containsKey(id)) activity.nativeLongPress(id, activity.generations.get(id));
                return true;
            });
            if (value == 0) view.setLongClickable(false);
            return;
        }
        if (code == 31) {
            // A pointer (a mouse, a stylus) entering and leaving the view.
            view.setOnHoverListener(value == 0 ? null : (v, e) -> {
                int action = e.getActionMasked();
                if (activity.active && activity.generations.containsKey(id) && (action == android.view.MotionEvent.ACTION_HOVER_ENTER ||
                    action == android.view.MotionEvent.ACTION_HOVER_EXIT)) {
                    activity.nativeHover(id, activity.generations.get(id), action == android.view.MotionEvent.ACTION_HOVER_ENTER ? 1 : 0);
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
                            if (other != view && other.getVisibility() == View.VISIBLE && shownScreens.contains(activity.idOf(other))) over = true;
                        }
                        if (!over) return;
                        view.setTranslationX(activity.root.getWidth());
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
        else if (code == 50 || code == 51 || (code == 8 && gradients.containsKey(id))) {
            int[] g = gradients.computeIfAbsent(id, k -> new int[] {backgrounds.getOrDefault(id, 0), 0, 180});
            if (code == 8) g[0] = value; else if (code == 50) g[1] = value; else g[2] = value;
            GradientDrawable paint = paintOf(id, view);
            if (g[1] == 0) { paint.setColors(null); paint.setColor(g[0] == 0 ? 0 : argb(g[0])); }
            else {
                // GradientDrawable turns in steps of 45 degrees: the nearest.
                GradientDrawable.Orientation[] ways = {GradientDrawable.Orientation.BOTTOM_TOP, GradientDrawable.Orientation.BL_TR,
                    GradientDrawable.Orientation.LEFT_RIGHT, GradientDrawable.Orientation.TL_BR, GradientDrawable.Orientation.TOP_BOTTOM,
                    GradientDrawable.Orientation.TR_BL, GradientDrawable.Orientation.RIGHT_LEFT, GradientDrawable.Orientation.BR_TL};
                paint.setOrientation(ways[Math.floorMod(Math.round(g[2] / 45f), 8)]);
                paint.setColors(new int[] {g[0] == 0 ? 0 : argb(g[0]), argb(g[1])});
            }
        }
        else if (code == 52) {
            // Style.blur: Android cannot blur what is behind a view; a translucent material
            // stands in for it where the element has no background of its own.
            if (value > 0) blurs.add(id); else blurs.remove(id);
            if (!gradients.containsKey(id) && backgrounds.getOrDefault(id, 0) == 0) paintOf(id, view).setColor(value > 0 ? material() : 0);
        }
        else if (code == 8) {
            backgrounds.put(id, value);
            GradientDrawable paint = paintOf(id, view);
            int to = value == 0 ? (blurs.contains(id) ? material() : 0) : argb(value);
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
        // Elevation casts the platform's shadow from the outline of the view's background.
        else if (code == 46) view.setElevation(value * d);
        else if (code == 47 && view instanceof ImageView)
            ((ImageView)view).setScaleType(value != 0 ? ImageView.ScaleType.CENTER_CROP : ImageView.ScaleType.FIT_CENTER);
        else if (code == 48 && view instanceof ImageView) {
            if (value != 0) pixelated.add(view); else pixelated.remove(view);
            filterImage((ImageView)view);
        }
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
            activity.applying = true; ((Switch)view).setChecked(value != 0); activity.applying = false;
        } else if (code == 19) view.setVisibility(value != 0 ? View.VISIBLE : View.INVISIBLE);
    }

    // Android's framework has no pull to refresh view (SwipeRefreshLayout is androidx): a
    // downward drag past 64 dp at the top of the scroll asks once per gesture; the indicator
    // overlaps the top of the content, as SwipeRefreshLayout's does.
    private void refresh(int id, ScrollView scroll, boolean on) {
        ProgressBar indicator = refreshers.get(id);
        if (indicator == null) {
            indicator = new ProgressBar(activity);
            indicator.setIndeterminate(true);
            indicator.setElevation(8 * activity.density());
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
                           && event.getY() - start[0] > 64 * activity.density()) {
                    armed[0] = false;
                    if (activity.active && activity.generations.containsKey(id)) activity.nativeRefresh(id, activity.generations.get(id));
                }
                return false;
            });
        }
        indicator.setVisibility(on ? View.VISIBLE : View.INVISIBLE);
        layoutRefresh(id, scroll);
    }
    void layoutRefresh(int id, ScrollView scroll) {
        ProgressBar indicator = refreshers.get(id);
        if (indicator == null) return;
        int size = Math.round(40 * activity.density());
        int left = (scroll.getWidth() - size) / 2, top = Math.round(8 * activity.density());
        indicator.measure(View.MeasureSpec.makeMeasureSpec(size, View.MeasureSpec.EXACTLY),
                          View.MeasureSpec.makeMeasureSpec(size, View.MeasureSpec.EXACTLY));
        indicator.layout(left, top, left + size, top + size);
    }

    private void applyStatusBar(int code, int value) {
        if (Build.VERSION.SDK_INT < 30) return;
        WindowInsetsController bars = activity.getWindow().getInsetsController();
        if (bars == null) return;
        if (code == 20) {
            // BarLight: light content (light icons); BarDark: dark icons; BarDefault: leave it.
            if (value == 1) bars.setSystemBarsAppearance(0, WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
            else if (value == 2) bars.setSystemBarsAppearance(WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS,
                                                              WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
        } else if (value != 0) bars.hide(WindowInsets.Type.statusBars());
        else bars.show(WindowInsets.Type.statusBars());
    }

    // A file in the APK's assets, a file by its absolute path (or a file:// URL), or an http(s)
    // URL fetched off the UI thread.
    private void filterImage(ImageView view) {
        android.graphics.drawable.Drawable drawable = view.getDrawable();
        if (drawable != null) { drawable.setFilterBitmap(!pixelated.contains(view)); view.invalidate(); }
    }
    private void showImage(ImageView view, Bitmap bitmap) {
        view.setImageBitmap(bitmap);
        filterImage(view);
    }
    void loadImage(ImageView view, String source) {
        view.setImageDrawable(null);
        if (source.isEmpty()) return;
        if (source.startsWith("/") || source.startsWith("file://")) {
            String path = source.startsWith("file://") ? source.substring(7) : source;
            new Thread(() -> {
                Bitmap bitmap = BitmapFactory.decodeFile(path);
                activity.runOnUiThread(() -> showImage(view, bitmap));
            }).start();
            return;
        }
        if (source.startsWith("http://") || source.startsWith("https://")) {
            new Thread(() -> {
                try (java.io.InputStream in = new java.net.URL(source).openStream()) {
                    Bitmap bitmap = BitmapFactory.decodeStream(in);
                    activity.runOnUiThread(() -> showImage(view, bitmap));
                } catch (Exception error) { /* a failed load leaves the view empty */ }
            }).start();
            return;
        }
        try (java.io.InputStream in = activity.getAssets().open(source)) {
            showImage(view, BitmapFactory.decodeStream(in));
        } catch (Exception error) { /* a missing asset leaves the view empty */ }
    }
}
