// host_views.inc: images, native views, screens, layout, events, frames and timers. Part of host.cpp, which #includes it (one translation unit).

// ---- images ----

static std::string exeDir(void);

// The file of a bundled asset (in assets/ next to the executable), or "".
static std::string assetFile(const std::string &name) {
    std::string path = exeDir() + "/assets/" + name;
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) ? path : "";
}

static void httpGet(const std::string &url, std::function<void(int status, std::string body)> done);

static void setTexture(Node *node, GdkTexture *texture) {
    if (node->texture) g_object_unref(node->texture);
    node->texture = texture;
    if (node->yoga) YGNodeMarkDirty(node->yoga);
    if (node->box) gtk_widget_queue_draw(node->box);
}

// A bundled image by file name, a file, or an http(s) URL fetched in the background. The image
// changes the node's measured size, so the layout runs again when it arrives.
static void loadImage(Node *node, const char *text) {
    node->source = text ? text : "";
    setTexture(node, nullptr);
    if (node->source.empty()) return;
    std::string source = node->source;
    if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) {
        node->refs++;
        httpGet(source, [node, source](int status, std::string body) {
            if (!node->removed && node->source == source && status >= 200 && status < 300) {
                GBytes *bytes = g_bytes_new(body.data(), body.size());
                GdkTexture *texture = gdk_texture_new_from_bytes(bytes, nullptr);
                g_bytes_unref(bytes);
                if (texture) { setTexture(node, texture); relayout(); }
            }
            release(node);
        });
        return;
    }
    std::string path = source.rfind("file://", 0) == 0 ? source.substr(7) : source[0] == '/' ? source : assetFile(source);
    if (path.empty()) return;
    setTexture(node, gdk_texture_new_from_filename(path.c_str(), nullptr));
}

// ---- native views ----

struct ViewType {
    UiViewCreate create;
    UiViewSet set;
};

static std::map<std::string, ViewType> &viewTypes(void) {
    static std::map<std::string, ViewType> types;
    return types;
}

extern "C" void ui_register_view(const char *type, UiViewCreate create, UiViewSet set) { viewTypes()[type] = {create, set}; }

extern "C" GtkWindow *ui_native_window(void) { return GTK_WINDOW(window); }

// Work for the main thread, from any thread.
static void onMain(std::function<void()> work) {
    auto *held = new std::function<void()>(std::move(work));
    g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, [](gpointer data) -> gboolean {
        auto *w = (std::function<void()> *)data;
        (*w)();
        return G_SOURCE_REMOVE;
    }, held, [](gpointer data) { delete (std::function<void()> *)data; });
}

extern "C" void ui_native_emit(const char *channel, const char *payload) {
    std::string c = channel ? channel : "", p = payload ? payload : "";
    onMain([c, p]() { ui_dispatch_native(c.c_str(), p.c_str()); });
}

struct UiViewEmitter {
    int id, generation;
};

extern "C" void ui_view_emit(UiViewEmitter *emitter, const char *name, const char *payload) {
    if (!emitter) return;
    int id = emitter->id, generation = emitter->generation;
    std::string n = name ? name : "", p = payload ? payload : "";
    onMain([id, generation, n, p]() { ui_dispatch_view(id, generation, n.c_str(), p.c_str()); });
}

// The widget a NativeView's type makes, filling its node's box.
static void mountNative(Node *node, const char *type) {
    if (node->content) { gtk_widget_unparent(node->content); node->content = nullptr; }
    node->nativeType = type ? type : "";
    auto entry = viewTypes().find(node->nativeType);
    if (entry == viewTypes().end()) { fprintf(stderr, "ui: no native view registered as %s\n", node->nativeType.c_str()); return; }
    UiViewEmitter *emitter = new UiViewEmitter{node->id, node->generation};
    GtkWidget *view = entry->second.create(emitter);
    if (!view) { delete emitter; return; }
    g_object_set_data_full(G_OBJECT(view), "ui-emitter", emitter, [](gpointer data) { delete (UiViewEmitter *)data; });
    gtk_widget_insert_after(view, node->box, nullptr);
    node->content = view;
}

