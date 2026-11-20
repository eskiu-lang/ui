// host_commit.inc: applying commits and the core's commands. Part of host.cpp, which #includes it (one translation unit).

// ---- applying commits ----

static void closeModal(Node *node) {
    auto at = std::find(modals.begin(), modals.end(), node);
    if (at != modals.end()) modals.erase(at);
}

static GtkWidget *containerOf(Node *node) { return node->document ? node->document : node->box; }

static void createNode(int id, int generation, int kind) {
    Node *node = new Node();
    node->id = id;
    node->generation = generation;
    node->kind = kind;
    node->yoga = YGNodeNew();
    YGNodeSetContext(node->yoga, node);
    GtkAccessibleRole role = GTK_ACCESSIBLE_ROLE_GENERIC;
    if (kind == 1 || kind == 2) role = GTK_ACCESSIBLE_ROLE_GROUP;
    else if (kind == 7) role = GTK_ACCESSIBLE_ROLE_IMG;
    else if (kind == 8) role = GTK_ACCESSIBLE_ROLE_BUTTON;
    else if (kind == 12) role = GTK_ACCESSIBLE_ROLE_DIALOG;
    node->box = newBox(role, kBoxNode, node);
    node->refs = 2;
    GtkWidget *content = nullptr;
    if (kind == 4) {
        content = gtk_button_new();
        GtkWidget *label = gtk_label_new("");
        gtk_button_set_child(GTK_BUTTON(content), label);
        g_signal_connect(content, "clicked", G_CALLBACK(onClicked), nullptr);
    } else if (kind == 5) {
        content = newEntry(node);
    } else if (kind == 3) {
        content = gtk_label_new("");
        gtk_label_set_wrap(GTK_LABEL(content), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(content), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_xalign(GTK_LABEL(content), 0);
        gtk_label_set_yalign(GTK_LABEL(content), 0);
        PangoAttrList *list = textAttributes(node, true, true);
        gtk_label_set_attributes(GTK_LABEL(content), list);
        pango_attr_list_unref(list);
    } else if (kind == 7) {
    } else if (kind == 8) {
        YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
        YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
        GtkGesture *click = gtk_gesture_click_new();
        g_signal_connect(click, "pressed", G_CALLBACK(onPressDown), nullptr);
        g_signal_connect(click, "released", G_CALLBACK(onPressUp), nullptr);
        g_signal_connect(click, "stopped", G_CALLBACK(onPressStop), nullptr);
        g_signal_connect(click, "cancel", G_CALLBACK(+[](GtkGesture *g, GdkEventSequence *, gpointer d) { onPressStop(g, d); }), nullptr);
        gtk_widget_add_controller(node->box, GTK_EVENT_CONTROLLER(click));
    } else if (kind == 15) {
        content = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
        gtk_scale_set_draw_value(GTK_SCALE(content), FALSE);
        g_signal_connect(content, "value-changed", G_CALLBACK(onSlider), nullptr);
    } else if (kind == 9) {
        content = gtk_switch_new();
        g_signal_connect(content, "notify::active", G_CALLBACK(onSwitch), nullptr);
    } else if (kind == 10) {
        content = gtk_spinner_new();
    } else if (kind == 11) {
        gtk_widget_set_visible(node->box, FALSE);
        YGNodeStyleSetDisplay(node->yoga, YGDisplayNone);
    } else if (kind == 12) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(node->box), GTK_ACCESSIBLE_PROPERTY_MODAL, TRUE, -1);
        GtkGesture *click = gtk_gesture_click_new();
        g_signal_connect(click, "released", G_CALLBACK(onBackdrop), nullptr);
        gtk_widget_add_controller(node->box, GTK_EVENT_CONTROLLER(click));
        node->background = 0x00000066;
        YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
    } else if (kind == 13) {
    } else if (kind == 6) {
        content = gtk_scrolled_window_new();
        GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(content);
        gtk_scrolled_window_set_policy(scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_overlay_scrolling(scroll, TRUE);
        gtk_scrolled_window_set_propagate_natural_height(scroll, FALSE);
        node->document = newBox(GTK_ACCESSIBLE_ROLE_GENERIC, kBoxDocument, node);
        GtkWidget *viewport = gtk_viewport_new(nullptr, nullptr);
        gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(viewport), FALSE);
        gtk_viewport_set_child(GTK_VIEWPORT(viewport), node->document);
        gtk_scrolled_window_set_child(scroll, viewport);
        node->adjustment = gtk_scrolled_window_get_vadjustment(scroll);
        g_signal_connect(node->adjustment, "value-changed", G_CALLBACK(onScroll), node);
        YGNodeStyleSetFlexDirection(node->yoga, YGFlexDirectionColumn);
        YGNodeStyleSetOverflow(node->yoga, YGOverflowScroll);
        YGNodeStyleSetFlexShrink(node->yoga, 1);
        YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
    } else {
        YGNodeStyleSetFlexDirection(node->yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
        YGNodeStyleSetGap(node->yoga, YGGutterAll, 8);
    }
    if (content) {
        gtk_widget_insert_after(content, node->box, nullptr);
        node->content = content;
    }
    if ((kind >= 3 && kind <= 5) || kind == 7 || kind == 9 || kind == 10 || kind == 15) YGNodeSetMeasureFunc(node->yoga, measure);
    nodes[id] = node;
}

