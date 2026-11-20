// host_access.inc: accessibility (Style's role and states, disabled), a text input's editing
// options (secure, keyboard, multiline, capitalize, autocorrect, submit) and the system's text
// scale. Part of host.cpp, which #includes it (one translation unit).

// ---- accessibility ----

// The widget that carries a node's accessible role, states and label: its control, else its box.
static GtkWidget *accessibleOf(Node *node) {
    if (node->content && node->kind != 6 && node->kind != 13) return node->content;
    return node->box;
}

// Role (prop 57) as GTK's: 0 is the widget's own.
static const GtkAccessibleRole kRoles[] = {
    GTK_ACCESSIBLE_ROLE_GENERIC, GTK_ACCESSIBLE_ROLE_BUTTON, GTK_ACCESSIBLE_ROLE_LINK, GTK_ACCESSIBLE_ROLE_HEADING,
    GTK_ACCESSIBLE_ROLE_IMG, GTK_ACCESSIBLE_ROLE_CHECKBOX, GTK_ACCESSIBLE_ROLE_RADIO, GTK_ACCESSIBLE_ROLE_RADIO_GROUP,
    GTK_ACCESSIBLE_ROLE_SWITCH, GTK_ACCESSIBLE_ROLE_TAB, GTK_ACCESSIBLE_ROLE_TAB_LIST, GTK_ACCESSIBLE_ROLE_SLIDER,
    GTK_ACCESSIBLE_ROLE_PROGRESS_BAR, GTK_ACCESSIBLE_ROLE_MENU, GTK_ACCESSIBLE_ROLE_MENU_ITEM, GTK_ACCESSIBLE_ROLE_LIST,
    GTK_ACCESSIBLE_ROLE_LIST_ITEM, GTK_ACCESSIBLE_ROLE_ALERT, GTK_ACCESSIBLE_ROLE_SEARCH};

// GTK 4 lets a widget's role change only while its accessible context is not realized, which
// it is from the time the widget is in a window until it leaves it. A node's props mostly
// come before it is placed, so the role is set directly; a widget already in the window
// leaves its parent for the change and comes back at the same place (the context unrealizes
// and realizes again with the new role).
static void setAccessibleRole(GtkWidget *widget, GtkAccessibleRole role) {
    if (gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(widget)) == role) return;
    GtkWidget *parent = gtk_widget_get_root(widget) ? gtk_widget_get_parent(widget) : nullptr;
    GtkWidget *next = parent ? gtk_widget_get_next_sibling(widget) : nullptr;
    if (parent) { g_object_ref(widget); gtk_widget_unparent(widget); }
    g_object_set(widget, "accessible-role", role, nullptr);
    if (parent) { gtk_widget_insert_before(widget, parent, next); g_object_unref(widget); }
}

static void applyRole(Node *node) {
    GtkWidget *target = accessibleOf(node);
    // The widget's own role, kept the first time the app changes it (plus one: 0 is unset).
    gpointer own = g_object_get_data(G_OBJECT(target), "ui-own-role");
    if (!own) {
        own = GINT_TO_POINTER((int)gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(target)) + 1);
        g_object_set_data(G_OBJECT(target), "ui-own-role", own);
    }
    int count = (int)(sizeof kRoles / sizeof kRoles[0]);
    GtkAccessibleRole role = node->role > 0 && node->role < count ? kRoles[node->role] : (GtkAccessibleRole)(GPOINTER_TO_INT(own) - 1);
    setAccessibleRole(target, role);
    GtkAccessible *accessible = GTK_ACCESSIBLE(target);
    if (node->role == 3) gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_LEVEL, 2, -1);
    else gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_LEVEL);
}

static void applyLabel(Node *node) {
    GtkAccessible *accessible = GTK_ACCESSIBLE(accessibleOf(node));
    if (node->label.empty()) gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL);
    else gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL, node->label.c_str(), -1);
}

