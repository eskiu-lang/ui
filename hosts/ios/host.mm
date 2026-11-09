// Phase-0 host glue. Components and state stay in Eskiu; UIKit owns native views.
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
    return {static_cast<float>(size.width), static_cast<float>(size.height)};
}

static void relayout(void);

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
        view.transform = CGAffineTransformMakeTranslation(view.bounds.size.width, 0);
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
        node.view.transform = CGAffineTransformMakeTranslation(node.view.bounds.size.width, 0);
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

static UIFontWeight weightOf(int weight) {
    if (weight <= 0) return UIFontWeightRegular;
    const UIFontWeight weights[] = {UIFontWeightUltraLight, UIFontWeightThin, UIFontWeightLight, UIFontWeightRegular,
        UIFontWeightMedium, UIFontWeightSemibold, UIFontWeightBold, UIFontWeightHeavy, UIFontWeightBlack};
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
        // Already registered (another file with the same font) is fine too.
        if (postScript && (CTFontManagerRegisterGraphicsFont(cgFont, &error) || [UIFont fontWithName:(__bridge NSString *)postScript size:12]))
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

static void applyFont(UiHostNode *node) {
    CGFloat size = node.fontSize > 0 ? node.fontSize : 18;
    NSString *custom = node.fontFile ? fontName(node.fontFile) : nil;
    UIFont *font = custom ? [UIFont fontWithName:custom size:size] : nil;
    if (!font) font = [UIFont systemFontOfSize:size weight:weightOf(node.fontWeight)];
    if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).font = font;
    else if ([node.view isKindOfClass:UIButton.class]) ((UIButton *)node.view).titleLabel.font = font;
    else if ([node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).font = font;
    YGNodeMarkDirty(node.yoga);
}

static void applyColor(UiHostNode *node, UIColor *color) {
    if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).textColor = color ?: UIColor.labelColor;
    else if ([node.view isKindOfClass:UIButton.class]) [(UIButton *)node.view setTitleColor:color forState:UIControlStateNormal];
    else if ([node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).textColor = color ?: UIColor.labelColor;
}

// A bundled resource by file name, or an http(s) URL fetched in the background. The image
// changes the node's measured size, so the layout runs again when it arrives.
static void loadImage(UIImageView *view, YGNodeRef yoga, NSString *source) {
    view.image = nil;
    if (!source) return;
    if ([source hasPrefix:@"http://"] || [source hasPrefix:@"https://"]) {
        NSURL *url = [NSURL URLWithString:source];
        [[NSURLSession.sharedSession dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *r, NSError *e) {
            UIImage *image = data ? [UIImage imageWithData:data] : nil;
            dispatch_async(dispatch_get_main_queue(), ^{
                view.image = image; YGNodeMarkDirty(yoga); relayout();
            });
        }] resume];
        return;
    }
    // A file by its absolute path (a photo the app saved) or a file:// URL, else a bundled one.
    NSString *path = [source hasPrefix:@"file://"] ? [NSURL URLWithString:source].path
                   : [source hasPrefix:@"/"] ? source : [NSBundle.mainBundle pathForResource:source ofType:nil];
    view.image = [UIImage imageWithContentsOfFile:path];
    YGNodeMarkDirty(yoga);
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
        node.view.bounds = bounds;
        // A radius past half the shorter side draws nothing on iOS: a pill asks for 999.
        if (node.radius > 0) node.view.layer.cornerRadius = std::min<CGFloat>(node.radius, std::min(w, h) / 2);
        if (node.blur) node.blur.layer.cornerRadius = node.view.layer.cornerRadius;
        node.view.center = CGPointMake(YGNodeLayoutGetLeft(child) + w / 2, YGNodeLayoutGetTop(child) + h / 2);
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
        if (![node.view isKindOfClass:UITextField.class] || !node.view.isFirstResponder) continue;
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

extern "C" void ui_host_schedule(void) {
    if (frameLink) return;
    frameLink = [CADisplayLink displayLinkWithTarget:events selector:@selector(frame:)];
    [frameLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
extern "C" void ui_host_cancel(void) { [frameLink invalidate]; frameLink = nil; }

// The display clock for use_frame: a display link of its own while it is on.
static CADisplayLink *clockLink;
extern "C" char *ui_host_asset_path(const char *name) {
    NSString *path = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@(name)];
    BOOL directory = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:path isDirectory:&directory] || directory) return NULL;
    return strdup(path.fileSystemRepresentation);
}

