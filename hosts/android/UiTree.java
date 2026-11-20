package org.eskiu.ui;

import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import java.util.HashMap;

// The view tree a commit changes (create, set text and props, insert, remove), and the frames
// Yoga gives it.
final class UiTree {
    private final UiActivity activity;
    UiTree(UiActivity activity) { this.activity = activity; }

    final java.util.HashSet<Integer> modalIds = new java.util.HashSet<>();
    // Scroll views whose offset the core wants (virtual lists), and the viewport last reported.
    final HashMap<Integer, Integer> scrollViewports = new HashMap<>();

    void apply(int op, int id, int generation, int parent, int before, int kind, String text) {
        UiActivity a = activity;
        HashMap<Integer, View> nodes = a.nodes;
        HashMap<Integer, Integer> generations = a.generations;
        if (op == 1) {
            View view;
            if (kind == 4) {
                Button button = new Button(a);
                button.setAllCaps(false);
                button.setOnClickListener(v -> a.nativeEvent(id, generations.get(id)));
                view = button;
            } else if (kind == 3) {
                TextView label = new TextView(a); label.setTextSize(18); view = label;
            } else if (kind == 5) {
                EditText field = new EditText(a);
                field.setSingleLine(true);
                field.setTextSize(18);
                field.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                    @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
                    @Override public void afterTextChanged(Editable text) {
                        if (!a.applying && a.active) a.nativeText(id, generations.get(id), text.toString());
                    }
                });
                field.setOnFocusChangeListener((v, focused) -> {
                    if (a.active && generations.containsKey(id)) a.nativeFocus(id, generations.get(id), focused ? 1 : 0);
                });
                view = field;
            } else if (kind == 8) {
                UiYogaGroup pressable = new UiYogaGroup(a);
                pressable.setClickable(true);
                pressable.setFocusable(true);
                pressable.setOnClickListener(v -> a.nativeEvent(id, generations.get(id)));
                view = pressable;
            } else if (kind == 15) {
                android.widget.SeekBar slider = new android.widget.SeekBar(a);
                a.props.sliderRanges.put(id, new int[] {0, 100});
                slider.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
                    @Override public void onProgressChanged(android.widget.SeekBar b, int progress, boolean user) {
                        if (user && a.active && generations.containsKey(id)) a.nativeValue(id, generations.get(id), progress + a.props.sliderRanges.get(id)[0]);
                    }
                    @Override public void onStartTrackingTouch(android.widget.SeekBar b) {}
                    @Override public void onStopTrackingTouch(android.widget.SeekBar b) {}
                });
                view = slider;
            } else if (kind == 9) {
                Switch toggle = new Switch(a);
                toggle.setOnCheckedChangeListener((b, on) -> {
                    if (!a.applying && a.active) a.nativeToggle(id, generations.get(id), on ? 1 : 0);
                });
                view = toggle;
            } else if (kind == 10) {
                ProgressBar spinner = new ProgressBar(a, null, android.R.attr.progressBarStyleSmall);
                spinner.setIndeterminate(true);
                view = spinner;
            } else if (kind == 11) {
                view = new View(a);
                view.setVisibility(View.GONE);
            } else if (kind == 12) {
                UiYogaGroup modal = new UiYogaGroup(a);
                modal.setBackgroundColor(0x66000000);
                modal.setClickable(true);
                // A tap on the backdrop (outside every child) asks it to close.
                modal.setOnTouchListener((v, e) -> {
                    if (e.getActionMasked() != android.view.MotionEvent.ACTION_UP || !a.active) return false;
                    ViewGroup group = (ViewGroup)v;
                    for (int i = 0; i < group.getChildCount(); i++) {
                        View child = group.getChildAt(i);
                        if (e.getX() >= child.getLeft() && e.getX() < child.getRight() && e.getY() >= child.getTop() &&
                            e.getY() < child.getBottom()) return false;
                    }
                    if (generations.containsKey(id)) a.nativeEvent(id, generations.get(id));
                    return false;
                });
                modalIds.add(id);
                view = modal;
            } else if (kind == 7) {
                ImageView image = new ImageView(a);
                image.setScaleType(ImageView.ScaleType.FIT_CENTER);
                view = image;
            } else if (kind == 6) {
                // A ScrollView holds one child: the group its children go into.
                // It clips its content to the viewport itself: a parent that lets its children
                // overflow (UiYogaGroup) turns off the clip the ScrollView relies on.
                ScrollView scroll = new ScrollView(a) {
                    @Override protected void dispatchDraw(android.graphics.Canvas canvas) {
                        int save = canvas.save();
                        canvas.clipRect(getScrollX(), getScrollY(), getScrollX() + getWidth(), getScrollY() + getHeight());
                        super.dispatchDraw(canvas);
                        canvas.restoreToCount(save);
                    }
                };
                scroll.addView(new UiYogaGroup(a));
                scroll.setOnScrollChangeListener((v, x, y, oldX, oldY) -> reportScroll(id, (ScrollView)v));
                view = scroll;
            } else if (kind == 13) {
                view = new FrameLayout(a);
            } else view = new UiYogaGroup(a);
            view.setId(View.generateViewId());
            if (view instanceof UiYogaGroup) ((UiYogaGroup)view).nodeId = id;
            nodes.put(id, view); generations.put(id, generation);
            return;
        }
        View view = nodes.get(id);
        if (view == null) { UiActivity.require(op == 6, "unknown node " + id); return; }
        if (op == 3 && view instanceof EditText) {
            EditText field = (EditText)view;
            if (!field.getText().toString().equals(text)) {
                a.applying = true;
                field.setText(text);
                field.setSelection(text.length());
                a.applying = false;
            }
        } else if (op == 3) ((TextView)view).setText(text);
        else if (op == 9) {
            // String props: 1 label, 2 placeholder, 3 image source, 4 native view type, 5 font file,
            // 8 accessibility value, 9 accessibility hint.
            if (kind == 8 || kind == 9) a.access.text(id, view, kind, text);
            else if (kind == 4 && view instanceof FrameLayout) a.mountNative(id, (FrameLayout)view, text);
            else if (kind == 1) view.setContentDescription(text.isEmpty() ? null : text);
            else if (kind == 5) a.props.fontFile(id, view, text);
            else if (kind == 2 && view instanceof EditText) ((EditText)view).setHint(text);
            else if (kind == 3 && view instanceof ImageView) a.props.loadImage((ImageView)view, text);
        } else if (op == 2) {
            // 57 to 64 accessibility (UiAccess), 65 to 72 a text input's editing (UiInput).
            if (kind >= 57 && kind <= 64) a.access.prop(id, view, kind, before);
            else if (kind >= 65 && kind <= 72) { if (view instanceof EditText) a.input.prop(id, (EditText)view, kind, before); }
            else a.props.prop(id, view, kind, before);
        }
        else if (op == 4 || op == 5) {
            if (modalIds.contains(id)) {
                // Over everything, outside the parent's layout.
                if (view.getParent() == null) {
                    ((FrameLayout)a.findViewById(android.R.id.content)).addView(view,
                        new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
                }
                return;
            }
            ViewGroup group = nodes.containsKey(parent) ? (ViewGroup)nodes.get(parent) : a.root;
            if (group instanceof ScrollView) group = (ViewGroup)group.getChildAt(0);
            if (view.getParent() != null) ((ViewGroup)view.getParent()).removeView(view);
            group.addView(view, before == 0 ? group.getChildCount() : group.indexOfChild(nodes.get(before)));
            a.access.placed(view);
        } else if (op == 6) {
            UiProps props = a.props;
            ViewGroup owner = (ViewGroup)view.getParent();
            boolean slide = false;
            if (owner != null && props.activeScreens.getOrDefault(id, false) && view.getVisibility() == View.VISIBLE) {
                for (int i = 0; i < owner.getChildCount(); i++) {
                    View other = owner.getChildAt(i);
                    if (other != view && props.shownScreens.contains(a.idOf(other))) slide = true;
                }
            }
            if (slide) {
                view.bringToFront();
                props.leaving.add(view);
                view.animate().translationX(a.root.getWidth()).setDuration(UiProps.kSlide)
                    .withEndAction(() -> { owner.removeView(view); props.leaving.remove(view); }).start();
            } else if (owner != null && !props.insideLeaving(view)) owner.removeView(view);
            props.forget(id); a.access.forget(id, view); a.input.forget(id);
            nodes.remove(id); generations.remove(id);
            modalIds.remove(id); a.nativeViews.remove(id); a.nativeTypes.remove(id);
            scrollViewports.remove(id);
        } else if (op == 7 || op == 8) {
            if (view instanceof ScrollView) { if (op == 7) scrollViewports.put(id, -1); else scrollViewports.remove(id); }
            else a.access.listen(view, op == 7);
        }
    }
    long measure(int id, float width, int wm, float height, int hm) {
        View view = activity.nodes.get(id);
        float d = activity.density();
        int wspec = wm == 0 ? View.MeasureSpec.UNSPECIFIED : wm == 1 ? View.MeasureSpec.EXACTLY : View.MeasureSpec.AT_MOST;
        int hspec = hm == 0 ? View.MeasureSpec.UNSPECIFIED : hm == 1 ? View.MeasureSpec.EXACTLY : View.MeasureSpec.AT_MOST;
        view.measure(View.MeasureSpec.makeMeasureSpec(Math.max(0, Math.round(width * d)), wspec),
                     View.MeasureSpec.makeMeasureSpec(Math.max(0, Math.round(height * d)), hspec));
        return ((long)view.getMeasuredWidth() << 32) | (view.getMeasuredHeight() & 0xffffffffL);
    }
    void place(int id, float x, float y, float width, float height) {
        View view = activity.nodes.get(id);
        UiYogaGroup root = activity.root;
        float d = activity.density();
        int l = Math.round(x * d), t = Math.round(y * d);
        int w = Math.round(width * d), h = Math.round(height * d);
        view.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY));
        // The root's own children sit inside its padding.
        if (view.getParent() == root) { l += root.getPaddingLeft(); t += root.getPaddingTop(); }
        view.layout(l, t, l + w, t + h);
    }
    private void reportScroll(int id, ScrollView view) {
        // What is inside moved in the window: listened nodes report their new positions.
        if (activity.active) activity.nativeScrolled();
        if (!activity.active || !scrollViewports.containsKey(id)) return;
        float d = activity.density();
        int viewport = Math.round(view.getHeight() / d);
        scrollViewports.put(id, viewport);
        activity.nativeScroll(id, activity.generations.get(id), Math.round(view.getScrollY() / d), viewport);
    }
    void content(int id, float width, float height) {
        float d = activity.density();
        ScrollView scroll = (ScrollView)activity.nodes.get(id);
        UiYogaGroup content = (UiYogaGroup)scroll.getChildAt(0);
        content.contentWidth = Math.round(width * d);
        content.contentHeight = Math.max(1, Math.round(height * d));
        content.measure(View.MeasureSpec.makeMeasureSpec(content.contentWidth, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(content.contentHeight, View.MeasureSpec.EXACTLY));
        content.layout(0, 0, content.contentWidth, content.contentHeight);
        // A virtual list hears its viewport once laid out, and again when it changes size.
        activity.props.layoutRefresh(id, scroll);
        Integer reported = scrollViewports.get(id);
        if (reported != null && reported != Math.round(scroll.getHeight() / d)) reportScroll(id, scroll);
    }
}
