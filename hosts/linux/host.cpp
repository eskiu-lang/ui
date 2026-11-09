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
// x, y, scale, rotate) and a screen's slide.
enum { kOpacity, kBackground, kTransform, kSlideX, kAnimated };

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
    // A pan in progress: started (past the slop), the last points for the velocity.
    bool panning = false;
    double panX = 0, panY = 0, panT = 0, velX = 0, velY = 0;
};

static void release(Node *node) {
    if (--node->refs > 0) return;
    if (node->texture) g_object_unref(node->texture);
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
    double t[4], s[4];
    valueOf(node, kTransform, t);
    valueOf(node, kSlideX, s);
    GskTransform *out = gsk_transform_translate(nullptr, addr(point((float)(node->x + s[0]), (float)node->y)));
    if (t[0] == 0 && t[1] == 0 && t[2] == 1 && t[3] == 0) return out;
    out = gsk_transform_translate(out, addr(point((float)(node->w / 2 + t[0]), (float)(node->h / 2 + t[1]))));
    out = gsk_transform_scale(out, (float)t[2], (float)t[2]);
    out = gsk_transform_rotate(out, (float)t[3]);
    return gsk_transform_translate(out, addr(point((float)(-node->w / 2), (float)(-node->h / 2))));
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
    int min = 0, nat = 0;
    if (node->kind == 9) {
        gtk_widget_measure(content, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, nullptr, nullptr);
        w = std::min<double>(w, nat);
        gtk_widget_measure(content, GTK_ORIENTATION_VERTICAL, (int)w, &min, &nat, nullptr, nullptr);
        y = std::max(0.0, (h - nat) / 2);
        h = nat;
    }
    if (node->kind == 5) {
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
            allocateChild(child, 0, 0, node->w, node->h, transformOf(node));
            continue;
        }
        if (self->role == kBoxNode && owner && child == owner->content) placeContent(owner, width, height);
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
        // A radius clips what is inside (a raised view's shadow stays outside).
        clipped = node->radius > 0;
        if (clipped) gtk_snapshot_push_rounded_clip(snapshot, &shape);
        double bg[4];
        valueOf(node, kBackground, bg);
        // A backdrop blur has no backdrop to blur here: the window's color, as the web's
        // fallback, under the app's own.
        if (node->blur && !node->background) {
            GdkRGBA glass = darkMode ? GdkRGBA{0.12f, 0.12f, 0.12f, 0.65f} : GdkRGBA{1, 1, 1, 0.65f};
            gtk_snapshot_append_color(snapshot, &glass, addr(rect(0, 0, (float)w, (float)h)));
        }
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

// ---- fonts and text ----

// The default text size, as the web host's.
static const int kTextSize = 16;

// A font file from the app's assets, added to fontconfig once; its family, or "" when it does
// not load.
static std::string fontFamily(const std::string &file);

// The attributes of a text in the node's font and color (sized when `sized`).
static PangoAttrList *textAttributes(Node *node, bool sized, bool colored) {
    PangoAttrList *list = pango_attr_list_new();
    if (sized) pango_attr_list_insert(list, pango_attr_size_new_absolute((node->fontSize > 0 ? node->fontSize : kTextSize) * PANGO_SCALE));
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
        gtk_widget_measure(content, GTK_ORIENTATION_VERTICAL, node->kind == 5 || node->kind == 15 ? -1 : (int)std::min(w, limitW), &min, &nat,
                           nullptr, nullptr);
        h = nat;
    }
    return {(float)std::min(std::ceil(w), limitW), (float)std::min(std::ceil(h), limitH)};
}

