// host_commit.inc: applying a commit, layout, the core's requests, the system's settings and reload under ui serve. Part of host.cpp, which #includes it (one translation unit).

// ---- applying a commit ----

static void applyButtonStyle(Node *node) {
    bool styled = node->background || node->borderWidth || node->radius || node->textColor || node->fontSize || node->fontWeight ||
                  !node->fontFile.empty() || node->gradientTo;
    if (node->kind == 5) {
        node->styled = node->background || node->borderWidth || node->radius;
        if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
        YGNodeMarkDirty(node->yoga);
        return;
    }
    if (node->kind != 4 || !node->hwnd) return;
    LONG style = GetWindowLongW(node->hwnd, GWL_STYLE);
    LONG want = (style & ~0xF) | (styled ? BS_OWNERDRAW : BS_PUSHBUTTON);
    if (want != style) SetWindowLongW(node->hwnd, GWL_STYLE, want);
    node->styled = styled;
    SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)(styled ? fontOf(node) : controlFont()), TRUE);
    InvalidateRect(node->hwnd, nullptr, TRUE);
    YGNodeMarkDirty(node->yoga);
}

static void applyFont(Node *node) {
    if (node->kind == 4) { applyButtonStyle(node); return; }
    if (node->kind == 3 && node->hwnd) SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)fontOf(node), TRUE);
    if (node->kind == 5 && node->inner) SendMessageW(node->inner, WM_SETFONT, (WPARAM)fontOf(node), TRUE);
    YGNodeMarkDirty(node->yoga);
}