static void ui_host_named(int id, const char *name, const char *value) {
    auto found = nodes.find(id);
    if (found == nodes.end() || !found->second->content || found->second->kind != 13) return;
    auto entry = viewTypes().find(found->second->nativeType);
    if (entry != viewTypes().end() && entry->second.set) entry->second.set(found->second->content, name, value);
}

// ---- screens ----

// A screen under the top is hidden (after a push's slide, which it stays under); one that
// comes on top for the first time over a screen that was shown slides in from the right once
// laid out (a push). A removed top screen slides out (a pop): see removeScreen.
static const double kSlide = 0.3;
static void screenActive(Node *node, int active) {
    node->active = active != 0;
    YGNodeStyleSetDisplay(node->yoga, active ? YGDisplayFlex : YGDisplayNone);
    if (active) {
        node->hideAt = -1;
        gtk_widget_set_visible(node->box, TRUE);
        if (!node->wasActive) node->slideIn = true;
        node->wasActive = true;
    } else if (node->wasActive && gtk_widget_get_mapped(node->box)) {
        node->hideAt = hostNow() + kSlide;
        watchAnimation(node);
    } else gtk_widget_set_visible(node->box, FALSE);
}

static bool insideLeaving(Node *node) {
    if (!node->box) return false;
    for (GtkWidget *up = gtk_widget_get_parent(node->box); up; up = gtk_widget_get_parent(up)) {
        Node *owner = nodeOfBox(up);
        if (owner && std::find(leaving.begin(), leaving.end(), owner) != leaving.end()) return true;
    }
    return false;
}

static bool otherScreenUnder(Node *node, bool visibleOnly) {
    GtkWidget *parent = gtk_widget_get_parent(node->box);
    for (auto &entry : nodes) {
        Node *other = entry.second;
        if (other == node || other->kind != 14 || !other->wasActive || !other->box) continue;
        if (gtk_widget_get_parent(other->box) != parent) continue;
        if (visibleOnly && !gtk_widget_get_visible(other->box)) continue;
        return true;
    }
    return false;
}

static bool removeScreen(Node *node) {
    if (node->kind != 14 || !node->box || !gtk_widget_get_visible(node->box) || !gtk_widget_get_parent(node->box)) return false;
    if (!gtk_widget_get_mapped(node->box) || !otherScreenUnder(node, false)) return false;
    GtkWidget *parent = gtk_widget_get_parent(node->box);
    gtk_widget_insert_before(node->box, parent, nullptr);
    double from[4];
    valueOf(node, kSlideX, from);
    node->slide = node->w;
    animateTo(node, kSlideX, from, (int)(kSlide * 1000), 2);
    if (!node->anims[kSlideX].on) return false;
    node->refs++;
    leaving.push_back(node);
    return true;
}

static void slideScreens(void) {
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (!node->slideIn) continue;
        node->slideIn = false;
        if (!node->box || !otherScreenUnder(node, true)) continue;
        double from[4] = {node->w, 0, 0, 0};
        node->slide = 0;
        animateTo(node, kSlideX, from, (int)(kSlide * 1000), 1);
    }
}

// ---- layout ----

// A scroll's offset, in points.
static double scrollOffset(Node *node) { return node->adjustment ? gtk_adjustment_get_value(node->adjustment) : 0; }

// The node's top left from the window's, through its Yoga parents and their scrolls.
static void windowPoint(Node *node, double &x, double &y) {
    x = node->x; y = node->y;
    YGNodeRef up = node->yoga ? YGNodeGetParent(node->yoga) : nullptr;
    while (up) {
        Node *owner = (Node *)YGNodeGetContext(up);
        if (!owner) break;
        x += owner->x; y += owner->y;
        if (owner->kind == 6) y -= scrollOffset(owner);
        up = YGNodeGetParent(up);
    }
}

