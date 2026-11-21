// The iOS host: the core (in Eskiu) sends commits, UIKit draws them as native views.
#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
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
#include "../driver.h"
#endif

@interface UiHostNode : NSObject
@property(nonatomic, strong) UIView *view;
@property(nonatomic) YGNodeRef yoga;
@property(nonatomic) int generation;
@property(nonatomic) CGFloat fontSize;
@property(nonatomic) int fontWeight;
@property(nonatomic, copy) NSString *fontFile;
@property(nonatomic) bool listening;
@property(nonatomic) int kind;
@property(nonatomic) CGFloat reportedViewport;
// A native view's type and the view its factory made (kind 13).
@property(nonatomic, copy) NSString *nativeType;
@property(nonatomic, strong) UIView *nativeView;
// A screen (kind 14): whether it was ever on top, and whether it slides in after layout.
@property(nonatomic) bool wasActive;
@property(nonatomic) bool slideIn;
// Whether the app listens to this node's frame (ui_on_layout), and the frame last reported.
@property(nonatomic) bool layoutListening;
@property(nonatomic) int nodeId;
// A slider's value as last shown or reported, in whole steps.
@property(nonatomic) int sliderValue;
// Transforms (translate in points, scale in percent, rotate in degrees) and the transition
// that animates changes (ms, Easing).
@property(nonatomic) int translateX, translateY, scalePercent, rotateDegrees, transitionMs, easing;
// The radius the app asked for; the layer's is at most half the shorter side (place).
@property(nonatomic) int radius;
// A background gradient (Style.gradient): its layer, under the subviews, and its colors (RGBA,
// 0: none) and angle (degrees, as CSS).
@property(nonatomic, strong) CAGradientLayer *gradient;
@property(nonatomic) int gradientFrom, gradientTo, gradientAngle;
// Style.blur: a material view under the subviews, or nil.
@property(nonatomic, strong) UIVisualEffectView *blur;
@property(nonatomic) CGRect reportedFrame;
@property(nonatomic) CGPoint reportedWindow;
// The parent the node was inserted in (0: the root), for disabled ancestors.
@property(nonatomic) int parentId;
// Accessibility (host_access.inc): Role, CheckState, selected, Disclosure, a range, Style.disabled,
// the spoken value and hint, whether the app gave a label, and once any was set (`access`) the
// traits and element flag the view had.
@property(nonatomic) int role, checked, selected, expanded, rangeNow, rangeMin, rangeMax;
@property(nonatomic) bool disabled, access, labelled, baseElement, fontSet;
@property(nonatomic) UIAccessibilityTraits baseTraits;
@property(nonatomic, copy) NSString *accessValue, *accessHint;
// A text input's options (host_input.inc), and what a swap between field and text view keeps.
@property(nonatomic) int secure, keyboard, returnKey, multiline, capitalize, autocorrect, autofill, submit, padding, textColor;
@property(nonatomic, copy) NSString *placeholder;
// Layout and text props since 73 (host_style.inc): the sides' padding (0: prop 2's), shrink,
// text align and lines, line height, italic, z index, clip, layout animation, an image's
// load listener, a modal's presentation and appearing, and the label's text.
@property(nonatomic) int padTop, padRight, padBottom, padLeft, shrink, textAlign, maxLines, lineHeight, layoutMs;
@property(nonatomic) int presentation, appear;
@property(nonatomic) bool italic, clip, cover, imageListening, placed, appearing;
@property(nonatomic, copy) NSString *text;
@end
@implementation UiHostNode
@end

static NSMutableDictionary<NSNumber *, UiHostNode *> *nodes;
static UIView *rootView;
static YGNodeRef rootYoga;
static int commitCount;
static double commitTime;
static bool selfTest;
static bool conformance;
static bool nativeTest;
// --link URL: start as if opened with that link (tests and tools).
static const char *launchLink;
// --press LABEL: press the element with that accessible name or title 1.5 s after launch,
// then go back 1.5 s later (to watch navigation on a simulator nothing can tap).
static const char *pressLabel;
static int launchExample;
static std::vector<double> frameTimes;
static CADisplayLink *frameLink;
static void (^testCompletion)(void);