// Checked, selected, expanded, the range, the spoken value and the hint, as the node has them.
static void applyStates(Node *node) {
    GtkAccessible *accessible = GTK_ACCESSIBLE(accessibleOf(node));
    // A switch keeps its own checked state.
    if (node->checked) {
        GtkAccessibleTristate state = node->checked == 2 ? GTK_ACCESSIBLE_TRISTATE_TRUE
                                      : node->checked == 3 ? GTK_ACCESSIBLE_TRISTATE_MIXED : GTK_ACCESSIBLE_TRISTATE_FALSE;
        gtk_accessible_update_state(accessible, GTK_ACCESSIBLE_STATE_CHECKED, (int)state, -1);
    } else if (node->kind != 9) gtk_accessible_reset_state(accessible, GTK_ACCESSIBLE_STATE_CHECKED);
    // A tab that is not selected says so; other elements only when they are.
    if (node->selected || node->role == 9) gtk_accessible_update_state(accessible, GTK_ACCESSIBLE_STATE_SELECTED, node->selected ? TRUE : FALSE, -1);
    else gtk_accessible_reset_state(accessible, GTK_ACCESSIBLE_STATE_SELECTED);
    if (node->expanded) gtk_accessible_update_state(accessible, GTK_ACCESSIBLE_STATE_EXPANDED, node->expanded == 2 ? TRUE : FALSE, -1);
    else gtk_accessible_reset_state(accessible, GTK_ACCESSIBLE_STATE_EXPANDED);
    // A slider (GtkScale) keeps its own range.
    if (node->kind != 15) {
        if (node->rangeMax > node->rangeMin) {
            gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, (double)node->rangeMin,
                                           GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, (double)node->rangeMax,
                                           GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, (double)node->rangeNow, -1);
        } else {
            gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_VALUE_MIN);
            gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_VALUE_MAX);
            gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_VALUE_NOW);
        }
    }
    if (node->valueText.empty()) gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT);
    else gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT, node->valueText.c_str(), -1);
    if (node->hint.empty()) gtk_accessible_reset_property(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION);
    else gtk_accessible_update_property(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, node->hint.c_str(), -1);
}

// Disabled (61) makes the box insensitive: GTK gives no input to it or to anything inside it,
// now or placed later, and it is sensitive again when nothing above it is disabled. GTK tells
// assistive technologies only about the insensitive widget itself, so the accessible state of
// what is inside says it too.
static bool anyDisabled;
static void disableTree(Node *node, bool above) {
    bool off = above || node->disabled;
    GtkWidget *target = accessibleOf(node);
    if (target != node->box || !node->disabled) gtk_accessible_update_state(GTK_ACCESSIBLE(target), GTK_ACCESSIBLE_STATE_DISABLED, off ? TRUE : FALSE, -1);
    for (size_t i = 0; node->yoga && i < YGNodeGetChildCount(node->yoga); ++i)
        disableTree((Node *)YGNodeGetContext(YGNodeGetChild(node->yoga, i)), off);
}
static void refreshDisabled(Node *node) {
    if (!anyDisabled || !node->yoga) return;
    bool above = false;
    for (YGNodeRef up = YGNodeGetParent(node->yoga); up && !above; up = YGNodeGetParent(up)) {
        Node *owner = (Node *)YGNodeGetContext(up);
        above = owner && owner->disabled;
    }
    disableTree(node, above);
}

// Props 57 to 64.
static void applyAccessProp(Node *node, int code, int value) {
    if (code == 61) {
        node->disabled = value != 0;
        if (node->disabled) anyDisabled = true;
        gtk_widget_set_sensitive(node->box, !node->disabled);
        refreshDisabled(node);
        return;
    }
    if (code == 57) { node->role = value; applyRole(node); }
    else if (code == 58) node->checked = value;
    else if (code == 59) node->selected = value;
    else if (code == 60) node->expanded = value;
    else if (code == 62) node->rangeNow = value;
    else if (code == 63) node->rangeMin = value;
    else if (code == 64) node->rangeMax = value;
    applyStates(node);
}

// String props 8 (the spoken value) and 9 (the hint).
static void applyAccessText(Node *node, int code, const std::string &value) {
    if (code == 8) node->valueText = value; else node->hint = value;
    applyStates(node);
}

// ---- text inputs ----

static void addFocusEvents(GtkWidget *field) {
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(+[](GtkEventControllerFocus *c, gpointer) { onFocus(c, GINT_TO_POINTER(1)); }), nullptr);
    g_signal_connect(focus, "leave", G_CALLBACK(+[](GtkEventControllerFocus *c, gpointer) { onFocus(c, GINT_TO_POINTER(0)); }), nullptr);
    gtk_widget_add_controller(field, focus);
}