static void applyProp(Node *node, int code, int value) {
    YGNodeRef yoga = node->yoga;
    switch (code) {
    case 1: YGNodeStyleSetGap(yoga, YGGutterAll, value); break;
    case 2:
        YGNodeStyleSetPadding(yoga, YGEdgeAll, value);
        node->padding = value;
        if (node->kind == 5) gtk_widget_queue_allocate(node->box);
        break;
    case 3:
        if (!node->content || node->kind != 5) break;
        if (value) gtk_widget_grab_focus(node->content);
        else if (gtk_widget_has_focus(node->content) || gtk_widget_is_ancestor(gtk_root_get_focus(GTK_ROOT(window)), node->content))
            gtk_root_set_focus(GTK_ROOT(window), nullptr);
        break;
    case 4: if (value) YGNodeStyleSetWidth(yoga, value); else YGNodeStyleSetWidthAuto(yoga); break;
    case 5: if (value) YGNodeStyleSetHeight(yoga, value); else YGNodeStyleSetHeightAuto(yoga); break;
    case 6:
        YGNodeStyleSetFlexGrow(yoga, value);
        if (node->kind != 6) YGNodeStyleSetFlexShrink(yoga, value > 0 ? 1 : 0);
        break;
    case 7: YGNodeStyleSetFlexWrap(yoga, value ? YGWrapWrap : YGWrapNoWrap); break;
    case 8: {
        double from[4];
        valueOf(node, kBackground, from);
        node->background = value;
        animateTo(node, kBackground, from, node->transitionMs, node->easing);
        gtk_widget_queue_draw(node->box);
        applyStyled(node);
        break;
    }
    case 50: node->gradientTo = value; gtk_widget_queue_draw(node->box); break;
    case 51: node->gradientAngle = value; gtk_widget_queue_draw(node->box); break;
    case 52: node->blur = value; gtk_widget_queue_draw(node->box); break;
    case 9:
        node->textColor = value;
        applyFont(node);
        break;
    case 10: node->fontSize = value; applyFont(node); break;
    case 11: node->fontWeight = value; applyFont(node); break;
    case 12:
        node->borderWidth = value;
        YGNodeStyleSetBorder(yoga, YGEdgeAll, value);
        gtk_widget_queue_draw(node->box);
        applyStyled(node);
        break;
    case 13: node->borderColor = value; gtk_widget_queue_draw(node->box); break;
    case 14: node->radius = value; gtk_widget_queue_draw(node->box); applyStyled(node); break;
    case 47: node->cover = value != 0; gtk_widget_queue_draw(node->box); break;
    case 48: node->pixelated = value != 0; gtk_widget_queue_draw(node->box); break;
    case 46: node->shadow = value; gtk_widget_queue_draw(node->box); break;
    case 15: {
        double from[4];
        valueOf(node, kOpacity, from);
        node->opacity = value;
        animateTo(node, kOpacity, from, node->transitionMs, node->easing);
        applyOpacity(node);
        break;
    }
    case 35: case 36: case 37: case 38: {
        double from[4];
        valueOf(node, kTransform, from);
        if (code == 35) node->translateX = value;
        else if (code == 36) node->translateY = value;
        else if (code == 37) node->scalePercent = value;
        else node->rotateDegrees = value;
        animateTo(node, kTransform, from, node->transitionMs, node->easing);
        GtkWidget *up = gtk_widget_get_parent(node->box);
        if (up) gtk_widget_queue_allocate(up);
        break;
    }
    case 39: node->transitionMs = value; break;
    case 40: node->easing = value; break;
    case 16: {
        const YGAlign aligns[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch};
        YGNodeStyleSetAlignItems(yoga, aligns[std::min(4, std::max(0, value))]);
        break;
    }
    case 17: {
        const YGJustify justifies[] = {YGJustifyFlexStart, YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                       YGJustifySpaceBetween, YGJustifySpaceAround};
        YGNodeStyleSetJustifyContent(yoga, justifies[std::min(5, std::max(0, value))]);
        break;
    }
    case 18: if (node->kind == 9 && node->content) gtk_switch_set_active(GTK_SWITCH(node->content), value != 0); break;
    case 19: if (node->kind == 10 && node->content) gtk_spinner_set_spinning(GTK_SPINNER(node->content), value != 0); break;
    case 23: screenActive(node, value); break;
    case 32: case 33: case 34:
        if (node->kind == 15 && node->content) {
            GtkAdjustment *a = gtk_range_get_adjustment(GTK_RANGE(node->content));
            if (code == 33) gtk_adjustment_set_lower(a, value);
            else if (code == 34) gtk_adjustment_set_upper(a, value);
            else { gtk_adjustment_set_value(a, value); node->sliderValue = value; }
        }
        break;
    case 41:
        setController(node, GTK_TYPE_GESTURE_DRAG, value != 0, []() {
            GtkGesture *drag = gtk_gesture_drag_new();
            g_signal_connect(drag, "drag-update", G_CALLBACK(onDragUpdate), nullptr);
            g_signal_connect(drag, "drag-end", G_CALLBACK(onDragEnd), nullptr);
            g_signal_connect(drag, "cancel", G_CALLBACK(onDragCancel), nullptr);
            return GTK_EVENT_CONTROLLER(drag);
        });
        break;
    case 31:
        setController(node, GTK_TYPE_EVENT_CONTROLLER_MOTION, value != 0, []() {
            GtkEventController *motion = gtk_event_controller_motion_new();
            g_signal_connect(motion, "enter", G_CALLBACK(onHover), GINT_TO_POINTER(1));
            g_signal_connect(motion, "leave", G_CALLBACK(onUnhover), nullptr);
            return motion;
        });
        break;
    case 49:
        setController(node, GTK_TYPE_GESTURE_LONG_PRESS, value != 0, []() {
            GtkGesture *press = gtk_gesture_long_press_new();
            g_signal_connect(press, "pressed", G_CALLBACK(onLongPress), nullptr);
            return GTK_EVENT_CONTROLLER(press);
        });
        break;
    case 30: node->layoutListening = value != 0; node->reported = false; break;
    case 42: YGNodeStyleSetMinWidth(yoga, value); break;
    case 43: YGNodeStyleSetMaxWidth(yoga, value ? (float)value : YGUndefined); break;
    case 44: YGNodeStyleSetMinHeight(yoga, value); break;
    case 45: YGNodeStyleSetMaxHeight(yoga, value ? (float)value : YGUndefined); break;
    case 24: YGNodeStyleSetPositionType(yoga, value ? YGPositionTypeAbsolute : YGPositionTypeRelative); break;
    case 25: case 26: case 27: case 28: {
        const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
        YGNodeStyleSetPosition(yoga, edges[code - 25], value == kUnset ? YGUndefined : (float)value);
        break;
    }
    case 29: YGNodeStyleSetAspectRatio(yoga, value ? value / 1000.0f : YGUndefined); break;
    default:
        // 57 to 64 accessibility, 65 to 72 a text input's editing (host_access.inc).
        if (code >= 57 && code <= 64) applyAccessProp(node, code, value);
        else if (code >= 65 && code <= 72) applyInputProp(node, code, value);
        break;
    }
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
    if (!node->box && op != 6) return;
    // The grid (hosts/grid.h) keeps the props it reads.
    if (op == 2 && node->yoga) ui_grid_prop(node->yoga, kind, before);
    if (op == 9 && node->yoga) ui_grid_tracks(node->yoga, kind, text);
    if (op == 2) applyProp(node, kind, before);
    else if (op == 9) {
        std::string value = text ? text : "";
        if (kind == 4 && node->kind == 13) mountNative(node, text);
        else if (kind == 1) { node->label = value; applyLabel(node); }
        else if (kind == 8 || kind == 9) applyAccessText(node, kind, value);
        else if (kind == 5) { node->fontFile = value; applyFont(node); }
        else if (kind == 2 && node->kind == 5 && node->content) {
            // A multiline field (GtkTextView) has no placeholder: it is kept for a one-line one.
            node->placeholder = value;
            if (GTK_IS_ENTRY(node->content)) gtk_entry_set_placeholder_text(GTK_ENTRY(node->content), value.c_str());
        }
        else if (kind == 3 && node->kind == 7) loadImage(node, text);
    } else if (op == 3) {
        const char *value = text ? text : "";
        if (node->kind == 4) { node->title = value; applyButtonTitle(node); }
        else if (node->kind == 3 && node->content) gtk_label_set_text(GTK_LABEL(node->content), value);
        else if (node->kind == 5) setInputText(node, value);
        YGNodeMarkDirty(node->yoga);
    } else if ((op == 4 || op == 5) && node->kind == 12) {
        // A Modal is a layer over the window's content, outside the parent's layout.
        if (std::find(modals.begin(), modals.end(), node) != modals.end()) return;
        modals.push_back(node);
        if (gtk_widget_get_parent(node->box)) gtk_widget_unparent(node->box);
        gtk_widget_insert_before(node->box, stage, nullptr);
    } else if (op == 4 || op == 5) {
        auto parentFound = nodes.find(parent);
        Node *parentNode = parentFound != nodes.end() ? parentFound->second : root;
        GtkWidget *container = containerOf(parentNode);
        YGNodeRef oldParent = YGNodeGetParent(node->yoga);
        if (oldParent) YGNodeRemoveChild(oldParent, node->yoga);
        Node *next = nullptr;
        if (before) { auto n = nodes.find(before); if (n != nodes.end()) next = n->second; }
        size_t index = YGNodeGetChildCount(parentNode->yoga);
        if (next) {
            for (size_t i = 0; i < YGNodeGetChildCount(parentNode->yoga); ++i) {
                if (YGNodeGetChild(parentNode->yoga, i) == next->yoga) { index = i; break; }
            }
        }
        GtkWidget *sibling = next && next->box && gtk_widget_get_parent(next->box) == container ? next->box : nullptr;
        if (gtk_widget_get_parent(node->box) && gtk_widget_get_parent(node->box) != container) {
            g_object_ref(node->box);
            gtk_widget_unparent(node->box);
            gtk_widget_insert_before(node->box, container, sibling);
            g_object_unref(node->box);
        } else gtk_widget_insert_before(node->box, container, sibling);
        YGNodeInsertChild(parentNode->yoga, node->yoga, index);
        // Under a disabled element it reads as unavailable too.
        refreshDisabled(node);
    } else if (op == 6) {
        if (node->yoga) {
            YGNodeRef parentYoga = YGNodeGetParent(node->yoga);
            if (parentYoga) YGNodeRemoveChild(parentYoga, node->yoga);
            ui_grid_free(node->yoga);
            node->yoga = nullptr;
        }
        if (node->kind == 12) closeModal(node);
        if (node->box && !removeScreen(node) && !insideLeaving(node) && gtk_widget_get_parent(node->box)) gtk_widget_unparent(node->box);
        node->removed = true;
        nodes.erase(found);
        release(node);
    } else if (op == 7 || op == 8) {
        node->listening = op == 7;
    } else {
        fprintf(stderr, "ui host: unknown op %d\n", op);
        abort();
    }
}

