package org.eskiu.ui;

import android.os.Build;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import java.util.HashMap;

// Style's accessibility (props 57 to 64, string props 8 and 9) on a node's AccessibilityNodeInfo
// through an AccessibilityDelegate, and Style.disabled (prop 61) down the view tree.
final class UiAccess {
    // What the core set on a node: role, checked, selected, expanded, the range (now, min,
    // max), the spoken value and the hint.
    static final class State {
        int role, checked, selected, expanded, now, min, max;
        String value = "", hint = "";
    }
    private final HashMap<Integer, State> states = new HashMap<>();
    // Views disabled by Style.disabled, and views the core does not listen to (op 8): a view is
    // enabled when neither it nor a view above it is disabled and it is listened to.
    private final java.util.Set<View> disabled = java.util.Collections.newSetFromMap(new java.util.WeakHashMap<>());
    private final java.util.Set<View> unlistened = java.util.Collections.newSetFromMap(new java.util.WeakHashMap<>());

    // Role names TalkBack speaks through the role description where Android has no widget class.
    private static final String[] CLASSES = {null, "android.widget.Button", null, null, "android.widget.ImageView",
        "android.widget.CheckBox", "android.widget.RadioButton", "android.widget.RadioGroup", "android.widget.Switch",
        null, "android.widget.TabWidget", "android.widget.SeekBar", "android.widget.ProgressBar", null, null,
        "android.widget.ListView", null, null, null};
    private static final String[] DESCRIPTIONS = {null, null, "Link", null, null, null, null, null, null, "Tab", null,
        null, null, "Menu", "Menu item", null, null, "Alert", "Search"};
    private static final String ROLE_DESCRIPTION = "AccessibilityNodeInfo.roleDescription";

    void forget(int id, View view) { states.remove(id); disabled.remove(view); unlistened.remove(view); }
    void clear() { states.clear(); disabled.clear(); unlistened.clear(); }

    private State stateOf(int id, View view) {
        State state = states.get(id);
        if (state != null) return state;
        State made = new State();
        states.put(id, made);
        view.setAccessibilityDelegate(new View.AccessibilityDelegate() {
            @Override public void onInitializeAccessibilityNodeInfo(View host, AccessibilityNodeInfo info) {
                super.onInitializeAccessibilityNodeInfo(host, info);
                describe(made, host, info);
            }
            @Override public boolean performAccessibilityAction(View host, int action, android.os.Bundle arguments) {
                // Expanding and collapsing is the element's press: the app toggles it.
                if ((action == AccessibilityNodeInfo.ACTION_EXPAND || action == AccessibilityNodeInfo.ACTION_COLLAPSE) && made.expanded != 0)
                    return host.isEnabled() && host.performClick();
                return super.performAccessibilityAction(host, action, arguments);
            }
        });
        return made;
    }

