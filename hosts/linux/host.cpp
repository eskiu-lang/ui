// The Linux host: GTK 4 widgets laid out by Yoga, as the macOS host does with AppKit. Components
// and state stay in Eskiu; the host applies commits and reports events (docs/hosts.md).
//
// Every node is a UiBox, a widget that places its children at the frames Yoga computed and
// paints what the app asked for (background, gradient, border, radius, shadow); a leaf holds
// the GTK control (GtkLabel, GtkButton, GtkEntry, GtkSwitch, GtkScale, GtkSpinner,
// GtkScrolledWindow). Transitions run on the host's own clock, which the driver owns in a dev
// build, so they move only when `ui agent`'s clock does.
#include <gtk/gtk.h>
#include <yoga/Yoga.h>
#include "../commit.h"
#include "../grid.h"
#include "../layout.h"
#include "ui_native.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <functional>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __linux__
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif

// The app's name, id and window size (UI_APP_NAME, UI_APP_ID, UI_WINDOW), from the ui tool's
// generated app.h (-include), with these defaults.
#ifndef UI_APP_NAME
#define UI_APP_NAME "app"
#endif
#ifndef UI_APP_ID
#define UI_APP_ID "org.eskiu.app"
#endif
#ifndef UI_WINDOW
#define UI_WINDOW "900x700"
#endif

#include "../core.h"
#ifdef UI_RELOAD
static void ui_core_error(const char *message) { fprintf(stderr, "ui: %s\n", message); }
#endif
#ifdef UI_TEST
static void driverOnMain(std::function<void()> work);
static std::string driverCarry(const std::string &directive);
#define UI_DRIVER_HOST_REQUESTS 1
static bool driverHostRequest(const std::string &request, std::string &answer);
#include "../driver.h"
#endif

// ---- nodes ----

// The values a transition animates: opacity, the background (RGBA), the transform (translate
// x, y, scale, rotate), a screen's slide and the frame (a layout animation, prop 90).
enum { kOpacity, kBackground, kTransform, kSlideX, kFrame, kAnimated };

struct Anim {
    bool on = false;
    double from[4] = {}, to[4] = {};
    double start = 0, duration = 0;
    int easing = 0;
};

struct Node {
    int refs = 1;
    int id = 0, generation = 0, kind = 0;
    bool removed = false;
    // The node's box, the control it holds (or null), and a scroll's document.
    GtkWidget *box = nullptr, *content = nullptr, *document = nullptr;
    GtkAdjustment *adjustment = nullptr;
    YGNodeRef yoga = nullptr;
    // The frame Yoga gave it, from its parent's top left.
    double x = 0, y = 0, w = 0, h = 0;
    // Paint: RGBA colors (0: none), points, percent, degrees.
    int background = 0, gradientTo = 0, gradientAngle = 180, borderWidth = 0, borderColor = 0, radius = 0, shadow = 0;
    int blur = 0, opacity = 100;
    int textColor = 0, fontSize = 0, fontWeight = 0, padding = 0;
    std::string fontFile, title;
    bool styled = false, listening = false, layoutListening = false, pressed = false;
    int translateX = 0, translateY = 0, scalePercent = 100, rotateDegrees = 0, transitionMs = 0, easing = 0;
    Anim anims[kAnimated];
    // A screen: whether it was ever on top, slides in once laid out, its slide offset.
    bool wasActive = false, slideIn = false, active = true;
    double slide = 0, hideAt = -1;
    int sliderValue = 0;
    double reportedViewport = -1;
    bool reported = false;
    int reportedFrame[4] = {}, reportedWindow[2] = {};
    // An image: its texture, cover or fit, pixelated.
    GdkTexture *texture = nullptr;
    bool cover = false, pixelated = false;
    std::string source;
    std::string nativeType;
    // Accessibility (Style's role, checked, selected, expanded, range, disabled; the label, the
    // spoken value and the hint), kept to apply again when a field changes widget.
    int role = 0, checked = 0, selected = 0, expanded = 0, rangeNow = 0, rangeMin = 0, rangeMax = 0;
    bool disabled = false;
    std::string label, valueText, hint;
    // A text input's editing options (props 65, 66, 68, 69, 70, 72) and its placeholder.
    int secure = 0, keyboard = 0, capitalize = 0, autocorrect = 1;
    bool multiline = false, submitListening = false;
    std::string placeholder;
    // A pan in progress: started (past the slop), the last points for the velocity.
    bool panning = false;
    double panX = 0, panY = 0, panT = 0, velX = 0, velY = 0;
    // Props 81 to 91 (host_props.inc); whether the app set shrink and listens to its image; a
    // modal's parent (for disabled above it) and whether it appeared; a refreshable scroll's
    // state and spinner; the widgets a backdrop blur drew last.
    int textAlign = 0, maxLines = 0, lineHeight = 0, zIndex = 0, layoutMs = 0, presentation = 0, appear = 0, parentId = 0;
    bool italic = false, clip = false, shrinkSet = false, imageListening = false, appeared = false;
    bool refreshable = false, refreshing = false, pulled = false;
    GtkWidget *refreshSpinner = nullptr;
    std::vector<GdkPaintable *> backdrop;
};

