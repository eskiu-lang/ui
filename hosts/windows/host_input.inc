// host_input.inc: measuring, the host's clock and transitions, windows, presses, hovers, drags and scrolling. Part of host.cpp, which #includes it (one translation unit).

// ---- measuring ----

// A memory DC for measuring, made once.
static HDC measureDC() {
    static HDC hdc;
    if (!hdc) hdc = CreateCompatibleDC(nullptr);
    return hdc;
}

static SIZE measureText(HFONT font, const std::wstring &text, int limit, bool wrap) {
    HDC hdc = measureDC();
    HGDIOBJ old = SelectObject(hdc, font);
    RECT r = {0, 0, wrap && limit > 0 ? limit : 0x3fffffff, 0};
    std::wstring shown = text.empty() ? L" " : text;
    UINT flags = DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS | (wrap ? DT_WORDBREAK : DT_SINGLELINE);
    DrawTextW(hdc, shown.c_str(), (int)shown.size(), &r, flags);
    if (text.empty()) r.right = 0;
    SelectObject(hdc, old);
    return {r.right - r.left, r.bottom - r.top};
}

static int lineHeight(HFONT font) {
    static std::map<HFONT, int> heights;
    auto found = heights.find(font);
    if (found != heights.end()) return found->second;
    HDC hdc = measureDC();
    HGDIOBJ old = SelectObject(hdc, font);
    TEXTMETRICW metrics;
    GetTextMetricsW(hdc, &metrics);
    SelectObject(hdc, old);
    return heights[font] = metrics.tmHeight;
}

// The field's inner insets in points when the app does not style it.
static const int kFieldInsetX = 10, kFieldInsetY = 6;

static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    Node *node = (Node *)YGNodeGetContext(yoga);
    double limitW = widthMode == YGMeasureModeUndefined ? 1e9 : width;
    double limitH = heightMode == YGMeasureModeUndefined ? 1e9 : height;
    double w = 0, h = 0;
    if (node->kind == 3) {
        SIZE size = measureText(fontOf(node), node->text, limitW < 1e8 ? (int)std::floor(limitW * scale) : 0, true);
        w = size.cx / scale;
        h = size.cy / scale;
    } else if (node->kind == 4) {
        if (node->styled) {
            SIZE size = measureText(fontOf(node), node->text, 0, false);
            w = size.cx / scale;
            h = size.cy / scale;
        } else {
            // The system's button: its title in the control font, as Windows 11 pads it.
            SIZE size = measureText(controlFont(), node->text, 0, false);
            w = size.cx / scale + 24;
            h = std::max(32.0, size.cy / scale + 12);
        }
    } else if (node->kind == 5) {
        double line = lineHeight(fontOf(node)) / scale + 2;
        w = 120;
        h = node->styled ? line : line + 2 * kFieldInsetY;
    } else if (node->kind == 7) {
        if (node->image) { w = node->image->GetWidth(); h = node->image->GetHeight(); }
    } else if (node->kind == 9) {
        w = 40; h = 20;
    } else if (node->kind == 10) {
        w = 20; h = 20;
    } else if (node->kind == 15) {
        w = 160; h = 32;
    }
    return {(float)std::min(std::ceil(w), limitW), (float)std::min(std::ceil(h), limitH)};
}

// ---- the host's clock and transitions ----

// The host's time in ms: the platform's, or the driver's while it owns the clock (a dev
// build), where `clock settle` moves it past every transition.
static bool clockOwned;
static double driverNow, settleBonus;
static double hostNow() { return clockOwned ? driverNow + settleBonus : nowMs(); }

struct Animation {
    Node *node;
    int property;  // 0 opacity, 1 translate x, 2 translate y, 3 background, 4 a screen's slide
    double from, to, start, duration;
    int easing;
    int fromColor, toColor;
    std::function<void()> done;
};
static std::vector<Animation> animations;

static double eased(double t, int easing) {
    t = std::min(1.0, std::max(0.0, t));
    switch (easing) {
        case 1: return 1 - (1 - t) * (1 - t) * (1 - t);
        case 2: return t * t * t;
        case 3: return t;
        case 4: {
            // A spring (damping 14, stiffness 100, mass 1), as the other hosts' default.
            double w0 = 10, zeta = 0.7, wd = w0 * std::sqrt(1 - zeta * zeta), s = t * 1.2;
            return 1 - std::exp(-zeta * w0 * s) * (std::cos(wd * s) + zeta * w0 / wd * std::sin(wd * s));
        }
        default: return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    }
}

