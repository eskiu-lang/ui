// host_conformance.inc: the host's own run of the conformance scene (tests/conformance built
// with UI_TEST=1, started with UI_CONFORMANCE=1): what the driver's ops cannot reach, read from
// GTK (each labeled element's accessible role, what the boxes draw, the modal's cover) after
// GTK's own input (a button's clicked, a field's text, a switch's state, the alert's button, a
// scroll's edge-overshot, Escape). It prints one line, CONFORMANCE and a JSON object, which
// tools/conformance.py compares with the other hosts'. Part of host.cpp, which #includes it in
// a test build only.

static Node *labeled(const std::string &label) {
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if ((node->kind == 4 ? node->title : node->label) == label) return node;
    }
    return nullptr;
}

// The role a labeled element has for assistive technologies, in the suite's words.
static const char *conformanceRole(Node *node) {
    if (node->kind == 6) return "scroll";
    switch (gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(accessibleOf(node)))) {
    case GTK_ACCESSIBLE_ROLE_BUTTON: return "button";
    case GTK_ACCESSIBLE_ROLE_SWITCH: return "switch";
    case GTK_ACCESSIBLE_ROLE_SLIDER: return "slider";
    case GTK_ACCESSIBLE_ROLE_PROGRESS_BAR: return "progressbar";
    case GTK_ACCESSIBLE_ROLE_IMG: return "image";
    case GTK_ACCESSIBLE_ROLE_TEXT_BOX: return "textfield";
    case GTK_ACCESSIBLE_ROLE_LABEL: return "text";
    case GTK_ACCESSIBLE_ROLE_GROUP: return "group";
    default: return "other";
    }
}

static std::string jsonText(const std::string &text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if ((unsigned char)c >= 32) out += c;
    }
    return out + "\"";
}

static std::string number(double value) {
    char text[32];
    snprintf(text, sizeof text, "%g", std::round(value * 100) / 100);
    return text;
}

static std::string boxIn(GtkWidget *widget, GtkWidget *space) {
    graphene_rect_t r;
    if (!widget || !space || !gtk_widget_compute_bounds(widget, space, &r)) return "null";
    return "[" + number(r.origin.x) + "," + number(r.origin.y) + "," + number(r.size.width) + "," + number(r.size.height) + "]";
}

static std::string hexColor(int value) {
    char text[16];
    snprintf(text, sizeof text, "%08x", (uint32_t)value);
    return jsonText(text);
}

// The first GtkButton under `widget` after skipping `skip` of them (an alert's buttons).
static GtkWidget *nthButton(GtkWidget *widget, int &skip) {
    if (GTK_IS_BUTTON(widget)) { if (skip-- == 0) return widget; return nullptr; }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = nthButton(child, skip);
        if (found) return found;
    }
    return nullptr;
}

static void click(const char *label) {
    Node *node = labeled(label);
    if (node && node->content) g_signal_emit_by_name(node->content, "clicked");
}