// ---- commands ----

static std::string dataDir;

// libcurl, loaded on first use (no link-time dependency): fetch and remote images.
struct Curl {
    void *(*init)(void);
    int (*setopt)(void *, int, ...);
    int (*perform)(void *);
    int (*getinfo)(void *, int, ...);
    void (*cleanup)(void *);
    void *(*slistAppend)(void *, const char *);
    void (*slistFree)(void *);
    const char *(*strerror)(int);
    int (*globalInit)(long);
};
static Curl *curl(void) {
    static Curl api;
    static int state;
    if (state) return state == 1 ? &api : nullptr;
    state = 2;
    const char *names[] = {"libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4", "libcurl.4.dylib", "libcurl.dylib"};
    void *lib = nullptr;
    for (const char *name : names) if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
    if (!lib) return nullptr;
#define UI_CURL(field, name) api.field = (decltype(api.field))dlsym(lib, name); if (!api.field) return nullptr;
    UI_CURL(init, "curl_easy_init") UI_CURL(setopt, "curl_easy_setopt") UI_CURL(perform, "curl_easy_perform")
    UI_CURL(getinfo, "curl_easy_getinfo") UI_CURL(cleanup, "curl_easy_cleanup") UI_CURL(slistAppend, "curl_slist_append")
    UI_CURL(slistFree, "curl_slist_free_all") UI_CURL(strerror, "curl_easy_strerror") UI_CURL(globalInit, "curl_global_init")
#undef UI_CURL
    api.globalInit(3);
    state = 1;
    return &api;
}

