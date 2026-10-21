// Phase-0 host glue. Components and state stay in Eskiu; UIKit owns native views.
#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
#include <yoga/Yoga.h>
#include "../commit.h"
#include "ui_native.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

extern "C" void ui_start(int example);
extern "C" void ui_dispatch(int id, int generation);
extern "C" void ui_dispatch_text(int id, int generation, const char *text);
extern "C" void ui_dispatch_focus(int id, int generation, int focused);
extern "C" void ui_dispatch_toggle(int id, int generation, int on);
extern "C" void ui_dispatch_scroll(int id, int generation, int offset, int viewport);
extern "C" void ui_dispatch_alert(int request, int choice);
extern "C" void ui_dispatch_refresh(int id, int generation);
extern "C" void ui_report_link(const char *url);
extern "C" void ui_report_window(int width, int height, int scale);
extern "C" int ui_edge_to_edge(void);
extern "C" void ui_report_insets(int top, int right, int bottom, int left, int keyboard);
extern "C" void ui_report_active(int active);
extern "C" const char *ui_conformance_log(void);
extern "C" void ui_tick(void);
extern "C" void ui_stop(void);
extern "C" void ui_dispatch_native(const char *channel, const char *payload);
extern "C" int ui_report_back(void);
extern "C" void ui_dispatch_layout(int id, int generation, int x, int y, int width, int height);
extern "C" void ui_dispatch_view(int id, int generation, const char *name, const char *payload);

@interface UiHostNode : NSObject
@property(nonatomic, strong) UIView *view;
@property(nonatomic) YGNodeRef yoga;
@property(nonatomic) int generation;
@property(nonatomic) CGFloat fontSize;
@property(nonatomic) int fontWeight;
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
@property(nonatomic) CGRect reportedFrame;
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

static void applyFont(UiHostNode *node) {
    UIFont *font = [UIFont systemFontOfSize:node.fontSize > 0 ? node.fontSize : 18 weight:weightOf(node.fontWeight)];
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
    view.image = [UIImage imageWithContentsOfFile:[NSBundle.mainBundle pathForResource:source ofType:nil]];
    YGNodeMarkDirty(yoga);
}

