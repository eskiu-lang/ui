// CSS grid for the native hosts, over Yoga (which has none). Shared by every native host;
// header-only C++. A grid container keeps its Yoga node; its items become absolute children
// whose size and offsets come from the track sizing and placement below, the subset of CSS
// grid that docs/style-and-layout.md describes. The hosts call ui_grid_prop and
// ui_grid_tracks for each int and string prop, ui_grid_calculate instead of
// YGNodeCalculateLayout and ui_grid_free instead of YGNodeFree.
#ifndef UI_GRID_H
#define UI_GRID_H
#include <yoga/Yoga.h>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// A track: 0 points, 1 fr, 2 auto.
struct UiGridTrack { int kind; float value; };

struct UiGridInfo {
    std::vector<UiGridTrack> tracks[2];
    bool grid = false, has[2] = {false, false};
    // Placement (props 53 to 56): start lines from 1 (0: auto) and spans.
    int start[2] = {0, 0}, span[2] = {1, 1};
    // The node's own props, restored when it stops being a grid item or container.
    int size[2] = {0, 0};
    bool absolute = false;
    int edges[4] = {-1000000, -1000000, -1000000, -1000000};
    // Whether the grid wrote this node's style (as an item; its height, as a container).
    bool item = false, ownsHeight = false;
    unsigned seen = 0;
    float frame[4] = {-1, -1, -1, -1};
};

static inline std::unordered_map<YGNodeRef, UiGridInfo> &ui_grid_table() {
    static std::unordered_map<YGNodeRef, UiGridInfo> table;
    return table;
}
static inline int &ui_grid_containers() { static int count = 0; return count; }

static inline UiGridInfo *ui_grid_find(YGNodeRef node) {
    auto &table = ui_grid_table();
    auto it = table.find(node);
    return it == table.end() ? nullptr : &it->second;
}

// Records the props the grid reads or overrides: 4 width, 5 height, 24 absolute, 25 to 28
// the edges, 53 column start, 54 column span, 55 row start, 56 row span.
static inline void ui_grid_prop(YGNodeRef node, int code, int value) {
    if (!(code == 4 || code == 5 || (code >= 24 && code <= 28) || (code >= 53 && code <= 56))) return;
    UiGridInfo &info = ui_grid_table()[node];
    if (code == 4 || code == 5) info.size[code - 4] = value;
    else if (code == 24) info.absolute = value != 0;
    else if (code <= 28) info.edges[code - 25] = value;
    else if (code == 53) info.start[0] = value;
    else if (code == 54) info.span[0] = value > 1 ? value : 1;
    else if (code == 55) info.start[1] = value;
    else info.span[1] = value > 1 ? value : 1;
}

// Parses a track list ("100 1fr auto": points, fractions, auto).
static inline std::vector<UiGridTrack> ui_grid_parse(const char *text) {
    std::vector<UiGridTrack> tracks;
    const char *p = text ? text : "";
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (p[0] == 'a' && p[1] == 'u' && p[2] == 't' && p[3] == 'o') { tracks.push_back({2, 0}); p += 4; continue; }
        char *end = nullptr;
        float value = strtof(p, &end);
        if (end == p) { while (*p && *p != ' ') p++; continue; }
        p = end;
        if (p[0] == 'f' && p[1] == 'r') { tracks.push_back({1, value}); p += 2; }
        else { if (p[0] == 'p' && p[1] == 'x') p += 2; tracks.push_back({0, value}); }
        while (*p && *p != ' ') p++;
    }
    return tracks;
}

// String props 6 (columns) and 7 (rows): a container is a grid while either is set.
static inline void ui_grid_tracks(YGNodeRef node, int code, const char *text) {
    if (code != 6 && code != 7) return;
    UiGridInfo &info = ui_grid_table()[node];
    bool was = info.grid;
    info.tracks[code - 6] = ui_grid_parse(text);
    info.has[code - 6] = text && *text;
    info.grid = info.has[0] || info.has[1];
    if (info.grid != was) ui_grid_containers() += info.grid ? 1 : -1;
}

static inline void ui_grid_restore_item(YGNodeRef node, UiGridInfo &info) {
    info.item = false;
    if (info.size[0]) YGNodeStyleSetWidth(node, info.size[0]); else YGNodeStyleSetWidthAuto(node);
    if (info.size[1]) YGNodeStyleSetHeight(node, info.size[1]); else YGNodeStyleSetHeightAuto(node);
    YGNodeStyleSetPositionType(node, info.absolute ? YGPositionTypeAbsolute : YGPositionTypeRelative);
    const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
    for (int i = 0; i < 4; i++)
        YGNodeStyleSetPosition(node, edges[i], info.edges[i] == -1000000 ? YGUndefined : (float)info.edges[i]);
}

