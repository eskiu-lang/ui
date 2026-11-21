// host_main.inc: the window. Part of host.cpp, which #includes it (one translation unit).

// ---- the window ----

static std::string exeDir(void) {
    static std::string dir;
    if (!dir.empty()) return dir;
    char path[4096] = {};
#ifdef __APPLE__
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0) path[0] = 0;
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    path[n > 0 ? n : 0] = 0;
#endif
    char *slash = strrchr(path, '/');
    dir = slash ? std::string(path, slash - path) : ".";
    return dir;
}

#ifdef __linux__
static std::string fontFamily(const std::string &file) {
    static std::map<std::string, std::string> families;
    auto known = families.find(file);
    if (known != families.end()) return known->second;
    std::string path = file[0] == '/' ? file : assetFile(file), family;
    FcConfig *config = FcConfigGetCurrent();
    int count = 0;
    FcPattern *pattern = path.empty() ? nullptr : FcFreeTypeQuery((const FcChar8 *)path.c_str(), 0, nullptr, &count);
    FcChar8 *name = nullptr;
    if (pattern && FcPatternGetString(pattern, FC_FAMILY, 0, &name) == FcResultMatch && FcConfigAppFontAddFile(config, (const FcChar8 *)path.c_str())) {
        family = (const char *)name;
        pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(pango_cairo_font_map_get_default()));
    }
    if (pattern) FcPatternDestroy(pattern);
    if (family.empty()) fprintf(stderr, "ui: font %s did not load\n", file.c_str());
    families[file] = family;
    return family;
}
#else
// GTK on macOS draws with Core Text, which this host does not register fonts with.
static std::string fontFamily(const std::string &) { return ""; }
#endif


// The system's appearance, accessibility settings and language, to the core.
static void reportSystem(void) {
    GtkSettings *settings = gtk_settings_get_default();
    gboolean preferDark = FALSE;
    char *theme = nullptr;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark, "gtk-theme-name", &theme, nullptr);
    bool dark = preferDark || (theme && g_str_has_suffix(theme, "-dark"));
#if GTK_CHECK_VERSION(4, 20, 0)
    GtkInterfaceColorScheme scheme = GTK_INTERFACE_COLOR_SCHEME_DEFAULT;
    g_object_get(settings, "gtk-interface-color-scheme", &scheme, nullptr);
    if (scheme == GTK_INTERFACE_COLOR_SCHEME_DARK) dark = true;
#endif
    g_free(theme);
    darkMode = dark;
    ui_report_appearance(dark ? 1 : 0);
    ui_report_accessibility(0, userAnimations ? 0 : 1, (int)std::lround(textScale * 100));
    const char *const *languages = g_get_language_names();
    std::string tag = languages && languages[0] ? languages[0] : "en";
    tag = tag.substr(0, tag.find_first_of(".@"));
    if (tag == "C" || tag == "POSIX" || tag.empty()) tag = "en";
    std::replace(tag.begin(), tag.end(), '_', '-');
    bool rtl = gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL;
    ui_report_locale(tag.c_str(), rtl ? 1 : 0);
    // Right to left: Yoga mirrors rows, start and end (GTK's widgets follow the default
    // direction on their own).
    if (rtl != ui_layout_rtl()) {
        ui_layout_rtl() = rtl;
        for (auto &entry : nodes) applyLabelLayout(entry.second);
        if (root) relayout();
    }
}

static int scaleFactor(void) { return window ? gtk_widget_get_scale_factor(window) : 1; }

// The window, the insets (none on a desktop) and the system's settings, once the core runs.
static void reportEnvironment(void) {
    ui_report_window(stageWidth, stageHeight, scaleFactor() * 100);
    ui_report_insets(0, 0, 0, 0, 0);
    reportSystem();
}

// The stage holds the app's root and, over it, the overlays; its size is the app's window.
static gboolean onResized(gpointer) {
    reportEnvironment();
    relayout();
    return G_SOURCE_REMOVE;
}
static void stageAllocated(int width, int height) {
    if (width == stageWidth && height == stageHeight) return;
    stageWidth = width;
    stageHeight = height;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, onResized, nullptr, nullptr);
}

