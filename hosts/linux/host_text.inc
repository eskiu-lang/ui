// host_text.inc: fonts, a text's attributes, a button's title, styled controls and measuring. Part of host.cpp, which #includes it (one translation unit).

// ---- fonts and text ----

// The default text size, as the web host's.
static const int kTextSize = 16;
// The system's text scale (GtkSettings' gtk-xft-dpi over 96 dpi; GNOME's text scaling factor):
// text sizes from the app are in points at a scale of 1.
static double textScale = 1;
static double fontPixels(Node *node) { return (node->fontSize > 0 ? node->fontSize : kTextSize) * textScale; }
// A multiline field's font (host_access.inc).
static void applyTextViewFont(Node *node);

// A font file from the app's assets, added to fontconfig once; its family, or "" when it does
// not load.
static std::string fontFamily(const std::string &file);

// The attributes of a text in the node's font and color (sized when `sized`).
static PangoAttrList *textAttributes(Node *node, bool sized, bool colored) {
    PangoAttrList *list = pango_attr_list_new();
    if (sized) pango_attr_list_insert(list, pango_attr_size_new_absolute((int)std::lround(fontPixels(node) * PANGO_SCALE)));
    if (node->italic) pango_attr_list_insert(list, pango_attr_style_new(PANGO_STYLE_ITALIC));
#if PANGO_VERSION_CHECK(1, 50, 0)
    if (node->lineHeight > 0) pango_attr_list_insert(list, pango_attr_line_height_new_absolute(node->lineHeight * PANGO_SCALE));
#endif
    if (node->fontWeight > 0) pango_attr_list_insert(list, pango_attr_weight_new((PangoWeight)node->fontWeight));
    if (!node->fontFile.empty()) {
        std::string family = fontFamily(node->fontFile);
        if (!family.empty()) pango_attr_list_insert(list, pango_attr_family_new(family.c_str()));
    }
    if (colored && node->textColor) {
        uint32_t v = (uint32_t)node->textColor;
        pango_attr_list_insert(list, pango_attr_foreground_new((guint16)((v >> 24) * 257), (guint16)(((v >> 16) & 255) * 257),
                                                               (guint16)(((v >> 8) & 255) * 257)));
        pango_attr_list_insert(list, pango_attr_foreground_alpha_new((guint16)((v & 255) * 257)));
    }
    return list;
}

// A button's title: the theme's for a plain button, else in the node's font and color.
static void applyButtonTitle(Node *node) {
    if (!node->content) return;
    GtkWidget *label = gtk_button_get_child(GTK_BUTTON(node->content));
    gtk_label_set_text(GTK_LABEL(label), node->title.c_str());
    bool plain = !node->styled && !node->textColor && !node->fontSize && !node->fontWeight && node->fontFile.empty();
    PangoAttrList *list = plain ? pango_attr_list_new() : textAttributes(node, node->styled || node->fontSize, true);
    gtk_label_set_attributes(GTK_LABEL(label), list);
    pango_attr_list_unref(list);
}

static void applyFont(Node *node) {
    if (!node->content) return;
    if (node->kind == 4) applyButtonTitle(node);
    else if (node->kind == 3) {
        PangoAttrList *list = textAttributes(node, true, true);
        gtk_label_set_attributes(GTK_LABEL(node->content), list);
        pango_attr_list_unref(list);
    } else if (node->kind == 5 && GTK_IS_TEXT_VIEW(node->content)) {
        applyTextViewFont(node);
    } else if (node->kind == 5) {
        PangoAttrList *list = textAttributes(node, true, true);
        gtk_entry_set_attributes(GTK_ENTRY(node->content), list);
        pango_attr_list_unref(list);
    }
    if (node->yoga) YGNodeMarkDirty(node->yoga);
}

// A field or a button the app gives a background, a border or a radius loses the theme's
// frame (the CSS class ui-styled), and its box paints what the app asked.
static void applyStyled(Node *node) {
    if (!node->content || (node->kind != 4 && node->kind != 5)) return;
    bool styled = node->background || node->borderWidth || node->radius;
    if (styled) gtk_widget_add_css_class(node->content, "ui-styled");
    else gtk_widget_remove_css_class(node->content, "ui-styled");
    node->styled = styled;
    applyFont(node);
    if (node->yoga) YGNodeMarkDirty(node->yoga);
    gtk_widget_queue_allocate(node->box);
}

static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    Node *node = (Node *)YGNodeGetContext(yoga);
    double limitW = widthMode == YGMeasureModeUndefined ? 1e9 : width, limitH = heightMode == YGMeasureModeUndefined ? 1e9 : height;
    double w = 0, h = 0;
    GtkWidget *content = node->content;
    if (node->kind == 3 && content) {
        // The label's text in its font, wrapped at the width it may take.
        PangoLayout *layout = gtk_widget_create_pango_layout(content, gtk_label_get_text(GTK_LABEL(content)));
        PangoAttrList *list = textAttributes(node, true, false);
        pango_layout_set_attributes(layout, list);
        pango_attr_list_unref(list);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        if (limitW < 1e9) pango_layout_set_width(layout, (int)(limitW * PANGO_SCALE));
        if (node->maxLines > 0) {
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_height(layout, -node->maxLines);
        }
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout, nullptr, &logical);
        g_object_unref(layout);
        w = logical.width; h = logical.height;
    } else if (node->kind == 7) {
        if (node->texture) { w = gdk_texture_get_width(node->texture); h = gdk_texture_get_height(node->texture); }
    } else if (node->kind == 10) {
        w = h = 20;
    } else if (content) {
        int min = 0, nat = 0;
        gtk_widget_measure(content, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, nullptr, nullptr);
        w = nat;
        if (node->kind == 5) w = std::max(w, 120.0);
        if (node->kind == 15) w = 160;
        // A multiline field wraps: its height at the width it may take.
        bool wraps = node->kind == 5 && GTK_IS_TEXT_VIEW(content);
        int across = wraps ? (int)std::min(limitW < 1e9 ? limitW : w, 1e6) : node->kind == 5 || node->kind == 15 ? -1 : (int)std::min(w, limitW);
        gtk_widget_measure(content, GTK_ORIENTATION_VERTICAL, across, &min, &nat, nullptr, nullptr);
        h = nat;
    }
    return {(float)std::min(std::ceil(w), limitW), (float)std::min(std::ceil(h), limitH)};
}
