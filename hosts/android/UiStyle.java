package org.eskiu.ui;

import android.animation.ValueAnimator;
import android.content.res.ColorStateList;
import android.graphics.PorterDuff;
import android.os.Build;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import java.util.HashMap;

// The props since 73 that reach the views (the layout ones are Yoga's, in host.cpp): a measured
// view's padding sides, text align, maximum lines, line height, italic, z index, clip, an
// image's listener, layout animation and a modal's presentation and appearing; a switch's or
// slider's accent; text with no color in the system's light or dark; and the direction.
final class UiStyle {
    private final UiActivity activity;
    UiStyle(UiActivity activity) { this.activity = activity; }

    // Per node: the sides' padding (top, right, bottom, left; 0: prop 2's) and prop 2, the text
    // props (align, lines, line height), the layout animation (ms), a modal's presentation and
    // appearing, the z index, whether it listens to its image, and the frames being animated.
    private final HashMap<Integer, int[]> paddings = new HashMap<>();
    private final HashMap<Integer, int[]> texts = new HashMap<>();
    private final HashMap<Integer, Integer> layoutMs = new HashMap<>();
    private final HashMap<Integer, int[]> modals = new HashMap<>();
    static final java.util.Map<View, Integer> zIndex = new java.util.WeakHashMap<>();
    final java.util.HashSet<Integer> imageListeners = new java.util.HashSet<>();
    private final HashMap<Integer, ValueAnimator> moving = new HashMap<>();
    private final HashMap<Integer, int[]> targets = new HashMap<>();
    // Texts the app gave a color; the others follow the system's light or dark.
    private final java.util.HashSet<Integer> colored = new java.util.HashSet<>();
    // Modals inserted since the last commit, which appear once laid out.
    private final java.util.ArrayList<Integer> appearing = new java.util.ArrayList<>();

    void forget(int id) {
        paddings.remove(id); texts.remove(id); layoutMs.remove(id); modals.remove(id); imageListeners.remove(id);
        colored.remove(id); appearing.remove((Integer)id);
        ValueAnimator move = moving.remove(id);
        if (move != null) move.cancel();
    }
    void clear() {
        paddings.clear(); texts.clear(); layoutMs.clear(); modals.clear(); imageListeners.clear(); colored.clear();
        appearing.clear();
        for (ValueAnimator move : moving.values()) move.cancel();
        moving.clear();
    }

    // Whether the system is in dark mode now.
    boolean night() {
        return (activity.getResources().getConfiguration().uiMode & android.content.res.Configuration.UI_MODE_NIGHT_MASK)
            == android.content.res.Configuration.UI_MODE_NIGHT_YES;
    }
    // A theme attribute's color as the platform's light or dark theme has it.
    private int themed(int attribute) {
        android.content.Context theme = new android.view.ContextThemeWrapper(activity,
            night() ? android.R.style.Theme_Material_NoActionBar : android.R.style.Theme_Material_Light_NoActionBar);
        android.content.res.TypedArray a = theme.obtainStyledAttributes(new int[] {attribute});
        int color = a.getColor(0, night() ? 0xffffffff : 0xff000000);
        a.recycle();
        return color;
    }
    // The window's background as the system's light or dark theme paints it.
    int background() { return themed(android.R.attr.colorBackground); }