static void place(YGNodeRef yoga, UIView *parent) {
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        // A hidden screen keeps its frame (it stays under a push's slide).
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(child);
        node.view.frame = CGRectMake(YGNodeLayoutGetLeft(child), YGNodeLayoutGetTop(child),
                                     YGNodeLayoutGetWidth(child), YGNodeLayoutGetHeight(child));
        if (node.layoutListening) {
            CGRect frame = CGRectMake(std::lround(YGNodeLayoutGetLeft(child)), std::lround(YGNodeLayoutGetTop(child)),
                                      std::lround(YGNodeLayoutGetWidth(child)), std::lround(YGNodeLayoutGetHeight(child)));
            if (!CGRectEqualToRect(frame, node.reportedFrame)) {
                node.reportedFrame = frame;
                ui_dispatch_layout(node.nodeId, node.generation, (int)frame.origin.x, (int)frame.origin.y,
                                   (int)frame.size.width, (int)frame.size.height);
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

@interface UiEvents : NSObject <UIScrollViewDelegate>
- (void)press:(UIControl *)sender;
- (void)edit:(UITextField *)sender;
- (void)toggle:(UISwitch *)sender;
- (void)refresh:(UIRefreshControl *)sender;
- (void)focus:(UITextField *)sender;
- (void)blur:(UITextField *)sender;
- (void)frame:(CADisplayLink *)sender;
@end
@implementation UiEvents
- (void)press:(UIControl *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch((int)sender.tag, node.generation);
}
- (void)scrollViewDidScroll:(UIScrollView *)scroll {
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
@end
static UiEvents *events;

extern "C" void ui_host_schedule(void) {
    if (frameLink) return;
    frameLink = [CADisplayLink displayLinkWithTarget:events selector:@selector(frame:)];
    [frameLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
extern "C" void ui_host_cancel(void) { [frameLink invalidate]; frameLink = nil; }

extern "C" void ui_host_apply(int op, int id, int generation, int parent,
                               int before, int kind, const char *text) {
    if (op == 1) {
        UiHostNode *node = [UiHostNode new];
        node.nodeId = id;
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
            UIControl *control = [UIControl new];
            control.tag = id;
            control.isAccessibilityElement = YES;
            control.accessibilityTraits = UIAccessibilityTraitButton;
            [control addTarget:events action:@selector(press:) forControlEvents:UIControlEventTouchUpInside];
            node.view = control;
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
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
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
        } else if (kind == 13) {
            node.view = [UIView new];
            node.view.clipsToBounds = YES;
        } else if (kind == 6) {
            UIScrollView *scroll = [UIScrollView new];
            scroll.tag = id;
            scroll.delegate = events;
            node.view = scroll;
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetOverflow(node.yoga, YGOverflowScroll);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        } else {
            node.view = [UIView new];
            YGNodeStyleSetFlexDirection(node.yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        }
        YGNodeSetContext(node.yoga, (__bridge void *)node);
        if ((kind >= 3 && kind <= 5) || kind == 7 || kind == 9 || kind == 10) YGNodeSetMeasureFunc(node.yoga, measure);
        nodes[@(id)] = node;
        return;
    }
    UiHostNode *node = nodes[@(id)];
    if (!node) { if (op == 6) return; fprintf(stderr, "ui host: op %d on unknown node %d\n", op, id); abort(); }
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
        } else if (kind == 6) YGNodeStyleSetFlexGrow(node.yoga, before);
        else if (kind == 7) YGNodeStyleSetFlexWrap(node.yoga, before ? YGWrapWrap : YGWrapNoWrap);
        else if (kind == 8) node.view.backgroundColor = colorOf(before);
        else if (kind == 9) applyColor(node, colorOf(before));
        else if (kind == 10) { node.fontSize = before; applyFont(node); }
        else if (kind == 11) { node.fontWeight = before; applyFont(node); }
        else if (kind == 12) { node.view.layer.borderWidth = before; YGNodeStyleSetBorder(node.yoga, YGEdgeAll, before); }
        else if (kind == 13) node.view.layer.borderColor = colorOf(before).CGColor;
        else if (kind == 14) { node.view.layer.cornerRadius = before; node.view.clipsToBounds = before > 0; }
        else if (kind == 15) node.view.alpha = before / 100.0;
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
        else if (kind == 30) { node.layoutListening = before != 0; node.reportedFrame = CGRectNull; }
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
        YGNodeFree(node.yoga);
        [modals removeObject:node];
        [nodes removeObjectForKey:@(id)];
    } else if (op == 7 || op == 8) {
        if ([node.view isKindOfClass:UIScrollView.class]) node.listening = op == 7;
        else node.view.userInteractionEnabled = op == 7;
    }
    else { fprintf(stderr, "ui host: unknown op %d\n", op); abort(); }
}

extern "C" void ui_host_ready(void);
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
        NSURL *url = [NSURL URLWithString:@(strings[0])];
        if (url) [UIApplication.sharedApplication openURL:url options:@{} completionHandler:nil];
    }
}

extern "C" void ui_host_commit(const uint8_t *data, int len) {
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    ui_host_ready();
    slideScreens();
}

static void relayout(void) {
    YGNodeCalculateLayout(rootYoga, rootView.bounds.size.width, rootView.bounds.size.height, YGDirectionLTR);
    place(rootYoga, rootView);
    for (UiHostNode *modal in modals) {
        CGRect screen = rootView.superview.bounds;
        modal.view.frame = screen;
        YGNodeCalculateLayout(modal.yoga, screen.size.width, screen.size.height, YGDirectionLTR);
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
                YGNodeFree(rootYoga);
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
@implementation UiController
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
}
- (void)edgeBack:(UIScreenEdgePanGestureRecognizer *)gesture {
    if (gesture.state == UIGestureRecognizerStateEnded) ui_report_back();
}
- (void)keyboardChanged:(NSNotification *)note {
    CGRect end = [self.view convertRect:[note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue] fromView:nil];
    self.keyboard = std::max<CGFloat>(0, CGRectGetMaxY(self.view.bounds) - CGRectGetMinY(end));
    [self.view setNeedsLayout];
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
    ui_report_window(self.view.bounds.size.width, self.view.bounds.size.height,
                     (int)std::lround(UIScreen.mainScreen.scale * 100));
}
@end

static NSString *roleOf(UIView *view) {
    if ([view isKindOfClass:UIButton.class]) return @"button";
    if ([view isKindOfClass:UISwitch.class]) return @"switch";
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
        for (UiHostNode *node in nodes.allValues) {
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
        };
        [tap sendActionsForControlEvents:UIControlEventTouchUpInside];
        [tap sendActionsForControlEvents:UIControlEventTouchUpInside];
        field.text = @"abc";
        [field sendActionsForControlEvents:UIControlEventEditingChanged];
        [press sendActionsForControlEvents:UIControlEventTouchUpInside];
        [toggle setOn:YES animated:NO];
        [toggle sendActionsForControlEvents:UIControlEventValueChanged];
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
    modals = [NSMutableArray new];
    // A link the app was launched with waits for the app to start (ui_link).
    NSURL *launchURL = options.URLContexts.anyObject.URL;
    if (launchURL) ui_report_link(launchURL.absoluteString.UTF8String);
    else if (launchLink) ui_report_link(launchLink);
    events = [UiEvents new];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UiController *controller = [UiController new];
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    rootView = [[UIView alloc] initWithFrame:controller.view.bounds];
    [controller.view addSubview:rootView];
    rootYoga = YGNodeNew();
    YGNodeStyleSetFlexDirection(rootYoga, YGFlexDirectionColumn);
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
            if (!found) { YGNodeFree(rootYoga); exit(1); }
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