// An edge offset that is not set (kUnset in src/core/state.esk).
static const int kUnset = -1000000;

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
    YGNodeCalculateLayout(root->yoga, stageWidth, stageHeight, YGDirectionLTR);
    root->w = stageWidth; root->h = stageHeight;
    place(root->yoga);
    for (Node *modal : modals) {
        modal->x = modal->y = 0;
        modal->w = stageWidth; modal->h = stageHeight;
        YGNodeCalculateLayout(modal->yoga, stageWidth, stageHeight, YGDirectionLTR);
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
        content = gtk_entry_new();
        gtk_editable_set_width_chars(GTK_EDITABLE(content), 1);
        PangoAttrList *list = textAttributes(node, true, true);
        gtk_entry_set_attributes(GTK_ENTRY(content), list);
        pango_attr_list_unref(list);
        g_signal_connect(content, "changed", G_CALLBACK(onText), nullptr);
        GtkEventController *focus = gtk_event_controller_focus_new();
        g_signal_connect(focus, "enter", G_CALLBACK(+[](GtkEventControllerFocus *c, gpointer) { onFocus(c, GINT_TO_POINTER(1)); }), nullptr);
        g_signal_connect(focus, "leave", G_CALLBACK(+[](GtkEventControllerFocus *c, gpointer) { onFocus(c, GINT_TO_POINTER(0)); }), nullptr);
        gtk_widget_add_controller(content, focus);
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

// The widget that carries a node's accessible label: its control, else its box.
static GtkWidget *accessibleOf(Node *node) {
    if (node->content && node->kind != 6 && node->kind != 13) return node->content;
    return node->box;
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
    default: break;
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
    if (op == 2) applyProp(node, kind, before);
    else if (op == 9) {
        std::string value = text ? text : "";
        if (kind == 4 && node->kind == 13) mountNative(node, text);
        else if (kind == 1) {
            if (value.empty()) gtk_accessible_reset_property(GTK_ACCESSIBLE(accessibleOf(node)), GTK_ACCESSIBLE_PROPERTY_LABEL);
            else gtk_accessible_update_property(GTK_ACCESSIBLE(accessibleOf(node)), GTK_ACCESSIBLE_PROPERTY_LABEL, value.c_str(), -1);
        } else if (kind == 5) { node->fontFile = value; applyFont(node); }
        else if (kind == 2 && node->kind == 5 && node->content) gtk_entry_set_placeholder_text(GTK_ENTRY(node->content), value.c_str());
        else if (kind == 3 && node->kind == 7) loadImage(node, text);
    } else if (op == 3) {
        const char *value = text ? text : "";
        if (node->kind == 4) { node->title = value; applyButtonTitle(node); }
        else if (node->kind == 3 && node->content) gtk_label_set_text(GTK_LABEL(node->content), value);
        else if (node->kind == 5 && node->content && strcmp(gtk_editable_get_text(GTK_EDITABLE(node->content)), value) != 0)
            gtk_editable_set_text(GTK_EDITABLE(node->content), value);
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
    } else if (op == 6) {
        if (node->yoga) {
            YGNodeRef parentYoga = YGNodeGetParent(node->yoga);
            if (parentYoga) YGNodeRemoveChild(parentYoga, node->yoga);
            YGNodeFree(node->yoga);
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

static bool userAnimations = true;

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
    ui_report_accessibility(0, userAnimations ? 0 : 1, 100);
    const char *const *languages = g_get_language_names();
    std::string tag = languages && languages[0] ? languages[0] : "en";
    tag = tag.substr(0, tag.find_first_of(".@"));
    if (tag == "C" || tag == "POSIX" || tag.empty()) tag = "en";
    std::replace(tag.begin(), tag.end(), '_', '-');
    bool rtl = gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL;
    ui_report_locale(tag.c_str(), rtl ? 1 : 0);
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
                               {"<Alt>Left", "app.back"}, {"F11", "app.fullscreen"}};
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
        if (node->yoga) YGNodeFree(node->yoga);
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
        gtk_editable_set_text(GTK_EDITABLE(node->content), text.c_str());
        gtk_editable_set_position(GTK_EDITABLE(node->content), -1);
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
#ifdef UI_TEST
    g_signal_connect(window, "map", G_CALLBACK(+[](GtkWidget *, gpointer) { driverMapped(); }), nullptr);
#endif
    startApp();
    if (launchLink) ui_report_link(launchLink);
    ui_start(launchExample);
    reportEnvironment();
    relayout();
    gtk_window_present(GTK_WINDOW(window));
    while (!quitting) g_main_context_iteration(nullptr, TRUE);
    return 0;
}