static void reportLayout(Node *node, bool force) {
    double wx, wy;
    windowPoint(node, wx, wy);
    int frame[4] = {(int)std::lround(node->x), (int)std::lround(node->y), (int)std::lround(node->w), (int)std::lround(node->h)};
    int at[2] = {(int)std::lround(wx), (int)std::lround(wy)};
    if (!force && node->reported && std::equal(frame, frame + 4, node->reportedFrame) && std::equal(at, at + 2, node->reportedWindow)) return;
    node->reported = true;
    std::copy(frame, frame + 4, node->reportedFrame);
    std::copy(at, at + 2, node->reportedWindow);
    ui_dispatch_layout(node->id, node->generation, frame[0], frame[1], frame[2], frame[3], at[0], at[1]);
}

static void place(YGNodeRef yoga) {
    Node *owner = (Node *)YGNodeGetContext(yoga);
    bool moved = false;
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        Node *node = (Node *)YGNodeGetContext(child);
        double x = YGNodeLayoutGetLeft(child), y = YGNodeLayoutGetTop(child), w = YGNodeLayoutGetWidth(child), h = YGNodeLayoutGetHeight(child);
        if (x != node->x || y != node->y || w != node->w || h != node->h) {
            node->x = x; node->y = y; node->w = w; node->h = h;
            moved = true;
            if (node->box) gtk_widget_queue_allocate(node->box);
        }
        if (node->layoutListening) reportLayout(node, false);
        place(child);
    }
    if (moved && owner) {
        GtkWidget *container = owner->document ? owner->document : owner->box;
        if (container) gtk_widget_queue_allocate(container);
    }
    // A scroll's document is as tall as its lowest child.
    if (owner && owner->kind == 6 && owner->document) {
        double bottom = 0;
        for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
            YGNodeRef child = YGNodeGetChild(yoga, i);
            bottom = std::max(bottom, (double)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
        }
        double viewport = YGNodeLayoutGetHeight(yoga);
        UiBox *document = UI_BOX(owner->document);
        int docW = (int)std::lround(YGNodeLayoutGetWidth(yoga));
        int docH = (int)std::ceil(std::max(viewport, bottom + YGNodeLayoutGetPadding(yoga, YGEdgeBottom)));
        if (docW != document->docWidth || docH != document->docHeight) {
            document->docWidth = docW;
            document->docHeight = docH;
            gtk_widget_queue_resize(owner->document);
            // The range now, before GTK's next layout, so a scroll right after it goes as far.
            GtkAdjustment *a = owner->adjustment;
            gtk_adjustment_configure(a, std::min(gtk_adjustment_get_value(a), std::max(0.0, docH - viewport)), 0, docH,
                                     gtk_adjustment_get_step_increment(a), viewport * 0.9, viewport);
        }
        if (owner->listening && viewport != owner->reportedViewport) {
            owner->reportedViewport = viewport;
            ui_dispatch_scroll(owner->id, owner->generation, (int)std::lround(scrollOffset(owner)), (int)std::lround(viewport));
        }
    }
}

// Scrolling moves what is inside in the window without a layout: the listened nodes' window
// positions are checked again.
static void reportWindows(void) {
    for (auto &entry : nodes) {
        Node *node = entry.second;
        if (node->layoutListening && node->reported && node->yoga) reportLayout(node, false);
    }
}

static int stageWidth = 900, stageHeight = 700;

static void relayout(void) {
    ui_grid_calculate(root->yoga, stageWidth, stageHeight, YGDirectionLTR);
    root->w = stageWidth; root->h = stageHeight;
    place(root->yoga);
    for (Node *modal : modals) {
        modal->x = modal->y = 0;
        modal->w = stageWidth; modal->h = stageHeight;
        ui_grid_calculate(modal->yoga, stageWidth, stageHeight, YGDirectionLTR);
        place(modal->yoga);
        if (modal->box) gtk_widget_queue_allocate(modal->box);
    }
    gtk_widget_queue_allocate(stage);
}

// ---- events ----

// While a commit applies, the controls' own signals are the app's doing, not the user's.
static bool applying;

static Node *nodeOfContent(GtkWidget *widget) {
    for (GtkWidget *up = widget; up; up = gtk_widget_get_parent(up)) {
        Node *node = nodeOfBox(up);
        if (node) return node;
    }
    return nullptr;
}

static bool live(Node *node) { return node && !node->removed; }

