// The macOS host: AppKit views laid out by Yoga, as the iOS host does with UIKit. Components
// and state stay in Eskiu; the host applies commits and reports events (docs/hosts.md).
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreText/CoreText.h>
#include <yoga/Yoga.h>
#include "../commit.h"
#include "../grid.h"
#include "ui_native.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

#include "../core.h"
#ifdef UI_RELOAD
static void ui_core_error(const char *message) { fprintf(stderr, "ui: %s\n", message); }
#endif
#ifdef UI_TEST
#include <functional>
#include <string>
#include <mach/mach.h>
static void driverOnMain(std::function<void()> work) { dispatch_async(dispatch_get_main_queue(), ^{ work(); }); }
static std::string driverCarry(const std::string &directive);
#define UI_DRIVER_HOST_REQUESTS 1
static bool driverHostRequest(const std::string &request, std::string &answer);
#include "../driver.h"
#endif

@interface UiHostNode : NSObject
@property(nonatomic, strong) NSView *view;
@property(nonatomic) YGNodeRef yoga;
@property(nonatomic) int generation;
@property(nonatomic) CGFloat fontSize;
@property(nonatomic) int fontWeight;
@property(nonatomic, copy) NSString *fontFile;
@property(nonatomic) bool listening;
@property(nonatomic) int kind;
@property(nonatomic) int nodeId;
@property(nonatomic) CGFloat reportedViewport;
// A native view's type and the view its factory made (kind 13).
@property(nonatomic, copy) NSString *nativeType;
@property(nonatomic, strong) NSView *nativeView;
// A screen (kind 14): whether it was ever on top, and whether it slides in after layout.
@property(nonatomic) bool wasActive;
@property(nonatomic) bool slideIn;
@property(nonatomic) bool layoutListening;
@property(nonatomic) int sliderValue;
// Transforms (translate in points, scale in percent, rotate in degrees) and the transition
// that animates changes (ms, Easing).
@property(nonatomic) int translateX, translateY, scalePercent, rotateDegrees, transitionMs, easing;
@property(nonatomic) int radius;
@property(nonatomic) int shadow;
@property(nonatomic) int borderWidth;
// The background (RGBA, 0: none), a gradient over it (Style.gradient) and its angle.
@property(nonatomic) int background, gradientTo, gradientAngle;
@property(nonatomic, strong) CAGradientLayer *gradient;
// Style.blur: a material view under the subviews, or nil.
@property(nonatomic, strong) NSVisualEffectView *blur;
// A button or field the app styles (a background or a border): drawn by the host's layer
// instead of the system's bezel. Its text, color and padding.
@property(nonatomic) bool styled;
@property(nonatomic, copy) NSString *title;
@property(nonatomic) int textColor;
@property(nonatomic) int padding;
// A modal (kind 12): the sheet window it is shown in, or nil for an overlay in the window.
@property(nonatomic, strong) NSWindow *sheet;
@property(nonatomic) CGRect reportedFrame;
@property(nonatomic) CGPoint reportedWindow;
@end
@implementation UiHostNode
@end

static NSMutableDictionary<NSNumber *, UiHostNode *> *nodes;
// Each node's view (and a scroll's document view) to its node.
static NSMapTable<NSView *, UiHostNode *> *viewNodes;
static NSWindow *mainWindow;
// The window's content: the app's root and, over it, the overlays.
static NSView *contentView;
static NSView *rootView;
static YGNodeRef rootYoga;
static int launchExample;
static std::vector<double> frameTimes;
static bool tickScheduled;
static void relayout(void);
static UiHostNode *nodeOf(NSView *view) { return view ? [viewNodes objectForKey:view] : nil; }

// Flipped (y down, as Yoga places), layer-backed views.
@interface UiView : NSView
@property(nonatomic, strong) NSTrackingArea *hoverArea;
@end
@implementation UiView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (!self.hoverArea) return;
    [self removeTrackingArea:self.hoverArea];
    self.hoverArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:self.hoverArea];
}
- (void)mouseEntered:(NSEvent *)event {
    UiHostNode *node = nodeOf(self);
    if (node) ui_dispatch_hover(node.nodeId, node.generation, 1);
}
- (void)mouseExited:(NSEvent *)event {
    UiHostNode *node = nodeOf(self);
    if (node) ui_dispatch_hover(node.nodeId, node.generation, 0);
}
@end