struct FetchCall {
    std::string url, method, headers, body, saveTo;
    bool hasBody = false;
    long timeoutMs = 0;
};
struct FetchResult {
    int status = 0;
    std::string body, headers;
};

static size_t curlWrite(char *data, size_t size, size_t count, void *out) {
    ((std::string *)out)->append(data, size * count);
    return size * count;
}

// One request with libcurl, on the calling thread.
static FetchResult fetchNow(const FetchCall &call) {
    FetchResult result;
    Curl *api = curl();
    if (!api) { result.body = "libcurl is not installed"; return result; }
    void *handle = api->init();
    void *list = nullptr;
    size_t start = 0;
    while (start < call.headers.size()) {
        size_t end = call.headers.find('\n', start);
        if (end == std::string::npos) end = call.headers.size();
        std::string line = call.headers.substr(start, end - start);
        if (line.find(':') != std::string::npos) list = api->slistAppend(list, line.c_str());
        start = end + 1;
    }
    api->setopt(handle, 10002, call.url.c_str());
    api->setopt(handle, 52, 1L);
    api->setopt(handle, 99, 1L);
    if (!call.method.empty() && call.method != "GET") api->setopt(handle, 10036, call.method.c_str());
    if (call.method == "HEAD") api->setopt(handle, 44, 1L);
    if (list) api->setopt(handle, 10023, list);
    if (call.hasBody) {
        api->setopt(handle, 60, (long)call.body.size());
        api->setopt(handle, 10015, call.body.c_str());
    }
    if (call.timeoutMs > 0) api->setopt(handle, 155, call.timeoutMs);
    std::string headerText;
    api->setopt(handle, 20011, curlWrite);
    api->setopt(handle, 10001, &result.body);
    api->setopt(handle, 20079, curlWrite);
    api->setopt(handle, 10029, &headerText);
    int code = api->perform(handle);
    if (code != 0) {
        result.body = api->strerror(code);
    } else {
        long status = 0;
        api->getinfo(handle, 0x200002, &status);
        result.status = (int)status;
        // The last response's headers (after redirects), "Name: value" lines.
        size_t at = 0;
        while (at < headerText.size()) {
            size_t end = headerText.find('\n', at);
            if (end == std::string::npos) end = headerText.size();
            std::string line = headerText.substr(at, end - at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("HTTP/", 0) == 0) result.headers.clear();
            else if (line.find(':') != std::string::npos) result.headers += line + "\n";
            at = end + 1;
        }
    }
    if (list) api->slistFree(list);
    api->cleanup(handle);
    return result;
}