static void onClicked(GtkButton *button, gpointer) {
    Node *node = nodeOfContent(GTK_WIDGET(button));
    if (live(node)) ui_dispatch(node->id, node->generation);
}

static void onSwitch(GObject *object, GParamSpec *, gpointer) {
    Node *node = nodeOfContent(GTK_WIDGET(object));
    if (applying || !live(node)) return;
    ui_dispatch_toggle(node->id, node->generation, gtk_switch_get_active(GTK_SWITCH(object)) ? 1 : 0);
}

static void onSlider(GtkRange *range, gpointer) {
    Node *node = nodeOfContent(GTK_WIDGET(range));
    if (applying || !live(node)) return;
    int value = (int)std::lround(gtk_range_get_value(range));
    if (value == node->sliderValue) return;
    node->sliderValue = value;
    ui_dispatch_value(node->id, node->generation, value);
}

static void onText(GtkEditable *editable, gpointer) {
    Node *node = nodeOfContent(GTK_WIDGET(editable));
    if (applying || !live(node)) return;
    ui_dispatch_text(node->id, node->generation, gtk_editable_get_text(editable));
}

static void onFocus(GtkEventControllerFocus *controller, gpointer focused) {
    Node *node = nodeOfContent(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
    if (live(node)) ui_dispatch_focus(node->id, node->generation, GPOINTER_TO_INT(focused));
}

static void onScroll(GtkAdjustment *adjustment, gpointer data) {
    Node *node = (Node *)data;
    reportWindows();
    if (!live(node) || !node->listening) return;
    node->reportedViewport = gtk_adjustment_get_page_size(adjustment);
    ui_dispatch_scroll(node->id, node->generation, (int)std::lround(gtk_adjustment_get_value(adjustment)),
                       (int)std::lround(node->h));
}

// A pressable dims while held; a release inside it presses it. Its text and rows press it
// too; a control or a scroll inside keeps its own clicks.
static void onPressDown(GtkGestureClick *gesture, int, double, double, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (!node) return;
    node->pressed = true;
    applyOpacity(node);
}
static void onPressUp(GtkGestureClick *gesture, int, double x, double y, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (!node || !node->pressed) return;
    node->pressed = false;
    applyOpacity(node);
    if (x >= 0 && y >= 0 && x <= node->w && y <= node->h && live(node)) ui_dispatch(node->id, node->generation);
}
static void onPressStop(GtkGesture *gesture, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (node && node->pressed) { node->pressed = false; applyOpacity(node); }
}

// A click on an overlay's backdrop (outside its content) asks it to close.
static void onBackdrop(GtkGestureClick *gesture, int, double x, double y, gpointer) {
    GtkWidget *view = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    Node *node = nodeOfBox(view);
    if (!live(node) || !node->listening) return;
    if (gtk_widget_pick(view, x, y, GTK_PICK_DEFAULT) != view) return;
    ui_dispatch(node->id, node->generation);
}

static void onHover(GtkEventControllerMotion *controller, double, double, gpointer inside) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
    if (live(node)) ui_dispatch_hover(node->id, node->generation, GPOINTER_TO_INT(inside));
}
static void onUnhover(GtkEventControllerMotion *controller, gpointer) { onHover(controller, 0, 0, GINT_TO_POINTER(0)); }

static void onLongPress(GtkGestureLongPress *gesture, double, double, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (live(node)) ui_dispatch_long_press(node->id, node->generation);
}