extern "C" void ui_host_frames(int on) {
    if (!on) { [clockLink invalidate]; clockLink = nil; return; }
    if (clockLink) return;
    clockLink = [CADisplayLink displayLinkWithTarget:events selector:@selector(clock:)];
    [clockLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
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

extern "C" void ui_host_apply(int op, int id, int generation, int parent,
                               int before, int kind, const char *text) {
    if (op == 1) {
        UiHostNode *node = [UiHostNode new];
        node.gradientAngle = 180;
        node.nodeId = id;
        node.scalePercent = 100;
        node.generation = generation;
        node.kind = kind;
        node.yoga = YGNodeNew();
        if (kind == 4) {
            UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
            button.tag = id;
            [button addTarget:events action:@selector(press:) forControlEvents:UIControlEventTouchUpInside];
            node.view = button;
        } else if (kind == 5) {
            UITextField *field = [UITextField new];
            field.borderStyle = UITextBorderStyleRoundedRect;
            field.font = [UIFont systemFontOfSize:18];
            field.tag = id;
            [field addTarget:events action:@selector(edit:) forControlEvents:UIControlEventEditingChanged];
            [field addTarget:events action:@selector(focus:) forControlEvents:UIControlEventEditingDidBegin];
            [field addTarget:events action:@selector(blur:) forControlEvents:UIControlEventEditingDidEnd];
            node.view = field;
        } else if (kind == 3) {
            UILabel *label = [UILabel new];
            label.numberOfLines = 0;
            label.font = [UIFont systemFontOfSize:18];
            node.view = label;
        } else if (kind == 7) {
            UIImageView *image = [UIImageView new];
            image.contentMode = UIViewContentModeScaleAspectFit;
            image.isAccessibilityElement = YES;
            image.accessibilityTraits = UIAccessibilityTraitImage;
            node.view = image;
        } else if (kind == 8) {
            UIControl *control = [UiPressable new];
            control.tag = id;
            control.isAccessibilityElement = YES;
            control.accessibilityTraits = UIAccessibilityTraitButton;
            [control addTarget:events action:@selector(press:) forControlEvents:UIControlEventTouchUpInside];
            node.view = control;
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        } else if (kind == 15) {
            UISlider *slider = [UISlider new];
            slider.tag = id;
            [slider addTarget:events action:@selector(slide:) forControlEvents:UIControlEventValueChanged];
            node.view = slider;
        } else if (kind == 9) {
            UISwitch *toggle = [UISwitch new];
            toggle.tag = id;
            [toggle addTarget:events action:@selector(toggle:) forControlEvents:UIControlEventValueChanged];
            node.view = toggle;
        } else if (kind == 10) {
            node.view = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
        } else if (kind == 11) {
            node.view = [UIView new];
            node.view.hidden = YES;
            YGNodeStyleSetDisplay(node.yoga, YGDisplayNone);
        } else if (kind == 12) {
            node.view = [UIView new];
            node.view.backgroundColor = [UIColor colorWithWhite:0 alpha:0.4];
            node.view.accessibilityViewIsModal = YES;
            node.view.tag = id;
            // A tap on the backdrop (outside the content) asks it to close.
            UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc] initWithTarget:events action:@selector(backdrop:)];
            tap.cancelsTouchesInView = NO;
            [node.view addGestureRecognizer:tap];
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
        } else if (kind == 13) {
            node.view = [UIView new];
            node.view.clipsToBounds = YES;
        } else if (kind == 6) {
            UIScrollView *scroll = [UIScrollView new];
            // The app places content from ui_insets() itself (as React Native's default): UIKit
            // adding the safe area again would leave a second gap under the status bar.
            scroll.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
            scroll.tag = id;
            scroll.delegate = events;
            node.view = scroll;
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetOverflow(node.yoga, YGOverflowScroll);
            // Shrinks to the space its parent has, as React Native's ScrollView, or it is as
            // tall as its content and never scrolls.
            YGNodeStyleSetFlexShrink(node.yoga, 1);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        } else {
            node.view = [UIView new];
            YGNodeStyleSetFlexDirection(node.yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        }
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
        // Props: the code in kind (1 gap, 2 padding, 3 focused), the value in before.
        if (kind == 1) YGNodeStyleSetGap(node.yoga, YGGutterAll, before);
        else if (kind == 2) {
            YGNodeStyleSetPadding(node.yoga, YGEdgeAll, before);
            // A text field draws its text from its left edge: inset it by the padding.
            if ([node.view isKindOfClass:UITextField.class]) {
                UITextField *field = (UITextField *)node.view;
                field.leftView = before ? [[UIView alloc] initWithFrame:CGRectMake(0, 0, before, 1)] : nil;
                field.leftViewMode = before ? UITextFieldViewModeAlways : UITextFieldViewModeNever;
            }
        }
        else if (kind == 3) {
            if (before) [node.view becomeFirstResponder]; else [node.view resignFirstResponder];
        } else if (kind == 4) {
            if (before) YGNodeStyleSetWidth(node.yoga, before); else YGNodeStyleSetWidthAuto(node.yoga);
        } else if (kind == 5) {
            if (before) YGNodeStyleSetHeight(node.yoga, before); else YGNodeStyleSetHeightAuto(node.yoga);
        } else if (kind == 6) {
            // A growing child also shrinks to its parent, so a scroll under it stays in the window.
            YGNodeStyleSetFlexGrow(node.yoga, before);
            if (![node.view isKindOfClass:UIScrollView.class]) YGNodeStyleSetFlexShrink(node.yoga, before > 0 ? 1 : 0);
        }
        else if (kind == 7) YGNodeStyleSetFlexWrap(node.yoga, before ? YGWrapWrap : YGWrapNoWrap);
        else if (kind == 8) {
            UIView *view = node.view;
            UIColor *color = colorOf(before);
            node.gradientFrom = before;
            if (node.gradient) applyGradient(node);
            else if (node.blur) node.blur.contentView.backgroundColor = color;
            else animated(node, ^{ view.backgroundColor = color; });
        }
        else if (kind == 50 || kind == 51) {
            if (kind == 50) node.gradientTo = before; else node.gradientAngle = before;
            applyGradient(node);
            // The gradient paints the background: the view's own color goes under it clear.
            node.view.backgroundColor = node.gradient ? UIColor.clearColor : colorOf(node.gradientFrom);
        }
        else if (kind == 52) applyBlur(node, before);
        else if (kind == 9) applyColor(node, colorOf(before));
        else if (kind == 10) { node.fontSize = before; applyFont(node); }
        else if (kind == 11) { node.fontWeight = before; applyFont(node); }
        else if (kind == 12) { node.view.layer.borderWidth = before; YGNodeStyleSetBorder(node.yoga, YGEdgeAll, before); }
        else if (kind == 13) node.view.layer.borderColor = colorOf(before).CGColor;
        else if (kind == 14) {
            node.radius = before;
            CGSize size = node.view.bounds.size;
            node.view.layer.cornerRadius = std::min<CGFloat>(before, std::min(size.width, size.height) / 2);
            node.view.clipsToBounds = before > 0 && !node.view.layer.shadowOpacity;
        }
        else if (kind == 47 && [node.view isKindOfClass:UIImageView.class]) {
            node.view.contentMode = before ? UIViewContentModeScaleAspectFill : UIViewContentModeScaleAspectFit;
            node.view.clipsToBounds = before || node.radius > 0;
        }
        else if (kind == 48 && [node.view isKindOfClass:UIImageView.class]) {
            NSString *filter = before ? kCAFilterNearest : kCAFilterLinear;
            node.view.layer.magnificationFilter = filter;
            node.view.layer.minificationFilter = filter;
        }
        else if (kind == 46) {
            // A shadow draws outside the bounds, so a raised view does not clip to its radius.
            CALayer *layer = node.view.layer;
            layer.shadowColor = [UIColor colorWithRed:17 / 255.0 green:24 / 255.0 blue:39 / 255.0 alpha:1].CGColor;
            layer.shadowOpacity = before ? 0.16 : 0;
            layer.shadowRadius = before;
            layer.shadowOffset = CGSizeMake(0, before / 2.0);
            node.view.clipsToBounds = layer.cornerRadius > 0 && !before;
        }
        else if (kind == 15) { UIView *view = node.view; animated(node, ^{ view.alpha = before / 100.0; }); }
        else if (kind >= 35 && kind <= 38) {
            if (kind == 35) node.translateX = before;
            else if (kind == 36) node.translateY = before;
            else if (kind == 37) node.scalePercent = before;
            else node.rotateDegrees = before;
            applyTransform(node);
        } else if (kind == 39) node.transitionMs = before;
        else if (kind == 40) node.easing = before;
        else if (kind == 16) {
            const YGAlign aligns[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch};
            YGNodeStyleSetAlignItems(node.yoga, aligns[std::min(4, std::max(0, before))]);
        } else if (kind == 17) {
            const YGJustify justifies[] = {YGJustifyFlexStart, YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                           YGJustifySpaceBetween, YGJustifySpaceAround};
            YGNodeStyleSetJustifyContent(node.yoga, justifies[std::min(5, std::max(0, before))]);
        } else if (kind == 18) [(UISwitch *)node.view setOn:before != 0 animated:NO];
        else if (kind == 19) {
            UIActivityIndicatorView *spinner = (UIActivityIndicatorView *)node.view;
            if (before) [spinner startAnimating]; else [spinner stopAnimating];
        } else if (kind == 22) {
            // Pull to refresh: the platform's control, attached on first use.
            UIScrollView *scroll = (UIScrollView *)node.view;
            if (!scroll.refreshControl) {
                UIRefreshControl *control = [UIRefreshControl new];
                [control addTarget:events action:@selector(refresh:) forControlEvents:UIControlEventValueChanged];
                scroll.refreshControl = control;
            }
            if (before && !scroll.refreshControl.refreshing) [scroll.refreshControl beginRefreshing];
            else if (!before && scroll.refreshControl.refreshing) [scroll.refreshControl endRefreshing];
        } else if (kind == 23) screenActive(node, before);
        else if (kind >= 32 && kind <= 34 && [node.view isKindOfClass:UISlider.class]) {
            UISlider *slider = (UISlider *)node.view;
            if (kind == 33) slider.minimumValue = before;
            else if (kind == 34) slider.maximumValue = before;
            else { slider.value = before; node.sliderValue = before; }
        } else if (kind == 41) {
            for (UIGestureRecognizer *g in node.view.gestureRecognizers.copy) {
                if ([g isKindOfClass:UIPanGestureRecognizer.class]) [node.view removeGestureRecognizer:g];
            }
            if (before) {
                node.view.tag = id;
                node.view.userInteractionEnabled = YES;
                [node.view addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:events action:@selector(pan:)]];
            }
        } else if (kind == 31) {
            for (UIGestureRecognizer *g in node.view.gestureRecognizers.copy) {
                if ([g isKindOfClass:UIHoverGestureRecognizer.class]) [node.view removeGestureRecognizer:g];
            }
            if (before) {
                node.view.tag = id;
                [node.view addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:events action:@selector(hover:)]];
            }
        } else if (kind == 49) {
            for (UIGestureRecognizer *g in node.view.gestureRecognizers.copy) {
                if ([g isKindOfClass:UILongPressGestureRecognizer.class]) [node.view removeGestureRecognizer:g];
            }
            if (before) {
                node.view.tag = id;
                node.view.userInteractionEnabled = YES;
                [node.view addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:events action:@selector(longPress:)]];
            }
        } else if (kind == 30) { node.layoutListening = before != 0; node.reportedFrame = CGRectNull; }
        else if (kind == 42) YGNodeStyleSetMinWidth(node.yoga, before);
        else if (kind == 43) YGNodeStyleSetMaxWidth(node.yoga, before ? (float)before : YGUndefined);
        else if (kind == 44) YGNodeStyleSetMinHeight(node.yoga, before);
        else if (kind == 45) YGNodeStyleSetMaxHeight(node.yoga, before ? (float)before : YGUndefined);
        else if (kind == 24) YGNodeStyleSetPositionType(node.yoga, before ? YGPositionTypeAbsolute : YGPositionTypeRelative);
        else if (kind >= 25 && kind <= 28) {
            const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
            YGNodeStyleSetPosition(node.yoga, edges[kind - 25], before == kUnset ? YGUndefined : (float)before);
        } else if (kind == 29) YGNodeStyleSetAspectRatio(node.yoga, before ? before / 1000.0f : YGUndefined);
        else if (kind == 20 || kind == 21) {
            if (kind == 20) barStyle = before; else barHidden = before != 0;
            [rootView.window.rootViewController setNeedsStatusBarAppearanceUpdate];
        }
    } else if (op == 9) {
        NSString *value = text && text[0] ? [NSString stringWithUTF8String:text] : nil;
        if (kind == 4 && node.kind == 13) mountNative(node, id, value);
        else if (kind == 1) node.view.accessibilityLabel = value;
        else if (kind == 5) { node.fontFile = value; applyFont(node); }
        else if (kind == 2 && [node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).placeholder = value;
        else if (kind == 3 && [node.view isKindOfClass:UIImageView.class]) loadImage((UIImageView *)node.view, node.yoga, value);
    } else if (op == 3) {
        NSString *value = [NSString stringWithUTF8String:text ? text : ""];
        if ([node.view isKindOfClass:UITextField.class]) {
            UITextField *field = (UITextField *)node.view;
            if (![field.text isEqualToString:value]) field.text = value;
        } else if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).text = value;
        else [(UIButton *)node.view setTitle:value forState:UIControlStateNormal];
        YGNodeMarkDirty(node.yoga);
    } else if ((op == 4 || op == 5) && node.kind == 12) {
        // Over everything, outside the parent's layout.
        [node.view removeFromSuperview];
        [rootView.superview addSubview:node.view];
        if (![modals containsObject:node]) [modals addObject:node];
    } else if (op == 4 || op == 5) {
        UiHostNode *parentNode = nodes[@(parent)];
        UIView *parentView = parentNode ? parentNode.view : rootView;
        YGNodeRef parentYoga = parentNode ? parentNode.yoga : rootYoga;
        YGNodeRef oldParent = YGNodeGetParent(node.yoga);
        if (oldParent) YGNodeRemoveChild(oldParent, node.yoga);
        [node.view removeFromSuperview];
        // Yoga's index counts only our nodes; a UIScrollView also holds its indicators, so
        // the view goes below its next sibling rather than at the same index.
        UiHostNode *next = before ? nodes[@(before)] : nil;
        size_t index = YGNodeGetChildCount(parentYoga);
        if (next) {
            for (size_t i = 0; i < YGNodeGetChildCount(parentYoga); ++i) {
                if (YGNodeGetChild(parentYoga, i) == next.yoga) { index = i; break; }
            }
            [parentView insertSubview:node.view belowSubview:next.view];
        } else [parentView addSubview:node.view];
        YGNodeInsertChild(parentYoga, node.yoga, index);
    } else if (op == 6) {
        YGNodeRef parentYoga = YGNodeGetParent(node.yoga);
        if (parentYoga) YGNodeRemoveChild(parentYoga, node.yoga);
        if (!removeScreen(node) && !insideLeaving(node.view)) [node.view removeFromSuperview];
        ui_grid_free(node.yoga);
        [modals removeObject:node];
        [nodes removeObjectForKey:@(id)];
    } else if (op == 7 || op == 8) {
        node.listening = op == 7;
        if (![node.view isKindOfClass:UIScrollView.class]) node.view.userInteractionEnabled = op == 7;
    }
    else { fprintf(stderr, "ui host: unknown op %d\n", op); abort(); }
}