static void dropBackdrop(Node *node);
static void release(Node *node) {
    if (--node->refs > 0) return;
    if (node->texture) g_object_unref(node->texture);
    dropBackdrop(node);
    delete node;
}

// ---- the box widget ----

enum { kBoxNode, kBoxDocument, kBoxStage };

#define UI_TYPE_BOX (ui_box_get_type())
G_DECLARE_FINAL_TYPE(UiBox, ui_box, UI, BOX, GtkWidget)
struct _UiBox {
    GtkWidget parent;
    Node *node;
    int role;
    // A document's size (the scroll sets it).
    int docWidth, docHeight;
};
G_DEFINE_TYPE(UiBox, ui_box, GTK_TYPE_WIDGET)

static std::unordered_map<int, Node *> nodes;
static GtkWidget *window, *stage;
static Node *root;
static int launchExample;
static bool tickScheduled;
static std::vector<Node *> modals;
static void relayout(void);
static void allocateChild(GtkWidget *child, double x, double y, double w, double h, GskTransform *transform);

// The address of a value for the end of the expression (graphene's points and rectangles).
template <class T> static const T *addr(const T &value) { return &value; }
static graphene_point_t point(double x, double y) { return GRAPHENE_POINT_INIT((float)x, (float)y); }
static graphene_rect_t rect(double x, double y, double w, double h) { return GRAPHENE_RECT_INIT((float)x, (float)y, (float)w, (float)h); }

static Node *nodeOfBox(GtkWidget *widget) {
    return UI_IS_BOX(widget) && UI_BOX(widget)->role == kBoxNode ? UI_BOX(widget)->node : nullptr;
}

// The host's clock, in seconds: the monotonic clock, or the driver's while it owns it.
static bool clockFrozen;
static double clockBase, clockOffset;
static double monotonic(void) { return (double)g_get_monotonic_time() / 1e6; }
static double hostNow(void);

// A cubic Bézier timing curve at time x (0 to 1), as CSS's.
static double bezier(double x1, double y1, double x2, double y2, double x) {
    double t = x;
    for (int i = 0; i < 8; i++) {
        double cx = 3 * x1 * t * (1 - t) * (1 - t) + 3 * x2 * t * t * (1 - t) + t * t * t - x;
        double dx = 3 * x1 * (1 - t) * (1 - t) + 6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
        if (std::fabs(cx) < 1e-6 || std::fabs(dx) < 1e-6) break;
        t -= cx / dx;
    }
    t = std::min(1.0, std::max(0.0, t));
    return 3 * y1 * t * (1 - t) * (1 - t) + 3 * y2 * t * t * (1 - t) + t * t * t;
}