static inline void ui_grid_free(YGNodeRef node) {
    if (UiGridInfo *info = ui_grid_find(node)) {
        if (info->grid) ui_grid_containers()--;
        ui_grid_table().erase(node);
    }
    YGNodeFree(node);
}

// A placed item: its node, its own props, its area (start track and span per axis) and its
// content size.
struct UiGridItem { YGNodeRef node; UiGridInfo *info; int at[2], span[2]; float content[2]; };

// Places the items as CSS's grid-auto-flow: row (sparse) does; returns the column count.
static inline int ui_grid_place(std::vector<UiGridItem> &items, int explicitColumns) {
    int columns = explicitColumns > 0 ? explicitColumns : 1;
    for (auto &item : items) {
        int start = item.info->start[0], span = item.info->span[0];
        columns = std::max(columns, start > 0 ? start - 1 + span : span);
    }
    std::vector<std::vector<bool>> used;
    auto fits = [&](int row, int col, int rows, int cols) {
        if (col + cols > columns) return false;
        for (int r = row; r < row + rows; r++) {
            if (r >= (int)used.size()) continue;
            for (int c = col; c < col + cols; c++) if (used[r][c]) return false;
        }
        return true;
    };
    auto take = [&](UiGridItem &item, int row, int col) {
        item.at[0] = col; item.at[1] = row;
        while ((int)used.size() < row + item.span[1]) used.push_back(std::vector<bool>(columns, false));
        for (int r = row; r < row + item.span[1]; r++)
            for (int c = col; c < col + item.span[0]; c++) used[r][c] = true;
    };
    for (auto &item : items) { item.span[0] = item.info->span[0]; item.span[1] = item.info->span[1]; item.at[0] = item.at[1] = -1; }
    for (auto &item : items)
        if (item.info->start[0] > 0 && item.info->start[1] > 0) take(item, item.info->start[1] - 1, item.info->start[0] - 1);
    std::unordered_map<int, int> rowCursor;
    for (auto &item : items) {
        if (item.info->start[0] > 0 || item.info->start[1] <= 0) continue;
        int row = item.info->start[1] - 1, col = rowCursor[row];
        while (!fits(row, col, item.span[1], item.span[0]) && col + item.span[0] <= columns) col++;
        if (col + item.span[0] > columns) col = 0;
        take(item, row, col);
        rowCursor[row] = col + item.span[0];
    }
    int row = 0, col = 0;
    for (auto &item : items) {
        if (item.info->start[1] > 0) continue;
        if (item.info->start[0] > 0) {
            int c = item.info->start[0] - 1;
            if (c < col) row++;
            col = c;
            while (!fits(row, col, item.span[1], item.span[0])) row++;
        } else {
            while (!fits(row, col, item.span[1], item.span[0])) {
                col++;
                if (col + item.span[0] > columns) { row++; col = 0; }
            }
        }
        take(item, row, col);
    }
    return columns;
}