extern "C" void ui_host_ready(void);
static UIView *splashView;
static bool splashHeld;
static void hideSplash(void);
// Commands: 1 an alert (title, message, buttons), 2 open a URL.
static void ui_host_command(int request, int command, int count, const char **strings) {
    if (command == 1 && count >= 2) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@(strings[0]) message:@(strings[1])
                                                                 preferredStyle:UIAlertControllerStyleAlert];
        __block bool answered = false;
        void (^answer)(int) = ^(int index) {
            if (answered) return;
            answered = true;
            shownAlert = nil; answerAlert = nil;
            ui_dispatch_alert(request, index);
        };
        for (int i = 2; i < count; i++) {
            int index = i - 2;
            [alert addAction:[UIAlertAction actionWithTitle:@(strings[i]) style:UIAlertActionStyleDefault
                                                    handler:^(UIAlertAction *) { answer(index); }]];
        }
        shownAlert = alert;
        answerAlert = answer;
        // Presented after the commit: the controller may be mid-layout now.
        dispatch_async(dispatch_get_main_queue(), ^{
            [rootView.window.rootViewController presentViewController:alert animated:NO completion:nil];
        });
    } else if (command == 2 && count >= 1) {
        // ui_open_settings's "app-settings:" is UIKit's own settings URL.
        NSString *text = strcmp(strings[0], "app-settings:") == 0 ? UIApplicationOpenSettingsURLString : @(strings[0]);
        NSURL *url = [NSURL URLWithString:text];
        if (url) [UIApplication.sharedApplication openURL:url options:@{} completionHandler:nil];
    } else if (command == 4) {
        [rootView.window endEditing:YES];
    } else if (command == 5 && count >= 2) {
        NSMutableArray *items = [NSMutableArray new];
        if (strings[0][0]) [items addObject:@(strings[0])];
        NSURL *url = strings[1][0] ? [NSURL URLWithString:@(strings[1])] : nil;
        if (url) [items addObject:url];
        UIActivityViewController *sheet = [[UIActivityViewController alloc] initWithActivityItems:items applicationActivities:nil];
        UIViewController *top = rootView.window.rootViewController;
        sheet.popoverPresentationController.sourceView = top.view;
        dispatch_async(dispatch_get_main_queue(), ^{ [top presentViewController:sheet animated:YES completion:nil]; });
    } else if (command == 6 && count >= 1) {
        UIPasteboard.generalPasteboard.string = @(strings[0]);
    } else if (command == 7) {
        NSString *text = UIPasteboard.generalPasteboard.string ?: @"";
        ui_dispatch_answer(request, text.UTF8String);
    } else if (command == 8 && count >= 1) {
        int kind = atoi(strings[0]);
        if (kind == 0) [[UISelectionFeedbackGenerator new] selectionChanged];
        else if (kind <= 3) {
            const UIImpactFeedbackStyle styles[] = {UIImpactFeedbackStyleLight, UIImpactFeedbackStyleMedium, UIImpactFeedbackStyleHeavy};
            [[[UIImpactFeedbackGenerator alloc] initWithStyle:styles[kind - 1]] impactOccurred];
        } else {
            const UINotificationFeedbackType types[] = {UINotificationFeedbackTypeSuccess, UINotificationFeedbackTypeWarning,
                                                        UINotificationFeedbackTypeError};
            [[UINotificationFeedbackGenerator new] notificationOccurred:types[std::min(kind - 4, 2)]];
        }
    } else if (command == 11 && count >= 1) {
        if (strings[0][0] == '1') splashHeld = splashView != nil;
        else hideSplash();
    } else if (command == 9 && count >= 1) {
        UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, @(strings[0]));
    } else if (command == 10 && count >= 6) {
        // A fetch through NSURLSession; the answer comes back on the main queue.
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
        // FetchRequest.save_to: a 2xx body goes to the file whole.
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

// The splash (ui.toml [splash]: Info.plist UiSplashBackground and the bundle's ui-splash.png)
// over the app until its first commit, or until ui_splash_hide() when the app held it.
static void showSplash(UIView *over) {
    NSString *hexText = [NSBundle.mainBundle objectForInfoDictionaryKey:@"UiSplashBackground"];
    unsigned rgb = 0;
    bool hasColor = hexText.length == 7 && [[NSScanner scannerWithString:[hexText substringFromIndex:1]] scanHexInt:&rgb];
    splashView = [[UIView alloc] initWithFrame:over.bounds];
    splashView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    splashView.backgroundColor = hasColor ? [UIColor colorWithRed:(rgb >> 16) / 255.0 green:((rgb >> 8) & 255) / 255.0
                                                             blue:(rgb & 255) / 255.0 alpha:1] : UIColor.systemBackgroundColor;
    NSString *path = [NSBundle.mainBundle pathForResource:@"ui-splash" ofType:@"png"];
    NSData *data = path ? [NSData dataWithContentsOfFile:path] : nil;
    // A third of its pixels in points, as the launch screen shows it.
    UIImage *image = data ? [UIImage imageWithData:data scale:3] : nil;
    if (image) {
        UIImageView *imageView = [[UIImageView alloc] initWithImage:image];
        imageView.center = CGPointMake(over.bounds.size.width / 2, over.bounds.size.height / 2);
        imageView.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin |
            UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin;
        [splashView addSubview:imageView];
    }
    [over addSubview:splashView];
}

static void hideSplash(void) {
    UIView *view = splashView;
    splashView = nil;
    splashHeld = false;
    if (!view) return;
    [UIView animateWithDuration:0.2 animations:^{ view.alpha = 0; } completion:^(BOOL done) { [view removeFromSuperview]; }];
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
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    ui_host_ready();
    slideScreens();
    if (splashView && !splashHeld) hideSplash();
}

static void relayout(void) {
    ui_grid_calculate(rootYoga, rootView.bounds.size.width, rootView.bounds.size.height, YGDirectionLTR);
    place(rootYoga, rootView);
    for (UiHostNode *modal in modals) {
        CGRect screen = rootView.superview.bounds;
        modal.view.frame = screen;
        ui_grid_calculate(modal.yoga, screen.size.width, screen.size.height, YGDirectionLTR);
        place(modal.yoga, modal.view);
    }
}

extern "C" void ui_host_ready(void) {
    auto start = std::chrono::steady_clock::now();
    relayout();
    commitTime += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    commitCount++;
    if (testCompletion) {
        void (^completion)(void) = testCompletion;
        testCompletion = nil;
        dispatch_async(dispatch_get_main_queue(), completion);
    }
}

static UIView *findView(Class kind, NSString *text) {
    for (UiHostNode *node in nodes.allValues) {
        if (![node.view isKindOfClass:kind]) continue;
        if (!text) return node.view;
        NSString *shown = [node.view isKindOfClass:UIButton.class] ? [(UIButton *)node.view titleForState:UIControlStateNormal]
                        : [node.view isKindOfClass:UILabel.class] ? ((UILabel *)node.view).text : ((UITextField *)node.view).text;
        if ([shown isEqualToString:text]) return node.view;
    }
    return nil;
}

// Type a task into the TODO's field, press Add, and look for the new row; frames come from
// the display link, so each step waits a little.
static void runTodoTest(void) {
    ui_start(1);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        UITextField *field = (UITextField *)findView(UITextField.class, nil);
        bool ok = field && [field.accessibilityLabel isEqualToString:@"New task"] && [field.placeholder isEqualToString:@"New task"];
        field.text = @"Write the docs";
        [field sendActionsForControlEvents:UIControlEventEditingChanged];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [(UIButton *)findView(UIButton.class, @"Add") sendActionsForControlEvents:UIControlEventTouchUpInside];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                bool added = findView(UILabel.class, @"Write the docs") != nil && field.text.length == 0;
                UIImageView *logo = (UIImageView *)findView(UIImageView.class, nil);
                bool image = logo.image != nil && [logo.accessibilityLabel isEqualToString:@"Eskiu"] &&
                             logo.frame.size.width == 48;
                bool pass = ok && added && image;
                printf("iOS todo: %s label=%d added=%d image=%d\n", pass ? "PASS" : "FAIL", ok, added, image);
                fflush(stdout);
                ui_stop();
                ui_grid_free(rootYoga);
                exit(pass ? 0 : 1);
            });
        });
    });
}

