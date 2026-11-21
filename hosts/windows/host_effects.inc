// host_effects.inc: what a child window cannot draw itself, drawn by its parent: a shadow
// (prop 46) around it, a scale or a rotation (props 37 and 38) as a transformed picture of it,
// and a backdrop blur (prop 52) of what is behind it. Part of host.cpp, which #includes it (one
// translation unit).

// Whether a node's scale or rotation shows: its window is then out of sight and its parent
// draws its picture, transformed; a click on the picture goes to what is under it in the node.
static bool transformed(Node *node) {
    return node->hwnd && node->kind != 12 && (std::fabs(node->shownScale - 100) > 0.01 || std::fabs(node->shownRotate) > 0.01);
}

// Where a node's window goes when its picture is drawn instead (far left of its parent).
static const int kAway = -32000;

// The node's laid out box in its parent's client area, in pixels, with its translation.
static Gdiplus::RectF boxOf(Node *node) {
    double f[4] = {node->x, node->y, node->w, node->h};
    if (node->frameT < 1) for (int i = 0; i < 4; i++) f[i] = node->frameFrom[i] + (f[i] - node->frameFrom[i]) * eased(node->frameT, node->easing);
    return Gdiplus::RectF((float)px(f[0] + node->shownX + node->slideX), (float)px(f[1] + node->shownY), (float)px(f[2]), (float)px(f[3]));
}

// The matrix that draws a node's picture: about its center, scaled then turned.
static void transformOf(Node *node, Gdiplus::Matrix &m) {
    Gdiplus::RectF r = boxOf(node);
    float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, s = (float)(node->shownScale / 100);
    m.Translate(cx, cy);
    m.Rotate((float)node->shownRotate);
    m.Scale(s, s);
    m.Translate(-r.Width / 2, -r.Height / 2);
}

// A soft shadow under a raised child (Style.shadow, elevation in points), as the web's two
// shadows: layers of a dark color, wider and fainter, offset down.
static void paintShadow(Gdiplus::Graphics &g, Node *node) {
    Gdiplus::RectF r = boxOf(node);
    float elevation = (float)px(node->shadow), radius = (float)px(node->radius);
    int layers = 8;
    for (int i = layers; i >= 1; i--) {
        float spread = elevation * 2 * i / layers;
        Gdiplus::RectF ring(r.X - spread / 2, r.Y - spread / 2 + elevation / 2, r.Width + spread, r.Height + spread);
        std::unique_ptr<Gdiplus::GraphicsPath> path(roundedPath(ring, radius + spread / 2));
        Gdiplus::SolidBrush brush(Gdiplus::Color((BYTE)std::lround(0.16 * 255 / layers * 1.6), 17, 24, 39));
        g.FillPath(&brush, path.get());
    }
}

// A picture of a window and what is in it, as it draws itself.
static Gdiplus::Bitmap *pictureOf(HWND hwnd, int w, int h, HDC like) {
    if (w <= 0 || h <= 0) return nullptr;
    HDC memory = CreateCompatibleDC(like);
    HBITMAP bitmap = CreateCompatibleBitmap(like, w, h);
    HGDIOBJ old = SelectObject(memory, bitmap);
    PrintWindow(hwnd, memory, PW_CLIENTONLY);
    SelectObject(memory, old);
    DeleteDC(memory);
    auto *picture = Gdiplus::Bitmap::FromHBITMAP(bitmap, nullptr);
    DeleteObject(bitmap);
    return picture;
}

// What a parent draws for its children: their shadows, and the pictures of those scaled or
// turned (in the children's order, so a later child's shadow falls on an earlier one).
static void paintChildEffects(HWND parent, HDC hdc) {
    Node *owner = nodeOf(parent);
    if (!owner) owner = owningNode(parent);
    YGNodeRef yoga = parent == rootWindow ? rootYoga : owner ? owner->yoga : nullptr;
    if (!yoga) return;
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        Node *child = (Node *)YGNodeGetContext(YGNodeGetChild(yoga, i));
        if (!child->hwnd || YGNodeStyleGetDisplay(child->yoga) == YGDisplayNone || !(GetWindowLongW(child->hwnd, GWL_STYLE) & WS_VISIBLE)) continue;
        if (!child->shadow && !transformed(child)) continue;
        Gdiplus::GraphicsState state = g.Save();
        if (transformed(child)) {
            Gdiplus::Matrix m;
            transformOf(child, m);
            g.SetTransform(&m);
            g.TranslateTransform(-boxOf(child).X, -boxOf(child).Y);
        }
        if (child->shadow) paintShadow(g, child);
        if (transformed(child)) {
            Gdiplus::RectF r = boxOf(child);
            std::unique_ptr<Gdiplus::Bitmap> picture(pictureOf(child->hwnd, (int)r.Width, (int)r.Height, hdc));
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            if (picture) g.DrawImage(picture.get(), r.X, r.Y, r.Width, r.Height);
        }
        g.Restore(state);
    }
}

