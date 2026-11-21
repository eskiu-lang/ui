package org.eskiu.ui;

import android.view.View;
import android.widget.EditText;
import android.widget.ScrollView;

// The driver's host side (hosts/driver.h, docs/agents.md), called from the native code of a
// dev build only. Input is the platform's: a click, the field's text, the scroll view's
// scroll; frames are in points from the window's top left.
final class UiDriver {
    private final UiActivity activity;
    UiDriver(UiActivity activity) { this.activity = activity; }

    String tap(int id) {
        View view = activity.nodes.get(id);
        if (view == null) return "error: #" + id + " has no view";
        if (view instanceof EditText) { cursor((EditText)view); view.requestFocus(); return ""; }
        // A switch toggles on a click with no click listener (performClick says false then).
        if (view instanceof android.widget.CompoundButton) { view.performClick(); return ""; }
        if (!view.performClick()) return "error: the view takes no press";
        return "";
    }
    String type(int id, String text) {
        View view = activity.nodes.get(id);
        if (!(view instanceof EditText)) return "error: not a text field";
        cursor((EditText)view);
        view.requestFocus();
        ((EditText)view).setText(text);
        return "";
    }
    String scroll(int id, int dy) {
        View view = activity.nodes.get(id);
        if (!(view instanceof ScrollView)) return "error: not a scroll view";
        float d = activity.density();
        view.scrollBy(0, Math.round(dy * d));
        return String.valueOf(Math.round(view.getScrollY() / d));
    }
    String layout() {
        StringBuilder rows = new StringBuilder();
        float d = activity.density();
        int[] at = new int[2];
        for (java.util.Map.Entry<Integer, View> entry : activity.nodes.entrySet()) {
            View view = entry.getValue();
            view.getLocationInWindow(at);
            // Shown, and some of it inside the window and every scroll above it: a row scrolled
            // away is not visible.
            boolean visible = view.isShown();
            if (visible && view.getWidth() > 0 && view.getHeight() > 0) visible = view.getGlobalVisibleRect(new android.graphics.Rect());
            else if (visible) visible = at[0] >= 0 && at[1] >= 0 && at[0] <= view.getRootView().getWidth() && at[1] <= view.getRootView().getHeight();
            rows.append(entry.getKey()).append(' ').append(at[0] / d).append(' ').append(at[1] / d).append(' ')
                .append(view.getWidth() / d).append(' ').append(view.getHeight() / d).append(' ')
                .append(visible ? 1 : 0).append('\n');
        }
        return rows.toString();
    }
    // While the driver owns the clock the animators' duration scale is 0: the host's
    // transitions and slides end with the frame that starts them, so nothing moves between
    // two requests. Its value before is restored when the driver gives the clock back.
    private float scale = -1;
    // A field's blinking cursor moves too: hidden while the driver owns the clock.
    private void cursor(EditText field) { field.setCursorVisible(scale < 0); }
    void clock(boolean owned) {
        for (View view : activity.nodes.values()) if (view instanceof EditText) ((EditText)view).setCursorVisible(!owned);
        try {
            java.lang.reflect.Method set = android.animation.ValueAnimator.class.getMethod("setDurationScale", float.class);
            if (owned && scale < 0) { scale = android.animation.ValueAnimator.getDurationScale(); set.invoke(null, 0f); }
            else if (!owned && scale >= 0) { set.invoke(null, scale); scale = -1; }
        } catch (Exception e) { android.util.Log.w("ui", "the driver cannot set the animator duration scale: " + e); }
    }
    long memory() {
        Runtime runtime = Runtime.getRuntime();
        return android.os.Debug.getNativeHeapAllocatedSize() + runtime.totalMemory() - runtime.freeMemory();
    }
}