// The Easing members, as the web host's CSS curves (spring overshoots a little).
static double ease(int easing, double p) {
    switch (easing) {
    case 1: return bezier(0, 0, 0.58, 1, p);
    case 2: return bezier(0.42, 0, 1, 1, p);
    case 3: return p;
    case 4: return bezier(0.34, 1.56, 0.64, 1, p);
    default: return bezier(0.42, 0, 0.58, 1, p);
    }
}

// The value an animated property shows now, into out[4]; target is where it ends.
static void current(Node *node, int which, const double target[4], double out[4]) {
    Anim &a = node->anims[which];
    if (!a.on) { std::copy(target, target + 4, out); return; }
    double p = a.duration > 0 ? (hostNow() - a.start) / a.duration : 1;
    if (p >= 1) { std::copy(a.to, a.to + 4, out); return; }
    double e = ease(a.easing, std::max(0.0, p));
    for (int i = 0; i < 4; i++) out[i] = a.from[i] + (a.to[i] - a.from[i]) * e;
}

static void rgbaOf(int value, double out[4]) {
    uint32_t v = (uint32_t)value;
    out[0] = (v >> 24) / 255.0; out[1] = ((v >> 16) & 255) / 255.0; out[2] = ((v >> 8) & 255) / 255.0; out[3] = (v & 255) / 255.0;
}
static GdkRGBA gdkColor(const double c[4]) { return GdkRGBA{(float)c[0], (float)c[1], (float)c[2], (float)c[3]}; }

static void targetOf(Node *node, int which, double out[4]) {
    if (which == kOpacity) { out[0] = node->opacity / 100.0; out[1] = out[2] = out[3] = 0; }
    else if (which == kBackground) rgbaOf(node->background, out);
    else if (which == kTransform) {
        out[0] = node->translateX; out[1] = node->translateY; out[2] = node->scalePercent / 100.0; out[3] = node->rotateDegrees;
    } else if (which == kFrame) {
        out[0] = node->x; out[1] = node->y; out[2] = node->w; out[3] = node->h;
    } else { out[0] = node->slide; out[1] = out[2] = out[3] = 0; }
}

static void valueOf(Node *node, int which, double out[4]) {
    double target[4];
    targetOf(node, which, target);
    current(node, which, target, out);
}