static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode widthMode,
                      float height, YGMeasureMode heightMode) {
    UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(yoga);
    CGSize limit = CGSizeMake(widthMode == YGMeasureModeUndefined ? CGFLOAT_MAX : width,
                              heightMode == YGMeasureModeUndefined ? CGFLOAT_MAX : height);
    CGSize size = [node.view sizeThatFits:limit];
    // A text view's width is its text's: as wide as a field at least.
    if ([node.view isKindOfClass:UITextView.class] && widthMode == YGMeasureModeUndefined) size.width = std::max<CGFloat>(size.width, 120);
    return {static_cast<float>(size.width), static_cast<float>(size.height)};
}

static void relayout(void);
// Whether the layout runs right to left: the user's language is written so (host_style.inc).
static bool layoutRTL;

// Scrolling moves what is inside in the window without a layout: the listened nodes' window
// positions are checked again.
static void reportWindows(void) {
    for (UiHostNode *node in nodes.allValues) {
        if (!node.layoutListening || !node.view.superview || CGRectIsNull(node.reportedFrame)) continue;
        CGPoint at = [node.view.superview convertPoint:node.reportedFrame.origin toView:nil];
        CGPoint window = CGPointMake(std::lround(at.x), std::lround(at.y));
        if (CGPointEqualToPoint(window, node.reportedWindow)) continue;
        node.reportedWindow = window;
        CGRect f = node.reportedFrame;
        ui_dispatch_layout(node.nodeId, node.generation, (int)f.origin.x, (int)f.origin.y, (int)f.size.width, (int)f.size.height,
                           (int)window.x, (int)window.y);
    }
}

// A change animated by the node's transition, when it has one and is on screen.
static void animated(UiHostNode *node, void (^change)(void)) {
    if (node.transitionMs <= 0 || !node.view.window) { change(); return; }
    NSTimeInterval duration = node.transitionMs / 1000.0;
    if (node.easing == 4) {
        [UIView animateWithDuration:duration delay:0 usingSpringWithDamping:0.7 initialSpringVelocity:0
                            options:UIViewAnimationOptionAllowUserInteraction | UIViewAnimationOptionBeginFromCurrentState
                         animations:change completion:nil];
        return;
    }
    const UIViewAnimationOptions curves[] = {UIViewAnimationOptionCurveEaseInOut, UIViewAnimationOptionCurveEaseOut,
                                             UIViewAnimationOptionCurveEaseIn, UIViewAnimationOptionCurveLinear};
    [UIView animateWithDuration:duration delay:0
                        options:curves[std::min(3, std::max(0, node.easing))] | UIViewAnimationOptionAllowUserInteraction |
                                UIViewAnimationOptionBeginFromCurrentState
                     animations:change completion:nil];
}

static void applyTransform(UiHostNode *node) {
    CGAffineTransform t = CGAffineTransformMakeTranslation(node.translateX, node.translateY);
    t = CGAffineTransformScale(t, node.scalePercent / 100.0, node.scalePercent / 100.0);
    t = CGAffineTransformRotate(t, node.rotateDegrees * M_PI / 180.0);
    UIView *view = node.view;
    animated(node, ^{ view.transform = t; });
}
// An edge offset that is not set (kUnset in src/core/state.esk).
static const int kUnset = -1000000;

// Native views by type, registered before the app starts (UI_NATIVE_SETUP).
@interface UiViewType : NSObject
@property(nonatomic, copy) UIView *(^create)(UiViewEmit emit);
@property(nonatomic, copy) void (^set)(UIView *view, NSString *name, NSString *value);
@end
@implementation UiViewType
@end

static NSMutableDictionary<NSString *, UiViewType *> *viewTypes(void) {
    static NSMutableDictionary *types;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ types = [NSMutableDictionary new]; });
    return types;
}

extern "C" void ui_register_view(NSString *type, UIView *(^create)(UiViewEmit emit),
                                 void (^set)(UIView *view, NSString *name, NSString *value)) {
    UiViewType *entry = [UiViewType new];
    entry.create = create;
    entry.set = set;
    viewTypes()[type] = entry;
}

extern "C" UIViewController *ui_view_controller(void) {
    UIViewController *top = rootView.window.rootViewController;
    while (top.presentedViewController && !top.presentedViewController.isBeingDismissed) top = top.presentedViewController;
    return top;
}
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
    UIView *view = entry.create(emit);
    view.frame = node.view.bounds;
    view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [node.view addSubview:view];
    node.nativeView = view;
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
        UIView *view = node.view;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kSlide * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if (YGNodeStyleGetDisplay(node.yoga) == YGDisplayNone) view.hidden = YES;
        });
    } else node.view.hidden = YES;
}