// Sizes the tracks of one axis. `space` is the inner size (NAN: indefinite).
static inline std::vector<float> ui_grid_size_tracks(const std::vector<UiGridTrack> &spec, int count, float gap,
                                                     float space, std::vector<UiGridItem> &items, int axis) {
    std::vector<UiGridTrack> tracks(count, UiGridTrack{2, 0});
    for (int i = 0; i < count && i < (int)spec.size(); i++) tracks[i] = spec[i];
    std::vector<float> size(count, 0);
    for (int i = 0; i < count; i++) if (tracks[i].kind == 0) size[i] = tracks[i].value;
    // Auto (and, in an indefinite space, fr) tracks fit their single-span items.
    for (auto &item : items) {
        if (item.span[axis] != 1) continue;
        int t = item.at[axis];
        if (tracks[t].kind == 2) size[t] = std::max(size[t], item.content[axis]);
    }
    // A spanning item over no fr track grows its auto tracks by what it lacks, shared equally.
    for (auto &item : items) {
        if (item.span[axis] == 1) continue;
        float have = gap * (item.span[axis] - 1);
        int autos = 0;
        bool flexible = false;
        for (int t = item.at[axis]; t < item.at[axis] + item.span[axis]; t++) {
            have += size[t];
            if (tracks[t].kind == 2) autos++;
            if (tracks[t].kind == 1) flexible = true;
        }
        if (flexible || autos == 0 || item.content[axis] <= have) continue;
        float extra = (item.content[axis] - have) / autos;
        for (int t = item.at[axis]; t < item.at[axis] + item.span[axis]; t++) if (tracks[t].kind == 2) size[t] += extra;
    }
    float flex = 0, used = count > 1 ? gap * (count - 1) : 0;
    int autos = 0;
    for (int i = 0; i < count; i++) {
        if (tracks[i].kind == 1) flex += tracks[i].value; else used += size[i];
        if (tracks[i].kind == 2) autos++;
    }
    if (flex > 0) {
        float fraction = 0;
        if (std::isnan(space)) {
            // An indefinite space: the fraction that fits each fr track's items.
            for (auto &item : items) {
                if (item.span[axis] != 1 || tracks[item.at[axis]].kind != 1) continue;
                float f = tracks[item.at[axis]].value;
                fraction = std::max(fraction, f > 1 ? item.content[axis] / f : item.content[axis]);
            }
        } else {
            float left = space - used;
            fraction = left > 0 ? left / std::max(flex, 1.0f) : 0;
        }
        for (int i = 0; i < count; i++) if (tracks[i].kind == 1) size[i] = tracks[i].value * fraction;
    } else if (!std::isnan(space) && autos > 0 && space > used) {
        // With no fr track, what is left stretches the auto tracks.
        float extra = (space - used) / autos;
        for (int i = 0; i < count; i++) if (tracks[i].kind == 2) size[i] += extra;
    }
    return size;
}

// Lays out one grid container whose own width Yoga has set; returns whether a frame changed.
static inline bool ui_grid_container(YGNodeRef grid, UiGridInfo &info, unsigned epoch) {
    std::vector<UiGridItem> items;
    std::vector<YGNodeRef> children;
    for (size_t i = 0; i < YGNodeGetChildCount(grid); ++i) children.push_back(YGNodeGetChild(grid, i));
    auto &table = ui_grid_table();
    for (YGNodeRef child : children) {
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        UiGridInfo &ci = table[child];
        if (ci.absolute) continue;
        ci.seen = epoch;
        items.push_back(UiGridItem{child, &ci, {0, 0}, {1, 1}, {0, 0}});
    }
    // Items are measured as roots of their own, then put back in their order.
    for (YGNodeRef child : children) YGNodeRemoveChild(grid, child);
    int columns = ui_grid_place(items, (int)info.tracks[0].size());
    int rows = (int)info.tracks[1].size();
    for (auto &item : items) rows = std::max(rows, item.at[1] + item.span[1]);
    auto gapOf = [&](YGGutter gutter) {
        float g = YGNodeStyleGetGap(grid, gutter);
        if (std::isnan(g)) g = YGNodeStyleGetGap(grid, YGGutterAll);
        return std::isnan(g) ? 0.0f : g;
    };
    float gaps[2] = {gapOf(YGGutterColumn), gapOf(YGGutterRow)};
    float left = YGNodeLayoutGetPadding(grid, YGEdgeLeft) + YGNodeLayoutGetBorder(grid, YGEdgeLeft);
    float top = YGNodeLayoutGetPadding(grid, YGEdgeTop) + YGNodeLayoutGetBorder(grid, YGEdgeTop);
    float right = YGNodeLayoutGetPadding(grid, YGEdgeRight) + YGNodeLayoutGetBorder(grid, YGEdgeRight);
    float bottom = YGNodeLayoutGetPadding(grid, YGEdgeBottom) + YGNodeLayoutGetBorder(grid, YGEdgeBottom);
    float width = YGNodeLayoutGetWidth(grid) - left - right;
    float height = info.size[1] ? info.size[1] - top - bottom : NAN;
    for (auto &item : items) {
        YGNodeRef n = item.node;
        if (item.info->size[0]) YGNodeStyleSetWidth(n, item.info->size[0]); else YGNodeStyleSetWidthAuto(n);
        if (item.info->size[1]) YGNodeStyleSetHeight(n, item.info->size[1]); else YGNodeStyleSetHeightAuto(n);
        YGNodeStyleSetPositionType(n, YGPositionTypeRelative);
        YGNodeCalculateLayout(n, YGUndefined, YGUndefined, YGDirectionLTR);
        item.content[0] = YGNodeLayoutGetWidth(n);
    }
    std::vector<float> cols = ui_grid_size_tracks(info.tracks[0], columns, gaps[0], width, items, 0);
    std::vector<float> colAt(columns + 1, 0);
    for (int i = 0; i < columns; i++) colAt[i + 1] = colAt[i] + cols[i] + gaps[0];
    for (auto &item : items) {
        float w = colAt[item.at[0] + item.span[0]] - colAt[item.at[0]] - gaps[0];
        YGNodeCalculateLayout(item.node, item.info->size[0] ? YGUndefined : w, YGUndefined, YGDirectionLTR);
        item.content[1] = YGNodeLayoutGetHeight(item.node);
    }
    std::vector<float> rowSizes = ui_grid_size_tracks(info.tracks[1], rows, gaps[1], height, items, 1);
    std::vector<float> rowAt(rows + 1, 0);
    for (int i = 0; i < rows; i++) rowAt[i + 1] = rowAt[i] + rowSizes[i] + gaps[1];
    for (YGNodeRef child : children) YGNodeInsertChild(grid, child, YGNodeGetChildCount(grid));
    bool changed = false;
    for (auto &item : items) {
        YGNodeRef n = item.node;
        float w = colAt[item.at[0] + item.span[0]] - colAt[item.at[0]] - gaps[0];
        float h = rowAt[item.at[1] + item.span[1]] - rowAt[item.at[1]] - gaps[1];
        float frame[4] = {left + colAt[item.at[0]], top + rowAt[item.at[1]], item.info->size[0] ? item.info->size[0] : w,
                          item.info->size[1] ? item.info->size[1] : h};
        for (int i = 0; i < 4; i++) if (std::fabs(frame[i] - item.info->frame[i]) > 0.01f) changed = true;
        for (int i = 0; i < 4; i++) item.info->frame[i] = frame[i];
        item.info->item = true;
        YGNodeStyleSetPositionType(n, YGPositionTypeAbsolute);
        YGNodeStyleSetPosition(n, YGEdgeLeft, frame[0]);
        YGNodeStyleSetPosition(n, YGEdgeTop, frame[1]);
        YGNodeStyleSetPosition(n, YGEdgeRight, YGUndefined);
        YGNodeStyleSetPosition(n, YGEdgeBottom, YGUndefined);
        YGNodeStyleSetWidth(n, frame[2]);
        YGNodeStyleSetHeight(n, frame[3]);
    }
    if (!info.size[1]) {
        float total = top + bottom + (rows ? rowAt[rows] - gaps[1] : 0);
        if (!info.ownsHeight || std::fabs(YGNodeStyleGetHeight(grid).value - total) > 0.01f) changed = true;
        YGNodeStyleSetHeight(grid, total);
        info.ownsHeight = true;
    }
    return changed;
}