static void hoverListen(NSView *view, bool on) {
    if (![view isKindOfClass:UiView.class]) return;
    UiView *v = (UiView *)view;
    if (v.hoverArea) { [v removeTrackingArea:v.hoverArea]; v.hoverArea = nil; }
    if (!on) return;
    v.hoverArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:v userInfo:nil];
    [v addTrackingArea:v.hoverArea];
}

// A pressable: a click on a descendant that does not handle clicks (a label, a row) is the
// pressable's, so its text presses it too. A control or a scroll inside keeps its own.
@interface UiPressable : UiView
@property(nonatomic) bool pressed;
@end
@implementation UiPressable
- (NSView *)hitTest:(NSPoint)point {
    NSView *hit = [super hitTest:point];
    if (!hit || hit == self) return hit;
    for (NSView *view = hit; view && view != self; view = view.superview) {
        if ([view isKindOfClass:NSControl.class] || [view isKindOfClass:NSScrollView.class]) return hit;
        if (view.gestureRecognizers.count) return hit;
    }
    return self;
}
- (void)mouseDown:(NSEvent *)event {
    self.pressed = true;
    self.layer.opacity = self.alphaValue * 0.7;
}
- (void)mouseUp:(NSEvent *)event {
    if (!self.pressed) return;
    self.pressed = false;
    self.layer.opacity = self.alphaValue;
    NSPoint at = [self convertPoint:event.locationInWindow fromView:nil];
    if (NSPointInRect(at, self.bounds)) [self accessibilityPerformPress];
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityButtonRole; }
- (BOOL)accessibilityPerformPress {
    UiHostNode *node = nodeOf(self);
    if (node) ui_dispatch(node.nodeId, node.generation);
    return YES;
}
@end

// A button: the system's push button, or a borderless one the host's layer paints when the
// app styles it. It dims while held.
@interface UiButton : NSButton
@end
@implementation UiButton
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)mouseDown:(NSEvent *)event {
    UiHostNode *node = nodeOf(self);
    if (!node.styled) { [super mouseDown:event]; return; }
    CGFloat alpha = self.alphaValue;
    self.layer.opacity = alpha * 0.7;
    [super mouseDown:event];
    self.layer.opacity = alpha;
}
@end

// A text field's cell that insets the text by the node's padding and centers it vertically,
// for a field the app styles (its own border and background).
@interface UiFieldCell : NSTextFieldCell
@property(nonatomic) CGFloat inset;
@end
@implementation UiFieldCell
- (NSRect)adjusted:(NSRect)bounds {
    NSRect rect = NSInsetRect(bounds, self.inset, 0);
    CGFloat height = [super cellSizeForBounds:NSMakeRect(0, 0, rect.size.width, CGFLOAT_MAX)].height;
    if (height < rect.size.height) rect.origin.y += std::floor((rect.size.height - height) / 2), rect.size.height = height;
    return rect;
}
- (NSRect)drawingRectForBounds:(NSRect)bounds { return [self adjusted:[super drawingRectForBounds:bounds]]; }
- (void)editWithFrame:(NSRect)frame inView:(NSView *)view editor:(NSText *)editor delegate:(id)delegate event:(NSEvent *)event {
    [super editWithFrame:[self adjusted:frame] inView:view editor:editor delegate:delegate event:event];
}
- (void)selectWithFrame:(NSRect)frame inView:(NSView *)view editor:(NSText *)editor delegate:(id)delegate
                  start:(NSInteger)start length:(NSInteger)length {
    [super selectWithFrame:[self adjusted:frame] inView:view editor:editor delegate:delegate start:start length:length];
}
@end