// Places the root: inside the safe area and above the keyboard, unless the app draws edge
// to edge; either way the raw insets go to the app.
// The native example (scene 6): ticks from its clock module, its UISlider's events and the
// value prop sent back; prints the result and exits.
static void runNativeTest(void) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        int ticks = 0;
        UISlider *slider = nil;
        for (UiHostNode *node in nodes.allValues) {
            if ([node.view isKindOfClass:UILabel.class] && [((UILabel *)node.view).text hasPrefix:@"Ticks: "]) {
                ticks = [((UILabel *)node.view).text substringFromIndex:7].intValue;
            }
            if ([node.nativeView isKindOfClass:UISlider.class]) slider = (UISlider *)node.nativeView;
        }
        bool initial = slider && lroundf(slider.value) == 30 && findView(UILabel.class, @"Volume: 30");
        slider.value = 64;
        [slider sendActionsForControlEvents:UIControlEventValueChanged];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            bool changed = findView(UILabel.class, @"Volume: 64") != nil;
            [(UIButton *)findView(UIButton.class, @"Mute") sendActionsForControlEvents:UIControlEventTouchUpInside];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                bool muted = findView(UILabel.class, @"Volume: 0") != nil && lroundf(slider.value) == 0;
                bool pass = ticks >= 1 && initial && changed && muted;
                printf("iOS native: %s ticks=%d initial=%d changed=%d muted=%d\n", pass ? "PASS" : "FAIL", ticks, initial,
                       changed, muted);
                fflush(stdout);
                exit(pass ? 0 : 1);
            });
        });
    });
}

