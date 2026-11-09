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

static NSFontWeight weightOf(int weight) {
    if (weight <= 0) return NSFontWeightRegular;
    const NSFontWeight weights[] = {NSFontWeightUltraLight, NSFontWeightThin, NSFontWeightLight, NSFontWeightRegular,
        NSFontWeightMedium, NSFontWeightSemibold, NSFontWeightBold, NSFontWeightHeavy, NSFontWeightBlack};
    return weights[std::min(8, std::max(0, weight / 100 - 1))];
}

// A font file from the bundle, registered once; its PostScript name, or nil when it does not load.
static NSString *fontName(NSString *file) {
    static NSMutableDictionary<NSString *, NSString *> *names;
    if (!names) names = [NSMutableDictionary dictionary];
    NSString *name = names[file];
    if (name) return name.length ? name : nil;
    name = @"";
    NSString *path = [file hasPrefix:@"/"] ? file : [NSBundle.mainBundle pathForResource:file ofType:nil];
    NSData *data = path ? [NSData dataWithContentsOfFile:path] : nil;
    CGDataProviderRef provider = data ? CGDataProviderCreateWithCFData((__bridge CFDataRef)data) : NULL;
    CGFontRef cgFont = provider ? CGFontCreateWithDataProvider(provider) : NULL;
    if (cgFont) {
        CFErrorRef error = NULL;
        CFStringRef postScript = CGFontCopyPostScriptName(cgFont);
        if (postScript && (CTFontManagerRegisterGraphicsFont(cgFont, &error) || [NSFont fontWithName:(__bridge NSString *)postScript size:12]))
            name = (__bridge NSString *)postScript;
        if (postScript) CFRelease(postScript);
        if (error) CFRelease(error);
        CGFontRelease(cgFont);
    }
    if (provider) CGDataProviderRelease(provider);
    if (!name.length) NSLog(@"ui: font %@ did not load", file);
    names[file] = name;
    return name.length ? name : nil;
}

// The default text size, as the web host's.
static const CGFloat kTextSize = 16;

static NSFont *fontOf(UiHostNode *node) {
    CGFloat size = node.fontSize > 0 ? node.fontSize : kTextSize;
    NSString *custom = node.fontFile ? fontName(node.fontFile) : nil;
    NSFont *font = custom ? [NSFont fontWithName:custom size:size] : nil;
    return font ?: [NSFont systemFontOfSize:size weight:weightOf(node.fontWeight)];
}

// A button's title: the system's for a push button, else in the node's font and color.
static void applyButtonTitle(UiHostNode *node) {
    NSButton *button = (NSButton *)node.view;
    NSString *title = node.title ?: @"";
    if (!node.styled && !node.textColor && !node.fontSize && !node.fontWeight && !node.fontFile) {
        button.title = title;
        return;
    }
    NSMutableParagraphStyle *paragraph = [NSMutableParagraphStyle new];
    paragraph.alignment = NSTextAlignmentCenter;
    NSColor *color = colorOf(node.textColor) ?: (node.styled ? NSColor.labelColor : NSColor.controlTextColor);
    NSFont *font = node.styled || node.fontSize ? fontOf(node) : [NSFont systemFontOfSize:NSFont.systemFontSize weight:weightOf(node.fontWeight)];
    button.attributedTitle = [[NSAttributedString alloc] initWithString:title attributes:@{
        NSFontAttributeName: font, NSForegroundColorAttributeName: color, NSParagraphStyleAttributeName: paragraph}];
}

// A field or a button the app gives a background or a border loses the system's bezel.
static void applyStyled(UiHostNode *node) {
    bool styled = node.background || node.borderWidth || node.radius;
    if ([node.view isKindOfClass:UiTextField.class]) {
        NSTextField *field = (NSTextField *)node.view;
        field.bezeled = !styled;
        field.bordered = !styled;
        field.drawsBackground = !styled;
        field.focusRingType = styled ? NSFocusRingTypeNone : NSFocusRingTypeDefault;
        ((UiFieldCell *)field.cell).inset = styled ? node.padding : 0;
        node.styled = styled;
        YGNodeMarkDirty(node.yoga);
    } else if ([node.view isKindOfClass:NSButton.class]) {
        NSButton *button = (NSButton *)node.view;
        button.bordered = !styled;
        node.styled = styled;
        applyButtonTitle(node);
        YGNodeMarkDirty(node.yoga);
    }
}

static void applyFont(UiHostNode *node) {
    if ([node.view isKindOfClass:NSButton.class]) applyButtonTitle(node);
    else if ([node.view isKindOfClass:NSTextField.class]) ((NSTextField *)node.view).font = fontOf(node);
    YGNodeMarkDirty(node.yoga);
}

static void applyColor(UiHostNode *node) {
    NSColor *color = colorOf(node.textColor);
    if ([node.view isKindOfClass:NSButton.class]) applyButtonTitle(node);
    else if ([node.view isKindOfClass:NSTextField.class]) ((NSTextField *)node.view).textColor = color ?: NSColor.labelColor;
}

// A bundled resource by file name, or an http(s) URL fetched in the background. The image
// changes the node's measured size, so the layout runs again when it arrives.
static void loadImage(NSImageView *view, YGNodeRef yoga, NSString *source) {
    view.image = nil;
    if (!source) return;
    if ([source hasPrefix:@"http://"] || [source hasPrefix:@"https://"]) {
        NSURL *url = [NSURL URLWithString:source];
        [[NSURLSession.sharedSession dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *r, NSError *e) {
            NSImage *image = data ? [[NSImage alloc] initWithData:data] : nil;
            dispatch_async(dispatch_get_main_queue(), ^{
                view.image = image; [view setNeedsDisplay:YES]; YGNodeMarkDirty(yoga); relayout();
            });
        }] resume];
        return;
    }
    NSString *path = [source hasPrefix:@"file://"] ? [NSURL URLWithString:source].path
                   : [source hasPrefix:@"/"] ? source : [NSBundle.mainBundle pathForResource:source ofType:nil];
    view.image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    [view setNeedsDisplay:YES];
    YGNodeMarkDirty(yoga);
}

static void applyGradient(UiHostNode *node) {
    if (!node.gradientTo) { [node.gradient removeFromSuperlayer]; node.gradient = nil; return; }
    if (!node.gradient) {
        node.gradient = [CAGradientLayer layer];
        [node.view.layer insertSublayer:node.gradient atIndex:0];
        node.gradient.frame = node.view.bounds;
    }
    NSColor *from = node.background ? colorOf(node.background) : NSColor.clearColor;
    node.gradient.colors = @[(id)from.CGColor, (id)colorOf(node.gradientTo).CGColor];
    // The layer is flipped with its view: y grows down, as CSS's angles assume.
    double a = node.gradientAngle * M_PI / 180.0;
    node.gradient.startPoint = CGPointMake(0.5 - sin(a) / 2, 0.5 + cos(a) / 2);
    node.gradient.endPoint = CGPointMake(0.5 + sin(a) / 2, 0.5 - cos(a) / 2);
}

