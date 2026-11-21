// host_props.inc: props 73 to 91 (padding sides and margins through hosts/layout.h, text
// align, maximum lines, shrink, the image listener, line height, italic, z index, clip, layout
// animation, a modal's presentation and appearing), a switch's or a slider's accent (prop 9),
// the backdrop blur, and pull to refresh. Part of host.cpp, which #includes it (one
// translation unit).

// Work after the current commit (a callback the core gets while it applies one would nest).
static void later(std::function<void()> work) {
    auto *held = new std::function<void()>(std::move(work));
    g_idle_add_full(G_PRIORITY_DEFAULT, [](gpointer data) -> gboolean {
        (*(std::function<void()> *)data)();
        return G_SOURCE_REMOVE;
    }, held, [](gpointer data) { delete (std::function<void()> *)data; });
}

// Whether an image loaded, to the app when it listens (prop 84).
static void imageReport(Node *node, bool loaded) {
    if (!node->imageListening) return;
    node->refs++;
    later([node, loaded]() {
        if (!node->removed) ui_dispatch_image(node->id, node->generation, loaded ? 1 : 0);
        release(node);
    });
}

// A Text's alignment (Style.text_align) and its maximum lines, cut with an ellipsis. GtkLabel
// mirrors xalign in a right-to-left layout, so 0 is the start; the lines' justification follows.
static void applyLabelLayout(Node *node) {
    if (node->kind != 3 || !node->content) return;
    GtkLabel *label = GTK_LABEL(node->content);
    bool rtl = ui_layout_rtl();
    float xalign = node->textAlign == 2 ? 0.5f : node->textAlign == 3 ? 1.0f : 0.0f;
    gtk_label_set_xalign(label, xalign);
    GtkJustification start = rtl ? GTK_JUSTIFY_RIGHT : GTK_JUSTIFY_LEFT, end = rtl ? GTK_JUSTIFY_LEFT : GTK_JUSTIFY_RIGHT;
    gtk_label_set_justify(label, node->textAlign == 2 ? GTK_JUSTIFY_CENTER : node->textAlign == 3 ? end
                                 : node->textAlign == 4 ? GTK_JUSTIFY_FILL : start);
    gtk_label_set_lines(label, node->maxLines > 0 ? node->maxLines : -1);
    gtk_label_set_ellipsize(label, node->maxLines > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
    if (node->yoga) YGNodeMarkDirty(node->yoga);
}

// A one-line field's alignment.
static void applyEntryAlign(Node *node) {
    if (node->kind != 5 || !node->content || !GTK_IS_EDITABLE(node->content)) return;
    gtk_editable_set_alignment(GTK_EDITABLE(node->content), node->textAlign == 2 ? 0.5f : node->textAlign == 3 ? 1.0f : 0.0f);
}

// A switch's or a slider's accent (Style.color): a CSS class per color on the display, the
// switch's track when on, the slider's filled part and its thumb.
static void applyAccent(Node *node) {
    if ((node->kind != 9 && node->kind != 15) || !node->content) return;
    static std::map<int, std::string> classes;
    const char *old = (const char *)g_object_get_data(G_OBJECT(node->content), "ui-accent");
    if (old) gtk_widget_remove_css_class(node->content, old);
    g_object_set_data(G_OBJECT(node->content), "ui-accent", nullptr);
    if (!node->textColor) return;
    auto found = classes.find(node->textColor);
    if (found == classes.end()) {
        char name[32], css[512];
        uint32_t v = (uint32_t)node->textColor;
        snprintf(name, sizeof name, "ui-accent-%08x", v);
        char color[48];
        snprintf(color, sizeof color, "rgba(%u, %u, %u, %.3f)", v >> 24, (v >> 16) & 255, (v >> 8) & 255, (v & 255) / 255.0);
        snprintf(css, sizeof css,
                 "switch.%s:checked { background-color: %s; }\n"
                 "scale.%s highlight { background-color: %s; border-color: %s; }\n"
                 "scale.%s slider { background-color: %s; border-color: %s; }\n",
                 name, color, name, color, color, name, color, color);
        GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
        gtk_css_provider_load_from_string(provider, css);
#else
        gtk_css_provider_load_from_data(provider, css, -1);
#endif
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        g_object_unref(provider);
        found = classes.emplace(node->textColor, name).first;
    }
    gtk_widget_add_css_class(node->content, found->second.c_str());
    g_object_set_data(G_OBJECT(node->content), "ui-accent", (gpointer)found->second.c_str());
}

// Whether any element set a z index (until then the children's order is the widgets').
static bool stacking;

// A container's node children by Style.z_index, then in the children's order (GTK draws and
// picks in its children's order); its own control stays under them.
static void restack(GtkWidget *container) {
    if (!stacking || !container) return;
    std::vector<std::pair<Node *, size_t>> children;
    size_t order = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(container); child; child = gtk_widget_get_next_sibling(child)) {
        Node *node = nodeOfBox(child);
        if (node) children.push_back({node, order++});
    }
    for (auto &entry : children) {
        YGNodeRef parent = entry.first->yoga ? YGNodeGetParent(entry.first->yoga) : nullptr;
        for (size_t i = 0; parent && i < YGNodeGetChildCount(parent); ++i)
            if (YGNodeGetChild(parent, i) == entry.first->yoga) entry.second = i;
    }
    std::stable_sort(children.begin(), children.end(), [](const std::pair<Node *, size_t> &a, const std::pair<Node *, size_t> &b) {
        return a.first->zIndex != b.first->zIndex ? a.first->zIndex < b.first->zIndex : a.second < b.second;
    });
    for (auto &entry : children) gtk_widget_insert_before(entry.first->box, container, nullptr);
}

