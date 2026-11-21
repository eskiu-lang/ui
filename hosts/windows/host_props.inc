// host_props.inc: props 73 to 91 (padding sides and margins through hosts/layout.h, text
// align, maximum lines, shrink, the image listener, line height, italic, z index, clip, layout
// animation, a modal's presentation and appearing), a Text's own painting, a slider's accent
// (prop 9) and pull to refresh. Part of host.cpp, which #includes it (one translation unit).

static bool reducedMotion() {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    return !animations;
}

// Work after the current commit (a callback the core gets while it applies one would nest).
static void later(std::function<void()> work) {
    PostMessageW(mainWindow, WM_UI_RUN, 0, (LPARAM) new std::function<void()>(std::move(work)));
}

// Whether an image loaded, to the app when it listens (prop 84).
static void imageReport(Node *node, bool loaded) {
    if (!node->imageListening) return;
    int id = node->id, generation = node->generation;
    later([id, generation, loaded]() { if (nodes.count(id)) ui_dispatch_image(id, generation, loaded ? 1 : 0); });
}

// ---- a Text ----

// A Text's lines at `width` pixels (0: no limit) in `font`, broken at spaces as DrawText's word
// break does, and a word wider than the line at a character.
static std::vector<std::wstring> textLines(HFONT font, const std::wstring &text, int width) {
    std::vector<std::wstring> lines;
    HDC hdc = measureDC();
    HGDIOBJ old = SelectObject(hdc, font);
    auto fits = [&](const std::wstring &s) {
        SIZE size;
        GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &size);
        return width <= 0 || size.cx <= width;
    };
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        std::wstring paragraph = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        std::wstring line;
        size_t at = 0;
        while (at < paragraph.size()) {
            size_t space = paragraph.find(L' ', at);
            std::wstring word = paragraph.substr(at, space == std::wstring::npos ? std::wstring::npos : space - at + 1);
            at = space == std::wstring::npos ? paragraph.size() : space + 1;
            if (fits(line + word)) { line += word; continue; }
            if (!line.empty()) { lines.push_back(line); line.clear(); }
            while (!word.empty() && !fits(word)) {
                size_t n = 1;
                while (n < word.size() && fits(word.substr(0, n + 1))) n++;
                lines.push_back(word.substr(0, n));
                word = word.substr(n);
            }
            line = word;
        }
        lines.push_back(line);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    SelectObject(hdc, old);
    return lines;
}

// A Text's height of a line in pixels: its line height, else the font's.
static int textLine(Node *node) { return node->lineHeight > 0 ? px(node->lineHeight) : lineHeight(fontOf(node)); }

// A Text's size at `limit` pixels across (0: no limit), when it has a line height or a
// maximum of lines (else DrawText's own measure).
static SIZE measureLines(Node *node, int limit) {
    std::vector<std::wstring> lines = textLines(fontOf(node), node->text, limit);
    size_t shown = node->maxLines > 0 ? std::min(lines.size(), (size_t)node->maxLines) : lines.size();
    HDC hdc = measureDC();
    HGDIOBJ old = SelectObject(hdc, fontOf(node));
    LONG widest = 0;
    for (size_t i = 0; i < shown; i++) {
        SIZE size;
        GetTextExtentPoint32W(hdc, lines[i].c_str(), (int)lines[i].size(), &size);
        widest = std::max(widest, size.cx);
    }
    SelectObject(hdc, old);
    if (limit > 0) widest = std::min<LONG>(widest, limit);
    return {widest, (LONG)(shown * textLine(node))};
}

// DrawText's alignment for Style.text_align: the start and the end follow the layout's
// direction (a justified Text is drawn from the start).
static UINT textAlignFlags(Node *node) {
    bool rtl = ui_layout_rtl();
    UINT flags = rtl ? DT_RTLREADING : 0;
    if (node->textAlign == 2) return flags | DT_CENTER;
    if (node->textAlign == 3) return flags | (rtl ? DT_LEFT : DT_RIGHT);
    return flags | (rtl ? DT_RIGHT : DT_LEFT);
}