// Return in a one-line field: the app's submit, with the text as it is now.
static void onActivate(GtkEntry *entry, gpointer) {
    Node *node = nodeOfContent(GTK_WIDGET(entry));
    if (live(node) && node->submitListening && !node->multiline)
        ui_dispatch_submit(node->id, node->generation, gtk_editable_get_text(GTK_EDITABLE(entry)));
}

static GtkWidget *newEntry(Node *node) {
    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 1);
    PangoAttrList *list = textAttributes(node, true, true);
    gtk_entry_set_attributes(GTK_ENTRY(entry), list);
    pango_attr_list_unref(list);
    g_signal_connect(entry, "changed", G_CALLBACK(onText), nullptr);
    g_signal_connect(entry, "activate", G_CALLBACK(onActivate), nullptr);
    addFocusEvents(entry);
    return entry;
}

static std::string inputText(Node *node) {
    if (!node->content) return "";
    if (!GTK_IS_TEXT_VIEW(node->content)) return gtk_editable_get_text(GTK_EDITABLE(node->content));
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(node->content));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string out = text ? text : "";
    g_free(text);
    return out;
}

// The field's text, when it differs (the cursor goes to the end).
static void setInputText(Node *node, const char *value) {
    if (!node->content || inputText(node) == value) return;
    if (!GTK_IS_TEXT_VIEW(node->content)) {
        gtk_editable_set_text(GTK_EDITABLE(node->content), value);
        return;
    }
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(node->content));
    gtk_text_buffer_set_text(buffer, value, -1);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_place_cursor(buffer, &end);
}

// A multiline field's font and color: one tag over all of its text (inserted text takes no
// tag of its own, so it is applied again after each change).
static void tagAll(GtkTextBuffer *buffer) {
    GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "ui-font");
    if (!tag) return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
}
static void applyTextViewFont(Node *node) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(node->content));
    GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "ui-font");
    if (!tag) tag = gtk_text_buffer_create_tag(buffer, "ui-font", nullptr);
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_absolute_size(font, fontPixels(node) * PANGO_SCALE);
    if (node->fontWeight > 0) pango_font_description_set_weight(font, (PangoWeight)node->fontWeight);
    std::string family = node->fontFile.empty() ? "" : fontFamily(node->fontFile);
    if (!family.empty()) pango_font_description_set_family(font, family.c_str());
    g_object_set(tag, "font-desc", font, nullptr);
    pango_font_description_free(font);
    if (node->textColor) {
        double c[4];
        rgbaOf(node->textColor, c);
        GdkRGBA color = gdkColor(c);
        g_object_set(tag, "foreground-rgba", &color, nullptr);
    } else g_object_set(tag, "foreground-set", FALSE, nullptr);
    tagAll(buffer);
}

static void onTextBuffer(GtkTextBuffer *buffer, gpointer view) {
    tagAll(buffer);
    Node *node = nodeOfContent(GTK_WIDGET(view));
    if (!live(node)) return;
    // It grows with its text.
    if (node->yoga) YGNodeMarkDirty(node->yoga);
    if (!applying) relayout();
    if (!applying) ui_dispatch_text(node->id, node->generation, inputText(node).c_str());
}

static GtkWidget *newTextView(void) {
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), FALSE);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), "changed", G_CALLBACK(onTextBuffer), view);
    addFocusEvents(view);
    return view;
}