// Props 73 to 91; false for any other code.
static bool styleProp(Node *node, int code, int value) {
    if (node->yoga && ui_layout_prop(node->yoga, code, value)) {
        if (node->kind == 3 && node->box) gtk_widget_queue_allocate(node->box);
        return true;
    }
    switch (code) {
    case 81: node->textAlign = value; applyLabelLayout(node); applyEntryAlign(node); return true;
    case 82: node->maxLines = value; applyLabelLayout(node); return true;
    case 83:
        node->shrinkSet = value != 0;
        if (!node->yoga) return true;
        if (value) YGNodeStyleSetFlexShrink(node->yoga, value);
        else YGNodeStyleSetFlexShrink(node->yoga, node->kind == 6 || YGNodeStyleGetFlexGrow(node->yoga) > 0 ? 1 : 0);
        return true;
    case 84: node->imageListening = value != 0; return true;
    case 85: node->lineHeight = value; applyFont(node); return true;
    case 86: node->italic = value != 0; applyFont(node); return true;
    case 87: {
        node->zIndex = value;
        stacking = stacking || value;
        GtkWidget *up = node->box ? gtk_widget_get_parent(node->box) : nullptr;
        restack(up);
        return true;
    }
    case 88: node->clip = value != 0; if (node->box) gtk_widget_queue_draw(node->box); return true;
    case 89: node->presentation = value; return true;
    case 90: node->layoutMs = value; return true;
    case 91: node->appear = value; return true;
    }
    return false;
}

// The frame Yoga gave a node: moved there over its layout animation (prop 90) when it was on
// screen with a frame, at once otherwise.
static void moveTo(Node *node, double x, double y, double w, double h) {
    double from[4];
    valueOf(node, kFrame, from);
    bool had = node->w > 0 || node->h > 0;
    node->x = x; node->y = y; node->w = w; node->h = h;
    if (node->layoutMs > 0 && had && userAnimations) animateTo(node, kFrame, from, node->layoutMs, node->easing);
    else node->anims[kFrame].on = false;
}

// A modal over the window as it opens (props 89 and 91): the layer fades in, a sheet's panel
// slides up from the bottom edge, a dialog's grows a little; nothing when it appears at once
// or the system turns animations off. A Modal (91 = 0) appears as it always did.
static void appearModals(void) {
    for (Node *modal : modals) {
        if (modal->appeared || !modal->box) continue;
        modal->appeared = true;
        if (modal->appear != 1 || !userAnimations) continue;
        double transparent[4] = {0, 0, 0, 0};
        animateTo(modal, kOpacity, transparent, 220, 1);
        Node *panel = modal->yoga && YGNodeGetChildCount(modal->yoga) ? (Node *)YGNodeGetContext(YGNodeGetChild(modal->yoga, 0)) : nullptr;
        if (!panel || !modal->presentation) continue;
        double from[4] = {0, modal->presentation == 1 ? panel->h : 0, modal->presentation == 1 ? 1 : 0.96, 0};
        animateTo(panel, kTransform, from, 220, 1);
    }
}

// ---- the backdrop blur ----

static void dropBackdrop(Node *node) {
    for (GdkPaintable *paintable : node->backdrop) {
        if (node->box) g_signal_handlers_disconnect_by_data(paintable, node->box);
        g_object_unref(paintable);
    }
    node->backdrop.clear();
}