// Screens sliding out: what was inside them stays until they go.
static NSMutableSet<UIView *> *leaving;
static bool insideLeaving(UIView *view) {
    for (UIView *up = view.superview; up; up = up.superview) if ([leaving containsObject:up]) return true;
    return false;
}

// A top screen leaving over one that is shown slides out before it goes.
static bool removeScreen(UiHostNode *node) {
    if (node.kind != 14 || node.view.hidden || !node.view.superview) return false;
    bool over = false;
    for (UiHostNode *other in nodes.allValues) {
        if (other != node && other.kind == 14 && other.wasActive && other.view.superview == node.view.superview) over = true;
    }
    if (!over) return false;
    UIView *view = node.view;
    if (!leaving) leaving = [NSMutableSet new];
    [leaving addObject:view];
    [view.superview bringSubviewToFront:view];
    [UIView animateWithDuration:kSlide delay:0 options:UIViewAnimationOptionCurveEaseIn animations:^{
        view.transform = CGAffineTransformMakeTranslation(layoutRTL ? -view.bounds.size.width : view.bounds.size.width, 0);
    } completion:^(BOOL done) { [view removeFromSuperview]; [leaving removeObject:view]; }];
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
        if (!over) continue;
        node.view.transform = CGAffineTransformMakeTranslation(layoutRTL ? -node.view.bounds.size.width : node.view.bounds.size.width, 0);
        [UIView animateWithDuration:kSlide delay:0 options:UIViewAnimationOptionCurveEaseOut animations:^{
            node.view.transform = CGAffineTransformIdentity;
        } completion:nil];
    }
}

static void ui_host_named(int id, const char *name, const char *value) {
    UiHostNode *node = nodes[@(id)];
    UiViewType *entry = node.nativeType ? viewTypes()[node.nativeType] : nil;
    if (!node.nativeView || !entry.set) return;
    entry.set(node.nativeView, [NSString stringWithUTF8String:name], [NSString stringWithUTF8String:value]);
}
// Modals: their views sit over the whole controller view, each laid out as its own root.
static NSMutableArray<UiHostNode *> *modals;
// The status bar the app asked for (BarStyle), read by the controller.
static int barStyle;
static bool barHidden;
// The alert on screen and how to answer it (also used by the conformance run).
static UIAlertController *shownAlert;
static void (^answerAlert)(int);

// An RGBA color from the core (0: the platform's default, nil here).
static UIColor *colorOf(int value) {
    uint32_t v = (uint32_t)value;
    if (!v) return nil;
    return [UIColor colorWithRed:(v >> 24) / 255.0 green:((v >> 16) & 255) / 255.0
                            blue:((v >> 8) & 255) / 255.0 alpha:(v & 255) / 255.0];
}

// An Image's load or failure, to the app when it listens (ui_on_image).
static void reportImage(UiHostNode *node, UIImage *image) {
    if (node.imageListening && nodes[@(node.nodeId)] == node) ui_dispatch_image(node.nodeId, node.generation, image ? 1 : 0);
}

// A bundled resource by file name, or an http(s) URL fetched in the background. The image
// changes the node's measured size, so the layout runs again when it arrives.
static void loadImage(UiHostNode *node, NSString *source) {
    UIImageView *view = (UIImageView *)node.view;
    YGNodeRef yoga = node.yoga;
    view.image = nil;
    if (!source) return;
    if ([source hasPrefix:@"http://"] || [source hasPrefix:@"https://"]) {
        NSURL *url = [NSURL URLWithString:source];
        [[NSURLSession.sharedSession dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *r, NSError *e) {
            UIImage *image = data ? [UIImage imageWithData:data] : nil;
            dispatch_async(dispatch_get_main_queue(), ^{
                view.image = image; YGNodeMarkDirty(yoga); relayout();
                reportImage(node, image);
            });
        }] resume];
        return;
    }
    // A file by its absolute path (a photo the app saved) or a file:// URL, else a bundled one.
    NSString *path = [source hasPrefix:@"file://"] ? [NSURL URLWithString:source].path
                   : [source hasPrefix:@"/"] ? source : [NSBundle.mainBundle pathForResource:source ofType:nil];
    view.image = path ? [UIImage imageWithContentsOfFile:path] : nil;
    YGNodeMarkDirty(yoga);
    UIImage *image = view.image;
    // After the commit that set the source, as the web reports it.
    dispatch_async(dispatch_get_main_queue(), ^{ reportImage(node, image); });
}

