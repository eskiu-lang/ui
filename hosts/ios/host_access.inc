// host_access.inc: Style's role and states (props 57 to 64, string props 8 and 9) as UIKit
// accessibility, and disabled (prop 61) down a subtree. Part of host.mm, which #includes it
// (one translation unit).

// UIAccessibilityTraitToggleButton (iOS 17), the trait UISwitch has carried since before it
// was public: VoiceOver reads a value of "1" or "0" as on or off.
static UIAccessibilityTraits toggleTrait(void) {
    if (@available(iOS 17.0, *)) return UIAccessibilityTraitToggleButton;
    return (UIAccessibilityTraits)1 << 53;
}

// The traits a Role gives an element (0: none, the kind's own).
static UIAccessibilityTraits roleTraits(int role) {
    switch (role) {
    case 1: case 6: case 9: case 14: return UIAccessibilityTraitButton;
    case 2: return UIAccessibilityTraitLink;
    case 3: return UIAccessibilityTraitHeader;
    case 4: return UIAccessibilityTraitImage;
    case 5: case 8: return UIAccessibilityTraitButton | toggleTrait();
    case 10: return UIAccessibilityTraitTabBar;
    case 11: return UIAccessibilityTraitAdjustable;
    case 12: return UIAccessibilityTraitUpdatesFrequently;
    default: return UIAccessibilityTraitNone;
    }
}

// Roles VoiceOver stops on as one element (a column with role button reads as one button).
static bool elementRole(int role) {
    return (role >= 1 && role <= 6) || role == 8 || role == 9 || role == 11 || role == 12 || role == 14;
}

// Roles that group what is inside: VoiceOver announces the list or the landmark around it.
static UIAccessibilityContainerType containerOf(int role) {
    if (role == 15) return UIAccessibilityContainerTypeList;
    if (role == 18) return UIAccessibilityContainerTypeLandmark;
    if (role == 7 || role == 13 || role == 17) return UIAccessibilityContainerTypeSemanticGroup;
    return UIAccessibilityContainerTypeNone;
}

// Words UIKit has no localized form of (a mixed check, and before iOS 18 expanded and
// collapsed): the latest the app sent (command 13, ui_set_words: mixed, expanded, collapsed,
// menu, menu item, alert, search, link), English until then.
static NSArray<NSString *> *accessWords;
static NSString *accessWord(int index) {
    static NSArray<NSString *> *english = @[@"mixed", @"expanded", @"collapsed", @"menu", @"menu item", @"alert",
                                             @"search", @"link"];
    NSString *word = index < (int)accessWords.count ? accessWords[index] : nil;
    return word.length ? word : english[index];
}

// True when node or a node above it is disabled (Style.disabled holds for what is inside).
static bool disabledAbove(UiHostNode *node) {
    for (UiHostNode *at = node; at; at = at.parentId ? nodes[@(at.parentId)] : nil) if (at.disabled) return true;
    return false;
}

// What the labels and buttons inside a view say, for an element made by its role that has no
// label of its own.
static NSString *childText(UIView *view) {
    NSMutableArray<NSString *> *parts = [NSMutableArray new];
    for (UIView *child in view.subviews) {
        NSString *text = [child isKindOfClass:UILabel.class] ? ((UILabel *)child).text
                       : [child isKindOfClass:UIButton.class] ? [(UIButton *)child titleForState:UIControlStateNormal]
                       : child.accessibilityLabel.length ? child.accessibilityLabel : childText(child);
        if (text.length) [parts addObject:text];
    }
    return [parts componentsJoinedByString:@", "];
}

// A range (props 62 to 64) as VoiceOver says it: a percent for a progress bar, else the number.
static NSString *rangeText(UiHostNode *node) {
    if (node.rangeMax <= node.rangeMin) return nil;
    NSNumberFormatter *format = [NSNumberFormatter new];
    if (node.role == 12) {
        format.numberStyle = NSNumberFormatterPercentStyle;
        return [format stringFromNumber:@((double)(node.rangeNow - node.rangeMin) / (node.rangeMax - node.rangeMin))];
    }
    format.numberStyle = NSNumberFormatterDecimalStyle;
    return [format stringFromNumber:@(node.rangeNow)];
}