@interface UiController : UIViewController
@property(nonatomic) CGFloat keyboard;
@end

// The system's appearance, accessibility settings and language, to the core.
static void reportSystem(UITraitCollection *traits) {
    ui_report_appearance(traits.userInterfaceStyle == UIUserInterfaceStyleDark ? 1 : 0);
    int scale = (int)std::lround([UIFontMetrics.defaultMetrics scaledValueForValue:100]);
    ui_report_accessibility(UIAccessibilityIsVoiceOverRunning() ? 1 : 0, UIAccessibilityIsReduceMotionEnabled() ? 1 : 0, scale);
    NSString *language = NSLocale.preferredLanguages.firstObject ?: @"en";
    bool rtl = [NSLocale characterDirectionForLanguage:language] == NSLocaleLanguageDirectionRightToLeft;
    ui_report_locale(language.UTF8String, rtl ? 1 : 0);
}

@implementation UiController
- (void)traitCollectionDidChange:(UITraitCollection *)previous {
    [super traitCollectionDidChange:previous];
    if (rootView) reportSystem(self.traitCollection);
}
- (void)systemChanged:(NSNotification *)note { reportSystem(self.traitCollection); }
- (UIStatusBarStyle)preferredStatusBarStyle {
    return barStyle == 1 ? UIStatusBarStyleLightContent : barStyle == 2 ? UIStatusBarStyleDarkContent : UIStatusBarStyleDefault;
}
- (BOOL)prefersStatusBarHidden { return barHidden; }
- (void)viewDidLoad {
    [super viewDidLoad];
    // The left edge swipe is back, as Android's back button.
    UIScreenEdgePanGestureRecognizer *edge = [[UIScreenEdgePanGestureRecognizer alloc] initWithTarget:self
                                                                                               action:@selector(edgeBack:)];
    edge.edges = UIRectEdgeLeft;
    [self.view addGestureRecognizer:edge];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(keyboardChanged:)
        name:UIKeyboardWillChangeFrameNotification object:nil];
    for (NSNotificationName name in @[UIAccessibilityVoiceOverStatusDidChangeNotification,
                                      UIAccessibilityReduceMotionStatusDidChangeNotification,
                                      UIContentSizeCategoryDidChangeNotification, NSCurrentLocaleDidChangeNotification]) {
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(systemChanged:) name:name object:nil];
    }
}
// The back swipe, interactive: the top screen follows the finger over the one below; let go
// past a third of the width (or flicked) and the app goes back (the screen slides out from
// where it is), short of that it returns. Without screens, a swipe is back as on Android.
static UiHostNode *swipeTop, *swipeBelow;
- (void)edgeBack:(UIScreenEdgePanGestureRecognizer *)gesture {
    UIGestureRecognizerState state = gesture.state;
    if (state == UIGestureRecognizerStateBegan) {
        swipeTop = swipeBelow = nil;
        for (UiHostNode *node in nodes.allValues) {
            if (node.kind != 14 || !node.wasActive || !node.view.superview) continue;
            if (!node.view.hidden && YGNodeStyleGetDisplay(node.yoga) != YGDisplayNone) swipeTop = node;
        }
        if (swipeTop) {
            NSArray<UIView *> *siblings = swipeTop.view.superview.subviews;
            NSUInteger at = [siblings indexOfObject:swipeTop.view];
            for (UiHostNode *node in nodes.allValues) {
                if (node.kind != 14 || node == swipeTop || node.view.superview != swipeTop.view.superview) continue;
                NSUInteger index = [siblings indexOfObject:node.view];
                if (index < at && (!swipeBelow || index > [siblings indexOfObject:swipeBelow.view])) swipeBelow = node;
            }
        }
        if (!swipeBelow) { swipeTop = nil; return; }
        swipeBelow.view.hidden = NO;
        return;
    }
    if (!swipeTop) {
        if (state == UIGestureRecognizerStateEnded) ui_report_back();
        return;
    }
    UIView *top = swipeTop.view, *below = swipeBelow.view;
    CGFloat x = std::max<CGFloat>(0, [gesture translationInView:top.superview].x);
    if (state == UIGestureRecognizerStateChanged) {
        top.transform = CGAffineTransformMakeTranslation(x, 0);
        return;
    }
    bool back = state == UIGestureRecognizerStateEnded &&
                (x > top.bounds.size.width / 3 || [gesture velocityInView:top.superview].x > 500);
    swipeTop = swipeBelow = nil;
    if (back && ui_report_back()) return;
    [UIView animateWithDuration:0.25 delay:0 options:UIViewAnimationOptionCurveEaseOut
                     animations:^{ top.transform = CGAffineTransformIdentity; }
                     completion:^(BOOL done) { if (top.superview && !top.hidden) below.hidden = YES; }];
}
- (void)keyboardChanged:(NSNotification *)note {
    CGRect end = [self.view convertRect:[note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue] fromView:nil];
    self.keyboard = std::max<CGFloat>(0, CGRectGetMaxY(self.view.bounds) - CGRectGetMinY(end));
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
    if (self.keyboard > 0 && !ui_edge_to_edge()) revealFocused();
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (!rootView) return;
    UIEdgeInsets safe = self.view.safeAreaInsets;
    CGRect frame = self.view.bounds;
    if (!ui_edge_to_edge()) {
        frame = UIEdgeInsetsInsetRect(frame, safe);
        frame.size.height -= std::max<CGFloat>(0, self.keyboard - safe.bottom);
    }
    rootView.frame = frame;
    relayout();
    ui_report_insets(safe.top, safe.right, safe.bottom, safe.left, self.keyboard);
    reportSystem(self.traitCollection);
    ui_report_window(self.view.bounds.size.width, self.view.bounds.size.height,
                     (int)std::lround(UIScreen.mainScreen.scale * 100));
}
@end