// Nodes whose animations run (each holds a reference), and the ticker that moves them.
static std::vector<Node *> animating;
static guint ticker;
static void tickAnimations(void);
static gboolean onTicker(gpointer) {
    tickAnimations();
    if (animating.empty()) { ticker = 0; return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}
static void watchAnimation(Node *node) {
    if (std::find(animating.begin(), animating.end(), node) == animating.end()) { node->refs++; animating.push_back(node); }
    if (!ticker) ticker = g_timeout_add(16, onTicker, nullptr);
}

// A property that changed: animated from `from` (what it showed) to its new value over `ms`,
// when the node is on screen.
static void animateTo(Node *node, int which, const double from[4], int ms, int easing) {
    Anim &a = node->anims[which];
    if (ms <= 0 || !node->box || !gtk_widget_get_mapped(node->box)) { a.on = false; return; }
    double to[4];
    targetOf(node, which, to);
    std::copy(from, from + 4, a.from);
    std::copy(to, to + 4, a.to);
    a.start = hostNow();
    a.duration = ms / 1000.0;
    a.easing = easing;
    a.on = true;
    watchAnimation(node);
}

static void applyOpacity(Node *node) {
    if (!node->box) return;
    double o[4];
    valueOf(node, kOpacity, o);
    gtk_widget_set_opacity(node->box, std::max(0.0, std::min(1.0, o[0])) * (node->pressed ? 0.7 : 1.0));
}

// Screens sliding out and those to hide once the newer one slid in.
static std::vector<Node *> leaving;
static void finishLeaving(Node *node) {
    auto at = std::find(leaving.begin(), leaving.end(), node);
    if (at == leaving.end()) return;
    leaving.erase(at);
    if (node->box && gtk_widget_get_parent(node->box)) gtk_widget_unparent(node->box);
    release(node);
}

static void tickAnimations(void) {
    double now = hostNow();
    std::vector<Node *> running;
    for (Node *node : animating) {
        bool any = false;
        for (int i = 0; i < kAnimated; i++) {
            Anim &a = node->anims[i];
            if (!a.on) continue;
            if (now - a.start >= a.duration) a.on = false; else any = true;
        }
        if (node->box) {
            applyOpacity(node);
            gtk_widget_queue_draw(node->box);
            GtkWidget *up = gtk_widget_get_parent(node->box);
            if (up) gtk_widget_queue_allocate(up);
        }
        if (node->hideAt >= 0 && now >= node->hideAt) {
            node->hideAt = -1;
            if (!node->active && node->box) gtk_widget_set_visible(node->box, FALSE);
        }
        if (!any && !node->anims[kSlideX].on && std::find(leaving.begin(), leaving.end(), node) != leaving.end()) finishLeaving(node);
        if (any || node->hideAt >= 0) running.push_back(node); else release(node);
    }
    animating.swap(running);
}

// The node's transform about its center, after its position (and a screen's slide).
static GskTransform *transformOf(Node *node) {
    double t[4], s[4], f[4];
    valueOf(node, kTransform, t);
    valueOf(node, kSlideX, s);
    valueOf(node, kFrame, f);
    GskTransform *out = gsk_transform_translate(nullptr, addr(point((float)(f[0] + s[0]), (float)f[1])));
    if (t[0] == 0 && t[1] == 0 && t[2] == 1 && t[3] == 0) return out;
    out = gsk_transform_translate(out, addr(point((float)(f[2] / 2 + t[0]), (float)(f[3] / 2 + t[1]))));
    out = gsk_transform_scale(out, (float)t[2], (float)t[2]);
    out = gsk_transform_rotate(out, (float)t[3]);
    return gsk_transform_translate(out, addr(point((float)(-f[2] / 2), (float)(-f[3] / 2))));
}

static void ui_box_init(UiBox *self) { self->node = nullptr; self->role = kBoxNode; }

static void ui_box_dispose(GObject *object) {
    UiBox *self = UI_BOX(object);
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self)))) gtk_widget_unparent(child);
    if (self->node && self->role == kBoxNode) {
        self->node->box = nullptr;
        self->node->content = nullptr;
        self->node->document = nullptr;
        self->node->adjustment = nullptr;
        release(self->node);
    }
    self->node = nullptr;
    G_OBJECT_CLASS(ui_box_parent_class)->dispose(object);
}

static void ui_box_measure(GtkWidget *widget, GtkOrientation orientation, int, int *minimum, int *natural, int *, int *) {
    UiBox *self = UI_BOX(widget);
    int size = self->role == kBoxDocument ? (orientation == GTK_ORIENTATION_HORIZONTAL ? self->docWidth : self->docHeight) : 0;
    *minimum = self->role == kBoxDocument && orientation == GTK_ORIENTATION_VERTICAL ? size : 0;
    *natural = size;
}