// A text input: its focus reported to the app.
@interface UiTextField : NSTextField
@end
@implementation UiTextField
+ (Class)cellClass { return UiFieldCell.class; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (BOOL)becomeFirstResponder {
    BOOL ok = [super becomeFirstResponder];
    UiHostNode *node = nodeOf(self);
    if (ok && node) ui_dispatch_focus(node.nodeId, node.generation, 1);
    return ok;
}
- (void)textDidEndEditing:(NSNotification *)note {
    [super textDidEndEditing:note];
    UiHostNode *node = nodeOf(self);
    if (node) ui_dispatch_focus(node.nodeId, node.generation, 0);
}
@end

// An image, fitted in its box or covering it (cropped), smoothed or pixelated.
@interface UiImageView : NSImageView
@property(nonatomic) bool cover;
@property(nonatomic) bool pixelated;
@end
@implementation UiImageView
- (BOOL)wantsUpdateLayer { return NO; }
- (void)drawRect:(NSRect)dirty {
    NSImage *image = self.image;
    if (!image) return;
    NSSize size = image.size, box = self.bounds.size;
    if (size.width <= 0 || size.height <= 0) return;
    CGFloat sx = box.width / size.width, sy = box.height / size.height;
    CGFloat scale = self.cover ? std::max(sx, sy) : std::min(sx, sy);
    NSRect rect = NSMakeRect((box.width - size.width * scale) / 2, (box.height - size.height * scale) / 2,
                             size.width * scale, size.height * scale);
    [NSGraphicsContext.currentContext saveGraphicsState];
    NSRectClip(self.bounds);
    NSGraphicsContext.currentContext.imageInterpolation = self.pixelated ? NSImageInterpolationNone : NSImageInterpolationHigh;
    [image drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    [NSGraphicsContext.currentContext restoreGraphicsState];
}
@end

// A sheet a Modal is shown in: Escape asks the app to close it.
@interface UiSheet : NSPanel
@end
@implementation UiSheet
- (BOOL)canBecomeKeyWindow { return YES; }
- (void)cancelOperation:(id)sender {
    for (UiHostNode *node in nodes.allValues) {
        if (node.sheet == self) { ui_dispatch(node.nodeId, node.generation); return; }
    }
}
@end

static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(yoga);
    CGFloat limitW = widthMode == YGMeasureModeUndefined ? CGFLOAT_MAX : width;
    CGFloat limitH = heightMode == YGMeasureModeUndefined ? CGFLOAT_MAX : height;
    NSView *view = node.view;
    NSSize size = NSZeroSize;
    if ([view isKindOfClass:UiTextField.class]) {
        NSTextField *field = (NSTextField *)view;
        UiFieldCell *cell = (UiFieldCell *)field.cell;
        CGFloat inset = cell.inset;
        cell.inset = 0;
        NSSize text = [cell cellSizeForBounds:NSMakeRect(0, 0, CGFLOAT_MAX, CGFLOAT_MAX)];
        cell.inset = inset;
        size = NSMakeSize(std::max<CGFloat>(text.width, 120), text.height);
    } else if ([view isKindOfClass:NSTextField.class]) {
        size = [((NSTextField *)view).cell cellSizeForBounds:NSMakeRect(0, 0, limitW, limitH)];
    } else if ([view isKindOfClass:NSButton.class]) {
        size = ((NSButton *)view).cell.cellSize;
    } else if ([view isKindOfClass:NSImageView.class]) {
        size = ((NSImageView *)view).image.size;
    } else if ([view isKindOfClass:NSProgressIndicator.class]) {
        size = NSMakeSize(20, 20);
    } else if ([view isKindOfClass:NSSlider.class]) {
        size = NSMakeSize(160, view.fittingSize.height);
    } else size = view.fittingSize;
    return {static_cast<float>(std::min<CGFloat>(std::ceil(size.width), limitW)),
            static_cast<float>(std::min<CGFloat>(std::ceil(size.height), limitH))};
}

// An edge offset that is not set (kUnset in src/core/state.esk).
static const int kUnset = -1000000;

// An RGBA color from the core (0: the platform's default, nil here).
static NSColor *colorOf(int value) {
    uint32_t v = (uint32_t)value;
    if (!v) return nil;
    return [NSColor colorWithSRGBRed:(v >> 24) / 255.0 green:((v >> 16) & 255) / 255.0 blue:((v >> 8) & 255) / 255.0
                               alpha:(v & 255) / 255.0];
}

// A layer property set, and animated from where it shows by the node's transition when it has
// one and is on screen. The animations run on the layer's clock, which the driver owns in a
// dev build.
static void animateLayer(UiHostNode *node, CALayer *layer, NSString *key, id to) {
    id from = [(layer.presentationLayer ?: layer) valueForKeyPath:key];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [layer setValue:to forKeyPath:key];
    [CATransaction commit];
    if (node.transitionMs <= 0 || !node.view.window || !from) return;
    CFTimeInterval duration = node.transitionMs / 1000.0;
    CABasicAnimation *animation;
    if (node.easing == 4) {
        CASpringAnimation *spring = [CASpringAnimation animationWithKeyPath:key];
        spring.damping = 14;
        spring.stiffness = 100;
        animation = spring;
    } else {
        animation = [CABasicAnimation animationWithKeyPath:key];
        NSString *curves[] = {kCAMediaTimingFunctionEaseInEaseOut, kCAMediaTimingFunctionEaseOut, kCAMediaTimingFunctionEaseIn,
                              kCAMediaTimingFunctionLinear};
        animation.timingFunction = [CAMediaTimingFunction functionWithName:curves[std::min(3, std::max(0, node.easing))]];
    }
    animation.fromValue = from;
    animation.toValue = to;
    animation.duration = duration;
    [layer addAnimation:animation forKey:key];
}

// The node's transform about its center (a layer-backed view's anchor is its top left).
static CATransform3D transformOf(UiHostNode *node) {
    NSSize size = node.view.bounds.size;
    CATransform3D t = CATransform3DMakeTranslation(node.translateX + size.width / 2, node.translateY + size.height / 2, 0);
    t = CATransform3DScale(t, node.scalePercent / 100.0, node.scalePercent / 100.0, 1);
    t = CATransform3DRotate(t, node.rotateDegrees * M_PI / 180.0, 0, 0, 1);
    return CATransform3DTranslate(t, -size.width / 2, -size.height / 2, 0);
}
static bool hasTransform(UiHostNode *node) {
    return node.translateX || node.translateY || node.scalePercent != 100 || node.rotateDegrees;
}
static void applyTransform(UiHostNode *node, bool animate) {
    if (!node.view.layer) return;
    NSValue *to = [NSValue valueWithCATransform3D:hasTransform(node) ? transformOf(node) : CATransform3DIdentity];
    if (animate) animateLayer(node, node.view.layer, @"transform", to);
    else {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        node.view.layer.transform = to.CATransform3DValue;
        [CATransaction commit];
    }
}

// Native views by type, registered before the app starts (UI_NATIVE_SETUP).
@interface UiViewType : NSObject
@property(nonatomic, copy) NSView *(^create)(UiViewEmit emit);
@property(nonatomic, copy) void (^set)(NSView *view, NSString *name, NSString *value);
@end
@implementation UiViewType
@end

static NSMutableDictionary<NSString *, UiViewType *> *viewTypes(void) {
    static NSMutableDictionary *types;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ types = [NSMutableDictionary new]; });
    return types;
}