static NSString *roleOf(UIView *view) {
    if ([view isKindOfClass:UIButton.class]) return @"button";
    if ([view isKindOfClass:UISwitch.class]) return @"switch";
    if ([view isKindOfClass:UISlider.class]) return @"slider";
    if ([view isKindOfClass:UITextField.class]) return @"textfield";
    if ([view isKindOfClass:UIControl.class]) return @"button";
    if ([view isKindOfClass:UIActivityIndicatorView.class]) return @"progressbar";
    if ([view isKindOfClass:UIImageView.class]) return @"image";
    if ([view isKindOfClass:UIScrollView.class]) return @"scroll";
    if ([view isKindOfClass:UILabel.class]) return @"text";
    return @"group";
}

// The conformance scene: frames relative to the scene, roles, then the handler log after
// two taps and typing "abc" (tools/conformance.py compares them).
static void runConformance(void) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        UIView *suite = nil;
        for (UiHostNode *node in nodes.allValues) if ([node.view.accessibilityLabel isEqualToString:@"Suite"]) suite = node.view;
        NSMutableDictionary *frames = [NSMutableDictionary new], *roles = [NSMutableDictionary new];
        UIButton *tap = nil;
        UITextField *field = nil;
        for (UiHostNode *node in nodes.allValues) {
            UIView *view = node.view;
            NSString *label = [view isKindOfClass:UIButton.class] ? [(UIButton *)view titleForState:UIControlStateNormal] : view.accessibilityLabel;
            if (!label.length) continue;
            CGRect r = [view convertRect:view.bounds toView:suite];
            auto round1 = [](CGFloat v) { return @(std::round(v * 10) / 10); };
            frames[label] = @[round1(r.origin.x), round1(r.origin.y), round1(r.size.width), round1(r.size.height)];
            roles[label] = roleOf(view);
            if ([label isEqualToString:@"Tap"]) tap = (UIButton *)view;
            if ([label isEqualToString:@"Field"]) field = (UITextField *)view;
        }
        // What was drawn: colors as rrggbbaa, sizes in points.
        auto hex = [](UIColor *color) {
            CGFloat r = 0, g = 0, b = 0, a = 0;
            [color getRed:&r green:&g blue:&b alpha:&a];
            return [NSString stringWithFormat:@"%02x%02x%02x%02x", (int)std::lround(r * 255), (int)std::lround(g * 255),
                    (int)std::lround(b * 255), (int)std::lround(a * 255)];
        };
        UIView *painted = nil;
        UILabel *title = nil;
        UIControl *press = nil;
        UISwitch *toggle = nil;
        UISlider *volume = nil;
        UIView *moved = nil;
        for (UiHostNode *node in nodes.allValues) {
            if ([node.view.accessibilityLabel isEqualToString:@"Volume"]) volume = (UISlider *)node.view;
            if ([node.view.accessibilityLabel isEqualToString:@"Moved"]) moved = node.view;
            NSString *label = node.view.accessibilityLabel;
            if ([label isEqualToString:@"Painted"]) painted = node.view;
            if ([label isEqualToString:@"Title"]) title = (UILabel *)node.view;
            if ([label isEqualToString:@"Press"]) press = (UIControl *)node.view;
            if ([label isEqualToString:@"Toggle"]) toggle = (UISwitch *)node.view;
        }
        NSNumber *weight = [title.font.fontDescriptor objectForKey:UIFontDescriptorTraitsAttribute][UIFontWeightTrait];
        int weightValue = 400;
        const CGFloat weights[] = {UIFontWeightUltraLight, UIFontWeightThin, UIFontWeightLight, UIFontWeightRegular,
            UIFontWeightMedium, UIFontWeightSemibold, UIFontWeightBold, UIFontWeightHeavy, UIFontWeightBlack};
        for (int i = 0; i < 9; ++i) if (std::fabs(weights[i] - weight.doubleValue) < 0.01) weightValue = (i + 1) * 100;
        NSDictionary *styles = @{
            @"Painted": @{@"background": hex(painted.backgroundColor), @"radius": @(painted.layer.cornerRadius),
                          @"border": @(painted.layer.borderWidth),
                          @"borderColor": hex([UIColor colorWithCGColor:painted.layer.borderColor]),
                          @"opacity": @(std::round(painted.alpha * 100) / 100)},
            @"Title": @{@"color": hex(title.textColor), @"fontSize": @(title.font.pointSize), @"fontWeight": @(weightValue)},
            @"Moved": @{@"translateX": @(moved.transform.tx), @"translateY": @(moved.transform.ty),
                        @"scale": @(std::round(std::hypot(moved.transform.a, moved.transform.b) * 100) / 100),
                        @"rotate": @(std::round(std::atan2(moved.transform.b, moved.transform.a) * 180 / M_PI))},
        };
        [tap sendActionsForControlEvents:UIControlEventTouchUpInside];
        [tap sendActionsForControlEvents:UIControlEventTouchUpInside];
        field.text = @"abc";
        [field sendActionsForControlEvents:UIControlEventEditingChanged];
        [press sendActionsForControlEvents:UIControlEventTouchUpInside];
        [toggle setOn:YES animated:NO];
        [toggle sendActionsForControlEvents:UIControlEventValueChanged];
        volume.value = 70;
        [volume sendActionsForControlEvents:UIControlEventValueChanged];
        [(UIButton *)findView(UIButton.class, @"Copy") sendActionsForControlEvents:UIControlEventTouchUpInside];
        UIScrollView *feed = nil;
        for (UiHostNode *node in nodes.allValues) if ([node.view.accessibilityLabel isEqualToString:@"Feed"]) feed = (UIScrollView *)node.view;
        UIButton *ask = (UIButton *)findView(UIButton.class, @"Ask");
        UIButton *openSheet = (UIButton *)findView(UIButton.class, @"Open sheet");
        __block NSDictionary *modalReport = nil;
        [ask sendActionsForControlEvents:UIControlEventTouchUpInside];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        // The alert: answer its second button.
        if (shownAlert) { [shownAlert dismissViewControllerAnimated:NO completion:nil]; answerAlert(1); }
        [openSheet sendActionsForControlEvents:UIControlEventTouchUpInside];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        UiHostNode *modal = modals.lastObject;
        UIView *sheet = nil;
        for (UiHostNode *node in nodes.allValues) if ([node.view.accessibilityLabel isEqualToString:@"Sheet"]) sheet = node.view;
        if (modal && sheet) {
            CGRect cover = [modal.view convertRect:modal.view.bounds toView:nil];
            CGRect box = [sheet convertRect:sheet.bounds toView:nil];
            CGSize window = rootView.window.bounds.size;
            modalReport = @{@"cover": @[@(cover.origin.x), @(cover.origin.y), @(cover.size.width), @(cover.size.height)],
                            @"window": @[@(window.width), @(window.height)],
                            @"sheet": @[@(box.origin.x), @(box.origin.y), @(box.size.width), @(box.size.height)]};
        }
        [(UIButton *)findView(UIButton.class, @"Close") sendActionsForControlEvents:UIControlEventTouchUpInside];
        __block NSMutableArray *refresh = [NSMutableArray new];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        // Pull the feed: what the control sends when the user pulls; "Loaded" hides it.
        [feed.refreshControl sendActionsForControlEvents:UIControlEventValueChanged];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        [refresh addObject:@(feed.refreshControl.refreshing)];
        [(UIButton *)findView(UIButton.class, @"Loaded") sendActionsForControlEvents:UIControlEventTouchUpInside];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        [refresh addObject:@(feed.refreshControl.refreshing)];
        // Scroll the feed: rows near 2000 points mount, rows from the top go.
        [feed setContentOffset:CGPointMake(0, 2000) animated:NO];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            auto round1 = [](CGFloat v) { return @(std::round(v * 10) / 10); };
            NSMutableDictionary *scrolled = [NSMutableDictionary new];
            for (UiHostNode *node in nodes.allValues) {
                NSString *label = node.view.accessibilityLabel;
                if (!([label hasPrefix:@"Row "] || [label isEqualToString:@"Feed"])) continue;
                // Rows are compared where they show: content coordinates minus the offset.
                CGRect r = [node.view convertRect:node.view.bounds toView:suite];
                scrolled[label] = @[round1(r.origin.x), round1(r.origin.y), round1(r.size.width), round1(r.size.height)];
            }
            NSDictionary *result = @{@"status": @"DONE", @"frames": frames, @"roles": roles, @"styles": styles,
                                     @"scrolled": scrolled, @"modal": modalReport ?: [NSNull null], @"refresh": refresh,
                                     @"log": [NSString stringWithUTF8String:ui_conformance_log()]};
            NSData *json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil];
            printf("CONFORMANCE %s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
            fflush(stdout);
            ui_stop();
            exit(0);
        });
        });
        });
        });
        });
        });
    });
}