// A Text, painted by the host: inside its padding, in its font, color and alignment, with its
// line height and its maximum of lines (the last cut with an ellipsis).
static void paintText(Node *node, HDC hdc, RECT rect) {
    FillRect(hdc, &rect, brushOf(ownBackdrop(node)));
    RECT inside = rect;
    if (node->yoga) {
        inside.left += px(YGNodeLayoutGetPadding(node->yoga, YGEdgeLeft));
        inside.top += px(YGNodeLayoutGetPadding(node->yoga, YGEdgeTop));
        inside.right -= px(YGNodeLayoutGetPadding(node->yoga, YGEdgeRight));
        inside.bottom -= px(YGNodeLayoutGetPadding(node->yoga, YGEdgeBottom));
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colorRef(node->textColor ? node->textColor : defaultText()));
    HGDIOBJ old = SelectObject(hdc, fontOf(node));
    UINT align = textAlignFlags(node) | DT_NOPREFIX | DT_EXPANDTABS;
    if (node->lineHeight <= 0 && node->maxLines <= 0) {
        DrawTextW(hdc, node->text.c_str(), (int)node->text.size(), &inside, align | DT_WORDBREAK);
    } else {
        std::vector<std::wstring> lines = textLines(fontOf(node), node->text, inside.right - inside.left);
        size_t shown = node->maxLines > 0 ? std::min(lines.size(), (size_t)node->maxLines) : lines.size();
        int line = textLine(node), font = lineHeight(fontOf(node));
        for (size_t i = 0; i < shown; i++) {
            RECT r = {inside.left, inside.top + (int)i * line + (line - font) / 2, inside.right, inside.top + (int)(i + 1) * line};
            std::wstring text = lines[i];
            // The last line shown holds the rest, cut with an ellipsis.
            if (i + 1 == shown && shown < lines.size()) for (size_t j = i + 1; j < lines.size(); j++) text += lines[j];
            DrawTextW(hdc, text.c_str(), (int)text.size(), &r, align | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    SelectObject(hdc, old);
}

// ---- stacking, clipping and the layout animation ----

// Whether any element set a z index (until then the children's order is the windows').
static bool stacking;

// A container's node windows by Style.z_index, then in the children's order: the last is on
// top of Windows' z-order.
static void restack(Node *parent) {
    if (!stacking) return;
    YGNodeRef yoga = parent ? parent->yoga : rootYoga;
    std::vector<std::pair<Node *, size_t>> children;
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) children.push_back({(Node *)YGNodeGetContext(YGNodeGetChild(yoga, i)), i});
    std::stable_sort(children.begin(), children.end(), [](const std::pair<Node *, size_t> &a, const std::pair<Node *, size_t> &b) {
        return a.first->zIndex != b.first->zIndex ? a.first->zIndex < b.first->zIndex : a.second < b.second;
    });
    for (auto &entry : children) {
        if (entry.first->hwnd) SetWindowPos(entry.first->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// Style.clip: a child window never draws outside its parent's; with a radius the window's
// region is its rounded box, which clips what is inside too.
static void applyClip(Node *node) {
    if (!node->hwnd) return;
    if (!node->clip || node->radius <= 0) { SetWindowRgn(node->hwnd, nullptr, TRUE); return; }
    int w = std::max(0, px(node->x + node->w) - px(node->x)), h = std::max(0, px(node->y + node->h) - px(node->y));
    int d = 2 * std::min(px(node->radius), std::min(w, h) / 2);
    SetWindowRgn(node->hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
}

// A frame the layout changed: moved there over the layout animation (prop 90) when the window
// shows with a frame, at once otherwise.
static void moveFrame(Node *node, double x, double y, double w, double h) {
    bool had = node->w > 0 || node->h > 0;
    double was[4] = {node->x, node->y, node->w, node->h};
    if (node->frameT < 1) {
        for (int i = 0; i < 4; i++) was[i] = node->frameFrom[i] + (was[i] - node->frameFrom[i]) * eased(node->frameT, node->easing);
    }
    bool moved = x != node->x || y != node->y || w != node->w || h != node->h;
    node->x = x; node->y = y; node->w = w; node->h = h;
    if (!moved || node->layoutMs <= 0 || !had || reducedMotion()) return;
    std::copy(was, was + 4, node->frameFrom);
    node->frameT = 0;
    animate(node, 6, 0, 1, nullptr, node->layoutMs, 3);
}

// ---- props ----

// A slider in the app's accent (Style.color): the trackbar's channel filled to the thumb, and
// the thumb, drawn by the host (NM_CUSTOMDRAW).
static LRESULT paintSliderAccent(Node *node, NMCUSTOMDRAW *draw) {
    if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    Gdiplus::Graphics g(draw->hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    RECT thumb;
    SendMessageW(node->hwnd, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
    float mid = (thumb.left + thumb.right) / 2.0f;
    if (draw->dwItemSpec == TBCD_CHANNEL) {
        RECT r = draw->rc;
        float cy = (r.top + r.bottom) / 2.0f, bar = (float)px(4);
        Gdiplus::SolidBrush rest(colorOf(darkMode ? rgba(90, 90, 90) : rgba(200, 200, 200)));
        Gdiplus::SolidBrush filled(colorOf(node->textColor));
        g.FillRectangle(&rest, (float)r.left, cy - bar / 2, (float)(r.right - r.left), bar);
        g.FillRectangle(&filled, (float)r.left, cy - bar / 2, mid - r.left, bar);
        return CDRF_SKIPDEFAULT;
    }
    if (draw->dwItemSpec == TBCD_THUMB) {
        float size = (float)std::min(thumb.right - thumb.left, thumb.bottom - thumb.top) + 4;
        float cy = (thumb.top + thumb.bottom) / 2.0f;
        Gdiplus::SolidBrush fill(colorOf(node->textColor));
        g.FillEllipse(&fill, mid - size / 2, cy - size / 2, size, size);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

static void applyAccent(Node *node) {
    if (node->hwnd && (node->kind == 9 || node->kind == 15)) InvalidateRect(node->hwnd, nullptr, TRUE);
}

// Props 73 to 91; false for any other code.
static bool styleProp(Node *node, int code, int value) {
    if (ui_layout_prop(node->yoga, code, value)) {
        if (node->kind == 3 && node->hwnd) InvalidateRect(node->hwnd, nullptr, FALSE);
        return true;
    }
    switch (code) {
    case 81:
        node->textAlign = value;
        if (node->kind == 5 && node->inner) {
            LONG style = GetWindowLongW(node->inner, GWL_STYLE) & ~(ES_CENTER | ES_RIGHT);
            SetWindowLongW(node->inner, GWL_STYLE, style | (value == 2 ? ES_CENTER : value == 3 ? ES_RIGHT : 0));
        }
        break;
    case 82: node->maxLines = value; break;
    case 83:
        node->shrinkSet = value != 0;
        if (value) YGNodeStyleSetFlexShrink(node->yoga, (float)value);
        else YGNodeStyleSetFlexShrink(node->yoga, node->kind == 6 || YGNodeStyleGetFlexGrow(node->yoga) > 0 ? 1.0f : 0.0f);
        return true;
    case 84: node->imageListening = value != 0; return true;
    case 85: node->lineHeight = value; break;
    case 86: node->italic = value != 0; applyFont(node); break;
    case 87: node->zIndex = value; stacking = stacking || value; restack(parentNode(node)); return true;
    case 88: node->clip = value != 0; applyClip(node); return true;
    case 89: node->presentation = value; return true;
    case 90: node->layoutMs = value; return true;
    case 91: node->appear = value; return true;
    default: return false;
    }
    if (YGNodeHasMeasureFunc(node->yoga)) YGNodeMarkDirty(node->yoga);
    if (node->hwnd) InvalidateRect(node->hwnd, nullptr, TRUE);
    return true;
}

// A modal over the window as it opens (props 89 and 91): the layer fades in, a sheet's panel
// slides up from the bottom edge; nothing when it appears at once or the system turns
// animations off. A Modal (91 = 0) appears as it always did.
static void appearModals() {
    for (Node *modal : modals) {
        if (modal->appeared || !modal->hwnd) continue;
        modal->appeared = true;
        if (modal->appear != 1 || reducedMotion()) continue;
        animate(modal, 0, 0, modal->opacity, nullptr, 220, 1);
        Node *panel = YGNodeGetChildCount(modal->yoga) ? (Node *)YGNodeGetContext(YGNodeGetChild(modal->yoga, 0)) : nullptr;
        if (panel && modal->presentation == 1) animate(panel, 2, panel->h, panel->translateY, nullptr, 220, 1);
    }
}

// ---- pull to refresh ----

// A scroll with a RefreshControl (prop 22). A desktop has no pull of its own: the wheel turned
// up at the top of the scroll by two notches within a second, or View > Refresh (F5) for the
// refreshable scroll on top, asks the app to refresh, and a spinner at the top of the scroll
// turns while the app says it is refreshing.
static void pullRefresh(Node *node) {
    if (!node->refreshable || node->refreshing) return;
    node->pulled = 0;
    ui_dispatch_refresh(node->id, node->generation);
}

static void wheelAtTop(Node *node, int delta) {
    if (!node->refreshable || node->refreshing || node->scrollY > 0 || delta <= 0) { node->pulled = 0; return; }
    double now = nowMs();
    if (now - node->pullAt > 1000) node->pulled = 0;
    node->pullAt = now;
    node->pulled += delta;
    if (node->pulled >= 2 * WHEEL_DELTA) pullRefresh(node);
}

static void setRefreshing(Node *node, bool on) {
    if (node->kind != 6 || !node->hwnd) return;
    node->refreshable = true;
    node->refreshing = on;
    if (on && !node->refreshSpinner) {
        Node *spinner = new Node();
        spinner->kind = 10;
        spinner->hwnd = CreateWindowExW(0, kSpinnerClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, node->hwnd, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
        windowNodes[spinner->hwnd] = spinner;
        accessible(spinner->hwnd, ROLE_SYSTEM_PROGRESSBAR, nullptr);
        node->refreshSpinner = spinner;
    }
    if (!node->refreshSpinner) return;
    Node *spinner = node->refreshSpinner;
    spinner->spinning = on;
    RECT r;
    GetClientRect(node->hwnd, &r);
    SetWindowPos(spinner->hwnd, HWND_TOP, (r.right - px(20)) / 2, px(8), px(20), px(20), SWP_NOACTIVATE | (on ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (on) animationsRun();
}

// View > Refresh: the refreshable scroll that shows, in the newest modal or else in the window.
static void refreshShown() {
    HWND top = modals.empty() ? rootWindow : modals.back()->hwnd;
    Node *found = nullptr;
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (!node->refreshable || node->refreshing || !node->hwnd || !IsWindowVisible(node->hwnd) || !IsChild(top, node->hwnd)) continue;
        if (!found || node->id > found->id) found = node;
    }
    if (found) pullRefresh(found);
}