#define UI_TYPE_STAGE (ui_stage_get_type())
G_DECLARE_FINAL_TYPE(UiStage, ui_stage, UI, STAGE, UiBox)
struct _UiStage {
    UiBox parent;
};
G_DEFINE_TYPE(UiStage, ui_stage, UI_TYPE_BOX)
static void ui_stage_init(UiStage *) {}
static void ui_stage_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
    stageAllocated(width, height);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        if (gtk_widget_get_visible(child)) allocateChild(child, 0, 0, width, height, nullptr);
    }
    (void)baseline;
}
static void ui_stage_measure(GtkWidget *, GtkOrientation, int, int *minimum, int *natural, int *, int *) {
    *minimum = 0;
    *natural = 0;
}
static void ui_stage_class_init(UiStageClass *klass) {
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->size_allocate = ui_stage_size_allocate;
    widget->measure = ui_stage_measure;
}

static bool quitting;
static void quit(void) {
    if (quitting) return;
    quitting = true;
    ui_stop();
}

static void back(void) { ui_report_back(); }

// The window's actions: quit, close, back, full screen, and Edit's for the focused field.
static void addActions(GtkWidget *target) {
    GSimpleActionGroup *app = g_simple_action_group_new();
    struct Entry {
        const char *name;
        void (*run)(void);
    };
    static const Entry entries[] = {
        {"quit", []() { gtk_window_close(GTK_WINDOW(window)); }},
        {"close", []() { gtk_window_close(GTK_WINDOW(window)); }},
        {"back", back},
        {"refresh", refreshShown},
        {"fullscreen", []() {
             if (gtk_window_is_fullscreen(GTK_WINDOW(window))) gtk_window_unfullscreen(GTK_WINDOW(window));
             else gtk_window_fullscreen(GTK_WINDOW(window));
         }},
        {"cut", []() { GtkWidget *f = gtk_root_get_focus(GTK_ROOT(window)); if (f) gtk_widget_activate_action(f, "clipboard.cut", nullptr); }},
        {"copy", []() { GtkWidget *f = gtk_root_get_focus(GTK_ROOT(window)); if (f) gtk_widget_activate_action(f, "clipboard.copy", nullptr); }},
        {"paste", []() { GtkWidget *f = gtk_root_get_focus(GTK_ROOT(window)); if (f) gtk_widget_activate_action(f, "clipboard.paste", nullptr); }},
        {"select-all", []() { GtkWidget *f = gtk_root_get_focus(GTK_ROOT(window)); if (f) gtk_widget_activate_action(f, "selection.select-all", nullptr); }},
    };
    for (const Entry &entry : entries) {
        GSimpleAction *action = g_simple_action_new(entry.name, nullptr);
        g_signal_connect(action, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer run) { ((void (*)(void))run)(); }),
                         (gpointer)entry.run);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
        g_object_unref(action);
    }
    gtk_widget_insert_action_group(target, "app", G_ACTION_GROUP(app));
    g_object_unref(app);
    // The shortcuts, in the capture phase for the window's own (a field keeps Ctrl+C and the
    // like: they are its clipboard actions too).
    GtkEventController *shortcuts = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcuts), GTK_SHORTCUT_SCOPE_GLOBAL);
    struct Key {
        const char *trigger, *action;
    };
    static const Key keys[] = {{"<Control>q", "app.quit"}, {"<Control>w", "app.close"}, {"Escape", "app.back"},
                               {"<Alt>Left", "app.back"}, {"F11", "app.fullscreen"}, {"F5", "app.refresh"},
                               {"<Control>r", "app.refresh"}};
    for (const Key &key : keys) {
        gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
            gtk_shortcut_new(gtk_shortcut_trigger_parse_string(key.trigger), gtk_named_action_new(key.action)));
    }
    gtk_widget_add_controller(target, shortcuts);
    // A mouse's back button.
    GtkGesture *mouse = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mouse), 8);
    g_signal_connect(mouse, "pressed", G_CALLBACK(+[](GtkGestureClick *, int, double, double, gpointer) { back(); }), nullptr);
    gtk_widget_add_controller(target, GTK_EVENT_CONTROLLER(mouse));
}