// A drag: it starts past 6 points, as on the web; y grows down as on the other hosts.
static void onDragUpdate(GtkGestureDrag *gesture, double dx, double dy, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (!live(node)) return;
    double now = monotonic();
    if (!node->panning) {
        if (std::hypot(dx, dy) < 6) return;
        node->panning = true;
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        node->panX = dx; node->panY = dy; node->panT = now; node->velX = node->velY = 0;
        ui_dispatch_pan(node->id, node->generation, 0, 0, 0, 0, 0);
    } else if (now > node->panT) {
        node->velX = (dx - node->panX) / (now - node->panT);
        node->velY = (dy - node->panY) / (now - node->panT);
        node->panX = dx; node->panY = dy; node->panT = now;
    }
    ui_dispatch_pan(node->id, node->generation, 1, (int)std::lround(dx), (int)std::lround(dy), (int)std::lround(node->velX),
                    (int)std::lround(node->velY));
}
static void onDragEnd(GtkGestureDrag *gesture, double dx, double dy, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (!node || !node->panning) return;
    node->panning = false;
    if (live(node))
        ui_dispatch_pan(node->id, node->generation, 2, (int)std::lround(dx), (int)std::lround(dy), (int)std::lround(node->velX),
                        (int)std::lround(node->velY));
}
static void onDragCancel(GtkGesture *gesture, GdkEventSequence *, gpointer) {
    Node *node = nodeOfBox(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    if (!node || !node->panning) return;
    node->panning = false;
    if (live(node)) ui_dispatch_pan(node->id, node->generation, 3, 0, 0, 0, 0);
}

// A controller of `type` on the node's box, added or removed.
static void setController(Node *node, GType type, bool on, std::function<GtkEventController *()> make) {
    GtkWidget *box = node->box;
    GListModel *list = gtk_widget_observe_controllers(box);
    for (guint i = g_list_model_get_n_items(list); i-- > 0;) {
        GtkEventController *controller = GTK_EVENT_CONTROLLER(g_list_model_get_item(list, i));
        if (G_TYPE_CHECK_INSTANCE_TYPE(controller, type) && g_object_get_data(G_OBJECT(controller), "ui-listener"))
            gtk_widget_remove_controller(box, controller);
        g_object_unref(controller);
    }
    g_object_unref(list);
    if (!on) return;
    GtkEventController *controller = make();
    g_object_set_data(G_OBJECT(controller), "ui-listener", GINT_TO_POINTER(1));
    gtk_widget_add_controller(box, controller);
}

// ---- frames and timers ----

static gboolean onTick(gpointer) {
    if (!tickScheduled) return G_SOURCE_REMOVE;
    tickScheduled = false;
    ui_tick();
    return G_SOURCE_REMOVE;
}

// A frame is rendered once the events of this turn of the main loop are in (many at once
// batch into one frame).
extern "C" void ui_host_schedule(void) {
    if (tickScheduled) return;
    tickScheduled = true;
    g_idle_add_full(G_PRIORITY_DEFAULT, onTick, nullptr, nullptr);
}
extern "C" void ui_host_cancel(void) { tickScheduled = false; }

extern "C" char *ui_host_asset_path(const char *name) {
    std::string path = assetFile(name ? name : "");
    return path.empty() ? nullptr : strdup(path.c_str());
}

// The display clock for use_frame: the window's frame clock.
static guint clockSource;
static gboolean onFrameClock(GtkWidget *, GdkFrameClock *clock, gpointer) {
    ui_dispatch_frame((double)gdk_frame_clock_get_frame_time(clock) / 1e6);
    return G_SOURCE_CONTINUE;
}
extern "C" void ui_host_frames(int on) {
    if (!on) {
        if (clockSource) gtk_widget_remove_tick_callback(window, clockSource);
        clockSource = 0;
        return;
    }
    if (!clockSource) clockSource = gtk_widget_add_tick_callback(window, onFrameClock, nullptr, nullptr);
}

// Timers for use_interval and use_timeout, on the main loop, by token.
static std::map<int, guint> timers;
struct TimerArgs {
    int token;
    bool repeat;
};
extern "C" void ui_host_timer_start(int token, int ms, int repeat) {
    auto *args = new TimerArgs{token, repeat != 0};
    timers[token] = g_timeout_add_full(G_PRIORITY_DEFAULT, (guint)std::max(0, ms), [](gpointer data) -> gboolean {
        auto *t = (TimerArgs *)data;
        int token = t->token;
        bool again = t->repeat;
        if (!again) timers.erase(token);
        ui_dispatch_timer(token);
        return again ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
    }, args, [](gpointer data) { delete (TimerArgs *)data; });
}
extern "C" void ui_host_timer_stop(int token) {
    auto found = timers.find(token);
    if (found == timers.end()) return;
    g_source_remove(found->second);
    timers.erase(found);
}
