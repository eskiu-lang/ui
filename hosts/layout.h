// Layout props the native hosts apply to Yoga alike, and the layout direction. Header-only
// C++, next to grid.h. The sides are physical (left is left in a right-to-left layout too,
// as CSS's under dir=rtl); only Yoga's direction mirrors rows, start and end.
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H
#include <yoga/Yoga.h>

// Props 73 to 76, the padding of one side (0: prop 2's for all sides), and 77 to 80, the
// margin of one side (points). True when `code` is one of them.
static inline bool ui_layout_prop(YGNodeRef node, int code, int value) {
    static const YGEdge sides[] = {YGEdgeTop, YGEdgeRight, YGEdgeBottom, YGEdgeLeft};
    if (code >= 73 && code <= 76) {
        YGNodeStyleSetPadding(node, sides[code - 73], value ? (float)value : YGUndefined);
        return true;
    }
    if (code >= 77 && code <= 80) {
        YGNodeStyleSetMargin(node, sides[code - 77], (float)value);
        return true;
    }
    return false;
}

// The direction the hosts lay out in: right to left when the system's language is written so
// (the hosts set it where they report the locale).
static inline bool &ui_layout_rtl() { static bool rtl = false; return rtl; }
static inline YGDirection ui_layout_direction() { return ui_layout_rtl() ? YGDirectionRTL : YGDirectionLTR; }
#endif