// The menu bar: File (Close, Quit), Edit (Cut, Copy, Paste, Select All) and View (Back, Full
// Screen), with their shortcuts shown.
static GtkWidget *menuBar(void) {
    GMenu *bar = g_menu_new();
    GMenu *file = g_menu_new();
    auto item = [](GMenu *menu, const char *label, const char *action, const char *accel) {
        GMenuItem *entry = g_menu_item_new(label, action);
        if (accel) g_menu_item_set_attribute(entry, "accel", "s", accel);
        g_menu_append_item(menu, entry);
        g_object_unref(entry);
    };
    item(file, "Close Window", "app.close", "<Control>w");
    item(file, "Quit", "app.quit", "<Control>q");
    g_menu_append_submenu(bar, "File", G_MENU_MODEL(file));
    GMenu *edit = g_menu_new();
    item(edit, "Cut", "app.cut", "<Control>x");
    item(edit, "Copy", "app.copy", "<Control>c");
    item(edit, "Paste", "app.paste", "<Control>v");
    item(edit, "Select All", "app.select-all", "<Control>a");
    g_menu_append_submenu(bar, "Edit", G_MENU_MODEL(edit));
    GMenu *view = g_menu_new();
    item(view, "Back", "app.back", "<Alt>Left");
    item(view, "Refresh", "app.refresh", "F5");
    item(view, "Full Screen", "app.fullscreen", "F11");
    g_menu_append_submenu(bar, "View", G_MENU_MODEL(view));
    GtkWidget *widget = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(bar));
    g_object_unref(file); g_object_unref(edit); g_object_unref(view); g_object_unref(bar);
    return widget;
}

// What GTK's theme draws for a button or a field the app styles: nothing (the box paints),
// and a pressed styled button dims as on the other hosts.
static const char *kStyles =
    "button.ui-styled, button.ui-styled:hover, button.ui-styled:active, button.ui-styled:checked, button.ui-styled:focus,"
    " button.ui-styled:focus-visible {"
    " background: none; border: none; box-shadow: none; outline: none; padding: 0; min-height: 0; min-width: 0; }\n"
    "button.ui-styled:active { opacity: 0.7; }\n"
    ".ui-stage { background-color: @window_bg_color; }\n"
    "entry.ui-styled, entry.ui-styled:focus, entry.ui-styled:focus-within {"
    " background: none; border: none; box-shadow: none; outline: none; padding: 0; min-height: 0; }\n";

#ifdef UI_RELOAD
// `ui serve`'s builds: UI_RELOAD_FILE names a file holding the path of the app's newest
// library. When it changes, the app starts again on the new code in the running process: the
// old core saves its routes and signals and stops, the widgets, timers and nodes it left go,
// and the new one starts on what the old one saved.
static std::string loadedCore;

static std::string newestCore(void) {
    const char *file = getenv("UI_RELOAD_FILE");
    if (!file) return "";
    gchar *text = nullptr;
    if (!g_file_get_contents(file, &text, nullptr, nullptr)) return "";
    std::string out = g_strstrip(text);
    g_free(text);
    return out;
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
    for (auto &timer : timers) g_source_remove(timer.second);
    timers.clear();
    modals.clear();
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (node->yoga) ui_grid_free(node->yoga);
        node->yoga = nullptr;
        node->removed = true;
        if (node->box && gtk_widget_get_parent(node->box)) gtk_widget_unparent(node->box);
        release(node);
    }
    nodes.clear();
    for (Node *node : leaving) { if (node->box && gtk_widget_get_parent(node->box)) gtk_widget_unparent(node->box); release(node); }
    leaving.clear();
    YGNodeRemoveAllChildren(root->yoga);
    if (!loadCore(path.c_str())) {
#ifdef UI_TEST
        free(kept);
#endif
        return;
    }
    loadedCore = path;
    ui_report_data_dir(dataDir.c_str());
#ifdef UI_TEST
    if (kept && ui_reload_restore) ui_reload_restore(kept);
    free(kept);
#endif
    ui_start(launchExample);
    reportEnvironment();
    relayout();
    fprintf(stderr, "ui: restarted on %s\n", strrchr(path.c_str(), '/') ? strrchr(path.c_str(), '/') + 1 : path.c_str());
}
#endif

#ifdef UI_TEST
static void runConformance(void);
// The driver (hosts/driver.h, docs/agents.md). While it owns the clock the host's transitions
// read the driver's time (hostNow), so they move only when the clock does, and GTK's own
// animations (a switch's slide, the spinner, a field's caret blinking) are off; `clock settle`
// moves the host's time past their ends.
// The requests wait for the window on screen (mapped), so what they see is what shows.
static bool windowMapped;
static std::vector<std::function<void()>> driverWaiting;
static void driverOnMain(std::function<void()> work) {
    onMain([work]() {
        if (windowMapped) work(); else driverWaiting.push_back(work);
    });
}
static void driverMapped(void) {
    windowMapped = true;
    for (auto &work : driverWaiting) onMain(work);
    driverWaiting.clear();
}