    // A text or a field the app gave no color: the theme's primary text, its placeholder faded.
    void defaultColor(int id, View view) {
        if (colored.contains(id) || !(view instanceof TextView) || view instanceof Button) return;
        ((TextView)view).setTextColor(themed(android.R.attr.textColorPrimary));
        if (view instanceof EditText) ((EditText)view).setHintTextColor(themed(android.R.attr.textColorHint));
    }
    // Dark mode turned on or off: texts with no color, and the window behind the app.
    void nightChanged() {
        for (java.util.Map.Entry<Integer, View> entry : activity.nodes.entrySet()) defaultColor(entry.getKey(), entry.getValue());
        if (activity.splash == null) activity.getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(background()));
    }

    // The direction of the views: right to left when the user's language is written so (the
    // manifest does not declare RTL support, so the views are told).
    void direction(boolean rtl) {
        activity.getWindow().getDecorView().setLayoutDirection(rtl ? View.LAYOUT_DIRECTION_RTL : View.LAYOUT_DIRECTION_LTR);
    }

    // Prop 9 (a text's color, a switch's or a slider's accent) and props 73 to 91; false when
    // the code is not one of them.
    boolean prop(int id, View view, int code, int value) {
        float d = activity.density();
        if (code == 9 && accent(view, value)) return true;
        if (code == 9) {
            if (!(view instanceof TextView)) return true;
            if (value == 0) { colored.remove(id); defaultColor(id, view); return true; }
            colored.add(id);
            ((TextView)view).setTextColor(UiProps.argb(value));
            // A field's placeholder: its text color, faded.
            if (view instanceof EditText) ((EditText)view).setHintTextColor((UiProps.argb(value) & 0x00ffffff) | 0x80000000);
            return true;
        }
        if (code == 2 || (code >= 73 && code <= 76)) {
            int[] p = paddings.computeIfAbsent(id, k -> new int[5]);
            p[code == 2 ? 4 : code - 73] = value;
            // A text, a field and a button pad their own text (Yoga leaves measured views unpadded).
            if (view instanceof TextView && !(view instanceof Switch)) {
                int all = p[4];
                if (all <= 0 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0) return true;
                int[] side = new int[4];
                for (int i = 0; i < 4; i++) side[i] = Math.round((p[i] != 0 ? p[i] : all) * d);
                view.setPadding(side[3], side[0], side[1], side[2]);
            }
            return true;
        }
        if (code == 81 || code == 82 || code == 85) {
            int[] t = texts.computeIfAbsent(id, k -> new int[3]);
            t[code == 81 ? 0 : code == 82 ? 1 : 2] = value;
            if (view instanceof TextView) applyText((TextView)view, t);
            return true;
        }
        if (code == 84) { if (value != 0) imageListeners.add(id); else imageListeners.remove(id); return true; }
        if (code == 86) { activity.props.italic(id, view, value != 0); return true; }
        if (code == 87) {
            if (value != 0) zIndex.put(view, value); else zIndex.remove(view);
            if (view.getParent() instanceof UiYogaGroup) ((UiYogaGroup)view.getParent()).restack();
            return true;
        }
        if (code == 88) {
            // Clipped through the view's outline: its background's (with the radius) or its box.
            if (view.getBackground() == null) view.setOutlineProvider(android.view.ViewOutlineProvider.BOUNDS);
            if (value != 0 || view.getBackground() == null) view.setClipToOutline(value != 0);
            if (view instanceof ViewGroup) ((ViewGroup)view).setClipChildren(value != 0);
            return true;
        }
        if (code == 90) { layoutMs.put(id, value); return true; }
        if (code == 89 || code == 91) { modals.computeIfAbsent(id, k -> new int[2])[code == 89 ? 0 : 1] = value; return true; }
        return false;
    }

    // A switch's track and thumb when on, or a slider's filled part and thumb, in the accent;
    // 0 gives back the theme's. Only the on state is tinted (SRC_ATOP with clear keeps the rest).
    private boolean accent(View view, int value) {
        ColorStateList tint = null;
        if (value != 0 && view instanceof Switch) {
            tint = new ColorStateList(new int[][] {{android.R.attr.state_checked}, {}}, new int[] {UiProps.argb(value), 0});
        } else if (value != 0) tint = ColorStateList.valueOf(UiProps.argb(value));
        if (view instanceof Switch) {
            Switch toggle = (Switch)view;
            toggle.setThumbTintMode(PorterDuff.Mode.SRC_ATOP);
            toggle.setTrackTintMode(PorterDuff.Mode.SRC_ATOP);
            toggle.setThumbTintList(tint);
            toggle.setTrackTintList(tint);
            return true;
        }
        if (view instanceof SeekBar) {
            SeekBar slider = (SeekBar)view;
            slider.setProgressTintList(tint);
            slider.setThumbTintList(tint);
            return true;
        }
        return false;
    }

    // Text align (TextAlign: start, center, end, justify), maximum lines with an ellipsis, and
    // the line height in SP, as the font's size.
    private void applyText(TextView view, int[] t) {
        int align = t[0];
        boolean button = view instanceof Button;
        view.setTextAlignment(align == 0 ? (button ? View.TEXT_ALIGNMENT_CENTER : View.TEXT_ALIGNMENT_VIEW_START)
            : align == 2 ? View.TEXT_ALIGNMENT_CENTER : align == 3 ? View.TEXT_ALIGNMENT_VIEW_END : View.TEXT_ALIGNMENT_VIEW_START);
        if (Build.VERSION.SDK_INT >= 26 && !(view instanceof EditText))
            view.setJustificationMode(align == 4 ? android.text.Layout.JUSTIFICATION_MODE_INTER_WORD : android.text.Layout.JUSTIFICATION_MODE_NONE);
        if (!(view instanceof EditText)) {
            view.setMaxLines(t[1] > 0 ? t[1] : Integer.MAX_VALUE);
            view.setEllipsize(t[1] > 0 ? android.text.TextUtils.TruncateAt.END : null);
        }
        if (t[2] > 0) {
            int px = Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, t[2], activity.getResources().getDisplayMetrics()));
            if (Build.VERSION.SDK_INT >= 28) view.setLineHeight(px);
            else view.setLineSpacing(px - view.getPaint().getFontMetricsInt(null), 1f);
        } else view.setLineSpacing(0, 1f);
    }
    // The text size changed (the system's): a line height in SP again.
    void rescale(int id, View view) {
        int[] t = texts.get(id);
        if (t != null && t[2] > 0 && view instanceof TextView) applyText((TextView)view, t);
    }

    // An Image's load or failure, to the app when it listens.
    void imageLoaded(int id, boolean loaded) {
        Integer generation = activity.generations.get(id);
        if (activity.active && generation != null && imageListeners.contains(id)) activity.nativeImage(id, generation, loaded ? 1 : 0);
    }

    // A frame from Yoga: moved there over Style.layout_ms when the view was laid out before and
    // the frame changed (at once when animations are off: reduce motion, the driver's clock).
    boolean place(int id, View view, int l, int t, int w, int h) {
        int ms = layoutMs.getOrDefault(id, 0);
        int[] target = {l, t, w, h};
        ValueAnimator running = moving.get(id);
        if (running != null) {
            // Already on its way there: it keeps going.
            if (java.util.Arrays.equals(targets.get(id), target)) return true;
            running.cancel();
            moving.remove(id);
        }
        if (ms <= 0 || !view.isLaidOut() || !view.isAttachedToWindow() ||
            (view.getLeft() == l && view.getTop() == t && view.getWidth() == w && view.getHeight() == h)) return false;
        int[] from = {view.getLeft(), view.getTop(), view.getWidth(), view.getHeight()};
        ValueAnimator move = ValueAnimator.ofFloat(0f, 1f);
        move.setDuration(ms);
        move.setInterpolator(activity.props.interpolator(activity.props.easingOf(id)));
        move.addUpdateListener(a -> {
            float f = (float)a.getAnimatedValue();
            int x = Math.round(from[0] + (l - from[0]) * f), y = Math.round(from[1] + (t - from[1]) * f);
            int fw = Math.round(from[2] + (w - from[2]) * f), fh = Math.round(from[3] + (h - from[3]) * f);
            view.measure(View.MeasureSpec.makeMeasureSpec(fw, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(fh, View.MeasureSpec.EXACTLY));
            view.layout(x, y, x + fw, y + fh);
        });
        move.addListener(new android.animation.AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(android.animation.Animator a) { if (moving.get(id) == move) moving.remove(id); }
        });
        moving.put(id, move);
        targets.put(id, target);
        move.start();
        return true;
    }

    // A modal put on screen: it appears once the commit is laid out.
    void modalInserted(int id) { if (!appearing.contains(id)) appearing.add(id); }
    // After a commit's layout: a sheet's content slides up (its backdrop fading in), a full
    // screen modal or a dialog fades in; at once when it asked to (2) or left it to the host (0,
    // which shows it at once), and when animations are off.
    void appear() {
        for (int id : appearing) {
            View view = activity.nodes.get(id);
            int[] m = modals.getOrDefault(id, new int[2]);
            if (view == null || m[1] != 1 || ValueAnimator.getDurationScale() == 0) continue;
            if (m[0] == 1 && view instanceof ViewGroup) {
                ViewGroup layer = (ViewGroup)view;
                android.graphics.drawable.Drawable dim = layer.getBackground();
                if (dim != null) {
                    dim.setAlpha(0);
                    ValueAnimator fade = ValueAnimator.ofInt(0, 255).setDuration(300);
                    fade.addUpdateListener(a -> dim.setAlpha((int)a.getAnimatedValue()));
                    fade.start();
                }
                for (int i = 0; i < layer.getChildCount(); i++) {
                    View panel = layer.getChildAt(i);
                    panel.setTranslationY(layer.getHeight() - panel.getTop());
                    panel.animate().translationY(0).setDuration(300).setInterpolator(new android.view.animation.DecelerateInterpolator()).start();
                }
            } else {
                view.setAlpha(0);
                view.animate().alpha(1).setDuration(200).start();
            }
        }
        appearing.clear();
    }
}