static void httpGet(const std::string &url, std::function<void(int status, std::string body)> done) {
    curl();
    std::thread([url, done]() {
        FetchCall call;
        call.url = url;
        call.method = "GET";
        FetchResult result = fetchNow(call);
        onMain([done, result]() { done(result.status, result.body); });
    }).detach();
}

static void answerAlert(GObject *source, GAsyncResult *result, gpointer data) {
    int request = GPOINTER_TO_INT(data);
    GError *error = nullptr;
    int index = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
    int buttons = GPOINTER_TO_INT(g_object_get_data(source, "ui-buttons"));
    if (error) { g_error_free(error); index = -1; }
    ui_dispatch_alert(request, index >= 0 && index < buttons ? index : -1);
}

static void copyText(const char *text) { gdk_clipboard_set_text(gtk_widget_get_clipboard(window), text); }

// Commands: 1 an alert, 2 open a URL, 4 hide the keyboard, 5 share, 6 copy, 7 paste, 8 a
// haptic, 9 announce, 10 fetch, 11 the splash (a desktop window has none).
static void ui_host_command(int request, int command, int count, const char **strings) {
    if (command == 1 && count >= 2) {
        GtkAlertDialog *alert = gtk_alert_dialog_new("%s", strings[0]);
        gtk_alert_dialog_set_detail(alert, strings[1]);
        std::vector<const char *> buttons;
        for (int i = 2; i < count; i++) buttons.push_back(strings[i]);
        if (buttons.empty()) buttons.push_back("OK");
        buttons.push_back(nullptr);
        gtk_alert_dialog_set_buttons(alert, buttons.data());
        gtk_alert_dialog_set_cancel_button(alert, -1);
        gtk_alert_dialog_set_modal(alert, TRUE);
        g_object_set_data(G_OBJECT(alert), "ui-buttons", GINT_TO_POINTER(count - 2));
        gtk_alert_dialog_choose(alert, GTK_WINDOW(window), nullptr, answerAlert, GINT_TO_POINTER(request));
        g_object_unref(alert);
    } else if (command == 2 && count >= 1) {
        GError *error = nullptr;
        if (strcmp(strings[0], "app-settings:") != 0 && !g_app_info_launch_default_for_uri(strings[0], nullptr, &error)) {
            fprintf(stderr, "ui: could not open %s: %s\n", strings[0], error ? error->message : "");
            if (error) g_error_free(error);
        }
    } else if (command == 4) {
        gtk_root_set_focus(GTK_ROOT(window), nullptr);
    } else if (command == 5 && count >= 2) {
        // No system share sheet on a Linux desktop: the text and the URL are copied, as the
        // web host does without navigator.share.
        std::string text = strings[0];
        if (strings[1][0]) text += text.empty() ? strings[1] : std::string(" ") + strings[1];
        copyText(text.c_str());
    } else if (command == 6 && count >= 1) {
        copyText(strings[0]);
    } else if (command == 7) {
        gdk_clipboard_read_text_async(gtk_widget_get_clipboard(window), nullptr, [](GObject *source, GAsyncResult *result, gpointer data) {
            char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, nullptr);
            ui_dispatch_answer(GPOINTER_TO_INT(data), text ? text : "");
            g_free(text);
        }, GINT_TO_POINTER(request));
    } else if (command == 9 && count >= 1) {
#if GTK_CHECK_VERSION(4, 14, 0)
        gtk_accessible_announce(GTK_ACCESSIBLE(window), strings[0], GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_HIGH);
#endif
    } else if (command == 10 && count >= 6) {
        FetchCall call;
        call.url = strings[0];
        call.method = strings[1];
        call.headers = strings[2];
        call.body = strings[3];
        call.timeoutMs = atol(strings[4]);
        call.hasBody = strings[5][0] == '1';
        if (count >= 7) call.saveTo = strings[6];
        curl();
        std::thread([call, request]() {
            FetchResult result = fetchNow(call);
            if (!call.saveTo.empty() && result.status >= 200 && result.status < 300) {
                std::string temporary = call.saveTo + ".part";
                FILE *file = fopen(temporary.c_str(), "wb");
                bool ok = file && fwrite(result.body.data(), 1, result.body.size(), file) == result.body.size();
                if (file) ok = fclose(file) == 0 && ok;
                ok = ok && rename(temporary.c_str(), call.saveTo.c_str()) == 0;
                if (ok) result.body.clear();
                else { result.status = 0; result.body = "could not write " + call.saveTo; }
            }
            onMain([request, result]() { ui_dispatch_fetch(request, result.status, result.body.c_str(), result.headers.c_str()); });
        }).detach();
    }
}

#ifdef UI_TEST
static void driverClockTo(double now);
static void driverClock(bool owned, double now, bool settle);
#endif
extern "C" void ui_host_commit(const uint8_t *data, int len) {
#ifdef UI_TEST
    // The animations this commit starts begin at the driver's time.
    driverClockTo(ui_driver_time());
#endif
    applying = true;
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    applying = false;
    relayout();
    slideScreens();
}

static double hostNow(void) {
#ifdef UI_TEST
    if (clockFrozen) return clockBase + ui_driver_time() / 1000.0;
#endif
    return monotonic() + clockOffset;
}