static void insertWindow(Node *node, HWND parent, Node *next) {
    if (!node->hwnd) return;
    if (GetParent(node->hwnd) != parent) { SetParent(node->hwnd, parent); node->placed.left = INT32_MIN; }
    // Later siblings draw over earlier ones: the top of Windows' z-order is the last child.
    HWND after = next && next->hwnd && GetParent(next->hwnd) == parent ? next->hwnd : HWND_TOP;
    SetWindowPos(node->hwnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (node->kind != 14 || node->active) {
        if (node->kind != 11) ShowWindow(node->hwnd, SW_SHOWNA);
    }
}

static void createNode(int id, int generation, int kind) {
    Node *node = new Node();
    node->id = id;
    node->generation = generation;
    node->kind = kind;
    node->yoga = YGNodeNew();
    YGNodeSetContext(node->yoga, node);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    (void)instance;
    if (kind == 3) {
        node->hwnd = makeWindow(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, nullptr);
        SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)fontOf(node), FALSE);
    } else if (kind == 4) {
        node->hwnd = makeWindow(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, nullptr);
        SendMessageW(node->hwnd, WM_SETFONT, (WPARAM)controlFont(), FALSE);
    } else if (kind == 5) {
        node->hwnd = makeWindow(kViewClass, L"", WS_CLIPCHILDREN, nullptr, WS_EX_CONTROLPARENT);
        node->inner = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0,
                                      node->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(node->inner, WM_SETFONT, (WPARAM)fontOf(node), FALSE);
        SendMessageW(node->inner, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    } else if (kind == 6) {
        node->hwnd = makeWindow(kScrollClass, L"", WS_CLIPCHILDREN | WS_VSCROLL, nullptr, WS_EX_CONTROLPARENT);
        node->inner = CreateWindowExW(WS_EX_CONTROLPARENT, kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, node->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        ShowScrollBar(node->hwnd, SB_VERT, FALSE);
        YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
        YGNodeStyleSetOverflow(node->yoga, YGOverflowScroll);
        YGNodeStyleSetFlexShrink(node->yoga, 1);
        YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
    } else if (kind == 7) {
        node->hwnd = makeWindow(kImageClass, L"", 0, nullptr);
        accessible(node->hwnd, ROLE_SYSTEM_GRAPHIC, nullptr);
    } else if (kind == 9) {
        node->hwnd = makeWindow(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, nullptr);
        accessible(node->hwnd, ROLE_SYSTEM_CHECKBUTTON, nullptr);
        accessibleState(node->hwnd, 0);
    } else if (kind == 10) {
        node->hwnd = makeWindow(kSpinnerClass, L"", 0, nullptr);
        accessible(node->hwnd, ROLE_SYSTEM_PROGRESSBAR, nullptr);
    } else if (kind == 15) {
        node->hwnd = makeWindow(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, nullptr);
        SendMessageW(node->hwnd, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(node->hwnd, TBM_SETRANGEMAX, FALSE, 100);
    } else {
        DWORD style = WS_CLIPCHILDREN | (kind == 8 ? WS_TABSTOP : 0);
        node->hwnd = makeWindow(kViewClass, L"", style, kind == 12 ? mainWindow : nullptr, WS_EX_CONTROLPARENT);
        if (kind == 8) accessible(node->hwnd, ROLE_SYSTEM_PUSHBUTTON, nullptr);
        if (kind == 12) {
            accessible(node->hwnd, ROLE_SYSTEM_DIALOG, nullptr);
            node->background = node->shownBackground = rgba(0, 0, 0, 102);
            YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
        } else if (kind == 11) {
            YGNodeStyleSetDisplay(node->yoga, YGDisplayNone);
        } else if (kind != 13) {
            YGNodeStyleSetFlexDirection(node->yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
        }
    }
    if (node->hwnd) track(node, node->hwnd);
    applyTheme(node);
    if ((kind >= 3 && kind <= 5) || kind == 7 || kind == 9 || kind == 10 || kind == 15) YGNodeSetMeasureFunc(node->yoga, measure);
    nodes[id] = node;
}

static void freeNode(Node *node) {
    dropAnimations(node);
    hovered.erase(node->id);
    if (gesture.press == node) gesture.press = nullptr;
    if (gesture.pan == node) gesture.pan = nullptr;
    if (gesture.longPress == node) gesture.longPress = nullptr;
    for (size_t i = 0; i < slidingIn.size(); i++) if (slidingIn[i] == node) { slidingIn.erase(slidingIn.begin() + (long)i); break; }
    delete node->emitter;
    ui_grid_free(node->yoga);
    delete node;
}

static void removeNode(Node *node) {
    YGNodeRef parent = YGNodeGetParent(node->yoga);
    if (parent) YGNodeRemoveChild(parent, node->yoga);
    for (size_t i = 0; i < modals.size(); i++) if (modals[i] == node) { modals.erase(modals.begin() + (long)i); break; }
    if (node->hwnd) {
        if (removeScreen(node)) {}
        else if (insideLeaving(node->hwnd)) {
            // It goes with its screen once the slide ends.
            windowNodes.erase(node->hwnd);
            node->hwnd = nullptr;
        } else {
            HWND hwnd = node->hwnd;
            DestroyWindow(hwnd);
        }
    }
    nodes.erase(node->id);
    freeNode(node);
}

extern "C" void ui_host_apply(int op, int id, int generation, int parent, int before, int kind, const char *text) {
    if (op == 1) { createNode(id, generation, kind); return; }
    auto found = nodes.find(id);
    if (found == nodes.end()) {
        if (op == 6) return;
        fprintf(stderr, "ui host: op %d on unknown node %d\n", op, id);
        abort();
    }
    Node *node = found->second;
    // The grid (hosts/grid.h) keeps the props it reads.
    if (op == 2 && node->yoga) ui_grid_prop(node->yoga, kind, before);
    if (op == 9 && node->yoga) ui_grid_tracks(node->yoga, kind, text);
    if (op == 2) {
        // Props: the code in kind, the value in before.
        int v = before;
        switch (kind) {
            case 1: YGNodeStyleSetGap(node->yoga, YGGutterAll, (float)v); break;
            case 2: YGNodeStyleSetPadding(node->yoga, YGEdgeAll, (float)v); node->padding = v; break;
            case 3:
                if (node->kind == 5 && node->inner) {
                    if (v) SetFocus(node->inner);
                    else if (GetFocus() == node->inner) SetFocus(mainWindow);
                }
                break;
            case 4: if (v) YGNodeStyleSetWidth(node->yoga, (float)v); else YGNodeStyleSetWidthAuto(node->yoga); break;
            case 5: if (v) YGNodeStyleSetHeight(node->yoga, (float)v); else YGNodeStyleSetHeightAuto(node->yoga); break;
            case 6:
                YGNodeStyleSetFlexGrow(node->yoga, (float)v);
                if (node->kind != 6) YGNodeStyleSetFlexShrink(node->yoga, v > 0 ? 1 : 0);
                break;
            case 7: YGNodeStyleSetFlexWrap(node->yoga, v ? YGWrapWrap : YGWrapNoWrap); break;
            case 8:
                node->background = v;
                if (node->kind == 12 && !v) v = rgba(0, 0, 0, 102), node->background = v;
                animateColor(node, v);
                applyButtonStyle(node);
                break;
            case 9: node->textColor = v; applyButtonStyle(node); if (node->hwnd) InvalidateRect(node->hwnd, nullptr, TRUE); if (node->inner) InvalidateRect(node->inner, nullptr, TRUE); break;
            case 10: node->fontSize = v; applyFont(node); break;
            case 11: node->fontWeight = v; applyFont(node); break;
            case 12:
                node->borderWidth = v;
                YGNodeStyleSetBorder(node->yoga, YGEdgeAll, (float)v);
                applyButtonStyle(node);
                break;
            case 13: node->borderColor = v; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 14: node->radius = v; applyButtonStyle(node); if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 15: {
                double from = node->shownOpacity;
                node->opacity = v;
                animate(node, 0, from, v);
                break;
            }
            case 16: {
                const YGAlign aligns[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch};
                YGNodeStyleSetAlignItems(node->yoga, aligns[std::min(4, std::max(0, v))]);
                break;
            }
            case 17: {
                const YGJustify justifies[] = {YGJustifyFlexStart, YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                               YGJustifySpaceBetween, YGJustifySpaceAround};
                YGNodeStyleSetJustifyContent(node->yoga, justifies[std::min(5, std::max(0, v))]);
                break;
            }
            case 18:
                node->on = v != 0;
                applyAccessStates(node);
                if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
                break;
            case 19:
                node->spinning = v != 0;
                if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
                if (spinnersShowing()) animationsRun();
                break;
            case 23: screenActive(node, v); break;
            case 24: YGNodeStyleSetPositionType(node->yoga, v ? YGPositionTypeAbsolute : YGPositionTypeRelative); break;
            case 25: case 26: case 27: case 28: {
                const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
                YGNodeStyleSetPosition(node->yoga, edges[kind - 25], v == kUnset ? YGUndefined : (float)v);
                break;
            }
            case 29: YGNodeStyleSetAspectRatio(node->yoga, v ? v / 1000.0f : YGUndefined); break;
            case 30: node->layoutListening = v != 0; node->reportedX = INT32_MIN; break;
            case 31: node->hoverListening = v != 0; break;
            case 32: case 33: case 34:
                if (node->kind == 15 && node->hwnd) {
                    if (kind == 33) { node->sliderMin = v; SendMessageW(node->hwnd, TBM_SETRANGEMIN, TRUE, v); }
                    else if (kind == 34) { node->sliderMax = v; SendMessageW(node->hwnd, TBM_SETRANGEMAX, TRUE, v); }
                    else { node->sliderValue = v; SendMessageW(node->hwnd, TBM_SETPOS, TRUE, v); }
                }
                break;
            case 35: node->translateX = v; animate(node, 1, node->shownX, v); break;
            case 36: node->translateY = v; animate(node, 2, node->shownY, v); break;
            case 37: node->scalePercent = v; break;
            case 38: node->rotateDegrees = v; break;
            case 39: node->transitionMs = v; break;
            case 40: node->easing = v; break;
            case 41: node->panListening = v != 0; break;
            case 42: YGNodeStyleSetMinWidth(node->yoga, (float)v); break;
            case 43: YGNodeStyleSetMaxWidth(node->yoga, v ? (float)v : YGUndefined); break;
            case 44: YGNodeStyleSetMinHeight(node->yoga, (float)v); break;
            case 45: YGNodeStyleSetMaxHeight(node->yoga, v ? (float)v : YGUndefined); break;
            case 46: node->shadow = v; break;
            case 47: node->cover = v != 0; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 48: node->pixelated = v != 0; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 49: node->longListening = v != 0; break;
            case 50: node->gradientTo = v; applyButtonStyle(node); if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 51: node->gradientAngle = v; if (node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE); break;
            case 52: node->blur = v; break;
            default:
                // 57 to 64 accessibility, 65 to 72 a text input's editing (host_access.inc).
                if (kind >= 57 && kind <= 64) applyAccessProp(node, kind, v);
                else if (kind >= 65 && kind <= 72) applyInputProp(node, kind, v);
                break;
        }
    } else if (op == 9) {
        std::wstring value = wide(text);
        if (kind == 4 && node->kind == 13) mountNative(node, text ? text : "");
        else if (kind == 1) {
            node->label = value;
            accessible(accessibleOf(node), 0, &value);
            applyAccessRole(node);
        } else if (kind == 8 || kind == 9) applyAccessText(node, kind, value);
        else if (kind == 5) { node->fontFile = value; applyFont(node); }
        else if (kind == 2 && node->kind == 5 && node->inner) {
            node->placeholder = value;
            SendMessageW(node->inner, EM_SETCUEBANNER, TRUE, (LPARAM)value.c_str());
        } else if (kind == 3 && node->kind == 7) loadImage(node, value);
    } else if (op == 3) {
        std::wstring value = wide(text);
        node->text = value;
        if (node->kind == 5 && node->inner) setEditText(node, value);
        else if (node->hwnd && (node->kind == 3 || node->kind == 4)) {
            SetWindowTextW(node->hwnd, value.c_str());
            InvalidateRect(node->hwnd, nullptr, TRUE);
        }
        YGNodeMarkDirty(node->yoga);
    } else if ((op == 4 || op == 5) && node->kind == 12) {
        // A Modal is a layer over the window's content, laid out on its own.
        if (std::find(modals.begin(), modals.end(), node) != modals.end()) return;
        modals.push_back(node);
        if (node->hwnd) {
            SetWindowPos(node->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ShowWindow(node->hwnd, SW_SHOWNA);
        }
    } else if (op == 4 || op == 5) {
        auto parentAt = nodes.find(parent);
        Node *parentNode = parentAt == nodes.end() ? nullptr : parentAt->second;
        YGNodeRef parentYoga = parentNode ? parentNode->yoga : rootYoga;
        YGNodeRef oldParent = YGNodeGetParent(node->yoga);
        if (oldParent) YGNodeRemoveChild(oldParent, node->yoga);
        Node *next = nullptr;
        if (before) { auto at = nodes.find(before); if (at != nodes.end()) next = at->second; }
        size_t index = YGNodeGetChildCount(parentYoga);
        if (next) {
            for (size_t i = 0; i < YGNodeGetChildCount(parentYoga); ++i) {
                if (YGNodeGetChild(parentYoga, i) == next->yoga) { index = i; break; }
            }
        }
        YGNodeInsertChild(parentYoga, node->yoga, index);
        insertWindow(node, containerOf(parentNode), next);
        // Under a disabled element it is disabled too.
        refreshEnabled(node);
    } else if (op == 6) {
        removeNode(node);
    } else if (op == 7 || op == 8) {
        node->listening = op == 7;
    } else {
        fprintf(stderr, "ui host: unknown op %d\n", op);
        abort();
    }
}

// ---- layout ----

// A node's frame from the window's top left in points, from the layout: false when it is not
// in the window's tree (a screen sliding out).
static bool frameOf(Node *node, double &x, double &y) {
    x = node->x;
    y = node->y;
    for (Node *up = parentNode(node); up; up = parentNode(up)) {
        x += up->x;
        y += up->y;
        if (up->kind == 6) y -= up->scrollY;
    }
    YGNodeRef top = node->yoga;
    while (YGNodeGetParent(top)) top = YGNodeGetParent(top);
    if (top == rootYoga) return true;
    for (Node *modal : modals) if (modal->yoga == top) return true;
    return false;
}

static void place(YGNodeRef yoga) {
    Node *owner = (Node *)YGNodeGetContext(yoga);
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        Node *node = (Node *)YGNodeGetContext(child);
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        node->x = YGNodeLayoutGetLeft(child);
        node->y = YGNodeLayoutGetTop(child);
        node->w = YGNodeLayoutGetWidth(child);
        node->h = YGNodeLayoutGetHeight(child);
        placeWindow(node);
        if (node->kind == 5 && node->inner) {
            // The EDIT in the field's content box, centered on its line.
            double left = YGNodeLayoutGetPadding(child, YGEdgeLeft) + YGNodeLayoutGetBorder(child, YGEdgeLeft);
            double right = YGNodeLayoutGetPadding(child, YGEdgeRight) + YGNodeLayoutGetBorder(child, YGEdgeRight);
            if (!node->styled) left += kFieldInsetX, right += kFieldInsetX;
            int line = lineHeight(fontOf(node)) + px(2);
            int width = std::max(0, px(node->w - left - right));
            int top = (px(node->h) - line) / 2;
            if (node->multiline) {
                // A multiline field's EDIT fills its content box from the top.
                double above = YGNodeLayoutGetPadding(child, YGEdgeTop) + YGNodeLayoutGetBorder(child, YGEdgeTop);
                double below = YGNodeLayoutGetPadding(child, YGEdgeBottom) + YGNodeLayoutGetBorder(child, YGEdgeBottom);
                if (!node->styled) above += kFieldInsetY, below += kFieldInsetY;
                top = px(above);
                line = std::max(line, px(node->h - above - below));
            }
            SetWindowPos(node->inner, nullptr, px(left), top, width, line, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (node->layoutListening) {
            double ax = 0, ay = 0;
            frameOf(node, ax, ay);
            int x = (int)std::lround(node->x), y = (int)std::lround(node->y), w = (int)std::lround(node->w), h = (int)std::lround(node->h);
            int wx = (int)std::lround(ax), wy = (int)std::lround(ay);
            if (x != node->reportedX || y != node->reportedY || w != node->reportedW || h != node->reportedH || wx != node->reportedWX ||
                wy != node->reportedWY) {
                node->reportedX = x; node->reportedY = y; node->reportedW = w; node->reportedH = h;
                node->reportedWX = wx; node->reportedWY = wy;
                ui_dispatch_layout(node->id, node->generation, x, y, w, h, wx, wy);
            }
        }
        place(child);
    }
    // A scroll's document is as tall as its lowest child.
    if (owner && owner->kind == 6) {
        double bottom = 0;
        for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
            YGNodeRef child = YGNodeGetChild(yoga, i);
            bottom = std::max(bottom, (double)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
        }
        owner->documentHeight = std::max((double)YGNodeLayoutGetHeight(yoga), bottom + YGNodeLayoutGetPadding(yoga, YGEdgeBottom));
        scrollTo(owner, owner->scrollY, false);
        double viewport = YGNodeLayoutGetHeight(yoga);
        if (owner->listening && viewport != owner->reportedViewport) {
            owner->reportedViewport = viewport;
            ui_dispatch_scroll(owner->id, owner->generation, (int)std::lround(owner->scrollY), (int)std::lround(viewport));
        }
    }
}

// Scrolling moves what is inside in the window without a layout: the listened nodes' window
// positions are checked again.
static void reportWindows() {
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (!node->layoutListening || !node->hwnd || node->reportedX == INT32_MIN) continue;
        double ax = 0, ay = 0;
        frameOf(node, ax, ay);
        int wx = (int)std::lround(ax), wy = (int)std::lround(ay);
        if (wx == node->reportedWX && wy == node->reportedWY) continue;
        node->reportedWX = wx;
        node->reportedWY = wy;
        ui_dispatch_layout(node->id, node->generation, node->reportedX, node->reportedY, node->reportedW, node->reportedH, wx, wy);
    }
}

static void relayout() {
    RECT client;
    GetClientRect(rootWindow, &client);
    double width = client.right / scale, height = client.bottom / scale;
    ui_grid_calculate(rootYoga, (float)width, (float)height, YGDirectionLTR);
    place(rootYoga);
    for (Node *modal : modals) {
        modal->x = modal->y = 0;
        modal->w = width;
        modal->h = height;
        if (modal->hwnd) SetWindowPos(modal->hwnd, nullptr, 0, 0, client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
        ui_grid_calculate(modal->yoga, (float)width, (float)height, YGDirectionLTR);
        place(modal->yoga);
    }
    RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

// ---- the core's requests ----

static void ui_host_command(int request, int command, int count, const char **strings);

#ifdef UI_TEST
static void driverClockTo(double now);
#endif

extern "C" void ui_host_commit(const uint8_t *data, int len) {
#ifdef UI_TEST
    // The transitions this commit starts begin at the driver's time.
    driverClockTo(ui_driver_time());
#endif
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    relayout();
    slideScreens();

}

// A frame is rendered once the messages already queued are handled (many events at once
// batch into one frame).
extern "C" void ui_host_schedule(void) {
    if (tickScheduled) return;
    tickScheduled = true;
    PostMessageW(mainWindow, WM_UI_TICK, 0, 0);
}
extern "C" void ui_host_cancel(void) { tickScheduled = false; }

extern "C" char *ui_host_asset_path(const char *name) {
    std::wstring path = exeDir + L"\\assets\\" + wide(name);
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return nullptr;
    return strdup(narrow(path.c_str()).c_str());
}

// The display clock for use_frame: a 60 Hz timer.
static bool framesOn;
extern "C" void ui_host_frames(int on) {
    framesOn = on != 0;
    if (on) SetTimer(mainWindow, kFramesTimer, 16, nullptr); else KillTimer(mainWindow, kFramesTimer);
}

// Timers for use_interval and use_timeout, by token.
static std::map<int, bool> timers;
extern "C" void ui_host_timer_start(int token, int ms, int repeat) {
    timers[token] = repeat != 0;
    SetTimer(mainWindow, kTimerBase + (UINT_PTR)token, (UINT)std::max(USER_TIMER_MINIMUM, ms), nullptr);
}
extern "C" void ui_host_timer_stop(int token) {
    KillTimer(mainWindow, kTimerBase + (UINT_PTR)token);
    timers.erase(token);
}

// An alert, shown once the commit is applied: a task dialog with the app's buttons.
struct Alert { int request; std::wstring title, message; std::vector<std::wstring> buttons; };
static void showAlert(Alert alert) {
    std::vector<TASKDIALOG_BUTTON> buttons;
    for (size_t i = 0; i < alert.buttons.size(); i++) buttons.push_back({(int)(1000 + i), alert.buttons[i].c_str()});
    TASKDIALOGCONFIG config = {sizeof config};
    config.hwndParent = mainWindow;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = appName.c_str();
    config.pszMainInstruction = alert.title.c_str();
    config.pszContent = alert.message.c_str();
    config.cButtons = (UINT)buttons.size();
    config.pButtons = buttons.empty() ? nullptr : buttons.data();
    if (buttons.empty()) config.dwCommonButtons = TDCBF_OK_BUTTON;
    typedef HRESULT(WINAPI * TaskDialogFn)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
    TaskDialogFn show = (TaskDialogFn)(void *)GetProcAddress(GetModuleHandleW(L"comctl32.dll"), "TaskDialogIndirect");
    int pressed = -1;
    if (show) show(&config, &pressed, nullptr, nullptr);
    else {
        std::wstring text = alert.title + L"\n\n" + alert.message;
        pressed = MessageBoxW(mainWindow, text.c_str(), appName.c_str(), MB_OKCANCEL) == IDOK ? 1000 : IDCANCEL;
    }
    int index = pressed >= 1000 && pressed < 1000 + (int)alert.buttons.size() ? pressed - 1000 : -1;
    ui_dispatch_alert(alert.request, index);
}

static void copyText(const std::wstring &text) {
    if (!OpenClipboard(mainWindow)) return;
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

static std::wstring pasteText() {
    std::wstring text;
    if (!OpenClipboard(mainWindow)) return text;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t *p = (const wchar_t *)GlobalLock(data);
        if (p) text = p;
        GlobalUnlock(data);
    }
    CloseClipboard();
    return text;
}

// Commands: 1 an alert, 2 open a URL, 4 hide the keyboard, 5 share, 6 copy, 7 paste, 8 a
// haptic, 9 announce, 10 fetch, 11 the splash (a desktop window has none).
static void ui_host_command(int request, int command, int count, const char **strings) {
    if (command == 1 && count >= 2) {
        Alert alert{request, wide(strings[0]), wide(strings[1]), {}};
        for (int i = 2; i < count; i++) alert.buttons.push_back(wide(strings[i]));
        onMain([alert]() { showAlert(alert); });
    } else if (command == 2 && count >= 1) {
        std::wstring url = strcmp(strings[0], "app-settings:") == 0 ? L"ms-settings:" : wide(strings[0]);
        ShellExecuteW(mainWindow, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (command == 4) {
        HWND focus = GetFocus();
        if (focus && IsChild(mainWindow, focus)) SetFocus(mainWindow);
    } else if (command == 5 && count >= 2) {
        // A desktop has no share sheet here: the text and the URL go to the clipboard.
        std::wstring text = wide(strings[0]);
        if (strings[1][0]) text += (text.empty() ? L"" : L" ") + wide(strings[1]);
        copyText(text);
    } else if (command == 6 && count >= 1) {
        copyText(wide(strings[0]));
    } else if (command == 7) {
        ui_dispatch_answer(request, narrow(pasteText().c_str()).c_str());
    } else if (command == 9 && count >= 1) {
        // Announced through the window's accessible name change, which screen readers follow.
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, mainWindow, OBJID_CLIENT, CHILDID_SELF);
    } else if (command == 10 && count >= 6) {
        std::wstring url = wide(strings[0]);
        std::string method = strings[1], headers = strings[2], body = strings[3];
        bool hasBody = strings[5][0] == '1';
        int timeout = atoi(strings[4]);
        std::string saveTo = count >= 7 ? strings[6] : "";
        std::thread([request, url, method, headers, body, hasBody, timeout, saveTo]() {
            int status = 0;
            std::vector<uint8_t> bytes;
            std::string responseHeaders, error;
            bool ok = httpGet(url, method, headers, hasBody ? &body : nullptr, timeout, status, bytes, responseHeaders, error);
            std::string text;
            if (!ok) status = 0, text = error;
            else if (!saveTo.empty() && status >= 200 && status < 300) {
                FILE *file = _wfopen(wide(saveTo.c_str()).c_str(), L"wb");
                if (!file || fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) status = 0, text = "could not write " + saveTo;
                if (file) fclose(file);
            } else text.assign(bytes.begin(), bytes.end());
            onMain([request, status, text, responseHeaders]() { ui_dispatch_fetch(request, status, text.c_str(), responseHeaders.c_str()); });
        }).detach();
    }
}

// ---- the system's settings ----

static bool systemDark() {
    DWORD light = 1, size = sizeof light;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        return false;
    return light == 0;
}

// Dark menus and title bar: uxtheme's SetPreferredAppMode (an ordinal since Windows 10 1903).
static void allowDarkMenus(bool dark) {
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxtheme) uxtheme = LoadLibraryW(L"uxtheme.dll");
    typedef int(WINAPI * SetModeFn)(int);
    typedef void(WINAPI * FlushFn)();
    auto setMode = uxtheme ? (SetModeFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)) : nullptr;
    auto flush = uxtheme ? (FlushFn)(void *)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(dark ? 2 : 3);
    if (flush) flush();
}

static void applyAppearance() {
    darkMode = systemDark();
    BOOL dark = darkMode;
    DwmSetWindowAttribute(mainWindow, 20, &dark, sizeof dark);
    allowDarkMenus(darkMode);
    for (auto &entry : nodes) applyTheme(entry.second);
    DrawMenuBar(mainWindow);
    RedrawWindow(mainWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_FRAME);
}

static void reportSystem() {
    ui_report_appearance(darkMode ? 1 : 0);
    HIGHCONTRASTW contrast = {sizeof contrast};
    BOOL animations = TRUE, reader = FALSE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    SystemParametersInfoW(SPI_GETSCREENREADER, 0, &reader, 0);
    (void)contrast;
    ui_report_accessibility(reader ? 1 : 0, animations ? 0 : 1, (int)std::lround(textScale * 100));
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"en-US";
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    DWORD reading = 0;
    GetLocaleInfoEx(locale, LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER, (LPWSTR)&reading, sizeof reading / sizeof(wchar_t));
    ui_report_locale(narrow(locale).c_str(), reading == 1 ? 1 : 0);
}

static void reportWindow() {
    RECT client;
    GetClientRect(rootWindow, &client);
    ui_report_window((int)std::lround(client.right / scale), (int)std::lround(client.bottom / scale), (int)std::lround(scale * 100));
}

static void reportEnvironment() {
    reportWindow();
    ui_report_insets(0, 0, 0, 0, 0);
    reportSystem();
}

static std::wstring dataDir() {
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
        dir = std::wstring(roaming) + L"\\" + appName;
        CoTaskMemFree(roaming);
    }
    if (!dir.empty()) SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

// ---- reload under `ui serve` ----

#ifdef UI_RELOAD
#ifdef UI_TEST
static void driverClock(bool owned, double now, bool settle);
#endif
// `ui serve`'s builds: UI_RELOAD_FILE names a file holding the path of the app's newest DLL.
// When it changes, the app starts again on the new code in the running process: the old core
// saves its routes and signals and stops, the windows, timers and nodes it left go, and the
// new one starts on what the old one saved.
static std::string loadedCore;

static std::string newestCore() {
    const char *file = getenv("UI_RELOAD_FILE");
    if (!file) return "";
    FILE *f = fopen(file, "rb");
    if (!f) return "";
    char buffer[4096] = {};
    size_t n = fread(buffer, 1, sizeof buffer - 1, f);
    fclose(f);
    std::string text(buffer, n);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

static void restartCore(const std::string &path) {
#ifdef UI_TEST
    char *kept = ui_reload_save ? strdup(ui_reload_save()) : nullptr;
#endif
    ui_stop();
#ifdef UI_TEST
    driverClock(false, 0, false);
#endif
    tickScheduled = false;
    ui_host_frames(0);
    for (auto &timer : timers) KillTimer(mainWindow, kTimerBase + (UINT_PTR)timer.first);
    timers.clear();
    animations.clear();
    modals.clear();
    hovered.clear();
    gesture = {};
    slidingIn.clear();
    std::vector<Node *> all;
    for (auto &entry : nodes) all.push_back(entry.second);
    nodes.clear();
    for (Node *node : all) {
        if (node->hwnd) DestroyWindow(node->hwnd);
    }
    for (Node *node : all) { delete node->emitter; ui_grid_free(node->yoga); delete node; }
    windowNodes.clear();
    for (HWND hwnd : std::set<HWND>(leavingWindows)) DestroyWindow(hwnd);
    leavingWindows.clear();
    YGNodeRemoveAllChildren(rootYoga);
    if (!loadCore(path.c_str())) {
#ifdef UI_TEST
        free(kept);
#endif
        return;
    }
    loadedCore = path;
    ui_report_data_dir(narrow(dataDir().c_str()).c_str());
#ifdef UI_TEST
    if (kept && ui_reload_restore) ui_reload_restore(kept);
    free(kept);
#endif
    ui_start(launchExample);
    reportEnvironment();
    relayout();
    size_t slash = path.find_last_of("\\/");
    fprintf(stderr, "ui: restarted on %s\n", path.substr(slash == std::string::npos ? 0 : slash + 1).c_str());
}
#endif