// The transformed child of `parent` whose picture is under `point` (client pixels), and the
// point in that child's own client area.
static Node *transformedAt(HWND parent, POINT point, POINT &local) {
    Node *owner = nodeOf(parent);
    if (!owner) owner = owningNode(parent);
    YGNodeRef yoga = parent == rootWindow ? rootYoga : owner ? owner->yoga : nullptr;
    for (size_t i = yoga ? YGNodeGetChildCount(yoga) : 0; i-- > 0;) {
        Node *child = (Node *)YGNodeGetContext(YGNodeGetChild(yoga, i));
        if (!transformed(child)) continue;
        Gdiplus::Matrix m;
        transformOf(child, m);
        m.Invert();
        Gdiplus::PointF p((float)point.x, (float)point.y);
        m.TransformPoints(&p, 1);
        Gdiplus::RectF r = boxOf(child);
        if (p.X < 0 || p.Y < 0 || p.X > r.Width || p.Y > r.Height) continue;
        local = {(LONG)p.X, (LONG)p.Y};
        return child;
    }
    return nullptr;
}

// A click on a transformed child's picture: the deepest enabled window there takes it as its
// own click (a button, a switch), or the pressable it is in is pressed.
static bool clickTransformed(HWND parent, POINT point) {
    POINT local;
    Node *child = transformedAt(parent, point, local);
    if (!child) return false;
    HWND at = child->hwnd;
    for (;;) {
        HWND next = ChildWindowFromPointEx(at, local, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!next || next == at) break;
        if (!IsWindowEnabled(next)) return true;
        MapWindowPoints(at, next, &local, 1);
        at = next;
    }
    Node *target = owningNode(at);
    if (target && (target->kind == 4 || target->kind == 9)) SendMessageW(target->hwnd, BM_CLICK, 0, 0);
    else if (Node *press = target ? findUp(target, [](Node *n) { return n->kind == 8; }) : nullptr) {
        if (!press->disabled) ui_dispatch(press->id, press->generation);
    }
    return true;
}

// ---- the backdrop blur ----

// Whether a blur's backdrop is being drawn (its pictures leave the blurred windows out).
static bool drawingBackdrop;

// Style.blur: what is behind the node, blurred, under its own background. A child window
// cannot see through itself, so the host draws that again: the window's color, each
// ancestor's own background and, on the way up, the pictures of the windows under the node's
// branch, shrunk and grown again by the radius (a box blur) and clipped to the node's box.
static void paintBlur(Node *node, HDC hdc, RECT rect) {
    int w = rect.right, h = rect.bottom;
    if (w <= 0 || h <= 0 || drawingBackdrop) return;
    drawingBackdrop = true;
    HDC memory = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(memory, bitmap);
    RECT all = {0, 0, w, h};
    FillRect(memory, &all, brushOf(windowBackground()));
    std::vector<HWND> path;
    for (HWND up = node->hwnd; up && up != mainWindow; up = GetParent(up)) path.push_back(up);
    for (size_t i = path.size(); i-- > 1;) {
        HWND parent = path[i], branch = path[i - 1];
        POINT origin = {0, 0};
        MapWindowPoints(parent, node->hwnd, &origin, 1);
        Node *owner = nodeOf(parent);
        if (owner && owner->shownBackground) {
            RECT r;
            GetClientRect(parent, &r);
            OffsetRect(&r, origin.x, origin.y);
            FillRect(memory, &r, brushOf(blend(owner->shownBackground, windowBackground())));
        }
        // The windows under the branch, the lowest first.
        std::vector<HWND> under;
        for (HWND below = GetWindow(branch, GW_HWNDNEXT); below; below = GetWindow(below, GW_HWNDNEXT)) under.push_back(below);
        for (size_t j = under.size(); j-- > 0;) {
            HWND sibling = under[j];
            if (!IsWindowVisible(sibling)) continue;
            RECT r;
            GetWindowRect(sibling, &r);
            MapWindowPoints(nullptr, node->hwnd, (POINT *)&r, 2);
            if (r.right <= 0 || r.bottom <= 0 || r.left >= w || r.top >= h) continue;
            SaveDC(memory);
            SetViewportOrgEx(memory, r.left, r.top, nullptr);
            PrintWindow(sibling, memory, PW_CLIENTONLY);
            RestoreDC(memory, -1);
        }
    }
    SelectObject(memory, old);
    DeleteDC(memory);
    std::unique_ptr<Gdiplus::Bitmap> backdrop(Gdiplus::Bitmap::FromHBITMAP(bitmap, nullptr));
    DeleteObject(bitmap);
    // A box blur: shrunk by the radius over 2 and grown back, smoothed.
    int factor = std::max(2, px(node->blur) / 2);
    int sw = std::max(1, w / factor), sh = std::max(1, h / factor);
    Gdiplus::Bitmap shrunk(sw, sh, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics s(&shrunk);
        s.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
        s.DrawImage(backdrop.get(), Gdiplus::Rect(0, 0, sw, sh), 0, 0, w, h, Gdiplus::UnitPixel);
    }
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    std::unique_ptr<Gdiplus::GraphicsPath> shape(roundedPath(Gdiplus::RectF(0, 0, (float)w, (float)h), (float)px(node->radius)));
    g.SetClip(shape.get());
    Gdiplus::ImageAttributes edges;
    edges.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    g.DrawImage(&shrunk, Gdiplus::Rect(0, 0, w, h), 0, 0, sw, sh, Gdiplus::UnitPixel, &edges);
    drawingBackdrop = false;
}

// The blurred nodes draw again when what is behind them may have changed (a commit, a scroll).
static void refreshBlurs() {
    for (auto &entry : nodes) if (entry.second->blur && entry.second->hwnd) InvalidateRect(entry.second->hwnd, nullptr, FALSE);
}