// A control's place in its box: a field inside the padding the app gave it (styled), a switch
// at its own size, the others filling it.
static void placeContent(Node *node, int width, int height) {
    GtkWidget *content = node->content;
    if (!content || !gtk_widget_get_visible(content)) return;
    double x = 0, y = 0, w = width, h = height;
    if (node->kind == 5 && node->styled) { x = y = node->padding; w -= 2 * node->padding; h -= 2 * node->padding; }
    // A Text's label sits inside its padding (Yoga measured it without).
    if (node->kind == 3 && node->yoga) {
        x = YGNodeLayoutGetPadding(node->yoga, YGEdgeLeft);
        y = YGNodeLayoutGetPadding(node->yoga, YGEdgeTop);
        w -= x + YGNodeLayoutGetPadding(node->yoga, YGEdgeRight);
        h -= y + YGNodeLayoutGetPadding(node->yoga, YGEdgeBottom);
    }
    int min = 0, nat = 0;
    if (node->kind == 9) {
        gtk_widget_measure(content, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, nullptr, nullptr);
        w = std::min<double>(w, nat);
        gtk_widget_measure(content, GTK_ORIENTATION_VERTICAL, (int)w, &min, &nat, nullptr, nullptr);
        y = std::max(0.0, (h - nat) / 2);
        h = nat;
    }
    // A one-line field is centered; a multiline one fills its box from the top.
    if (node->kind == 5 && !GTK_IS_TEXT_VIEW(content)) {
        gtk_widget_measure(content, GTK_ORIENTATION_VERTICAL, -1, &min, &nat, nullptr, nullptr);
        if (nat < h) { y += std::floor((h - nat) / 2); h = nat; }
    }
    allocateChild(content, x, y, w, h, nullptr);
}

// A child allocated at least at its minimum size (GTK warns below it); the box clips it.
static void allocateChild(GtkWidget *child, double x, double y, double w, double h, GskTransform *transform) {
    int minW = 0, minH = 0, nat = 0;
    gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1, &minW, &nat, nullptr, nullptr);
    int width = std::max(minW, (int)std::lround(w));
    gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, width, &minH, &nat, nullptr, nullptr);
    int height = std::max(minH, (int)std::lround(h));
    if (!transform) transform = gsk_transform_translate(nullptr, addr(point((float)x, (float)y)));
    gtk_widget_allocate(child, width, height, -1, transform);
}

static void ui_box_size_allocate(GtkWidget *widget, int width, int height, int) {
    UiBox *self = UI_BOX(widget);
    Node *owner = self->node;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        if (!gtk_widget_get_visible(child)) continue;
        Node *node = nodeOfBox(child);
        if (node) {
            double f[4];
            valueOf(node, kFrame, f);
            allocateChild(child, 0, 0, f[2], f[3], transformOf(node));
            continue;
        }
        if (self->role == kBoxNode && owner && child == owner->content) placeContent(owner, width, height);
        else if (owner && child == owner->refreshSpinner) allocateChild(child, std::floor((width - 20) / 2.0), 8, 20, 20, nullptr);
        else allocateChild(child, 0, 0, width, height, nullptr);
    }
}

// The rounded outline of a box `w` by `h` with radius `r` (a pill asks for 999).
static GskRoundedRect outline(double w, double h, int r) {
    GskRoundedRect shape;
    float radius = (float)std::min<double>(r, std::min(w, h) / 2);
    gsk_rounded_rect_init_from_rect(&shape, addr(rect(0, 0, w, h)), radius);
    return shape;
}

static bool darkMode;
// Whether GTK's animations are on (gtk-enable-animations: off asks for reduced motion).
static bool userAnimations = true;
static void paintBackdrop(Node *node, GtkWidget *widget, GtkSnapshot *snapshot, double w, double h);