// A backdrop blur: the system material nearest the radius (they follow the appearance).
static void applyBlur(UiHostNode *node, int radius) {
    if (radius <= 0) {
        [node.blur removeFromSuperview];
        node.blur = nil;
        if (!node.gradient) node.view.layer.backgroundColor = colorOf(node.background).CGColor;
        return;
    }
    if (!node.blur) {
        node.blur = [NSVisualEffectView new];
        node.blur.blendingMode = NSVisualEffectBlendingModeWithinWindow;
        node.blur.state = NSVisualEffectStateActive;
        node.blur.frame = node.view.bounds;
        node.blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        node.view.autoresizesSubviews = YES;
        [node.view addSubview:node.blur positioned:NSWindowBelow relativeTo:nil];
    }
    node.blur.material = radius <= 10 ? NSVisualEffectMaterialHUDWindow : radius <= 20 ? NSVisualEffectMaterialPopover
                       : radius <= 35 ? NSVisualEffectMaterialMenu : NSVisualEffectMaterialUnderWindowBackground;
    if (!node.gradient) node.view.layer.backgroundColor = colorOf(node.background).CGColor;
}

// The view children go into: a scroll's document view, else the node's own.
static NSView *container(UiHostNode *node) {
    if ([node.view isKindOfClass:NSScrollView.class]) return ((NSScrollView *)node.view).documentView;
    return node.view;
}

static void place(YGNodeRef yoga) {
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(child);
        CGFloat x = YGNodeLayoutGetLeft(child), y = YGNodeLayoutGetTop(child);
        CGFloat w = YGNodeLayoutGetWidth(child), h = YGNodeLayoutGetHeight(child);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        node.view.frame = NSMakeRect(x, y, w, h);
        // A radius past half the shorter side: a pill asks for 999.
        if (node.radius > 0) node.view.layer.cornerRadius = std::min<CGFloat>(node.radius, std::min(w, h) / 2);
        if (node.blur) node.blur.layer.cornerRadius = node.view.layer.cornerRadius;
        if (node.gradient) node.gradient.frame = CGRectMake(0, 0, w, h);
        [CATransaction commit];
        // AppKit sets a layer-backed view's layer geometry with its frame: the transform again.
        if (hasTransform(node) && ![node.view.layer animationForKey:@"transform"]) applyTransform(node, false);
        if (node.layoutListening) {
            CGRect frame = CGRectMake(std::lround(x), std::lround(y), std::lround(w), std::lround(h));
            NSPoint at = [node.view.superview convertPoint:NSMakePoint(x, y) toView:node.view.window.contentView];
            CGPoint window = CGPointMake(std::lround(at.x), std::lround(at.y));
            if (!CGRectEqualToRect(frame, node.reportedFrame) || !CGPointEqualToPoint(window, node.reportedWindow)) {
                node.reportedFrame = frame;
                node.reportedWindow = window;
                ui_dispatch_layout(node.nodeId, node.generation, (int)frame.origin.x, (int)frame.origin.y,
                                   (int)frame.size.width, (int)frame.size.height, (int)window.x, (int)window.y);
            }
        }
        place(child);
    }
    // A scroll's document is as tall as its lowest child.
    UiHostNode *owner = (__bridge UiHostNode *)YGNodeGetContext(yoga);
    if (owner && [owner.view isKindOfClass:NSScrollView.class]) {
        CGFloat bottom = 0;
        for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
            YGNodeRef child = YGNodeGetChild(yoga, i);
            bottom = std::max(bottom, (CGFloat)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
        }
        NSScrollView *scroll = (NSScrollView *)owner.view;
        CGFloat viewport = YGNodeLayoutGetHeight(yoga);
        [scroll.documentView setFrameSize:NSMakeSize(YGNodeLayoutGetWidth(yoga),
            std::max<CGFloat>(viewport, bottom + YGNodeLayoutGetPadding(yoga, YGEdgeBottom)))];
        if (owner.listening && viewport != owner.reportedViewport) {
            owner.reportedViewport = viewport;
            ui_dispatch_scroll(owner.nodeId, owner.generation, (int)std::lround(scroll.contentView.bounds.origin.y),
                               (int)std::lround(viewport));
        }
    }
}

// Scrolling moves what is inside in the window without a layout: the listened nodes' window
// positions are checked again.
static void reportWindows(void) {
    for (UiHostNode *node in nodes.allValues) {
        if (!node.layoutListening || !node.view.superview || CGRectIsNull(node.reportedFrame)) continue;
        NSPoint at = [node.view.superview convertPoint:node.reportedFrame.origin toView:node.view.window.contentView];
        CGPoint window = CGPointMake(std::lround(at.x), std::lround(at.y));
        if (CGPointEqualToPoint(window, node.reportedWindow)) continue;
        node.reportedWindow = window;
        CGRect f = node.reportedFrame;
        ui_dispatch_layout(node.nodeId, node.generation, (int)f.origin.x, (int)f.origin.y, (int)f.size.width,
                           (int)f.size.height, (int)window.x, (int)window.y);
    }
}

@interface UiEvents : NSObject <NSTextFieldDelegate>
@end
@implementation UiEvents
- (void)press:(NSButton *)sender {
    UiHostNode *node = nodeOf(sender);
    if (node) ui_dispatch(node.nodeId, node.generation);
}
- (void)toggle:(NSSwitch *)sender {
    UiHostNode *node = nodeOf(sender);
    if (node) ui_dispatch_toggle(node.nodeId, node.generation, sender.state == NSControlStateValueOn ? 1 : 0);
}
- (void)slide:(NSSlider *)sender {
    UiHostNode *node = nodeOf(sender);
    int value = (int)std::lround(sender.doubleValue);
    if (!node || value == node.sliderValue) return;
    node.sliderValue = value;
    ui_dispatch_value(node.nodeId, node.generation, value);
}
- (void)controlTextDidChange:(NSNotification *)note {
    NSTextField *field = note.object;
    UiHostNode *node = nodeOf(field);
    if (node) ui_dispatch_text(node.nodeId, node.generation, field.stringValue.UTF8String ?: "");
}
- (void)scrolled:(NSNotification *)note {
    NSClipView *clip = note.object;
    reportWindows();
    UiHostNode *node = nodeOf(clip.superview);
    if (!node || !node.listening) return;
    node.reportedViewport = clip.bounds.size.height;
    ui_dispatch_scroll(node.nodeId, node.generation, (int)std::lround(clip.bounds.origin.y), (int)std::lround(clip.bounds.size.height));
}
- (void)pan:(NSPanGestureRecognizer *)gesture {
    UiHostNode *node = nodeOf(gesture.view);
    if (!node) return;
    NSView *space = gesture.view.superview;
    NSPoint t = [gesture translationInView:space], v = [gesture velocityInView:space];
    // The superview is flipped, so y grows down as on the other hosts.
    int phase = gesture.state == NSGestureRecognizerStateBegan ? 0 : gesture.state == NSGestureRecognizerStateChanged ? 1
              : gesture.state == NSGestureRecognizerStateEnded ? 2 : 3;
    if (phase == 0) ui_dispatch_pan(node.nodeId, node.generation, 0, 0, 0, 0, 0);
    ui_dispatch_pan(node.nodeId, node.generation, phase == 0 ? 1 : phase, (int)std::lround(t.x), (int)std::lround(t.y),
                    (int)std::lround(v.x), (int)std::lround(v.y));
}
- (void)longPress:(NSPressGestureRecognizer *)gesture {
    UiHostNode *node = nodeOf(gesture.view);
    if (node && gesture.state == NSGestureRecognizerStateBegan) ui_dispatch_long_press(node.nodeId, node.generation);
}
// A click on an overlay's backdrop (outside its content) asks it to close.
- (void)backdrop:(NSClickGestureRecognizer *)gesture {
    NSView *view = gesture.view;
    NSPoint at = [gesture locationInView:view.superview];
    if ([view hitTest:at] != view) return;
    UiHostNode *node = nodeOf(view);
    if (node && node.listening) ui_dispatch(node.nodeId, node.generation);
}
- (void)tick {
    tickScheduled = false;
    auto start = std::chrono::steady_clock::now();
    ui_tick();
    frameTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
- (void)clock:(id)sender { ui_dispatch_frame(CACurrentMediaTime()); }
@end
static UiEvents *events;

// A frame is rendered once the events of this turn of the run loop are in (many at once
// batch into one frame).
extern "C" void ui_host_schedule(void) {
    if (tickScheduled) return;
    tickScheduled = true;
    dispatch_async(dispatch_get_main_queue(), ^{ if (tickScheduled) [events tick]; });
}
extern "C" void ui_host_cancel(void) { tickScheduled = false; }

extern "C" char *ui_host_asset_path(const char *name) {
    NSString *path = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@(name)];
    BOOL directory = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory] || directory) return NULL;
    return strdup(path.fileSystemRepresentation);
}