static void driverClockTo(double) {
    if (clockFrozen) tickAnimations();
}
static void driverClock(bool owned, double now, bool settle) {
    GtkSettings *settings = gtk_settings_get_default();
    if (owned) {
        if (!clockFrozen) {
            double local = hostNow();
            clockFrozen = true;
            clockBase = local - now / 1000.0;
            g_object_set(settings, "gtk-enable-animations", FALSE, "gtk-cursor-blink", FALSE, nullptr);
        }
        if (settle) clockBase += 60;
        driverClockTo(now);
    } else if (clockFrozen) {
        double paused = hostNow();
        clockFrozen = false;
        clockOffset = paused - monotonic();
        g_object_set(settings, "gtk-enable-animations", userAnimations, "gtk-cursor-blink", TRUE, nullptr);
    }
}

// GTK lays out at its next frame, which a hidden or covered window may not get soon: the
// window is laid out now, as GTK would, so a screenshot shows what the app has.
static void allocateNow(void) {
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    int width = surface ? gdk_surface_get_width(surface) : 0, height = surface ? gdk_surface_get_height(surface) : 0;
    if (width <= 0 || height <= 0) gtk_window_get_default_size(GTK_WINDOW(window), &width, &height);
    gtk_widget_allocate(window, width, height, -1, nullptr);
}

static bool driverHostRequest(const std::string &request, std::string &answer) {
    if (driverText(request, "op") != "screenshot") return false;
    // The window's content as GTK draws it (the stage, under the menu bar, with the window's
    // background), at its scale, drawn now rather than from the last frame shown.
    std::string file = driverText(request, "file");
    allocateNow();
    GtkWidget *column = gtk_widget_get_parent(stage);
    graphene_rect_t bounds;
    GdkTexture *texture = nullptr;
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
    if (renderer && column && gtk_widget_compute_bounds(stage, column, &bounds)) {
        int scale = scaleFactor();
        GtkSnapshot *snapshot = gtk_snapshot_new();
        gtk_snapshot_scale(snapshot, (float)scale, (float)scale);
        gtk_snapshot_translate(snapshot, addr(point(-bounds.origin.x, -bounds.origin.y)));
        gtk_widget_snapshot_child(column, stage, snapshot);
        GskRenderNode *render = gtk_snapshot_free_to_node(snapshot);
        if (render) {
            graphene_rect_t viewport = GRAPHENE_RECT_INIT(0, 0, bounds.size.width * scale, bounds.size.height * scale);
            texture = gsk_renderer_render_texture(renderer, render, &viewport);
            gsk_render_node_unref(render);
        }
    }
    if (!texture || !gdk_texture_save_to_png(texture, file.c_str())) {
        if (texture) g_object_unref(texture);
        answer = "{\"ok\": false, \"op\": \"screenshot\", \"error\": \"could not write the screenshot\"}";
        return true;
    }
    answer = "{\"ok\": true, \"op\": \"screenshot\", \"width\": " + std::to_string(gdk_texture_get_width(texture)) +
             ", \"height\": " + std::to_string(gdk_texture_get_height(texture)) + "}";
    g_object_unref(texture);
    return true;
}

// The process's memory: its resident set (its footprint on macOS).
static double memoryBytes(void) {
#ifdef __APPLE__
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
    return (double)info.phys_footprint;
#else
    FILE *file = fopen("/proc/self/statm", "r");
    long pages = 0, resident = 0;
    if (file) {
        if (fscanf(file, "%ld %ld", &pages, &resident) != 2) resident = 0;
        fclose(file);
    }
    return (double)resident * (double)sysconf(_SC_PAGESIZE);
#endif
}

// Whether the node shows: in the window, not hidden, not invisible.
static bool shows(Node *node) {
    if (!node->box || !gtk_widget_get_mapped(node->box)) return false;
    for (GtkWidget *up = node->box; up; up = gtk_widget_get_parent(up)) {
        if (!gtk_widget_get_visible(up) || gtk_widget_get_opacity(up) <= 0) return false;
        Node *owner = nodeOfBox(up);
        if (owner && owner->yoga && YGNodeStyleGetDisplay(owner->yoga) == YGDisplayNone) return false;
    }
    return true;
}