@interface UiDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation UiDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    // Native code without constructors (Swift): the classes `ui build` lists, each one's +setup.
    for (NSString *name in [NSBundle.mainBundle objectForInfoDictionaryKey:@"UiNativeClasses"]) {
        Class type = NSClassFromString(name);
        if ([type respondsToSelector:@selector(setup)]) [type performSelector:@selector(setup)];
    }
    return YES;
}
@end

#ifdef UI_RELOAD
// `ui serve`'s builds: UI_RELOAD_FILE names a file holding the path of the app's newest dylib.
// When it changes, the app starts again on the new code in the running process: the old core
// saves its routes and signals and stops, the views, timers and nodes it left go, and the new
// one starts on what the old one saved.
static NSString *loadedCore;
static UIViewController *rootController;

static NSString *newestCore(void) {
    const char *file = getenv("UI_RELOAD_FILE");
    if (!file) return nil;
    NSString *text = [NSString stringWithContentsOfFile:@(file) encoding:NSUTF8StringEncoding error:nil];
    return [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

static void restartCore(NSString *path) {
#ifdef UI_TEST
    // What the old core keeps across the reload (src/core/reload.esk): its routes and signals.
    char *kept = ui_reload_save ? strdup(ui_reload_save()) : nullptr;
#endif
    ui_stop();
#ifdef UI_TEST
    // The new core starts on the real clock, until the driver takes it again.
    driverClock(false, 0, false);
#endif
    [frameLink invalidate];
    frameLink = nil;
    [clockLink invalidate];
    clockLink = nil;
    for (NSTimer *timer in timers.allValues) [timer invalidate];
    [timers removeAllObjects];
    if (shownAlert) [shownAlert dismissViewControllerAnimated:NO completion:nil];
    shownAlert = nil;
    swipeTop = swipeBelow = nil;
    for (UiHostNode *node in nodes.allValues) {
        [node.view removeFromSuperview];
        ui_grid_free(node.yoga);
    }
    [nodes removeAllObjects];
    [modals removeAllObjects];
    [leaving removeAllObjects];
    for (UIView *view in rootView.subviews.copy) [view removeFromSuperview];
    if (!loadCore(path.UTF8String)) {
#ifdef UI_TEST
        free(kept);
#endif
        return;
    }
    loadedCore = path;
    NSString *data = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    ui_report_data_dir(data.UTF8String);
#ifdef UI_TEST
    if (kept && ui_reload_restore) ui_reload_restore(kept);
    free(kept);
#endif
    ui_start(launchExample);
    [rootController.view setNeedsLayout];
    [rootController.view layoutIfNeeded];
    fprintf(stderr, "ui: restarted on %s\n", path.lastPathComponent.UTF8String);
}
#endif

#ifdef UI_TEST
// The driver (hosts/driver.h, docs/agents.md). While it owns the clock the window's layer is
// stopped (speed 0) and its time is the driver's (timeOffset), so the views' animations move
// only when the clock does; `clock settle` moves it past their ends.
static bool clockFrozen;
static CFTimeInterval clockBase;
static void driverClockTo(double now) {
    if (clockFrozen && rootView.window) rootView.window.layer.timeOffset = clockBase + now / 1000.0;
}
static void driverClock(bool owned, double now, bool settle) {
    CALayer *layer = rootView.window.layer;
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
            UIView *view = node.view;
            if (!view) continue;
            CGRect r = [view convertRect:view.bounds toView:nil];
            bool shown = view.window != nil;
            for (UIView *up = view; up && shown; up = up.superview) if (up.hidden || up.alpha <= 0) shown = false;
            snprintf(line, sizeof line, "%d %.1f %.1f %.1f %.1f %d\n", node.nodeId, r.origin.x, r.origin.y, r.size.width,
                     r.size.height, shown ? 1 : 0);
            rows += line;
        }
        return rows;
    }
    int id = (int)driverNumber(d, "node");
    UiHostNode *node = nodes[@(id)];
    if (!node || !node.view) return "error: #" + std::to_string(id) + " has no view";
    UIView *view = node.view;
    if (op == "tap") {
        if ([view isKindOfClass:UISwitch.class]) {
            UISwitch *toggle = (UISwitch *)view;
            [toggle setOn:!toggle.on animated:NO];
            [toggle sendActionsForControlEvents:UIControlEventValueChanged];
        } else if ([view isKindOfClass:UITextField.class]) [view becomeFirstResponder];
        else if (node.kind == 12) ui_dispatch(id, node.generation);
        else if ([view isKindOfClass:UIControl.class]) [(UIControl *)view sendActionsForControlEvents:UIControlEventTouchUpInside];
        else return "error: the view takes no press";
        return "";
    }
    if (op == "type") {
        if (![view isKindOfClass:UITextField.class]) return "error: not a text field";
        UITextField *field = (UITextField *)view;
        [field becomeFirstResponder];
        field.text = @(driverText(d, "text").c_str());
        [field sendActionsForControlEvents:UIControlEventEditingChanged];
        return "";
    }
    if (op == "scroll") {
        if (![view isKindOfClass:UIScrollView.class]) return "error: not a scroll view";
        UIScrollView *scroll = (UIScrollView *)view;
        CGFloat most = std::max<CGFloat>(0, scroll.contentSize.height - scroll.bounds.size.height);
        CGFloat y = std::min(most, std::max<CGFloat>(0, scroll.contentOffset.y + driverNumber(d, "dy")));
        [scroll setContentOffset:CGPointMake(scroll.contentOffset.x, y) animated:NO];
        return std::to_string((int)std::lround(y));
    }
    return "error: the iOS host does not know " + op;
}
#endif