extern "C" void ui_register_view(NSString *type, NSView *(^create)(UiViewEmit emit),
                                 void (^set)(NSView *view, NSString *name, NSString *value)) {
    UiViewType *entry = [UiViewType new];
    entry.create = create;
    entry.set = set;
    viewTypes()[type] = entry;
}

extern "C" NSWindow *ui_native_window(void) { return mainWindow; }

extern "C" void ui_native_emit(const char *channel, const char *payload) {
    NSString *c = [NSString stringWithUTF8String:channel ? channel : ""];
    NSString *p = [NSString stringWithUTF8String:payload ? payload : ""];
    dispatch_async(dispatch_get_main_queue(), ^{ ui_dispatch_native(c.UTF8String, p.UTF8String); });
}

// The view a NativeView's type makes, filling its node's view.
static void mountNative(UiHostNode *node, int id, NSString *type) {
    [node.nativeView removeFromSuperview];
    node.nativeView = nil;
    node.nativeType = type;
    UiViewType *entry = type ? viewTypes()[type] : nil;
    if (!entry) { fprintf(stderr, "ui: no native view registered as %s\n", type.UTF8String); return; }
    int generation = node.generation;
    UiViewEmit emit = ^(NSString *name, NSString *payload) {
        NSString *n = name ?: @"", *p = payload ?: @"";
        void (^send)(void) = ^{ ui_dispatch_view(id, generation, n.UTF8String, p.UTF8String); };
        if (NSThread.isMainThread) send(); else dispatch_async(dispatch_get_main_queue(), send);
    };
    NSView *view = entry.create(emit);
    view.frame = node.view.bounds;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    node.view.autoresizesSubviews = YES;
    [node.view addSubview:view];
    node.nativeView = view;
}