static void runConformance(void) {
    static std::string frames, roles, styles, modal = "null";
    static std::vector<bool> refresh;
    static std::vector<std::function<void()>> steps = {
        []() {
            Node *suite = labeled("Suite");
            for (auto &entry : nodes) {
                Node *node = entry.second;
                std::string label = node->kind == 4 ? node->title : node->label;
                if (label.empty() || !node->box || !gtk_widget_get_mapped(node->box)) continue;
                frames += (frames.empty() ? "" : ",") + jsonText(label) + ":" + boxIn(node->box, suite->box);
                roles += (roles.empty() ? "" : ",") + jsonText(label) + ":" + jsonText(conformanceRole(node));
            }
            Node *painted = labeled("Painted"), *title = labeled("Title"), *moved = labeled("Moved");
            double t[4];
            valueOf(moved, kTransform, t);
            styles = "{\"Painted\":{\"background\":" + hexColor(painted->background) + ",\"radius\":" +
                     number(std::min<double>(painted->radius, std::min(painted->w, painted->h) / 2)) + ",\"border\":" +
                     number(painted->borderWidth) + ",\"borderColor\":" + hexColor(painted->borderColor) + ",\"opacity\":" +
                     number(gtk_widget_get_opacity(painted->box)) + "},\"Title\":{\"color\":" + hexColor(title->textColor) +
                     ",\"fontSize\":" + number(fontPixels(title) / textScale) + ",\"fontWeight\":" +
                     number(title->fontWeight ? title->fontWeight : 400) + "},\"Moved\":{\"translateX\":" + number(t[0]) +
                     ",\"translateY\":" + number(t[1]) + ",\"scale\":" + number(t[2]) + ",\"rotate\":" + number(t[3]) + "}}";
            click("Tap");
            click("Tap");
            gtk_editable_set_text(GTK_EDITABLE(labeled("Field")->content), "abc");
        },
        []() {
            ui_dispatch(labeled("Press")->id, labeled("Press")->generation);
            gtk_switch_set_active(GTK_SWITCH(labeled("Toggle")->content), TRUE);
        },
        []() { gtk_range_set_value(GTK_RANGE(labeled("Volume")->content), 70); },
        []() { click("Copy"); },
        []() { click("Ask"); },
        []() {
            // The alert's second button, in its own window.
            GListModel *list = gtk_window_get_toplevels();
            for (guint i = 0; i < g_list_model_get_n_items(list); i++) {
                GtkWidget *top = GTK_WIDGET(g_list_model_get_item(list, i));
                int skip = 1;
                GtkWidget *button = top != window ? nthButton(top, skip) : nullptr;
                if (button) g_signal_emit_by_name(button, "clicked");
                g_object_unref(top);
                if (button) break;
            }
        },
        []() { click("Open sheet"); },
        []() {
            Node *layer = modals.empty() ? nullptr : modals.back();
            Node *sheet = labeled("Sheet");
            if (layer && sheet) {
                modal = "{\"cover\":" + boxIn(layer->box, stage) + ",\"window\":[" + number(stageWidth) + "," +
                        number(stageHeight) + "],\"sheet\":" + boxIn(sheet->box, stage) + "}";
            }
            click("Close");
        },
        []() { click("Open sheet"); },
        []() { back(); },
        []() {
            // Scrolling the feed past its top, as a touchpad's overshoot does.
            g_signal_emit_by_name(labeled("Feed")->content, "edge-overshot", GTK_POS_TOP);
        },
        []() {
            refresh.push_back(labeled("Feed")->refreshing);
            click("Loaded");
        },
        []() {
            refresh.push_back(labeled("Feed")->refreshing);
            gtk_adjustment_set_value(labeled("Feed")->adjustment, 2000);
        },
        []() {
            Node *suite = labeled("Suite");
            std::string scrolled;
            for (auto &entry : nodes) {
                Node *node = entry.second;
                if (!node->box || !(node->label.rfind("Row ", 0) == 0 || node->label == "Feed")) continue;
                scrolled += (scrolled.empty() ? "" : ",") + jsonText(node->label) + ":" + boxIn(node->box, suite->box);
            }
            std::string result = "{\"status\":\"DONE\",\"frames\":{" + frames + "},\"roles\":{" + roles + "},\"styles\":" + styles +
                                 ",\"scrolled\":{" + scrolled + "},\"modal\":" + modal + ",\"refresh\":[" +
                                 (refresh.size() > 0 && refresh[0] ? "true" : "false") + "," +
                                 (refresh.size() > 1 && refresh[1] ? "true" : "false") + "],\"log\":" +
                                 jsonText(ui_conformance_log()) + "}";
            printf("CONFORMANCE %s\n", result.c_str());
            fflush(stdout);
            ui_stop();
            exit(0);
        },
    };
    // One step every 300 ms, after the scene's first frame and its timer.
    for (size_t i = 0; i < steps.size(); i++) {
        g_timeout_add((guint)(500 + 300 * i), [](gpointer data) -> gboolean {
            steps[GPOINTER_TO_SIZE(data)]();
            return G_SOURCE_REMOVE;
        }, GSIZE_TO_POINTER(i));
    }
}