// The display clock for use_frame: the window's display link (macOS 14), else a 60 Hz timer.
static id clockSource;
extern "C" void ui_host_frames(int on) {
    if (!on) { [clockSource invalidate]; clockSource = nil; return; }
    if (clockSource) return;
    if (@available(macOS 14.0, *)) {
        CADisplayLink *link = [rootView displayLinkWithTarget:events selector:@selector(clock:)];
        [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        clockSource = link;
    } else {
        NSTimer *timer = [NSTimer timerWithTimeInterval:1 / 60.0 target:events selector:@selector(clock:) userInfo:nil repeats:YES];
        [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
        clockSource = timer;
    }
}

// Timers for use_interval and use_timeout, on the main run loop, by token.
static NSMutableDictionary<NSNumber *, NSTimer *> *timers;
extern "C" void ui_host_timer_start(int token, int ms, int repeat) {
    if (!timers) timers = [NSMutableDictionary new];
    NSTimer *timer = [NSTimer timerWithTimeInterval:ms / 1000.0 repeats:repeat != 0 block:^(NSTimer *t) {
        if (!repeat) [timers removeObjectForKey:@(token)];
        ui_dispatch_timer(token);
    }];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
    timers[@(token)] = timer;
}
extern "C" void ui_host_timer_stop(int token) {
    [timers[@(token)] invalidate];
    [timers removeObjectForKey:@(token)];
}

static void setGestures(UiHostNode *node, Class kind, bool on, SEL action) {
    for (NSGestureRecognizer *g in node.view.gestureRecognizers.copy) {
        if ([g isKindOfClass:kind]) [node.view removeGestureRecognizer:g];
    }
    if (on) [node.view addGestureRecognizer:[[kind alloc] initWithTarget:events action:action]];
}

// The sheet window a Modal shows in, attached to the window (or to the sheet on top).
static void showSheet(UiHostNode *node) {
    UiSheet *sheet = [[UiSheet alloc] initWithContentRect:NSMakeRect(0, 0, 200, 100) styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered defer:NO];
    UiView *content = [UiView new];
    content.wantsLayer = YES;
    sheet.contentView = content;
    node.view.frame = content.bounds;
    [content addSubview:node.view];
    node.sheet = sheet;
    NSWindow *parent = mainWindow;
    while (parent.attachedSheet) parent = parent.attachedSheet;
    [parent beginSheet:sheet completionHandler:nil];
}

static void closeModal(UiHostNode *node) {
    if (node.sheet) {
        [node.sheet.sheetParent endSheet:node.sheet];
        [node.sheet orderOut:nil];
        node.sheet = nil;
    }
    [modals removeObject:node];
}

extern "C" void ui_host_apply(int op, int id, int generation, int parent, int before, int kind, const char *text) {
    if (op == 1) {
        UiHostNode *node = [UiHostNode new];
        node.gradientAngle = 180;
        node.nodeId = id;
        node.scalePercent = 100;
        node.generation = generation;
        node.kind = kind;
        node.yoga = YGNodeNew();
        node.reportedFrame = CGRectNull;
        if (kind == 4) {
            UiButton *button = [UiButton buttonWithTitle:@"" target:events action:@selector(press:)];
            button.bezelStyle = NSBezelStyleRounded;
            node.view = button;
        } else if (kind == 5) {
            UiTextField *field = [UiTextField new];
            field.font = [NSFont systemFontOfSize:kTextSize];
            field.delegate = events;
            field.cell.scrollable = YES;
            field.cell.usesSingleLineMode = YES;
            node.view = field;
        } else if (kind == 3) {
            NSTextField *label = [NSTextField wrappingLabelWithString:@""];
            label.selectable = NO;
            label.font = [NSFont systemFontOfSize:kTextSize];
            label.textColor = NSColor.labelColor;
            node.view = label;
        } else if (kind == 7) {
            UiImageView *image = [UiImageView new];
            image.imageScaling = NSImageScaleProportionallyUpOrDown;
            image.accessibilityElement = YES;
            image.accessibilityRole = NSAccessibilityImageRole;
            node.view = image;
        } else if (kind == 8) {
            node.view = [UiPressable new];
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        } else if (kind == 15) {
            NSSlider *slider = [NSSlider sliderWithTarget:events action:@selector(slide:)];
            slider.continuous = YES;
            node.view = slider;
        } else if (kind == 9) {
            NSSwitch *toggle = [NSSwitch new];
            toggle.target = events;
            toggle.action = @selector(toggle:);
            node.view = toggle;
        } else if (kind == 10) {
            NSProgressIndicator *spinner = [NSProgressIndicator new];
            spinner.style = NSProgressIndicatorStyleSpinning;
            spinner.controlSize = NSControlSizeSmall;
            spinner.displayedWhenStopped = NO;
            spinner.accessibilityRole = NSAccessibilityProgressIndicatorRole;
            node.view = spinner;
        } else if (kind == 11) {
            node.view = [UiView new];
            node.view.hidden = YES;
            YGNodeStyleSetDisplay(node.yoga, YGDisplayNone);
        } else if (kind == 12) {
            node.view = [UiView new];
            node.view.accessibilityRole = NSAccessibilitySheetRole;
            node.view.accessibilityElement = YES;
            NSClickGestureRecognizer *click = [[NSClickGestureRecognizer alloc] initWithTarget:events action:@selector(backdrop:)];
            click.delaysPrimaryMouseButtonEvents = NO;
            [node.view addGestureRecognizer:click];
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
        } else if (kind == 13) {
            node.view = [UiView new];
        } else if (kind == 6) {
            NSScrollView *scroll = [NSScrollView new];
            scroll.drawsBackground = NO;
            scroll.hasVerticalScroller = YES;
            scroll.autohidesScrollers = YES;
            scroll.scrollerStyle = NSScrollerStyleOverlay;
            scroll.borderType = NSNoBorder;
            UiView *document = [UiView new];
            scroll.documentView = document;
            scroll.contentView.postsBoundsChangedNotifications = YES;
            [NSNotificationCenter.defaultCenter addObserver:events selector:@selector(scrolled:)
                                                       name:NSViewBoundsDidChangeNotification object:scroll.contentView];
            [viewNodes setObject:node forKey:document];
            node.view = scroll;
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetOverflow(node.yoga, YGOverflowScroll);
            YGNodeStyleSetFlexShrink(node.yoga, 1);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        } else {
            node.view = [UiView new];
            if (kind == 1 || kind == 2) node.view.accessibilityRole = NSAccessibilityGroupRole;
            YGNodeStyleSetFlexDirection(node.yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        }
        node.view.wantsLayer = YES;
        node.view.autoresizesSubviews = NO;
        if (kind == 12) node.view.layer.backgroundColor = [NSColor colorWithWhite:0 alpha:0.4].CGColor;
        [viewNodes setObject:node forKey:node.view];
        YGNodeSetContext(node.yoga, (__bridge void *)node);
        if ((kind >= 3 && kind <= 5) || kind == 7 || kind == 9 || kind == 10 || kind == 15) YGNodeSetMeasureFunc(node.yoga, measure);
        nodes[@(id)] = node;
        return;
    }
    UiHostNode *node = nodes[@(id)];
    if (!node) { if (op == 6) return; fprintf(stderr, "ui host: op %d on unknown node %d\n", op, id); abort(); }
    // The grid (hosts/grid.h) keeps the props it reads.
    if (op == 2 && node.yoga) ui_grid_prop(node.yoga, kind, before);
    if (op == 9 && node.yoga) ui_grid_tracks(node.yoga, kind, text);
    if (op == 2) {
        // Props: the code in kind, the value in before.
        if (kind == 1) YGNodeStyleSetGap(node.yoga, YGGutterAll, before);
        else if (kind == 2) {
            YGNodeStyleSetPadding(node.yoga, YGEdgeAll, before);
            node.padding = before;
            if ([node.view isKindOfClass:UiTextField.class]) applyStyled(node);
        } else if (kind == 3) {
            if (before) [node.view.window makeFirstResponder:node.view];
            else if (node.view.window.firstResponder && [node.view.window.firstResponder isKindOfClass:NSTextView.class] &&
                     (__bridge void *)((NSTextView *)node.view.window.firstResponder).delegate == (__bridge void *)node.view)
                [node.view.window makeFirstResponder:nil];
        } else if (kind == 4) {
            if (before) YGNodeStyleSetWidth(node.yoga, before); else YGNodeStyleSetWidthAuto(node.yoga);
        } else if (kind == 5) {
            if (before) YGNodeStyleSetHeight(node.yoga, before); else YGNodeStyleSetHeightAuto(node.yoga);
        } else if (kind == 6) {
            YGNodeStyleSetFlexGrow(node.yoga, before);
            if (![node.view isKindOfClass:NSScrollView.class]) YGNodeStyleSetFlexShrink(node.yoga, before > 0 ? 1 : 0);
        } else if (kind == 7) YGNodeStyleSetFlexWrap(node.yoga, before ? YGWrapWrap : YGWrapNoWrap);
        else if (kind == 8) {
            node.background = before;
            if (node.gradient) applyGradient(node);
            else animateLayer(node, node.view.layer, @"backgroundColor", (__bridge NSObject *)colorOf(before).CGColor);
            applyStyled(node);
        } else if (kind == 50 || kind == 51) {
            if (kind == 50) node.gradientTo = before; else node.gradientAngle = before;
            applyGradient(node);
            node.view.layer.backgroundColor = node.gradient ? nil : colorOf(node.background).CGColor;
        } else if (kind == 52) applyBlur(node, before);
        else if (kind == 9) { node.textColor = before; applyColor(node); }
        else if (kind == 10) { node.fontSize = before; applyFont(node); }
        else if (kind == 11) { node.fontWeight = before; applyFont(node); }
        else if (kind == 12) {
            node.borderWidth = before;
            node.view.layer.borderWidth = before;
            YGNodeStyleSetBorder(node.yoga, YGEdgeAll, before);
            applyStyled(node);
        } else if (kind == 13) node.view.layer.borderColor = colorOf(before).CGColor;
        else if (kind == 14) {
            node.radius = before;
            NSSize size = node.view.bounds.size;
            node.view.layer.cornerRadius = std::min<CGFloat>(before, std::min(size.width, size.height) / 2);
            node.view.layer.masksToBounds = before > 0 && !node.shadow;
            applyStyled(node);
        } else if (kind == 47 && [node.view isKindOfClass:UiImageView.class]) {
            ((UiImageView *)node.view).cover = before != 0;
            [node.view setNeedsDisplay:YES];
        } else if (kind == 48 && [node.view isKindOfClass:UiImageView.class]) {
            ((UiImageView *)node.view).pixelated = before != 0;
            [node.view setNeedsDisplay:YES];
        } else if (kind == 46) {
            // A shadow draws outside the bounds, so a raised view does not clip to its radius.
            node.shadow = before;
            CALayer *layer = node.view.layer;
            layer.shadowColor = [NSColor colorWithSRGBRed:17 / 255.0 green:24 / 255.0 blue:39 / 255.0 alpha:1].CGColor;
            layer.shadowOpacity = before ? 0.16 : 0;
            layer.shadowRadius = before;
            // The layer is flipped with its view: a positive offset is down.
            layer.shadowOffset = CGSizeMake(0, before / 2.0);
            layer.masksToBounds = node.radius > 0 && !before;
        } else if (kind == 15) {
            animateLayer(node, node.view.layer, @"opacity", @(before / 100.0));
            node.view.alphaValue = before / 100.0;
        } else if (kind >= 35 && kind <= 38) {
            if (kind == 35) node.translateX = before;
            else if (kind == 36) node.translateY = before;
            else if (kind == 37) node.scalePercent = before;
            else node.rotateDegrees = before;
            applyTransform(node, true);
        } else if (kind == 39) node.transitionMs = before;
        else if (kind == 40) node.easing = before;
        else if (kind == 16) {
            const YGAlign aligns[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch};
            YGNodeStyleSetAlignItems(node.yoga, aligns[std::min(4, std::max(0, before))]);
        } else if (kind == 17) {
            const YGJustify justifies[] = {YGJustifyFlexStart, YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                           YGJustifySpaceBetween, YGJustifySpaceAround};
            YGNodeStyleSetJustifyContent(node.yoga, justifies[std::min(5, std::max(0, before))]);
        } else if (kind == 18) ((NSSwitch *)node.view).state = before ? NSControlStateValueOn : NSControlStateValueOff;
        else if (kind == 19) {
            NSProgressIndicator *spinner = (NSProgressIndicator *)node.view;
            if (before) [spinner startAnimation:nil]; else [spinner stopAnimation:nil];
        } else if (kind == 23) screenActive(node, before);
        else if (kind >= 32 && kind <= 34 && [node.view isKindOfClass:NSSlider.class]) {
            NSSlider *slider = (NSSlider *)node.view;
            if (kind == 33) slider.minValue = before;
            else if (kind == 34) slider.maxValue = before;
            else { slider.doubleValue = before; node.sliderValue = before; }
        } else if (kind == 41) setGestures(node, NSPanGestureRecognizer.class, before, @selector(pan:));
        else if (kind == 31) hoverListen(node.view, before);
        else if (kind == 49) setGestures(node, NSPressGestureRecognizer.class, before, @selector(longPress:));
        else if (kind == 30) { node.layoutListening = before != 0; node.reportedFrame = CGRectNull; }
        else if (kind == 42) YGNodeStyleSetMinWidth(node.yoga, before);
        else if (kind == 43) YGNodeStyleSetMaxWidth(node.yoga, before ? (float)before : YGUndefined);
        else if (kind == 44) YGNodeStyleSetMinHeight(node.yoga, before);
        else if (kind == 45) YGNodeStyleSetMaxHeight(node.yoga, before ? (float)before : YGUndefined);
        else if (kind == 24) YGNodeStyleSetPositionType(node.yoga, before ? YGPositionTypeAbsolute : YGPositionTypeRelative);
        else if (kind >= 25 && kind <= 28) {
            const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
            YGNodeStyleSetPosition(node.yoga, edges[kind - 25], before == kUnset ? YGUndefined : (float)before);
        } else if (kind == 29) YGNodeStyleSetAspectRatio(node.yoga, before ? before / 1000.0f : YGUndefined);
    } else if (op == 9) {
        NSString *value = text && text[0] ? [NSString stringWithUTF8String:text] : nil;
        if (kind == 4 && node.kind == 13) mountNative(node, id, value);
        else if (kind == 1) {
            node.view.accessibilityLabel = value;
            if (node.kind == 1 || node.kind == 2 || node.kind == 13) node.view.accessibilityElement = value != nil;
        } else if (kind == 5) { node.fontFile = value; applyFont(node); }
        else if (kind == 2 && [node.view isKindOfClass:UiTextField.class]) ((NSTextField *)node.view).placeholderString = value;
        else if (kind == 3 && [node.view isKindOfClass:NSImageView.class]) loadImage((NSImageView *)node.view, node.yoga, value);
    } else if (op == 3) {
        NSString *value = [NSString stringWithUTF8String:text ? text : ""];
        if ([node.view isKindOfClass:NSButton.class]) { node.title = value; applyButtonTitle(node); }
        else if ([node.view isKindOfClass:NSTextField.class]) {
            NSTextField *field = (NSTextField *)node.view;
            if (![field.stringValue isEqualToString:value]) field.stringValue = value;
        }
        YGNodeMarkDirty(node.yoga);
    } else if ((op == 4 || op == 5) && node.kind == 12) {
        // A Modal is a sheet; one with a background of its own (an Overlay: popovers, menus)
        // is a layer over the window's content, outside the parent's layout.
        if ([modals containsObject:node]) return;
        [modals addObject:node];
        if (!node.background) showSheet(node);
        else [contentView addSubview:node.view];
    } else if (op == 4 || op == 5) {
        UiHostNode *parentNode = nodes[@(parent)];
        NSView *parentView = parentNode ? container(parentNode) : rootView;
        YGNodeRef parentYoga = parentNode ? parentNode.yoga : rootYoga;
        YGNodeRef oldParent = YGNodeGetParent(node.yoga);
        if (oldParent) YGNodeRemoveChild(oldParent, node.yoga);
        [node.view removeFromSuperviewWithoutNeedingDisplay];
        UiHostNode *next = before ? nodes[@(before)] : nil;
        size_t index = YGNodeGetChildCount(parentYoga);
        if (next) {
            for (size_t i = 0; i < YGNodeGetChildCount(parentYoga); ++i) {
                if (YGNodeGetChild(parentYoga, i) == next.yoga) { index = i; break; }
            }
            [parentView addSubview:node.view positioned:NSWindowBelow relativeTo:next.view];
        } else [parentView addSubview:node.view];
        YGNodeInsertChild(parentYoga, node.yoga, index);
    } else if (op == 6) {
        YGNodeRef parentYoga = YGNodeGetParent(node.yoga);
        if (parentYoga) YGNodeRemoveChild(parentYoga, node.yoga);
        if (node.kind == 12) closeModal(node);
        if (!removeScreen(node) && !insideLeaving(node.view)) [node.view removeFromSuperview];
        if ([node.view isKindOfClass:NSScrollView.class]) {
            [NSNotificationCenter.defaultCenter removeObserver:events name:NSViewBoundsDidChangeNotification
                                                        object:((NSScrollView *)node.view).contentView];
        }
        ui_grid_free(node.yoga);
        [nodes removeObjectForKey:@(id)];
    } else if (op == 7 || op == 8) {
        node.listening = op == 7;
    } else { fprintf(stderr, "ui host: unknown op %d\n", op); abort(); }
}

// Commands: 1 an alert, 2 open a URL, 4 hide the keyboard, 5 share, 6 copy, 7 paste, 8 a
// haptic, 9 announce, 10 fetch, 11 the splash (a desktop window has none).
static void ui_host_command(int request, int command, int count, const char **strings) {
    if (command == 1 && count >= 2) {
        NSAlert *alert = [NSAlert new];
        alert.messageText = @(strings[0]);
        alert.informativeText = @(strings[1]);
        for (int i = 2; i < count; i++) [alert addButtonWithTitle:@(strings[i])];
        shownAlert = alert;
        // Shown after the commit, as a sheet on the window.
        dispatch_async(dispatch_get_main_queue(), ^{
            [alert beginSheetModalForWindow:mainWindow completionHandler:^(NSModalResponse response) {
                if (shownAlert == alert) shownAlert = nil;
                int index = response >= NSAlertFirstButtonReturn && response < NSAlertFirstButtonReturn + count - 2
                    ? (int)(response - NSAlertFirstButtonReturn) : -1;
                ui_dispatch_alert(request, index);
            }];
        });
    } else if (command == 2 && count >= 1) {
        NSString *text = strcmp(strings[0], "app-settings:") == 0 ? @"x-apple.systempreferences:" : @(strings[0]);
        NSURL *url = [NSURL URLWithString:text];
        if (url) [NSWorkspace.sharedWorkspace openURL:url];
    } else if (command == 4) {
        [mainWindow makeFirstResponder:nil];
    } else if (command == 5 && count >= 2) {
        NSMutableArray *items = [NSMutableArray new];
        if (strings[0][0]) [items addObject:@(strings[0])];
        NSURL *url = strings[1][0] ? [NSURL URLWithString:@(strings[1])] : nil;
        if (url) [items addObject:url];
        NSSharingServicePicker *picker = [[NSSharingServicePicker alloc] initWithItems:items];
        dispatch_async(dispatch_get_main_queue(), ^{
            NSRect at = NSMakeRect(contentView.bounds.size.width / 2, contentView.bounds.size.height / 2, 1, 1);
            [picker showRelativeToRect:at ofView:contentView preferredEdge:NSRectEdgeMinY];
        });
    } else if (command == 6 && count >= 1) {
        [NSPasteboard.generalPasteboard clearContents];
        [NSPasteboard.generalPasteboard setString:@(strings[0]) forType:NSPasteboardTypeString];
    } else if (command == 7) {
        NSString *text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] ?: @"";
        ui_dispatch_answer(request, text.UTF8String);
    } else if (command == 8) {
        [NSHapticFeedbackManager.defaultPerformer performFeedbackPattern:NSHapticFeedbackPatternGeneric
                                                         performanceTime:NSHapticFeedbackPerformanceTimeNow];
    } else if (command == 9 && count >= 1) {
        NSAccessibilityPostNotificationWithUserInfo(mainWindow, NSAccessibilityAnnouncementRequestedNotification, @{
            NSAccessibilityAnnouncementKey: @(strings[0]), NSAccessibilityPriorityKey: @(NSAccessibilityPriorityHigh)});
    } else if (command == 10 && count >= 6) {
        NSURL *url = [NSURL URLWithString:@(strings[0])];
        if (!url) { ui_dispatch_fetch(request, 0, "invalid URL", ""); return; }
        NSMutableURLRequest *call = [NSMutableURLRequest requestWithURL:url];
        call.HTTPMethod = @(strings[1]);
        for (NSString *line in [@(strings[2]) componentsSeparatedByString:@"\n"]) {
            NSRange colon = [line rangeOfString:@":"];
            if (colon.location == NSNotFound) continue;
            NSString *name = [[line substringToIndex:colon.location] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
            NSString *value = [[line substringFromIndex:colon.location + 1] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
            [call setValue:value forHTTPHeaderField:name];
        }
        if (strings[5][0] == '1') call.HTTPBody = [@(strings[3]) dataUsingEncoding:NSUTF8StringEncoding];
        int timeout = atoi(strings[4]);
        if (timeout > 0) call.timeoutInterval = timeout / 1000.0;
        NSString *saveTo = count >= 7 && strings[6][0] ? @(strings[6]) : nil;
        [[NSURLSession.sharedSession dataTaskWithRequest:call completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            int status = error ? 0 : (int)((NSHTTPURLResponse *)response).statusCode;
            NSError *writing = nil;
            if (saveTo && status >= 200 && status < 300) {
                [data ?: [NSData data] writeToFile:saveTo options:NSDataWritingAtomic error:&writing];
                if (writing) status = 0;
            }
            NSString *body = error ? error.localizedDescription : writing ? writing.localizedDescription
                           : saveTo && status >= 200 && status < 300 ? @""
                           : ([[NSString alloc] initWithData:data ?: [NSData data] encoding:NSUTF8StringEncoding] ?: @"");
            NSMutableString *headers = [NSMutableString new];
            if (!error) {
                [((NSHTTPURLResponse *)response).allHeaderFields enumerateKeysAndObjectsUsingBlock:^(id name, id value, BOOL *stop) {
                    [headers appendFormat:@"%@: %@\n", name, value];
                }];
            }
            dispatch_async(dispatch_get_main_queue(), ^{ ui_dispatch_fetch(request, status, body.UTF8String, headers.UTF8String); });
        }] resume];
    }
}

#ifdef UI_TEST
static void driverClockTo(double now);
static void driverClock(bool owned, double now, bool settle);
#endif
static int commitCount;
extern "C" void ui_host_commit(const uint8_t *data, int len) {
#ifdef UI_TEST
    // The animations this commit starts begin at the driver's time.
    driverClockTo(ui_driver_time());
#endif
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    relayout();
    slideScreens();
    commitCount++;
}

// A sheet is as big as what the modal holds: laid out in the window's size first, then in
// the box its children take.
static void layoutSheet(UiHostNode *modal) {
    NSSize window = contentView.bounds.size;
    ui_grid_calculate(modal.yoga, window.width, window.height, YGDirectionLTR);
    CGFloat right = 0, bottom = 0;
    for (size_t i = 0; i < YGNodeGetChildCount(modal.yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(modal.yoga, i);
        right = std::max(right, (CGFloat)(YGNodeLayoutGetLeft(child) + YGNodeLayoutGetWidth(child)));
        bottom = std::max(bottom, (CGFloat)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
    }
    float pad = YGNodeLayoutGetPadding(modal.yoga, YGEdgeRight);
    NSSize size = NSMakeSize(std::max<CGFloat>(1, std::ceil(right + pad)), std::max<CGFloat>(1, std::ceil(bottom + pad)));
    ui_grid_calculate(modal.yoga, size.width, size.height, YGDirectionLTR);
    if (!NSEqualSizes(modal.sheet.contentView.bounds.size, size)) [modal.sheet setContentSize:size];
    modal.view.frame = NSMakeRect(0, 0, size.width, size.height);
    place(modal.yoga);
}

static void relayout(void) {
    ui_grid_calculate(rootYoga, rootView.bounds.size.width, rootView.bounds.size.height, YGDirectionLTR);
    place(rootYoga);
    for (UiHostNode *modal in modals) {
        if (modal.sheet) { layoutSheet(modal); continue; }
        NSRect screen = contentView.bounds;
        modal.view.frame = screen;
        ui_grid_calculate(modal.yoga, screen.size.width, screen.size.height, YGDirectionLTR);
        place(modal.yoga);
    }
}

// The app's window: Escape, at the end of the responder chain, is back (the newest modal
// closes, else the app's back).
@interface UiWindow : NSWindow
@end
@implementation UiWindow
- (void)cancelOperation:(id)sender { ui_report_back(); }
- (void)back:(id)sender { ui_report_back(); }
@end

// The window's content: the root fills it, and reports its size to the app.
@interface UiContent : UiView
@end
@implementation UiContent
- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (!rootView) return;
    rootView.frame = self.bounds;
    ui_report_window((int)std::lround(size.width), (int)std::lround(size.height),
                     (int)std::lround((self.window.backingScaleFactor ?: 2) * 100));
    relayout();
}
// A swipe right is back (the newest modal closes, else the app's back).
- (void)swipeWithEvent:(NSEvent *)event { if (event.deltaX > 0) ui_report_back(); }
@end

// The system's appearance, accessibility settings and language, to the core.
static void reportSystem(void) {
    NSAppearanceName name = [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    ui_report_appearance([name isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0);
    NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
    ui_report_accessibility(workspace.voiceOverEnabled ? 1 : 0, workspace.accessibilityDisplayShouldReduceMotion ? 1 : 0, 100);
    NSString *language = NSLocale.preferredLanguages.firstObject ?: @"en";
    bool rtl = [NSLocale characterDirectionForLanguage:language] == NSLocaleLanguageDirectionRightToLeft;
    ui_report_locale(language.UTF8String, rtl ? 1 : 0);
}

// The window, the insets (none on a desktop) and the system's settings, once the core runs.
static void reportEnvironment(void) {
    NSSize size = contentView.bounds.size;
    ui_report_window((int)std::lround(size.width), (int)std::lround(size.height),
                     (int)std::lround((mainWindow.backingScaleFactor ?: 2) * 100));
    ui_report_insets(0, 0, 0, 0, 0);
    reportSystem();
}

#ifdef UI_RELOAD
// `ui serve`'s builds: UI_RELOAD_FILE names a file holding the path of the app's newest
// bundle. When it changes, the app starts again on the new code in the running process: the
// old core saves its routes and signals and stops, the views, timers and nodes it left go,
// and the new one starts on what the old one saved.
static NSString *loadedCore;

static NSString *newestCore(void) {
    const char *file = getenv("UI_RELOAD_FILE");
    if (!file) return nil;
    NSString *text = [NSString stringWithContentsOfFile:@(file) encoding:NSUTF8StringEncoding error:nil];
    return [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

static void restartCore(NSString *path) {
#ifdef UI_TEST
    char *kept = ui_reload_save ? strdup(ui_reload_save()) : nullptr;
#endif
    ui_stop();
#ifdef UI_TEST
    driverClock(false, 0, false);
#endif
    tickScheduled = false;
    [clockSource invalidate];
    clockSource = nil;
    for (NSTimer *timer in timers.allValues) [timer invalidate];
    [timers removeAllObjects];
    if (shownAlert) [mainWindow endSheet:shownAlert.window];
    shownAlert = nil;
    for (UiHostNode *modal in modals.copy) closeModal(modal);
    for (UiHostNode *node in nodes.allValues) {
        [node.view removeFromSuperview];
        ui_grid_free(node.yoga);
    }
    [nodes removeAllObjects];
    [viewNodes removeAllObjects];
    [leaving removeAllObjects];
    for (NSView *view in rootView.subviews.copy) [view removeFromSuperview];
    YGNodeRemoveAllChildren(rootYoga);
    if (!loadCore(path.UTF8String)) {
#ifdef UI_TEST
        free(kept);
#endif
        return;
    }
    loadedCore = path;
    NSString *data = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    data = [data stringByAppendingPathComponent:NSBundle.mainBundle.bundleIdentifier ?: @"eskiu-ui"];
    ui_report_data_dir(data.UTF8String);
#ifdef UI_TEST
    if (kept && ui_reload_restore) ui_reload_restore(kept);
    free(kept);
#endif
    ui_start(launchExample);
    reportEnvironment();
    relayout();
    fprintf(stderr, "ui: restarted on %s\n", path.lastPathComponent.UTF8String);
}
#endif

#ifdef UI_TEST
// The driver (hosts/driver.h, docs/agents.md). While it owns the clock the window content's
// layer is stopped (speed 0) and its time is the driver's (timeOffset), so the views'
// animations move only when the clock does; `clock settle` moves it past their ends.
static bool clockFrozen;
static CFTimeInterval clockBase;
static void driverClockTo(double now) {
    if (clockFrozen && contentView.layer) contentView.layer.timeOffset = clockBase + now / 1000.0;
}
static void driverClock(bool owned, double now, bool settle) {
    CALayer *layer = contentView.layer;
    if (!layer) return;
    if (owned) {
        if (!clockFrozen) {
            CFTimeInterval local = [layer convertTime:CACurrentMediaTime() fromLayer:nil];
            layer.speed = 0;
            clockFrozen = true;
            clockBase = local - now / 1000.0;
        }
        if (settle) clockBase += 60;
        driverClockTo(now);
    } else if (clockFrozen) {
        CFTimeInterval paused = layer.timeOffset;
        layer.speed = 1;
        layer.timeOffset = 0;
        layer.beginTime = 0;
        layer.beginTime = [layer convertTime:CACurrentMediaTime() fromLayer:nil] - paused;
        clockFrozen = false;
    }
    [CATransaction flush];
}

// A view's frame in points from its window's top left, and whether it shows.
static NSRect windowFrame(NSView *view) {
    NSView *top = view.window.contentView;
    return top ? [view convertRect:view.bounds toView:top] : NSZeroRect;
}

static bool driverHostRequest(const std::string &request, std::string &answer) {
    if (driverText(request, "op") != "screenshot") return false;
    // The window's content as AppKit draws it, at the screen's scale.
    std::string file = driverText(request, "file");
    NSView *view = contentView;
    NSBitmapImageRep *bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
    // In sRGB, as the app gave its colors, whatever the display's space (Display P3).
    NSBitmapImageRep *srgb = [bitmap bitmapImageRepByConvertingToColorSpace:NSColorSpace.sRGBColorSpace
                                                            renderingIntent:NSColorRenderingIntentDefault];
    if (srgb) bitmap = srgb;
    NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png || ![png writeToFile:@(file.c_str()) atomically:YES]) {
        answer = "{\"ok\": false, \"op\": \"screenshot\", \"error\": \"could not write the screenshot\"}";
        return true;
    }
    answer = "{\"ok\": true, \"op\": \"screenshot\", \"width\": " + std::to_string(bitmap.pixelsWide) +
             ", \"height\": " + std::to_string(bitmap.pixelsHigh) + "}";
    return true;
}

static std::string driverCarry(const std::string &d) {
    std::string op = driverOp(d);
    if (op == "hello" || op == "clock") {
        driverClock(driverFlag(d, "virtual"), driverNumber(d, "now"), driverFlag(d, "settle"));
        return "";
    }
    if (op == "logs") return driverLogs();
    if (op == "perf") {
        task_vm_info_data_t info;
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return driverPerf(0);
        return driverPerf((double)info.phys_footprint);
    }
    if (op == "layout") {
        std::string rows;
        char line[160];
        for (UiHostNode *node in nodes.allValues) {
            NSView *view = node.view;
            if (!view) continue;
            NSRect r = windowFrame(view);
            bool shown = view.window != nil && !view.hiddenOrHasHiddenAncestor;
            for (NSView *up = view; up && shown; up = up.superview) if (up.alphaValue <= 0) shown = false;
            snprintf(line, sizeof line, "%d %.1f %.1f %.1f %.1f %d\n", node.nodeId, r.origin.x, r.origin.y, r.size.width,
                     r.size.height, shown ? 1 : 0);
            rows += line;
        }
        return rows;
    }
    int id = (int)driverNumber(d, "node");
    UiHostNode *node = nodes[@(id)];
    if (!node || !node.view) return "error: #" + std::to_string(id) + " has no view";
    NSView *view = node.view;
    // The input goes in as AppKit's: a control's action, a field's edit, the clip view's scroll.
    if (op == "tap") {
        if ([view isKindOfClass:NSSwitch.class]) {
            NSSwitch *toggle = (NSSwitch *)view;
            toggle.state = toggle.state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn;
            [toggle sendAction:toggle.action to:toggle.target];
        } else if ([view isKindOfClass:UiTextField.class]) [view.window makeFirstResponder:view];
        else if (node.kind == 12) ui_dispatch(id, node.generation);
        else if ([view isKindOfClass:NSButton.class]) [(NSButton *)view sendAction:((NSButton *)view).action to:((NSButton *)view).target];
        else if ([view isKindOfClass:UiPressable.class]) [view accessibilityPerformPress];
        else return "error: the view takes no press";
        return "";
    }
    if (op == "type") {
        if (![view isKindOfClass:UiTextField.class]) return "error: not a text field";
        NSTextField *field = (NSTextField *)view;
        [field.window makeFirstResponder:field];
        NSString *text = @(driverText(d, "text").c_str());
        NSText *editor = [field currentEditor];
        if (editor) editor.string = text; else field.stringValue = text;
        [NSNotificationCenter.defaultCenter postNotificationName:NSControlTextDidChangeNotification object:field];
        return "";
    }
    if (op == "scroll") {
        if (![view isKindOfClass:NSScrollView.class]) return "error: not a scroll view";
        NSScrollView *scroll = (NSScrollView *)view;
        NSClipView *clip = scroll.contentView;
        CGFloat most = std::max<CGFloat>(0, NSHeight([scroll.documentView frame]) - NSHeight(clip.bounds));
        CGFloat y = std::min(most, std::max<CGFloat>(0, clip.bounds.origin.y + driverNumber(d, "dy")));
        [clip scrollToPoint:NSMakePoint(clip.bounds.origin.x, y)];
        [scroll reflectScrolledClipView:clip];
        return std::to_string((int)std::lround(y));
    }
    return "error: the macOS host does not know " + op;
}
#endif

static NSMenuItem *menuItem(NSMenu *menu, NSString *title, SEL action, NSString *key, NSEventModifierFlags mask) {
    NSMenuItem *item = [menu addItemWithTitle:title action:action keyEquivalent:key];
    if (mask) item.keyEquivalentModifierMask = mask;
    return item;
}

// The menu bar: the app's menu (Quit), Edit with the standard shortcuts (the text fields
// take them through the responder chain), View and Window.
static void setupMenu(NSString *name) {
    NSMenu *bar = [NSMenu new];
    NSMenu *app = [NSMenu new];
    menuItem(app, [@"About " stringByAppendingString:name], @selector(orderFrontStandardAboutPanel:), @"", 0);
    [app addItem:NSMenuItem.separatorItem];
    menuItem(app, [@"Hide " stringByAppendingString:name], @selector(hide:), @"h", 0);
    menuItem(app, @"Hide Others", @selector(hideOtherApplications:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption);
    menuItem(app, @"Show All", @selector(unhideAllApplications:), @"", 0);
    [app addItem:NSMenuItem.separatorItem];
    menuItem(app, [@"Quit " stringByAppendingString:name], @selector(terminate:), @"q", 0);
    [bar addItemWithTitle:name action:nil keyEquivalent:@""].submenu = app;
    NSMenu *file = [[NSMenu alloc] initWithTitle:@"File"];
    menuItem(file, @"Close Window", @selector(performClose:), @"w", 0);
    [bar addItemWithTitle:@"File" action:nil keyEquivalent:@""].submenu = file;
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    menuItem(edit, @"Undo", @selector(undo:), @"z", 0);
    menuItem(edit, @"Redo", @selector(redo:), @"Z", 0);
    [edit addItem:NSMenuItem.separatorItem];
    menuItem(edit, @"Cut", @selector(cut:), @"x", 0);
    menuItem(edit, @"Copy", @selector(copy:), @"c", 0);
    menuItem(edit, @"Paste", @selector(paste:), @"v", 0);
    menuItem(edit, @"Select All", @selector(selectAll:), @"a", 0);
    [bar addItemWithTitle:@"Edit" action:nil keyEquivalent:@""].submenu = edit;
    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    menuItem(view, @"Back", @selector(back:), @"[", 0);
    menuItem(view, @"Enter Full Screen", @selector(toggleFullScreen:), @"f", NSEventModifierFlagCommand | NSEventModifierFlagControl);
    [bar addItemWithTitle:@"View" action:nil keyEquivalent:@""].submenu = view;
    NSMenu *window = [[NSMenu alloc] initWithTitle:@"Window"];
    menuItem(window, @"Minimize", @selector(performMiniaturize:), @"m", 0);
    menuItem(window, @"Zoom", @selector(performZoom:), @"", 0);
    [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""].submenu = window;
    NSApp.windowsMenu = window;
    NSApp.mainMenu = bar;
}

static const char *launchLink;
static NSSize windowSize = NSMakeSize(900, 700);

@interface UiDelegate : NSObject <NSApplicationDelegate>
@end
@implementation UiDelegate
- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls {
    if (urls.firstObject) ui_report_link(urls.firstObject.absoluteString.UTF8String);
}
- (void)applicationDidBecomeActive:(NSNotification *)note { ui_report_active(1); }
- (void)applicationDidResignActive:(NSNotification *)note { ui_report_active(0); }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
- (void)applicationWillTerminate:(NSNotification *)note { ui_stop(); }
- (void)observeValueForKeyPath:(NSString *)path ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    reportSystem();
}
- (void)systemChanged:(NSNotification *)note { reportSystem(); }
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    nodes = [NSMutableDictionary new];
    viewNodes = [NSMapTable weakToStrongObjectsMapTable];
    modals = [NSMutableArray new];
    events = [UiEvents new];
#ifdef UI_TEST
    // The driver: the app's output kept for `logs` (and still printed), and its socket.
    int printed = dup(2);
    driverCapture([printed](const char *bytes, size_t n) { (void)!write(printed, bytes, n); });
    const char *agentPort = getenv("UI_AGENT_PORT");
    int port = agentPort ? atoi(agentPort) : 8768;
    if (!driverListenTcp(port)) fprintf(stderr, "ui: the driver could not listen on 127.0.0.1:%d\n", port);
#endif
#ifdef UI_RELOAD
    loadedCore = newestCore();
    if (!loadedCore || !loadCore(loadedCore.UTF8String)) { fprintf(stderr, "ui: no app bundle (UI_RELOAD_FILE)\n"); exit(1); }
    // The pid next to the reload file: `ui run macos` reloads a running app instead of starting it.
    NSString *pidFile = [[@(getenv("UI_RELOAD_FILE")) stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"pid"];
    [[NSString stringWithFormat:@"%d", getpid()] writeToFile:pidFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSTimer *poll = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *t) {
        NSString *path = newestCore();
        if (path.length && ![path isEqualToString:loadedCore]) restartCore(path);
    }];
    [NSRunLoop.mainRunLoop addTimer:poll forMode:NSRunLoopCommonModes];
#endif
    NSString *name = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"]
                  ?: [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleName"] ?: NSProcessInfo.processInfo.processName;
    setupMenu(name);
    // The app's data directory, before the app starts (ui_data_dir).
    NSString *data = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    data = [data stringByAppendingPathComponent:NSBundle.mainBundle.bundleIdentifier ?: @"eskiu-ui"];
    [NSFileManager.defaultManager createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
    ui_report_data_dir(data.UTF8String);
    if (launchLink) ui_report_link(launchLink);
    mainWindow = [[UiWindow alloc] initWithContentRect:NSMakeRect(0, 0, windowSize.width, windowSize.height)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
          backing:NSBackingStoreBuffered defer:NO];
    mainWindow.title = name;
    mainWindow.minSize = NSMakeSize(320, 240);
    mainWindow.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    mainWindow.releasedWhenClosed = NO;
    contentView = [[UiContent alloc] initWithFrame:NSMakeRect(0, 0, windowSize.width, windowSize.height)];
    contentView.wantsLayer = YES;
    rootView = [[UiView alloc] initWithFrame:contentView.bounds];
    rootView.wantsLayer = YES;
    [contentView addSubview:rootView];
    rootYoga = YGNodeNew();
    YGNodeStyleSetFlexDirection(rootYoga, YGFlexDirectionColumn);
    mainWindow.contentView = contentView;
    [mainWindow center];
    [NSApp addObserver:self forKeyPath:@"effectiveAppearance" options:0 context:nil];
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(systemChanged:)
        name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(systemChanged:)
        name:NSCurrentLocaleDidChangeNotification object:nil];
    ui_start(launchExample);
    reportEnvironment();
    relayout();
    [mainWindow makeKeyAndOrderFront:nil];
    // `ui agent` and `ui test` (UI_NO_OPEN=1) leave the focus where it is.
    const char *quiet = getenv("UI_NO_OPEN");
    if (!quiet || strcmp(quiet, "1") != 0) [NSApp activateIgnoringOtherApps:YES];
}
@end

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--link") && i + 1 < argc) launchLink = argv[i + 1];
        if (!strcmp(argv[i], "--example") && i + 1 < argc) launchExample = atoi(argv[i + 1]);
    }
    // The window's size in points: UI_WINDOW=WxH, else Info.plist's UiWindowSize (ui.toml
    // [macos] window), else 900 by 700.
    NSString *size = [NSBundle.mainBundle objectForInfoDictionaryKey:@"UiWindowSize"];
    if (getenv("UI_WINDOW")) size = @(getenv("UI_WINDOW"));
    int w = 0, h = 0;
    if (size && sscanf(size.UTF8String, "%dx%d", &w, &h) == 2 && w >= 200 && h >= 150) windowSize = NSMakeSize(w, h);
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        UiDelegate *delegate = [UiDelegate new];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