static void ui_host_named(int id, const char *name, const char *value) {
    UiHostNode *node = nodes[@(id)];
    UiViewType *entry = node.nativeType ? viewTypes()[node.nativeType] : nil;
    if (!node.nativeView || !entry.set) return;
    entry.set(node.nativeView, [NSString stringWithUTF8String:name], [NSString stringWithUTF8String:value]);
}

// A screen under the top is hidden (after a push's slide, which it stays under); one that
// comes on top for the first time over a screen that was shown slides in from the right once
// laid out (a push). A removed top screen slides out (a pop): see removeScreen.
static const double kSlide = 0.3;
static void screenActive(UiHostNode *node, int active) {
    YGNodeStyleSetDisplay(node.yoga, active ? YGDisplayFlex : YGDisplayNone);
    if (active) {
        node.view.hidden = NO;
        if (!node.wasActive) node.slideIn = true;
        node.wasActive = true;
    } else if (node.wasActive) {
        NSView *view = node.view;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kSlide * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if (YGNodeStyleGetDisplay(node.yoga) == YGDisplayNone) view.hidden = YES;
        });
    } else node.view.hidden = YES;
}

static CABasicAnimation *slide(CGFloat from, CGFloat to, NSString *curve) {
    CABasicAnimation *animation = [CABasicAnimation animationWithKeyPath:@"transform.translation.x"];
    animation.fromValue = @(from);
    animation.toValue = @(to);
    animation.duration = kSlide;
    animation.timingFunction = [CAMediaTimingFunction functionWithName:curve];
    animation.fillMode = kCAFillModeForwards;
    animation.removedOnCompletion = NO;
    return animation;
}

// Screens sliding out: what was inside them stays until they go.
static NSMutableSet<NSView *> *leaving;
static bool insideLeaving(NSView *view) {
    for (NSView *up = view.superview; up; up = up.superview) if ([leaving containsObject:up]) return true;
    return false;
}

static bool removeScreen(UiHostNode *node) {
    if (node.kind != 14 || node.view.hidden || !node.view.superview) return false;
    bool over = false;
    for (UiHostNode *other in nodes.allValues) {
        if (other != node && other.kind == 14 && other.wasActive && other.view.superview == node.view.superview) over = true;
    }
    if (!over) return false;
    NSView *view = node.view;
    if (!leaving) leaving = [NSMutableSet new];
    [leaving addObject:view];
    [view.superview addSubview:view positioned:NSWindowAbove relativeTo:nil];
    [CATransaction begin];
    [CATransaction setCompletionBlock:^{ [view removeFromSuperview]; [leaving removeObject:view]; }];
    [view.layer addAnimation:slide(0, view.bounds.size.width, kCAMediaTimingFunctionEaseIn) forKey:@"slide"];
    [CATransaction commit];
    return true;
}

static void slideScreens(void) {
    for (UiHostNode *node in nodes.allValues) {
        if (!node.slideIn) continue;
        node.slideIn = false;
        bool over = false;
        for (UiHostNode *other in nodes.allValues) {
            if (other != node && other.kind == 14 && other.wasActive && other.view.superview == node.view.superview &&
                !other.view.hidden) over = true;
        }
        if (!over || !node.view.layer) continue;
        CABasicAnimation *in = slide(node.view.bounds.size.width, 0, kCAMediaTimingFunctionEaseOut);
        in.removedOnCompletion = YES;
        in.fillMode = kCAFillModeBackwards;
        [node.view.layer addAnimation:in forKey:@"slide"];
    }
}

// Modals: sheets over the window, and overlays (a Modal with a background: popovers, menus)
// in it, each laid out as its own root.
static NSMutableArray<UiHostNode *> *modals;
// The alert on screen and how to answer it.
static NSAlert *shownAlert;

#include "host_views.inc"
#include "host_main.inc"