// The gradient layer for a node's colors and angle, made, updated or dropped.
static void applyGradient(UiHostNode *node) {
    if (!node.gradientTo) { [node.gradient removeFromSuperlayer]; node.gradient = nil; return; }
    if (!node.gradient) {
        node.gradient = [CAGradientLayer layer];
        [node.view.layer insertSublayer:node.gradient atIndex:0];
        node.gradient.frame = node.view.bounds;
    }
    UIColor *from = node.gradientFrom ? colorOf(node.gradientFrom) : UIColor.clearColor;
    node.gradient.colors = @[(id)from.CGColor, (id)colorOf(node.gradientTo).CGColor];
    double a = node.gradientAngle * M_PI / 180.0;
    node.gradient.startPoint = CGPointMake(0.5 - sin(a) / 2, 0.5 + cos(a) / 2);
    node.gradient.endPoint = CGPointMake(0.5 + sin(a) / 2, 0.5 - cos(a) / 2);
}

// A backdrop blur: the system material nearest the radius (they adapt to dark mode).
static void applyBlur(UiHostNode *node, int radius) {
    if (radius <= 0) {
        if (node.blur && !node.gradient) node.view.backgroundColor = colorOf(node.gradientFrom);
        [node.blur removeFromSuperview]; node.blur = nil; return;
    }
    UIBlurEffectStyle style = radius <= 10 ? UIBlurEffectStyleSystemUltraThinMaterial
        : radius <= 20 ? UIBlurEffectStyleSystemThinMaterial
        : radius <= 35 ? UIBlurEffectStyleSystemMaterial : UIBlurEffectStyleSystemThickMaterial;
    if (!node.blur) {
        node.blur = [[UIVisualEffectView alloc] initWithEffect:nil];
        node.blur.frame = node.view.bounds;
        node.blur.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        node.blur.userInteractionEnabled = NO;
        node.blur.clipsToBounds = YES;
        node.blur.layer.cornerRadius = node.view.layer.cornerRadius;
        [node.view insertSubview:node.blur atIndex:0];
    }
    node.blur.effect = [UIBlurEffect effectWithStyle:style];
    // The background tints the material, over the blur.
    node.blur.contentView.backgroundColor = colorOf(node.gradientFrom);
    if (!node.gradient) node.view.backgroundColor = UIColor.clearColor;
}