static int mixColor(int a, int b, double t) {
    uint32_t x = (uint32_t)a, y = (uint32_t)b;
    auto c = [&](int shift) { return (int)std::lround(((x >> shift) & 255) * (1 - t) + ((y >> shift) & 255) * t); };
    return (int)(((uint32_t)c(24) << 24) | ((uint32_t)c(16) << 16) | ((uint32_t)c(8) << 8) | (uint32_t)c(0));
}

static void applyOpacity(Node *node);
static void placeWindow(Node *node);

static void showAnimated(Animation &a, double t) {
    Node *node = a.node;
    double v = a.from + (a.to - a.from) * eased(t, a.easing);
    if (a.property == 0) { node->shownOpacity = v; applyOpacity(node); }
    else if (a.property == 1) { node->shownX = v; placeWindow(node); }
    else if (a.property == 2) { node->shownY = v; placeWindow(node); }
    else if (a.property == 4) { node->slideX = v; placeWindow(node); }
    else if (a.property == 5) {}
    else {
        node->shownBackground = t >= 1 ? a.toColor : mixColor(a.fromColor, a.toColor, eased(t, a.easing));
        if (node->hwnd) RedrawWindow(node->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

static bool spinnersShowing();
static void stepAnimations() {
    double now = hostNow();
    std::vector<std::function<void()>> finished;
    for (size_t i = 0; i < animations.size();) {
        Animation &a = animations[i];
        double t = a.duration <= 0 ? 1 : (now - a.start) / a.duration;
        showAnimated(a, t);
        if (t >= 1) {
            if (a.done) finished.push_back(a.done);
            animations.erase(animations.begin() + (long)i);
        } else i++;
    }
    for (auto &done : finished) done();
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (node->kind == 10 && node->spinning && node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
    }
    if (animations.empty() && !spinnersShowing()) KillTimer(mainWindow, kAnimTimer);
}

static void animationsRun() { SetTimer(mainWindow, kAnimTimer, 16, nullptr); }

// A property moved to `to`, through the node's transition when it has one and shows.
static void animate(Node *node, int property, double from, double to, std::function<void()> done = nullptr,
                    double duration = -1, int easing = -1) {
    for (size_t i = 0; i < animations.size(); i++) {
        if (animations[i].node == node && animations[i].property == property) { animations.erase(animations.begin() + (long)i); break; }
    }
    double ms = duration >= 0 ? duration : node->transitionMs;
    Animation a{node, property, from, to, hostNow(), ms, easing >= 0 ? easing : node->easing, 0, 0, done};
    if (ms <= 0 || !node->hwnd || !IsWindowVisible(node->hwnd)) {
        showAnimated(a, 1);
        if (done) done();
        return;
    }
    animations.push_back(a);
    animationsRun();
}
static void animateColor(Node *node, int to) {
    for (size_t i = 0; i < animations.size(); i++) {
        if (animations[i].node == node && animations[i].property == 3) { animations.erase(animations.begin() + (long)i); break; }
    }
    Animation a{node, 3, 0, 1, hostNow(), (double)node->transitionMs, node->easing, node->shownBackground, to, nullptr};
    if (node->transitionMs <= 0 || !node->hwnd || !IsWindowVisible(node->hwnd)) { showAnimated(a, 1); return; }
    animations.push_back(a);
    animationsRun();
}
static void dropAnimations(Node *node) {
    for (size_t i = 0; i < animations.size();) {
        if (animations[i].node == node) animations.erase(animations.begin() + (long)i); else i++;
    }
}

static bool spinnersShowing() {
    if (clockOwned) return false;
    for (auto &entry : nodes) if (entry.second->kind == 10 && entry.second->spinning) return true;
    return false;
}

// ---- windows ----

static const wchar_t *kViewClass = L"EskiuUiView", *kScrollClass = L"EskiuUiScroll", *kImageClass = L"EskiuUiImage",
                     *kSpinnerClass = L"EskiuUiSpinner", *kMainClass = L"EskiuUiWindow";

static LRESULT CALLBACK nodeSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);

// The window children of `node` go in.
static HWND containerOf(Node *node) {
    if (!node) return rootWindow;
    if (node->kind == 6 && node->inner) return node->inner;
    return node->hwnd;
}

static void applyOpacity(Node *node) {
    if (!node->hwnd) return;
    double alpha = node->shownOpacity / 100.0;
    if (node->pressed) alpha *= 0.7;
    LONG ex = GetWindowLongW(node->hwnd, GWL_EXSTYLE);
    if (alpha >= 0.999) {
        if (ex & WS_EX_LAYERED) SetWindowLongW(node->hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        return;
    }
    if (!(ex & WS_EX_LAYERED)) SetWindowLongW(node->hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(node->hwnd, 0, (BYTE)std::lround(std::max(0.0, alpha) * 255), LWA_ALPHA);
}

// The node's window at its laid out frame, moved by its translation and a screen's slide.
static void placeWindow(Node *node) {
    if (!node->hwnd || node->kind == 12) return;
    int x = px(node->x + node->shownX + node->slideX), y = px(node->y + node->shownY);
    int w = std::max(0, px(node->x + node->w) - px(node->x)), h = std::max(0, px(node->y + node->h) - px(node->y));
    RECT want = {x, y, w, h};
    if (EqualRect(&want, &node->placed)) return;
    node->placed = want;
    SetWindowPos(node->hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static HWND makeWindow(const wchar_t *cls, const wchar_t *text, DWORD style, HWND parent, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 0, 0, parent ? parent : rootWindow, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

static void applyTheme(Node *node) {
    if (!node->hwnd) return;
    const wchar_t *theme = darkMode ? L"DarkMode_Explorer" : nullptr;
    if (node->kind == 4 || node->kind == 6) SetWindowTheme(node->hwnd, theme, nullptr);
    if (node->kind == 5 && node->inner) SetWindowTheme(node->inner, darkMode ? L"DarkMode_CFD" : nullptr, nullptr);
}

// ---- press, hover, long press and drags ----

// The gesture under way: the pressable a click goes to, the node a drag goes to, and where it began.
static struct {
    Node *press = nullptr, *pan = nullptr, *longPress = nullptr;
    int pressId = 0, panId = 0;
    POINT start = {};
    bool panning = false, captured = false;
    double lastX = 0, lastY = 0, lastTime = 0, vx = 0, vy = 0;
} gesture;

static Node *findUp(Node *node, std::function<bool(Node *)> test) {
    for (Node *at = node; at; at = parentNode(at)) if (test(at)) return at;
    return nullptr;
}

static void setPressed(Node *node, bool pressed) {
    if (!node || node->pressed == pressed) return;
    node->pressed = pressed;
    applyOpacity(node);
    if (node->hwnd) RedrawWindow(node->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static bool alive(Node *node, int id) {
    auto found = nodes.find(id);
    return found != nodes.end() && found->second == node;
}

static void gestureDown(HWND hwnd, Node *node, POINT screen) {
    gesture.press = findUp(node, [](Node *n) { return n->kind == 8; });
    gesture.pan = findUp(node, [](Node *n) { return n->panListening; });
    gesture.longPress = findUp(node, [](Node *n) { return n->longListening; });
    gesture.pressId = gesture.press ? gesture.press->id : 0;
    gesture.panId = gesture.pan ? gesture.pan->id : 0;
    gesture.start = screen;
    gesture.panning = false;
    gesture.lastX = screen.x;
    gesture.lastY = screen.y;
    gesture.lastTime = nowMs();
    gesture.vx = gesture.vy = 0;
    SetCapture(hwnd);
    gesture.captured = true;
    setPressed(gesture.press, true);
    if (gesture.longPress) SetTimer(mainWindow, kLongPressTimer, 500, nullptr);
}

static void gestureMove(POINT screen) {
    if (!gesture.captured) return;
    double dx = (screen.x - gesture.start.x) / scale, dy = (screen.y - gesture.start.y) / scale;
    double now = nowMs(), dt = std::max(1.0, now - gesture.lastTime);
    gesture.vx = (screen.x - gesture.lastX) / scale * 1000 / dt;
    gesture.vy = (screen.y - gesture.lastY) / scale * 1000 / dt;
    gesture.lastX = screen.x; gesture.lastY = screen.y; gesture.lastTime = now;
    if (!gesture.panning && std::hypot(dx, dy) > 6) {
        KillTimer(mainWindow, kLongPressTimer);
        gesture.longPress = nullptr;
        if (gesture.pan && alive(gesture.pan, gesture.panId)) {
            gesture.panning = true;
            setPressed(gesture.press, false);
            gesture.press = nullptr;
            ui_dispatch_pan(gesture.pan->id, gesture.pan->generation, 0, 0, 0, 0, 0);
        } else if (gesture.press) {
            // A press that moved away from its pressable stops being one.
            RECT r;
            GetWindowRect(gesture.press->hwnd, &r);
            setPressed(gesture.press, PtInRect(&r, screen) != 0);
        }
    }
    if (gesture.panning && alive(gesture.pan, gesture.panId))
        ui_dispatch_pan(gesture.pan->id, gesture.pan->generation, 1, (int)std::lround(dx), (int)std::lround(dy),
                        (int)std::lround(gesture.vx), (int)std::lround(gesture.vy));
}

static void gestureUp(POINT screen, bool cancelled) {
    if (!gesture.captured) return;
    gesture.captured = false;
    KillTimer(mainWindow, kLongPressTimer);
    if (GetCapture()) ReleaseCapture();
    if (gesture.panning && alive(gesture.pan, gesture.panId)) {
        double dx = (screen.x - gesture.start.x) / scale, dy = (screen.y - gesture.start.y) / scale;
        ui_dispatch_pan(gesture.pan->id, gesture.pan->generation, cancelled ? 3 : 2, (int)std::lround(dx), (int)std::lround(dy),
                        (int)std::lround(gesture.vx), (int)std::lround(gesture.vy));
    }
    Node *press = gesture.press;
    if (press && alive(press, gesture.pressId)) {
        bool inside = press->pressed;
        setPressed(press, false);
        if (inside && !cancelled) ui_dispatch(press->id, press->generation);
    }
    gesture.press = gesture.pan = gesture.longPress = nullptr;
    gesture.panning = false;
}

// Hover: the listening nodes under the pointer, entered and left as it moves.
static std::set<int> hovered;
static void updateHover() {
    POINT at;
    GetCursorPos(&at);
    std::set<int> now;
    HWND under = WindowFromPoint(at);
    if (under && (under == mainWindow || IsChild(mainWindow, under))) {
        for (Node *node = owningNode(under); node; node = parentNode(node)) if (node->hoverListening) now.insert(node->id);
    }
    for (int id : hovered) {
        if (now.count(id)) continue;
        auto found = nodes.find(id);
        if (found != nodes.end()) ui_dispatch_hover(id, found->second->generation, 0);
    }
    for (int id : now) {
        if (hovered.count(id)) continue;
        ui_dispatch_hover(id, nodes[id]->generation, 1);
    }
    hovered = now;
    if (hovered.empty()) KillTimer(mainWindow, kHoverTimer); else SetTimer(mainWindow, kHoverTimer, 100, nullptr);
}

// ---- scrolling ----

static void reportWindows();
static double scrollMost(Node *node) { return std::max(0.0, node->documentHeight - node->h); }

static void scrollTo(Node *node, double y, bool report) {
    y = std::min(scrollMost(node), std::max(0.0, y));
    node->scrollY = y;
    if (node->inner) {
        RECT r;
        GetClientRect(node->hwnd, &r);
        SetWindowPos(node->inner, nullptr, 0, -px(y), r.right, std::max((int)r.bottom, px(node->documentHeight)),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SCROLLINFO info = {sizeof info, SIF_ALL | SIF_DISABLENOSCROLL};
    info.nMin = 0;
    info.nMax = std::max(0, px(node->documentHeight) - 1);
    info.nPage = (UINT)std::max(0, px(node->h));
    info.nPos = px(y);
    SetScrollInfo(node->hwnd, SB_VERT, &info, TRUE);
    ShowScrollBar(node->hwnd, SB_VERT, node->documentHeight > node->h + 0.5);
    if (!report) return;
    reportWindows();
    if (node->listening) {
        node->reportedViewport = node->h;
        ui_dispatch_scroll(node->id, node->generation, (int)std::lround(y), (int)std::lround(node->h));
    }
}