// Style.blur: what is behind the box, blurred, under its own background. GTK has no backdrop
// filter, so the host draws that again: the window's color, each ancestor's own background and,
// on the way up, the widgets before the box's branch (through GtkWidgetPaintables), blurred by
// the radius and clipped to the box; when one of them draws again, so does the box.
static void paintBackdrop(Node *node, GtkWidget *widget, GtkSnapshot *snapshot, double w, double h) {
    dropBackdrop(node);
    double r = node->blur;
    gtk_snapshot_push_clip(snapshot, addr(rect(0, 0, (float)w, (float)h)));
    gtk_snapshot_push_blur(snapshot, r);
    GdkRGBA base = darkMode ? GdkRGBA{0.14f, 0.14f, 0.14f, 1} : GdkRGBA{0.98f, 0.98f, 0.98f, 1};
    gtk_snapshot_append_color(snapshot, &base, addr(rect((float)-2 * r, (float)-2 * r, (float)(w + 4 * r), (float)(h + 4 * r))));
    std::vector<GtkWidget *> path;
    for (GtkWidget *up = widget; up; up = gtk_widget_get_parent(up)) {
        path.push_back(up);
        if (up == stage) break;
    }
    graphene_point_t origin = GRAPHENE_POINT_INIT(0, 0);
    for (size_t i = path.size(); i-- > 1;) {
        GtkWidget *parent = path[i], *branch = path[i - 1];
        Node *owner = nodeOfBox(parent);
        graphene_point_t at;
        if (owner && owner->background && gtk_widget_compute_point(parent, widget, &origin, &at)) {
            double c[4];
            rgbaOf(owner->background, c);
            GdkRGBA color = gdkColor(c);
            gtk_snapshot_append_color(snapshot, &color, addr(rect(at.x, at.y, (float)gtk_widget_get_width(parent),
                                                                  (float)gtk_widget_get_height(parent))));
        }
        for (GtkWidget *child = gtk_widget_get_first_child(parent); child && child != branch; child = gtk_widget_get_next_sibling(child)) {
            if (!gtk_widget_get_visible(child) || !gtk_widget_compute_point(child, widget, &origin, &at)) continue;
            GdkPaintable *paintable = gtk_widget_paintable_new(child);
            gtk_snapshot_save(snapshot);
            gtk_snapshot_translate(snapshot, &at);
            gdk_paintable_snapshot(paintable, snapshot, gtk_widget_get_width(child), gtk_widget_get_height(child));
            gtk_snapshot_restore(snapshot);
            g_signal_connect_swapped(paintable, "invalidate-contents", G_CALLBACK(gtk_widget_queue_draw), node->box);
            node->backdrop.push_back(paintable);
        }
    }
    gtk_snapshot_pop(snapshot);
    gtk_snapshot_pop(snapshot);
}

// ---- pull to refresh ----

// A scroll with a RefreshControl (prop 22). A desktop has no pull of its own: scrolling past the
// top (the touchpad's or the wheel's overshoot, GtkScrolledWindow's edge-overshot), or View >
// Refresh (F5, Ctrl+R) for the refreshable scroll on top, asks the app to refresh, and a spinner
// at the top of the scroll shows while the app says it is refreshing.
static void pullRefresh(Node *node) {
    if (!live(node) || !node->refreshable || node->refreshing || node->pulled) return;
    node->pulled = true;
    ui_dispatch_refresh(node->id, node->generation);
}

static void onOvershot(GtkScrolledWindow *, GtkPositionType position, gpointer data) {
    if (position == GTK_POS_TOP) pullRefresh((Node *)data);
}

static void setRefreshing(Node *node, bool on) {
    if (node->kind != 6 || !node->content) return;
    if (!node->refreshable) {
        node->refreshable = true;
        g_signal_connect(node->content, "edge-overshot", G_CALLBACK(onOvershot), node);
    }
    node->refreshing = on;
    if (!on) node->pulled = false;
    if (on && !node->refreshSpinner) {
        node->refreshSpinner = GTK_WIDGET(g_object_new(GTK_TYPE_SPINNER, "accessible-role", GTK_ACCESSIBLE_ROLE_PROGRESS_BAR, nullptr));
        gtk_widget_insert_before(node->refreshSpinner, node->box, nullptr);
    }
    if (node->refreshSpinner) {
        gtk_spinner_set_spinning(GTK_SPINNER(node->refreshSpinner), on);
        gtk_widget_set_visible(node->refreshSpinner, on);
        gtk_widget_queue_allocate(node->box);
    }
}

// View > Refresh: the refreshable scroll that shows, in the newest modal or else in the window.
static void refreshShown(void) {
    GtkWidget *top = modals.empty() ? root->box : modals.back()->box;
    Node *found = nullptr;
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (!node->refreshable || node->refreshing || !node->box || !gtk_widget_is_drawable(node->box)) continue;
        if (!gtk_widget_is_ancestor(node->box, top)) continue;
        if (!found || node->id > found->id) found = node;
    }
    if (found) { found->pulled = false; pullRefresh(found); }
}