static std::string driverCarry(const std::string &d) {
    std::string op = driverOp(d);
    if (op == "hello" || op == "clock") {
        driverClock(driverFlag(d, "virtual"), driverNumber(d, "now"), driverFlag(d, "settle"));
        return "";
    }
    if (op == "logs") return driverLogs();
    if (op == "perf") return driverPerf(memoryBytes());
    if (op == "layout") {
        std::string rows;
        char line[160];
        for (auto &entry : nodes) {
            Node *node = entry.second;
            if (!node->box || !node->yoga) continue;
            double x = 0, y = 0;
            windowPoint(node, x, y);
            snprintf(line, sizeof line, "%d %.1f %.1f %.1f %.1f %d\n", node->id, x, y, node->w, node->h, shows(node) ? 1 : 0);
            rows += line;
        }
        return rows;
    }
    int id = (int)driverNumber(d, "node");
    auto found = nodes.find(id);
    Node *node = found == nodes.end() ? nullptr : found->second;
    if (!node || !node->box) return "error: #" + std::to_string(id) + " has no view";
    // The input goes in as GTK's: a control's signal, a field's edit, the adjustment's scroll.
    if (op == "tap") {
        if (node->kind == 9 && node->content) gtk_switch_set_active(GTK_SWITCH(node->content), !gtk_switch_get_active(GTK_SWITCH(node->content)));
        else if (node->kind == 5 && node->content) gtk_widget_grab_focus(node->content);
        else if (node->kind == 12) ui_dispatch(id, node->generation);
        else if (node->kind == 4 && node->content) g_signal_emit_by_name(node->content, "clicked");
        else if (node->kind == 8) ui_dispatch(id, node->generation);
        else return "error: the view takes no press";
        return "";
    }
    if (op == "type") {
        if (node->kind != 5 || !node->content) return "error: not a text field";
        gtk_widget_grab_focus(node->content);
        std::string text = driverText(d, "text");
        setInputText(node, text.c_str());
        if (GTK_IS_EDITABLE(node->content)) gtk_editable_set_position(GTK_EDITABLE(node->content), -1);
        return "";
    }
    if (op == "scroll") {
        if (node->kind != 6 || !node->adjustment) return "error: not a scroll view";
        GtkAdjustment *a = node->adjustment;
        double most = std::max(0.0, gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a));
        double y = std::min(most, std::max(0.0, gtk_adjustment_get_value(a) + driverNumber(d, "dy")));
        gtk_adjustment_set_value(a, y);
        return std::to_string((int)std::lround(y));
    }
    return "error: the Linux host does not know " + op;
}
#endif

static void startApp(void) {
    // The app's data directory, before the app starts (ui_data_dir).
    dataDir = std::string(g_get_user_data_dir()) + "/" + UI_APP_ID;
    g_mkdir_with_parents(dataDir.c_str(), 0700);
    ui_report_data_dir(dataDir.c_str());
}

static const char *launchLink;

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--link") && i + 1 < argc) launchLink = argv[i + 1];
        if (!strcmp(argv[i], "--example") && i + 1 < argc) launchExample = atoi(argv[i + 1]);
    }
    // The window's size in points: UI_WINDOW=WxH, else ui.toml [linux] window, else 900 by 700.
    const char *size = getenv("UI_WINDOW") ? getenv("UI_WINDOW") : UI_WINDOW;
    int w = 0, h = 0;
    if (sscanf(size, "%dx%d", &w, &h) == 2 && w >= 200 && h >= 150) { stageWidth = w; stageHeight = h; }
    // The app's id, for the desktop and the portal (the app's .desktop file has the same).
    g_set_prgname(UI_APP_ID);
    g_set_application_name(UI_APP_NAME);
    gtk_init();
    g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &userAnimations, nullptr);
    textScale = systemTextScale();
#ifdef UI_TEST
    // The driver: the app's output kept for `logs` (and still printed), and its socket.
    int printed = dup(2);
    driverCapture([printed](const char *bytes, size_t n) { (void)!write(printed, bytes, n); });
    const char *agentPort = getenv("UI_AGENT_PORT");
    int port = agentPort ? atoi(agentPort) : 8769;
    if (!driverListenTcp(port)) fprintf(stderr, "ui: the driver could not listen on 127.0.0.1:%d\n", port);