@interface UiSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation UiSceneDelegate
- (void)sceneDidBecomeActive:(UIScene *)scene { ui_report_active(1); }
- (void)sceneWillResignActive:(UIScene *)scene { ui_report_active(0); }
- (void)scene:(UIScene *)scene openURLContexts:(NSSet<UIOpenURLContext *> *)contexts {
    ui_report_link(contexts.anyObject.URL.absoluteString.UTF8String);
}
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    nodes = [NSMutableDictionary new];
#ifdef UI_TEST
    // The driver: the app's output kept for `logs` (and still printed), and its socket.
    int printed = dup(2);
    driverCapture([printed](const char *bytes, size_t n) { (void)!write(printed, bytes, n); });
    const char *agentPort = getenv("UI_AGENT_PORT");
    int port = agentPort ? atoi(agentPort) : 8766;
    if (!driverListenTcp(port)) fprintf(stderr, "ui: the driver could not listen on 127.0.0.1:%d\n", port);
#endif
    modals = [NSMutableArray new];
#ifdef UI_RELOAD
    loadedCore = newestCore();
    if (!loadedCore || !loadCore(loadedCore.UTF8String)) { fprintf(stderr, "ui: no app dylib (UI_RELOAD_FILE)\n"); exit(1); }
    // The pid next to the reload file: `ui run ios` restarts a running app instead of launching it.
    NSString *pidFile = [[@(getenv("UI_RELOAD_FILE")) stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"pid"];
    [[NSString stringWithFormat:@"%d", getpid()] writeToFile:pidFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
    // Every 50 ms: a new build shows within a frame or three of being written.
    [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *t) {
        NSString *path = newestCore();
        if (path.length && ![path isEqualToString:loadedCore]) restartCore(path);
    }];
#endif
    // The app's data directory, before the app starts (ui_data_dir).
    NSString *data = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    [NSFileManager.defaultManager createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
    ui_report_data_dir(data.UTF8String);
    // A link the app was launched with waits for the app to start (ui_link).
    NSURL *launchURL = options.URLContexts.anyObject.URL;
    if (launchURL) ui_report_link(launchURL.absoluteString.UTF8String);
    else if (launchLink) ui_report_link(launchLink);
    events = [UiEvents new];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UiController *controller = [UiController new];
#ifdef UI_RELOAD
    rootController = controller;
#endif
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    rootView = [[UIView alloc] initWithFrame:controller.view.bounds];
    [controller.view addSubview:rootView];
    rootYoga = YGNodeNew();
    YGNodeStyleSetFlexDirection(rootYoga, YGFlexDirectionColumn);
    showSplash(controller.view);
    ui_start(conformance ? 3 : nativeTest ? 6 : launchExample);
    if (conformance) runConformance();
    if (nativeTest) runNativeTest();
    if (pressLabel) {
        NSString *label = @(pressLabel);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            for (UiHostNode *node in nodes.allValues) {
                if (![node.view isKindOfClass:UIControl.class]) continue;
                NSString *title = [node.view isKindOfClass:UIButton.class] ? [(UIButton *)node.view titleForState:UIControlStateNormal] : nil;
                if ([node.view.accessibilityLabel isEqualToString:label] || [title isEqualToString:label]) {
                    [(UIControl *)node.view sendActionsForControlEvents:UIControlEventTouchUpInside];
                    break;
                }
            }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                ui_report_back();
            });
        });
    }
    if (selfTest) {
        dispatch_async(dispatch_get_main_queue(), ^{
            UIButton *button = nil;
            for (UiHostNode *node in nodes.allValues) {
                if ([node.view isKindOfClass:UIButton.class]) button = (UIButton *)node.view;
            }
            testCompletion = ^{
            bool found = false;
            for (UiHostNode *node in nodes.allValues) {
                if ([node.view isKindOfClass:UILabel.class] && [((UILabel *)node.view).text isEqualToString:@"Count: 100"]) {
                    found = true;
                    printf("label=%s frame=%.1f,%.1f,%.1f,%.1f\n", ((UILabel *)node.view).text.UTF8String,
                           node.view.frame.origin.x, node.view.frame.origin.y, node.view.frame.size.width, node.view.frame.size.height);
                }
            }
            std::sort(frameTimes.begin(), frameTimes.end());
            UIEdgeInsets safe = rootView.superview.safeAreaInsets;
            bool inside = safe.top > 0 && rootView.frame.origin.y == safe.top;
            printf("safe top=%.0f root y=%.0f\n", safe.top, rootView.frame.origin.y);
            found = found && inside && commitCount == 2 && frameTimes.size() == 1;
            printf("iOS counter: %s events=100 commits=%d layout_total_ms=%.3f frame_p95_ms=%.3f\n",
                   found ? "PASS" : "FAIL", commitCount, commitTime,
                   frameTimes.empty() ? 0 : frameTimes[frameTimes.size() * 95 / 100]);
            fflush(stdout);
            ui_stop();
            if (!found) { ui_grid_free(rootYoga); exit(1); }
            runTodoTest();
            };
            for (int i = 0; i < 100; ++i) [button sendActionsForControlEvents:UIControlEventTouchUpInside];
        });
    }
}
@end

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--self-test")) selfTest = true;
        if (!strcmp(argv[i], "--conformance")) conformance = true;
        if (!strcmp(argv[i], "--native-test")) nativeTest = true;
        if (!strcmp(argv[i], "--link") && i + 1 < argc) launchLink = argv[i + 1];
        if (!strcmp(argv[i], "--press") && i + 1 < argc) pressLabel = argv[i + 1];
        if (!strcmp(argv[i], "--example") && i + 1 < argc) launchExample = atoi(argv[i + 1]);
    }
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(UiDelegate.class)); }
}