static void place(YGNodeRef yoga, UIView *parent) {
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        // A hidden screen keeps its frame (it stays under a push's slide).
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(child);
        // Bounds and center, not frame, which a transform makes undefined.
        CGFloat w = YGNodeLayoutGetWidth(child), h = YGNodeLayoutGetHeight(child);
        // Only the size: a scroll view's bounds origin is its scroll offset.
        CGRect bounds = node.view.bounds;
        bounds.size = CGSizeMake(w, h);
        CGPoint center = CGPointMake(YGNodeLayoutGetLeft(child) + w / 2, YGNodeLayoutGetTop(child) + h / 2);
        UIView *view = node.view;
        // Style.layout_ms: a frame the layout changes moves there, once the view was placed.
        bool moves = node.layoutMs > 0 && node.placed && view.window && !UIAccessibilityIsReduceMotionEnabled() &&
                     (!CGSizeEqualToSize(view.bounds.size, bounds.size) || !CGPointEqualToPoint(view.center, center));
        node.placed = true;
        void (^frame)(void) = ^{ view.bounds = bounds; view.center = center; };
        if (moves) {
            const UIViewAnimationOptions curves[] = {UIViewAnimationOptionCurveEaseInOut, UIViewAnimationOptionCurveEaseOut,
                                                     UIViewAnimationOptionCurveEaseIn, UIViewAnimationOptionCurveLinear};
            UIViewAnimationOptions options = UIViewAnimationOptionAllowUserInteraction | UIViewAnimationOptionBeginFromCurrentState;
            if (node.easing == 4) {
                [UIView animateWithDuration:node.layoutMs / 1000.0 delay:0 usingSpringWithDamping:0.7 initialSpringVelocity:0
                                    options:options animations:frame completion:nil];
            } else {
                [UIView animateWithDuration:node.layoutMs / 1000.0 delay:0 options:options | curves[std::min(3, std::max(0, node.easing))]
                                 animations:frame completion:nil];
            }
        } else frame();
        // A radius past half the shorter side draws nothing on iOS: a pill asks for 999.
        if (node.radius > 0) node.view.layer.cornerRadius = std::min<CGFloat>(node.radius, std::min(w, h) / 2);
        if (node.blur) node.blur.layer.cornerRadius = node.view.layer.cornerRadius;
        if (node.gradient) {
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            node.gradient.frame = CGRectMake(0, 0, w, h);
            [CATransaction commit];
        }
        if (node.layoutListening) {
            CGRect frame = CGRectMake(std::lround(YGNodeLayoutGetLeft(child)), std::lround(YGNodeLayoutGetTop(child)),
                                      std::lround(YGNodeLayoutGetWidth(child)), std::lround(YGNodeLayoutGetHeight(child)));
            // The ancestors are placed already: the window position is final.
            CGPoint at = [node.view.superview convertPoint:CGPointMake(YGNodeLayoutGetLeft(child), YGNodeLayoutGetTop(child)) toView:nil];
            CGPoint window = CGPointMake(std::lround(at.x), std::lround(at.y));
            if (!CGRectEqualToRect(frame, node.reportedFrame) || !CGPointEqualToPoint(window, node.reportedWindow)) {
                node.reportedFrame = frame;
                node.reportedWindow = window;
                ui_dispatch_layout(node.nodeId, node.generation, (int)frame.origin.x, (int)frame.origin.y,
                                   (int)frame.size.width, (int)frame.size.height, (int)window.x, (int)window.y);
            }
        }
        place(child, node.view);
    }
    // A scroll view's content is as tall as its lowest child.
    UiHostNode *owner = (__bridge UiHostNode *)YGNodeGetContext(yoga);
    if (owner && [owner.view isKindOfClass:UIScrollView.class]) {
        CGFloat bottom = 0;
        for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
            YGNodeRef child = YGNodeGetChild(yoga, i);
            bottom = std::max(bottom, (CGFloat)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
        }
        UIScrollView *scroll = (UIScrollView *)owner.view;
        scroll.contentSize = CGSizeMake(YGNodeLayoutGetWidth(yoga), bottom + YGNodeLayoutGetPadding(yoga, YGEdgeBottom));
        // A virtual list hears its viewport once laid out, and again when it changes size.
        CGFloat viewport = YGNodeLayoutGetHeight(yoga);
        if (owner.listening && viewport != owner.reportedViewport) {
            owner.reportedViewport = viewport;
            ui_dispatch_scroll((int)scroll.tag, owner.generation, (int)std::lround(scroll.contentOffset.y),
                               (int)std::lround(viewport));
        }
    }
}

// A pressable: a touch on a descendant that does not handle touches (a label, a row) is the
// pressable's, so its text presses it too. A control, a scroll or a gesture inside keeps its own.
@interface UiPressable : UIControl
@end
@implementation UiPressable
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    if (!hit || hit == self) return hit;
    for (UIView *view = hit; view && view != self; view = view.superview) {
        if ([view isKindOfClass:UIControl.class] || [view isKindOfClass:UIScrollView.class]) return hit;
        for (UIGestureRecognizer *g in view.gestureRecognizers)
            if (![g isKindOfClass:UIHoverGestureRecognizer.class]) return hit;
    }
    return self;
}
@end

@interface UiEvents : NSObject <UIScrollViewDelegate>
- (void)press:(UIControl *)sender;
- (void)hover:(UIHoverGestureRecognizer *)gesture;
- (void)longPress:(UILongPressGestureRecognizer *)gesture;
- (void)slide:(UISlider *)sender;
- (void)pan:(UIPanGestureRecognizer *)gesture;
- (void)backdrop:(UITapGestureRecognizer *)gesture;
- (void)edit:(UITextField *)sender;
- (void)toggle:(UISwitch *)sender;
- (void)refresh:(UIRefreshControl *)sender;
- (void)focus:(UITextField *)sender;
- (void)blur:(UITextField *)sender;
- (void)frame:(CADisplayLink *)sender;
- (void)clock:(CADisplayLink *)sender;
@end
// The focused field inside its nearest scroll view, above the keyboard: the root already
// ends at the keyboard, so the scroll view only has to bring the field into its bounds.
static void revealFocused(void) {
    for (UiHostNode *node in nodes.allValues) {
        bool input = [node.view isKindOfClass:UITextField.class] || [node.view isKindOfClass:UITextView.class];
        if (!input || !node.view.isFirstResponder) continue;
        for (UIView *up = node.view.superview; up; up = up.superview) {
            if (![up isKindOfClass:UIScrollView.class]) continue;
            CGRect rect = CGRectInset([node.view convertRect:node.view.bounds toView:up], 0, -16);
            [(UIScrollView *)up scrollRectToVisible:rect animated:YES];
            break;
        }
        return;
    }
}