#endif
#ifdef UI_RELOAD
    loadedCore = newestCore();
    if (loadedCore.empty() || !loadCore(loadedCore.c_str())) { fprintf(stderr, "ui: no app library (UI_RELOAD_FILE)\n"); return 1; }
    // The pid next to the reload file: `ui run linux` reloads a running app instead of starting it.
    std::string pidFile = std::string(getenv("UI_RELOAD_FILE"));
    pidFile = pidFile.substr(0, pidFile.rfind('/') + 1) + "pid";
    g_file_set_contents(pidFile.c_str(), std::to_string(getpid()).c_str(), -1, nullptr);
    g_timeout_add(50, [](gpointer) -> gboolean {
        std::string path = newestCore();
        if (!path.empty() && path != loadedCore) restartCore(path);
        return G_SOURCE_CONTINUE;
    }, nullptr);
#endif
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, kStyles);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), UI_APP_NAME);
    std::string icon = exeDir() + "/icon.png";
    if (g_file_test(icon.c_str(), G_FILE_TEST_EXISTS)) {
        // A window icon from a file: the theme's search path gets the executable's directory.
        GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
        gtk_icon_theme_add_search_path(theme, exeDir().c_str());
        gtk_window_set_icon_name(GTK_WINDOW(window), "icon");
    }
    stage = GTK_WIDGET(g_object_new(UI_TYPE_STAGE, "accessible-role", GTK_ACCESSIBLE_ROLE_GENERIC, nullptr));
    UI_BOX(stage)->role = kBoxStage;
    gtk_widget_add_css_class(stage, "ui-stage");
    gtk_widget_set_hexpand(stage, TRUE);
    gtk_widget_set_vexpand(stage, TRUE);
    root = new Node();
    root->kind = 1;
    root->yoga = YGNodeNew();
    YGNodeStyleSetFlexDirection(root->yoga, YGFlexDirectionColumn);
    root->box = newBox(GTK_ACCESSIBLE_ROLE_GENERIC, kBoxNode, root);
    root->refs = 2;
    gtk_widget_insert_before(root->box, stage, nullptr);
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *menu = menuBar();
    gtk_box_append(GTK_BOX(column), menu);
    gtk_box_append(GTK_BOX(column), stage);
    gtk_window_set_child(GTK_WINDOW(window), column);
    addActions(window);
    // The window is the app's size under its menu bar, resizable down to 320 by 240.
    int menuHeight = 0, natural = 0;
    gtk_widget_measure(menu, GTK_ORIENTATION_VERTICAL, stageWidth, &menuHeight, &natural, nullptr, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(window), stageWidth, stageHeight + natural);
    gtk_widget_set_size_request(stage, 320, 240);
    g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer) -> gboolean { quit(); return FALSE; }), nullptr);
    g_signal_connect(window, "notify::is-active", G_CALLBACK(+[](GObject *object, GParamSpec *, gpointer) {
        if (!quitting) ui_report_active(gtk_window_is_active(GTK_WINDOW(object)) ? 1 : 0);
    }), nullptr);
    GtkSettings *settings = gtk_settings_get_default();
    for (const char *name : {"notify::gtk-application-prefer-dark-theme", "notify::gtk-theme-name"
#if GTK_CHECK_VERSION(4, 20, 0)
                             , "notify::gtk-interface-color-scheme"
#endif
         }) {
        g_signal_connect(settings, name, G_CALLBACK(+[](GObject *, GParamSpec *, gpointer) { reportSystem(); }), nullptr);
    }
    // The system's text scale: the texts in their new size, and the factor to the core.
    g_signal_connect(settings, "notify::gtk-xft-dpi", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer) {
        applyTextScale();
        reportSystem();
    }), nullptr);
#ifdef UI_TEST
    g_signal_connect(window, "map", G_CALLBACK(+[](GtkWidget *, gpointer) { driverMapped(); }), nullptr);
#endif
    startApp();
    if (launchLink) ui_report_link(launchLink);
#ifdef UI_TEST
    // The conformance scene's own run (host_conformance.inc): example 3 asks it to fetch too.
    bool conformance = getenv("UI_CONFORMANCE") && !strcmp(getenv("UI_CONFORMANCE"), "1");
    if (conformance) launchExample = 3;
#endif
    ui_start(launchExample);
    reportEnvironment();
    relayout();
#ifdef UI_TEST
    if (conformance) runConformance();
#endif
    gtk_window_present(GTK_WINDOW(window));
    while (!quitting) g_main_context_iteration(nullptr, TRUE);
    return 0;
}
