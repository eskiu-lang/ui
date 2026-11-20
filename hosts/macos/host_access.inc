// host_access.inc: Style's role and states (props 57 to 64, string props 8 and 9) as AppKit
// accessibility, and disabled (prop 61) down a subtree. Part of host.mm, which #includes it
// (one translation unit).

// The role a Role gives an element. AXHeading is NSAccessibilityHeadingRole (public in macOS
// 26, used by WebKit long before), so VoiceOver knows it on older systems too.
static NSAccessibilityRole roleOf(int role) {
    switch (role) {
    case 1: return NSAccessibilityButtonRole;
    case 2: return NSAccessibilityLinkRole;
    case 3: return @"AXHeading";
    case 4: return NSAccessibilityImageRole;
    case 5: case 8: return NSAccessibilityCheckBoxRole;
    case 6: case 9: return NSAccessibilityRadioButtonRole;
    case 7: return NSAccessibilityRadioGroupRole;
    case 10: return NSAccessibilityTabGroupRole;
    case 11: return NSAccessibilitySliderRole;
    case 12: return NSAccessibilityProgressIndicatorRole;
    case 13: return NSAccessibilityMenuRole;
    case 14: return NSAccessibilityMenuItemRole;
    case 15: return NSAccessibilityListRole;
    default: return NSAccessibilityGroupRole;
    }
}

// The subrole: a switch is a checkbox, a tab a radio button, an alert and a search region
// groups with the landmark subroles WebKit gives ARIA's alert and search.
static NSAccessibilitySubrole subroleOf(int role) {
    if (role == 8) return NSAccessibilitySwitchSubrole;
    if (role == 9) return NSAccessibilityTabButtonSubrole;
    if (role == 17) return @"AXApplicationAlert";
    if (role == 18) return @"AXLandmarkSearch";
    return nil;
}

static bool elementRole(int role) {
    return (role >= 1 && role <= 6) || role == 8 || role == 9 || role == 11 || role == 12 || role == 14;
}

static bool disabledAbove(UiHostNode *node) {
    for (UiHostNode *at = node; at; at = at.parentId ? nodes[@(at.parentId)] : nil) if (at.disabled) return true;
    return false;
}

// What the texts and buttons inside a view say.
static NSString *childText(NSView *view) {
    NSMutableArray<NSString *> *parts = [NSMutableArray new];
    for (NSView *child in view.subviews) {
        NSString *text = [child isKindOfClass:NSButton.class] ? ((NSButton *)child).title
                       : [child isKindOfClass:NSTextField.class] ? ((NSTextField *)child).stringValue
                       : child.accessibilityLabel.length ? child.accessibilityLabel : childText(child);
        if (text.length) [parts addObject:text];
    }
    return [parts componentsJoinedByString:@", "];
}

static NSString *roleLabel(NSView *view) {
    UiHostNode *node = nodeOf(view);
    if (!node || node.view != view || !elementRole(node.role) || node.labelled) return nil;
    NSString *text = childText(view);
    return text.length ? text : nil;
}

// The node's accessibility from its props. The first call keeps the role, subrole and
// element flag the view had, which role 0 goes back to.
static void applyAccess(UiHostNode *node) {
    NSView *view = node.view;
    if (!node.access) {
        node.access = true;
        // A control's role comes from its cell: the view's own is nil, and setting nil reads
        // as an unknown role.
        NSCell *cell = [view isKindOfClass:NSControl.class] ? ((NSControl *)view).cell : nil;
        node.baseRole = view.accessibilityRole ?: cell.accessibilityRole;
        node.baseSubrole = view.accessibilitySubrole ?: cell.accessibilitySubrole;
        node.baseElement = view.isAccessibilityElement;
    }
    int role = node.role;
    // Each attribute is left as AppKit has it until a prop sets it.
    if (role || node.roleSet) {
        view.accessibilityRole = role ? roleOf(role) : node.baseRole;
        view.accessibilitySubrole = role ? subroleOf(role) : node.baseSubrole;
        view.accessibilityElement = role ? YES : node.baseElement;
        node.roleSet = role != 0;
    }
    // The value: a tab's selection, a checkbox's, radio's or switch's state (0 off, 1 on, 2
    // mixed), else the range's value; the app's value text describes it, or is the value.
    bool range = node.rangeMax > node.rangeMin;
    id value = nil;
    if (role == 9) value = @(node.selected ? 1 : 0);
    else if (node.checked) value = @(node.checked == 2 ? 1 : node.checked == 3 ? 2 : 0);
    else if (range) value = @(node.rangeNow);
    NSString *text = node.accessValue;
    if (!value && text.length) value = text;
    // Left alone until set, so a slider keeps its own value and bounds.
    if (value || node.valueSet) {
        view.accessibilityValue = value;
        view.accessibilityMinValue = range ? @(node.rangeMin) : nil;
        view.accessibilityMaxValue = range ? @(node.rangeMax) : nil;
        node.valueSet = value != nil;
    }
    view.accessibilityValueDescription = value && value != text ? text : nil;
    if (node.selected || node.selectSet) {
        view.accessibilitySelected = node.selected != 0;
        node.selectSet = true;
    }
    if (node.expanded || node.expandSet) {
        view.accessibilityExpanded = node.expanded == 2;
        node.expandSet = true;
    }
    view.accessibilityEnabled = !disabledAbove(node);
    view.accessibilityHelp = node.accessHint;
}

// A node takes input or not, from its own flag and those above it: native disabled on
// controls (not on labels, which would grey), a text area not editable, accessibility
// enabled off.
static void applyEnabled(UiHostNode *node) {
    bool off = disabledAbove(node);
    bool control = node.kind == 4 || node.kind == 5 || node.kind == 9 || node.kind == 15;
    if (control && [node.view isKindOfClass:NSControl.class]) ((NSControl *)node.view).enabled = !off;
    else if (isTextArea(node.view)) ((UiTextArea *)node.view).textView.editable = !off;
    if (off || node.access) applyAccess(node);
}

// The node and everything inside it, modals the node holds included.
static void refreshEnabled(UiHostNode *node) {
    applyEnabled(node);
    for (size_t i = 0; i < YGNodeGetChildCount(node.yoga); ++i) {
        UiHostNode *child = (__bridge UiHostNode *)YGNodeGetContext(YGNodeGetChild(node.yoga, i));
        if (child) refreshEnabled(child);
    }
    for (UiHostNode *modal in modals.copy) if (modal.parentId == node.nodeId) refreshEnabled(modal);
}

// Props 57 to 64; false when the code is not one of them.
static bool accessProp(UiHostNode *node, int code, int value) {
    if (code == 57) node.role = value;
    else if (code == 58) node.checked = value;
    else if (code == 59) node.selected = value;
    else if (code == 60) node.expanded = value;
    else if (code == 62) node.rangeNow = value;
    else if (code == 63) node.rangeMin = value;
    else if (code == 64) node.rangeMax = value;
    else if (code == 61) {
        if (node.disabled != (value != 0)) disabledCount += value ? 1 : -1;
        node.disabled = value != 0;
        refreshEnabled(node);
        return true;
    } else return false;
    applyAccess(node);
    return true;
}

// String props 8 (the spoken value) and 9 (the hint).
static bool accessText(UiHostNode *node, int code, NSString *value) {
    if (code == 8) node.accessValue = value;
    else if (code == 9) node.accessHint = value;
    else return false;
    applyAccess(node);
    return true;
}
