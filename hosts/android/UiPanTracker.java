package org.eskiu.ui;

import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewConfiguration;

// Drags past the touch slop, in screen coordinates (the view itself may move), with their
// velocity; a touch that does not move is still a click. A group takes the drag from a
// child that would consume it (a pressable inside), once it passes the slop.
final class UiPanTracker {
    private final UiActivity activity;
    final int id;
    final float[] start = new float[2];
    boolean down, panning;
    VelocityTracker velocity;
    final int slop;
    UiPanTracker(UiActivity activity, int id) {
        this.activity = activity;
        this.id = id;
        slop = ViewConfiguration.get(activity).getScaledTouchSlop();
    }
    void track(MotionEvent e) {
        MotionEvent screen = MotionEvent.obtain(e);
        screen.setLocation(e.getRawX(), e.getRawY());
        if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
            start[0] = e.getRawX(); start[1] = e.getRawY(); down = true; panning = false;
            if (velocity != null) velocity.recycle();
            velocity = VelocityTracker.obtain();
        }
        if (velocity != null) velocity.addMovement(screen);
        screen.recycle();
    }
    boolean past(MotionEvent e) { return down && Math.hypot(e.getRawX() - start[0], e.getRawY() - start[1]) >= slop; }
    // A group's interception: true once the drag passes the slop.
    boolean intercept(MotionEvent e) {
        track(e);
        int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) { down = false; return false; }
        return action == MotionEvent.ACTION_MOVE && past(e);
    }
    boolean touch(View v, MotionEvent e) {
        if (e.getActionMasked() != MotionEvent.ACTION_DOWN || !down) track(e);
        float d = activity.density();
        int action = e.getActionMasked();
        int dx = Math.round((e.getRawX() - start[0]) / d), dy = Math.round((e.getRawY() - start[1]) / d);
        if (!activity.active || !activity.generations.containsKey(id)) return true;
        int generation = activity.generations.get(id);
        if (action == MotionEvent.ACTION_MOVE) {
            if (!panning && !past(e)) return true;
            if (!panning) {
                panning = true;
                if (v.getParent() != null) v.getParent().requestDisallowInterceptTouchEvent(true);
                activity.nativePan(id, generation, 0, 0, 0, 0, 0);
            }
            activity.nativePan(id, generation, 1, dx, dy, 0, 0);
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            int vx = 0, vy = 0;
            if (velocity != null) {
                velocity.computeCurrentVelocity(1000);
                vx = Math.round(velocity.getXVelocity() / d); vy = Math.round(velocity.getYVelocity() / d);
                velocity.recycle(); velocity = null;
            }
            if (panning) activity.nativePan(id, generation, action == MotionEvent.ACTION_UP ? 2 : 3, dx, dy, vx, vy);
            else if (action == MotionEvent.ACTION_UP) v.performClick();
            panning = false; down = false;
        }
        return true;
    }
}