@implementation UiEvents
- (void)backdrop:(UITapGestureRecognizer *)gesture {
    UIView *view = gesture.view;
    if ([view hitTest:[gesture locationInView:view] withEvent:nil] != view) return;
    UiHostNode *node = nodes[@(view.tag)];
    if (node && node.listening) ui_dispatch((int)view.tag, node.generation);
}
// A drag on a node with a pan handler, measured in its superview (the node itself may move).
- (void)pan:(UIPanGestureRecognizer *)gesture {
    UiHostNode *node = nodes[@(gesture.view.tag)];
    if (!node) return;
    UIView *space = gesture.view.superview;
    CGPoint t = [gesture translationInView:space], v = [gesture velocityInView:space];
    int phase = gesture.state == UIGestureRecognizerStateBegan ? 0 : gesture.state == UIGestureRecognizerStateChanged ? 1
              : gesture.state == UIGestureRecognizerStateEnded ? 2 : 3;
    if (phase == 0) ui_dispatch_pan((int)gesture.view.tag, node.generation, 0, 0, 0, 0, 0);
    ui_dispatch_pan((int)gesture.view.tag, node.generation, phase == 0 ? 1 : phase, (int)std::lround(t.x), (int)std::lround(t.y),
                    (int)std::lround(v.x), (int)std::lround(v.y));
}
// A slider moved: each new whole step is reported.
- (void)slide:(UISlider *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    int value = (int)lroundf(sender.value);
    if (!node || value == node.sliderValue) return;
    node.sliderValue = value;
    ui_dispatch_value((int)sender.tag, node.generation, value);
}
// A pointer (an iPad's, a Mac's) entering and leaving a node with a hover handler.
- (void)hover:(UIHoverGestureRecognizer *)gesture {
    UiHostNode *node = nodes[@(gesture.view.tag)];
    if (!node) return;
    if (gesture.state == UIGestureRecognizerStateBegan) ui_dispatch_hover((int)gesture.view.tag, node.generation, 1);
    else if (gesture.state == UIGestureRecognizerStateEnded || gesture.state == UIGestureRecognizerStateCancelled)
        ui_dispatch_hover((int)gesture.view.tag, node.generation, 0);
}
// A node with a long press handler held: the recognizer cancels the touch, so it is not also
// a press.
- (void)longPress:(UILongPressGestureRecognizer *)gesture {
    UiHostNode *node = nodes[@(gesture.view.tag)];
    if (node && gesture.state == UIGestureRecognizerStateBegan) ui_dispatch_long_press((int)gesture.view.tag, node.generation);
}
- (void)press:(UIControl *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch((int)sender.tag, node.generation);
}
- (void)scrollViewDidScroll:(UIScrollView *)scroll {
    reportWindows();
    UiHostNode *node = nodes[@(scroll.tag)];
    if (!node || !node.listening) return;
    node.reportedViewport = scroll.bounds.size.height;
    ui_dispatch_scroll((int)scroll.tag, node.generation, (int)std::lround(scroll.contentOffset.y),
                       (int)std::lround(scroll.bounds.size.height));
}
- (void)refresh:(UIRefreshControl *)sender {
    UIView *scroll = sender.superview;
    UiHostNode *node = nodes[@(scroll.tag)];
    if (node) ui_dispatch_refresh((int)scroll.tag, node.generation);
}
- (void)toggle:(UISwitch *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch_toggle((int)sender.tag, node.generation, sender.on ? 1 : 0);
}
- (void)edit:(UITextField *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch_text((int)sender.tag, node.generation, sender.text.UTF8String ?: "");
}
- (void)focus:(UITextField *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch_focus((int)sender.tag, node.generation, 1);
    // With the keyboard already up, a newly focused field may be under it.
    dispatch_async(dispatch_get_main_queue(), ^{ revealFocused(); });
}
- (void)blur:(UITextField *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch_focus((int)sender.tag, node.generation, 0);
}
- (void)frame:(CADisplayLink *)sender {
    [sender invalidate]; frameLink = nil;
    auto start = std::chrono::steady_clock::now();
    ui_tick();
    frameTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
- (void)clock:(CADisplayLink *)sender { ui_dispatch_frame(sender.timestamp); }
@end
static UiEvents *events;

#include "host_access.inc"
#include "host_text.inc"
#include "host_style.inc"
#include "host_views.inc"
#include "host_tests.inc"
#include "host_main.inc"
