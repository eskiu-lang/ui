// The native hosts' grid (hosts/grid.h) over Yoga: track sizing and placement, checked
// against the frames Chrome gives the same CSS grid. Run by tools/ci.sh.
#include "../../hosts/grid.h"
#include <cstdio>

static int failures = 0;

// An int prop as a host applies it: to Yoga, and to the grid's record.
static void prop(YGNodeRef node, int code, int value) {
    if (code == 1) YGNodeStyleSetGap(node, YGGutterAll, value);
    else if (code == 2) YGNodeStyleSetPadding(node, YGEdgeAll, value);
    else if (code == 4) { if (value) YGNodeStyleSetWidth(node, value); else YGNodeStyleSetWidthAuto(node); }
    else if (code == 5) { if (value) YGNodeStyleSetHeight(node, value); else YGNodeStyleSetHeightAuto(node); }
    else if (code == 24) YGNodeStyleSetPositionType(node, value ? YGPositionTypeAbsolute : YGPositionTypeRelative);
    ui_grid_prop(node, code, value);
}

static YGNodeRef box(YGNodeRef parent, int width, int height) {
    YGNodeRef node = YGNodeNew();
    YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
    YGNodeStyleSetGap(node, YGGutterAll, 8);
    if (width) prop(node, 4, width);
    if (height) prop(node, 5, height);
    YGNodeInsertChild(parent, node, YGNodeGetChildCount(parent));
    return node;
}

static void place(YGNodeRef node, int column, int columnSpan, int row, int rowSpan) {
    if (column) prop(node, 53, column);
    if (columnSpan) prop(node, 54, columnSpan);
    if (row) prop(node, 55, row);
    if (rowSpan) prop(node, 56, rowSpan);
}

static void expect(const char *name, YGNodeRef node, float x, float y, float w, float h) {
    float got[4] = {YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node), YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node)};
    float want[4] = {x, y, w, h};
    for (int i = 0; i < 4; i++) {
        if (std::fabs(got[i] - want[i]) > 0.5f) {
            printf("FAIL grid %s: %g %g %g %g, expected %g %g %g %g\n", name, got[0], got[1], got[2], got[3], x, y, w, h);
            failures++;
            return;
        }
    }
}

// The conformance scene's grid (tests/harness/conformance.esk): fixed, fr and auto columns,
// a fixed and auto rows, spans, a definite cell and a definite column, auto-placed rest.
static void scene() {
    YGNodeRef root = YGNodeNew();
    YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
    prop(root, 4, 320); prop(root, 2, 10);
    YGNodeRef grid = box(root, 0, 0);
    prop(grid, 2, 5); prop(grid, 1, 10);
    ui_grid_tracks(grid, 6, "60 1fr auto 2fr");
    ui_grid_tracks(grid, 7, "40 auto");
    YGNodeRef a = box(grid, 0, 30), b = box(grid, 0, 0), c = box(grid, 50, 20), d = box(grid, 0, 0);
    YGNodeRef e = box(grid, 0, 25), f = box(grid, 0, 20), g = box(grid, 0, 0), h = box(grid, 0, 35);
    YGNodeRef i = box(grid, 0, 30), j = box(grid, 0, 0), k = box(grid, 0, 15);
    place(e, 0, 2, 0, 0); place(f, 4, 0, 3, 0); place(g, 0, 0, 0, 2); place(k, 2, 0, 0, 0);
    ui_grid_calculate(root, YGUndefined, YGUndefined, YGDirectionLTR);
    expect("container", grid, 10, 10, 300, 160);
    expect("A", a, 5, 5, 60, 30);
    expect("B", b, 75, 5, 50, 40);
    expect("C", c, 135, 5, 50, 20);
    expect("D", d, 195, 5, 100, 40);
    expect("E", e, 5, 55, 120, 25);
    expect("G", g, 135, 55, 50, 75);
    expect("H", h, 195, 55, 100, 35);
    expect("F", f, 195, 100, 100, 20);
    expect("I", i, 5, 100, 60, 30);
    expect("J", j, 75, 100, 50, 30);
    expect("K", k, 75, 140, 50, 15);
    // A child after the grid follows its height.
    YGNodeRef after = box(root, 0, 10);
    ui_grid_calculate(root, YGUndefined, YGUndefined, YGDirectionLTR);
    expect("after", after, 10, 170, 300, 10);
    // No longer a grid: the children are back in the column's flow with their own sizes.
    ui_grid_tracks(grid, 6, "");
    ui_grid_tracks(grid, 7, "");
    ui_grid_calculate(root, YGUndefined, YGUndefined, YGDirectionLTR);
    expect("column A", a, 5, 5, 290, 30);
    expect("column C", c, 5, 55, 50, 20);
    YGNodeFreeRecursive(root);
    ui_grid_table().clear();
    ui_grid_containers() = 0;
}

// Without fr tracks the auto ones share what is left; fr rows in a grid of no height fit the
// tallest item per fraction; an absolute child stays out of the grid.
static void tracks() {
    YGNodeRef root = YGNodeNew();
    prop(root, 4, 200);
    YGNodeRef grid = box(root, 0, 0);
    prop(grid, 1, 0);
    ui_grid_tracks(grid, 6, "50 auto auto");
    ui_grid_tracks(grid, 7, "1fr 2fr");
    YGNodeRef a = box(grid, 0, 0), b = box(grid, 30, 0), c = box(grid, 0, 0), d = box(grid, 0, 40);
    YGNodeRef pinned = box(grid, 10, 10);
    prop(pinned, 24, 1);
    ui_grid_calculate(root, YGUndefined, YGUndefined, YGDirectionLTR);
    // Columns: 50, then the 120 left after b's 30 shared: 90 and 60. Rows: d (40) under 2fr
    // makes the fraction 20, so the 1fr row is 20.
    expect("fr row", a, 0, 0, 50, 20);
    expect("stretched auto", b, 50, 0, 30, 20);
    expect("stretched auto 2", c, 140, 0, 60, 20);
    expect("2fr row", d, 0, 20, 50, 40);
    expect("container", grid, 0, 0, 200, 60);
    expect("absolute", pinned, 0, 0, 10, 10);
    YGNodeFreeRecursive(root);
    ui_grid_table().clear();
    ui_grid_containers() = 0;
}

int main() {
    scene();
    tracks();
    if (failures) return 1;
    printf("PASS grid\n");
    return 0;
}
