package org.eskiu.ui;

import android.view.MotionEvent;
import android.view.ViewGroup;

// A group Yoga lays out: the root, a view with children, a scroll view's content.
final class UiYogaGroup extends ViewGroup {
    private final UiActivity activity;
    // The node this group draws (-1: the root, a scroll's content).
    int nodeId = -1;
    // The size Yoga gave a scroll view's content; 0 for every other group.
    int contentWidth, contentHeight;
    UiYogaGroup(UiActivity activity) {
        super(activity);
        this.activity = activity;
        // Overflow is visible, as in Yoga: an absolute child may draw outside (a radius clips
        // through the outline).
        setClipChildren(false);
        setClipToPadding(false);
    }
    @Override public boolean onInterceptTouchEvent(MotionEvent e) {
        UiPanTracker tracker = activity.props.pans.get(nodeId);
        return tracker != null && tracker.intercept(e);
    }
    @Override protected void onMeasure(int width, int height) {
        if (contentHeight > 0) { setMeasuredDimension(contentWidth, contentHeight); return; }
        setMeasuredDimension(MeasureSpec.getSize(width), MeasureSpec.getSize(height));
    }
    @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
        if (this == activity.root && activity.active) {
            float d = activity.density();
            activity.nativeWindow(Math.round((r - l) / d), Math.round((b - t) / d), Math.round(d * 100));
            activity.nativeLayout(activity.width(), activity.height());
        }
    }
}