static void ui_box_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    UiBox *self = UI_BOX(widget);
    Node *node = self->role == kBoxNode ? self->node : nullptr;
    double w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
    bool clipped = false;
    if (node) {
        GskRoundedRect shape = outline(w, h, node->radius);
        // A raised view: the web's two shadows.
        if (node->shadow) {
            GdkRGBA far = {17 / 255.f, 24 / 255.f, 39 / 255.f, 0.14f}, near = {17 / 255.f, 24 / 255.f, 39 / 255.f, 0.06f};
            gtk_snapshot_append_outset_shadow(snapshot, &shape, &far, 0, node->shadow / 2.f, 0, node->shadow * 2.f);
            gtk_snapshot_append_outset_shadow(snapshot, &shape, &near, 0, 1, 0, 2);
        }
        // A radius or Style.clip clips what is inside (a raised view's shadow stays outside).
        clipped = node->radius > 0 || node->clip;
        if (clipped) gtk_snapshot_push_rounded_clip(snapshot, &shape);
        double bg[4];
        valueOf(node, kBackground, bg);
        // A backdrop blur: what is behind the box, blurred (host_props.inc).
        if (node->blur) paintBackdrop(node, widget, snapshot, w, h);
        if (node->gradientTo) {
            double to[4];
            rgbaOf(node->gradientTo, to);
            GskColorStop stops[2] = {{0, gdkColor(bg)}, {1, gdkColor(to)}};
            // CSS's angles: 0 up, 90 right, 180 down; the line as long as CSS makes it.
            double a = node->gradientAngle * G_PI / 180.0, dx = std::sin(a), dy = -std::cos(a);
            double half = (std::fabs(w * dx) + std::fabs(h * dy)) / 2;
            gtk_snapshot_append_linear_gradient(snapshot, addr(rect(0, 0, (float)w, (float)h)),
                addr(point((float)(w / 2 - dx * half), (float)(h / 2 - dy * half))),
                addr(point((float)(w / 2 + dx * half), (float)(h / 2 + dy * half))), stops, 2);
        } else if (bg[3] > 0) {
            GdkRGBA color = gdkColor(bg);
            gtk_snapshot_append_color(snapshot, &color, addr(rect(0, 0, (float)w, (float)h)));
        }
        if (node->borderWidth > 0) {
            double c[4];
            rgbaOf(node->borderColor ? node->borderColor : (int)0x000000ff, c);
            float widths[4] = {(float)node->borderWidth, (float)node->borderWidth, (float)node->borderWidth, (float)node->borderWidth};
            GdkRGBA colors[4] = {gdkColor(c), gdkColor(c), gdkColor(c), gdkColor(c)};
            gtk_snapshot_append_border(snapshot, &shape, widths, colors);
        }
        // An image, fitted in its box or covering it (cropped), smoothed or pixelated.
        if (node->texture) {
            double iw = gdk_texture_get_width(node->texture), ih = gdk_texture_get_height(node->texture);
            if (iw > 0 && ih > 0) {
                double sx = w / iw, sy = h / ih, scale = node->cover ? std::max(sx, sy) : std::min(sx, sy);
                double dw = iw * scale, dh = ih * scale;
                gtk_snapshot_push_clip(snapshot, addr(rect(0, 0, (float)w, (float)h)));
                gtk_snapshot_append_scaled_texture(snapshot, node->texture,
                    node->pixelated ? GSK_SCALING_FILTER_NEAREST : GSK_SCALING_FILTER_TRILINEAR,
                    addr(rect((float)((w - dw) / 2), (float)((h - dh) / 2), (float)dw, (float)dh)));
                gtk_snapshot_pop(snapshot);
            }
        }
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        gtk_widget_snapshot_child(widget, child, snapshot);
    }
    if (clipped) gtk_snapshot_pop(snapshot);
}

static void ui_box_class_init(UiBoxClass *klass) {
    G_OBJECT_CLASS(klass)->dispose = ui_box_dispose;
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->measure = ui_box_measure;
    widget->size_allocate = ui_box_size_allocate;
    widget->snapshot = ui_box_snapshot;
}

static GtkWidget *newBox(GtkAccessibleRole role, int kind, Node *node) {
    GtkWidget *box = GTK_WIDGET(g_object_new(UI_TYPE_BOX, "accessible-role", role, nullptr));
    UI_BOX(box)->role = kind;
    UI_BOX(box)->node = node;
    return box;
}

#include "host_text.inc"

// An edge offset that is not set (kUnset in src/core/state.esk).
static const int kUnset = -1000000;

#include "host_views.inc"
#include "host_props.inc"
#include "host_access.inc"
#include "host_commit.inc"
#include "host_main.inc"
