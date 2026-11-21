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
        // Children drawn, and touched, in z index order (Style.z_index).
        setChildrenDrawingOrderEnabled(true);
    }
    // The children's order by z index, the children's own order among equals; null until
    // asked for again after a change.
    private int[] order;
    void restack() { order = null; invalidate(); }
    @Override public void onViewAdded(android.view.View child) { super.onViewAdded(child); order = null; }
    @Override public void onViewRemoved(android.view.View child) { super.onViewRemoved(child); order = null; }
    @Override protected int getChildDrawingOrder(int count, int i) {
        if (order == null || order.length != count) {
            Integer[] sorted = new Integer[count];
            for (int k = 0; k < count; k++) sorted[k] = k;
            java.util.Arrays.sort(sorted, (x, y) -> Integer.compare(UiStyle.zIndex.getOrDefault(getChildAt(x), 0),
                                                                   UiStyle.zIndex.getOrDefault(getChildAt(y), 0)));
            order = new int[count];
            for (int k = 0; k < count; k++) order[k] = sorted[k];
        }
        return i < order.length ? order[i] : i;
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