    // An int prop from 57 to 64.
    void prop(int id, View view, int code, int value) {
        if (code == 61) {
            if (value != 0) disabled.add(view); else disabled.remove(view);
            refresh(view);
            return;
        }
        State state = stateOf(id, view);
        if (code == 57) {
            state.role = value >= 0 && value < CLASSES.length ? value : 0;
            // An alert is read when it appears and when it changes.
            view.setAccessibilityLiveRegion(state.role == 17 ? View.ACCESSIBILITY_LIVE_REGION_ASSERTIVE : View.ACCESSIBILITY_LIVE_REGION_NONE);
            if (Build.VERSION.SDK_INT >= 28) view.setAccessibilityHeading(state.role == 3);
            if (state.role != 0 && state.role != 3 && state.role != 15 && view.getImportantForAccessibility() == View.IMPORTANT_FOR_ACCESSIBILITY_AUTO)
                view.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        } else if (code == 58) state.checked = value;
        else if (code == 59) state.selected = value;
        else if (code == 60) state.expanded = value;
        else if (code == 62) state.now = value;
        else if (code == 63) state.min = value;
        else if (code == 64) state.max = value;
        if (Build.VERSION.SDK_INT >= 30 && (code == 58 || code == 60)) view.setStateDescription(stateText(state));
    }
    // A string prop: 8 the spoken value, 9 the hint.
    void text(int id, View view, int code, String text) {
        State state = stateOf(id, view);
        if (code == 8) state.value = text; else state.hint = text;
        if (Build.VERSION.SDK_INT >= 30 && code == 8) view.setStateDescription(stateText(state));
    }
    // What is said for a state no widget class says: the value text, a mixed check, a
    // disclosure.
    private static String stateText(State state) {
        String said = state.value;
        if (state.checked == 3) said = said.isEmpty() ? "Partially checked" : "Partially checked, " + said;
        if (state.expanded != 0) said = (state.expanded == 2 ? "Expanded" : "Collapsed") + (said.isEmpty() ? "" : ", " + said);
        return said.isEmpty() ? null : said;
    }
    private static void describe(State state, View host, AccessibilityNodeInfo info) {
        if (state.role != 0) {
            if (CLASSES[state.role] != null) info.setClassName(CLASSES[state.role]);
            if (DESCRIPTIONS[state.role] != null) info.getExtras().putCharSequence(ROLE_DESCRIPTION, DESCRIPTIONS[state.role]);
            if (state.role == 3 && Build.VERSION.SDK_INT < 28)
                info.setCollectionItemInfo(AccessibilityNodeInfo.CollectionItemInfo.obtain(0, 1, 0, 1, true));
            if (state.role == 15 && host instanceof ViewGroup)
                info.setCollectionInfo(AccessibilityNodeInfo.CollectionInfo.obtain(((ViewGroup)host).getChildCount(), 1, false));
            if (state.role == 16 && host.getParent() instanceof ViewGroup) {
                int index = ((ViewGroup)host.getParent()).indexOfChild(host);
                info.setCollectionItemInfo(AccessibilityNodeInfo.CollectionItemInfo.obtain(index, 1, 0, 1, false));
            }
        }
        if (state.checked != 0) { info.setCheckable(true); info.setChecked(state.checked == 2); }
        if (state.selected != 0) info.setSelected(true);
        if (state.expanded == 1) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_EXPAND);
        else if (state.expanded == 2) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_COLLAPSE);
        if (state.max > state.min) {
            info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_INT, state.min, state.max, state.now));
        }
        // Before API 30 there is no state description: the state is read after the label.
        if (Build.VERSION.SDK_INT < 30) {
            String said = stateText(state);
            if (said != null) {
                CharSequence label = info.getContentDescription();
                info.setContentDescription(label == null || label.length() == 0 ? said : label + ", " + said);
            }
        }
        if (!state.hint.isEmpty() && Build.VERSION.SDK_INT >= 26) {
            CharSequence placeholder = info.getHintText();
            info.setHintText(placeholder == null || placeholder.length() == 0 ? state.hint : placeholder + ", " + state.hint);
        }
    }

    // Op 7 and 8: whether the core listens to the view (a button without an action is disabled).
    void listen(View view, boolean on) {
        if (on) unlistened.remove(view); else unlistened.add(view);
        view.setEnabled(on && !disabledAbove(view));
    }
    // A view placed under a parent: disabled when a view above it is.
    void placed(View view) { if (!disabled.isEmpty()) refresh(view); }
    private boolean disabledAbove(View view) {
        if (disabled.isEmpty()) return false;
        for (Object at = view; at instanceof View; at = ((View)at).getParent()) if (disabled.contains(at)) return true;
        return false;
    }
    // The view and everything under it, enabled again unless something above is still disabled.
    private void refresh(View view) {
        boolean off = disabledAbove(view);
        view.setEnabled(!off && !unlistened.contains(view));
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup)view;
            for (int i = 0; i < group.getChildCount(); i++) refreshUnder(group.getChildAt(i), off);
        }
    }
    private void refreshUnder(View view, boolean above) {
        boolean off = above || disabled.contains(view);
        view.setEnabled(!off && !unlistened.contains(view));
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup)view;
            for (int i = 0; i < group.getChildCount(); i++) refreshUnder(group.getChildAt(i), off);
        }
    }
}