static inline bool ui_grid_walk(YGNodeRef node, unsigned epoch) {
    bool changed = false;
    UiGridInfo *info = ui_grid_find(node);
    if (info && info->grid && YGNodeStyleGetDisplay(node) != YGDisplayNone) changed = ui_grid_container(node, *info, epoch);
    else if (info && info->ownsHeight) {
        info->ownsHeight = false;
        if (info->size[1]) YGNodeStyleSetHeight(node, info->size[1]); else YGNodeStyleSetHeightAuto(node);
        changed = true;
    }
    if (YGNodeStyleGetDisplay(node) == YGDisplayNone) return changed;
    for (size_t i = 0; i < YGNodeGetChildCount(node); ++i) changed = ui_grid_walk(YGNodeGetChild(node, i), epoch) || changed;
    return changed;
}

// YGNodeCalculateLayout, then the grid containers under `root`, outer ones first, until
// their frames settle.
static inline void ui_grid_calculate(YGNodeRef root, float width, float height, YGDirection direction) {
    YGNodeCalculateLayout(root, width, height, direction);
    static unsigned epoch = 0;
    bool anyItems = false;
    for (auto &entry : ui_grid_table()) if (entry.second.item || entry.second.ownsHeight) { anyItems = true; break; }
    if (ui_grid_containers() == 0 && !anyItems) return;
    for (int pass = 0; pass < 4; pass++) {
        unsigned now = ++epoch;
        bool changed = ui_grid_walk(root, now);
        // An item no grid holds any more gets its own props back.
        for (auto &entry : ui_grid_table()) {
            if (!entry.second.item || entry.second.seen == now) continue;
            YGNodeRef owner = YGNodeGetParent(entry.first);
            bool under = false;
            for (YGNodeRef up = owner; up; up = YGNodeGetParent(up)) if (up == root) { under = true; break; }
            if (!under && entry.first != root) continue;
            ui_grid_restore_item(entry.first, entry.second);
            changed = true;
        }
        YGNodeCalculateLayout(root, width, height, direction);
        if (!changed) break;
    }
}
#endif