// Secure, keyboard, capitalize and autocorrect as GTK's input purpose and hints (an input
// method and an on-screen keyboard read them); secure also hides a one-line field's text.
static void applyInputOptions(Node *node) {
    if (!node->content) return;
    const GtkInputPurpose purposes[] = {GTK_INPUT_PURPOSE_FREE_FORM, GTK_INPUT_PURPOSE_EMAIL, GTK_INPUT_PURPOSE_NUMBER,
                                        GTK_INPUT_PURPOSE_NUMBER, GTK_INPUT_PURPOSE_PHONE, GTK_INPUT_PURPOSE_URL,
                                        GTK_INPUT_PURPOSE_FREE_FORM};
    GtkInputPurpose purpose = node->keyboard >= 0 && node->keyboard <= 6 ? purposes[node->keyboard] : GTK_INPUT_PURPOSE_FREE_FORM;
    if (node->secure) purpose = purpose == GTK_INPUT_PURPOSE_NUMBER || purpose == GTK_INPUT_PURPOSE_PHONE ? GTK_INPUT_PURPOSE_PIN : GTK_INPUT_PURPOSE_PASSWORD;
    int hints = GTK_INPUT_HINT_NONE;
    if (node->capitalize == 1) hints |= GTK_INPUT_HINT_LOWERCASE;
    else if (node->capitalize == 2) hints |= GTK_INPUT_HINT_UPPERCASE_WORDS;
    else if (node->capitalize == 3) hints |= GTK_INPUT_HINT_UPPERCASE_SENTENCES;
    else if (node->capitalize == 4) hints |= GTK_INPUT_HINT_UPPERCASE_CHARS;
    if (!node->autocorrect) hints |= GTK_INPUT_HINT_NO_SPELLCHECK;
    if (node->secure) hints |= GTK_INPUT_HINT_PRIVATE;
    if (GTK_IS_TEXT_VIEW(node->content)) {
        gtk_text_view_set_input_purpose(GTK_TEXT_VIEW(node->content), purpose);
        gtk_text_view_set_input_hints(GTK_TEXT_VIEW(node->content), (GtkInputHints)hints);
    } else {
        GtkEntry *entry = GTK_ENTRY(node->content);
        gtk_entry_set_visibility(entry, node->secure ? FALSE : TRUE);
        gtk_entry_set_input_purpose(entry, purpose);
        gtk_entry_set_input_hints(entry, (GtkInputHints)hints);
    }
}

// Multiline (prop 68): the field becomes a GtkTextView, or a GtkEntry again, in the same box
// (the node's id, place and props stay), with its text, focus, options, font, frame and
// accessibility.
static void setMultiline(Node *node, bool on) {
    node->multiline = on;
    if (!node->content || on == (bool)GTK_IS_TEXT_VIEW(node->content)) return;
    std::string text = inputText(node);
    GtkWidget *focus = window ? gtk_root_get_focus(GTK_ROOT(window)) : nullptr;
    bool focused = focus && (focus == node->content || gtk_widget_is_ancestor(focus, node->content));
    GtkWidget *next = on ? newTextView() : newEntry(node);
    gtk_widget_unparent(node->content);
    gtk_widget_insert_after(next, node->box, nullptr);
    node->content = next;
    bool was = applying;
    applying = true;
    setInputText(node, text.c_str());
    applying = was;
    if (!on) gtk_entry_set_placeholder_text(GTK_ENTRY(next), node->placeholder.c_str());
    applyInputOptions(node);
    applyStyled(node);
    applyFont(node);
    applyLabel(node);
    if (node->role) applyRole(node);
    applyStates(node);
    refreshDisabled(node);
    if (focused) gtk_widget_grab_focus(next);
    if (node->yoga) YGNodeMarkDirty(node->yoga);
    gtk_widget_queue_allocate(node->box);
}

// Props 65 to 72 on a text input. The return key (67) has nothing to show on a desktop, and
// GTK has no autofill (71).
static void applyInputProp(Node *node, int code, int value) {
    if (node->kind != 5) return;
    if (code == 65) node->secure = value;
    else if (code == 66) node->keyboard = value;
    else if (code == 69) node->capitalize = value;
    else if (code == 70) node->autocorrect = value;
    else if (code == 72) { node->submitListening = value != 0; return; }
    else if (code == 68) { setMultiline(node, value != 0); return; }
    else return;
    applyInputOptions(node);
}

// ---- the system's text scale ----

static double systemTextScale(void) {
    int dpi = -1;
    g_object_get(gtk_settings_get_default(), "gtk-xft-dpi", &dpi, nullptr);
    return dpi > 0 ? dpi / 1024.0 / 96.0 : 1.0;
}

// The text scale changed: every text again in its new size, laid out again.
static void applyTextScale(void) {
    double scale = systemTextScale();
    if (std::fabs(scale - textScale) < 0.001) return;
    textScale = scale;
    for (auto &entry : nodes) if (entry.second->kind >= 3 && entry.second->kind <= 5) applyFont(entry.second);
    if (root) relayout();
}