// The node's accessibility from its props. The first call keeps the traits and element flag
// the view had, which role 0 goes back to.
static void applyAccess(UiHostNode *node) {
    UIView *view = node.view;
    if (!node.access) {
        node.access = true;
        node.baseTraits = view.accessibilityTraits & ~(UIAccessibilityTraitNotEnabled | UIAccessibilityTraitSelected);
        node.baseElement = view.isAccessibilityElement;
    }
    int role = node.role;
    UIAccessibilityTraits traits = role ? roleTraits(role) : node.baseTraits;
    bool toggle = role == 5 || role == 8 || (role == 0 && node.kind == 9);
    // A radio, a checked menu item, and a toggle with words of the app's: "selected", in the
    // user's language.
    if (node.selected || (!toggle && node.checked == 2) || (toggle && node.checked == 2 && node.accessValue.length))
        traits |= UIAccessibilityTraitSelected;
    if (disabledAbove(node)) traits |= UIAccessibilityTraitNotEnabled;
    view.accessibilityTraits = traits;
    view.accessibilityContainerType = containerOf(role);
    bool element = role ? elementRole(role) : node.baseElement;
    view.isAccessibilityElement = element;
    // An element by its role reads what is inside it, unless the app labelled it.
    if (element && !node.baseElement && !node.labelled) {
        if (@available(iOS 17.0, *)) {
            __weak UIView *weak = view;
            view.accessibilityLabelBlock = ^NSString * { return childText(weak); };
        } else view.accessibilityLabel = childText(view);
    } else if (@available(iOS 17.0, *)) view.accessibilityLabelBlock = nil;
    // The spoken value: the app's value text or the range, a mixed check, then whether it is
    // open where UIKit has no expanded status.
    NSMutableArray<NSString *> *parts = [NSMutableArray new];
    NSString *text = node.accessValue ?: rangeText(node);
    if (text.length) [parts addObject:text];
    else if (node.checked == 3) [parts addObject:accessWord(0)];
    if (@available(iOS 18.0, *)) {
        view.accessibilityExpandedStatus = node.expanded == 1 ? UIAccessibilityExpandedStatusCollapsed
                                         : node.expanded == 2 ? UIAccessibilityExpandedStatusExpanded
                                                              : UIAccessibilityExpandedStatusUnsupported;
    } else if (node.expanded) [parts addObject:accessWord(node.expanded == 2 ? 1 : 2)];
    // A checkbox or a switch: "1" or "0" alone, which VoiceOver reads as on or off in the
    // user's language (with the app's words it is "selected" when on, above).
    if (toggle && (node.checked == 1 || node.checked == 2) && !parts.count) [parts addObject:node.checked == 2 ? @"1" : @"0"];
    view.accessibilityValue = parts.count ? [parts componentsJoinedByString:@", "] : nil;
    view.accessibilityHint = node.accessHint;
}

// Nodes with Style.disabled set: while there are none, an insert has nothing to check.
static int disabledCount;

// A node takes input or not, from its own flag and those above it: native disabled on
// controls, a text view not editable, and the not enabled trait.
static void applyEnabled(UiHostNode *node) {
    bool off = disabledAbove(node);
    if ([node.view isKindOfClass:UIControl.class]) ((UIControl *)node.view).enabled = !off;
    else if ([node.view isKindOfClass:UITextView.class]) ((UITextView *)node.view).editable = !off;
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

// Props 57 to 64 and string props 8 and 9; false when the code is not one of them.
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

static bool accessText(UiHostNode *node, int code, NSString *value) {
    if (code == 8) node.accessValue = value;
    else if (code == 9) node.accessHint = value;
    else return false;
    applyAccess(node);
    return true;
}

// Command 13: the app's words for what UIKit does not translate; what is shown says them now.
static void setAccessWords(int count, const char **strings) {
    NSMutableArray<NSString *> *words = [NSMutableArray new];
    for (int i = 0; i < count; i++) [words addObject:@(strings[i])];
    accessWords = words;
    for (UiHostNode *node in nodes.allValues) if (node.access) applyAccess(node);
}
