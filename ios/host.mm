// Phase-0 host glue. Components and state stay in Eskiu; UIKit owns native views.
#import <UIKit/UIKit.h>
#import <QuartzCore/CADisplayLink.h>
#include <yoga/Yoga.h>
#include "../hosts/ui_commit.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

extern "C" void ui_start(int example);
extern "C" void ui_dispatch(int id, int generation);
extern "C" void ui_dispatch_text(int id, int generation, const char *text);
extern "C" void ui_dispatch_focus(int id, int generation, int focused);
extern "C" void ui_tick(void);
extern "C" void ui_stop(void);

@interface UiHostNode : NSObject
@property(nonatomic, strong) UIView *view;
@property(nonatomic) YGNodeRef yoga;
@property(nonatomic) int generation;
@end
@implementation UiHostNode
@end

static NSMutableDictionary<NSNumber *, UiHostNode *> *nodes;
static UIView *rootView;
static YGNodeRef rootYoga;
static int commitCount;
static double commitTime;
static bool selfTest;
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

static void place(YGNodeRef yoga, UIView *parent) {
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(child);
        node.view.frame = CGRectMake(YGNodeLayoutGetLeft(child), YGNodeLayoutGetTop(child),
                                     YGNodeLayoutGetWidth(child), YGNodeLayoutGetHeight(child));
        place(child, node.view);
    }
}

@interface UiEvents : NSObject
- (void)press:(UIButton *)sender;
- (void)edit:(UITextField *)sender;
- (void)focus:(UITextField *)sender;
- (void)blur:(UITextField *)sender;
- (void)frame:(CADisplayLink *)sender;
@end
@implementation UiEvents
- (void)press:(UIButton *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node) ui_dispatch((int)sender.tag, node.generation);
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
        node.generation = generation;
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
        } else {
            node.view = [UIView new];
            YGNodeStyleSetFlexDirection(node.yoga, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        }
        YGNodeSetContext(node.yoga, (__bridge void *)node);
        if (kind >= 3) YGNodeSetMeasureFunc(node.yoga, measure);
        nodes[@(id)] = node;
        return;
    }
    UiHostNode *node = nodes[@(id)];
    if (!node) { if (op == 6) return; fprintf(stderr, "ui host: op %d on unknown node %d\n", op, id); abort(); }
    if (op == 2) {
        // Props: the code in kind (1 gap, 2 padding, 3 focused), the value in before.
        if (kind == 1) YGNodeStyleSetGap(node.yoga, YGGutterAll, before);
        else if (kind == 2) YGNodeStyleSetPadding(node.yoga, YGEdgeAll, before);
        else if (kind == 3) {
            if (before) [node.view becomeFirstResponder]; else [node.view resignFirstResponder];
        }
    } else if (op == 9) {
        NSString *value = text && text[0] ? [NSString stringWithUTF8String:text] : nil;
        if (kind == 1) node.view.accessibilityLabel = value;
        else if (kind == 2 && [node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).placeholder = value;
    } else if (op == 3) {
        NSString *value = [NSString stringWithUTF8String:text ? text : ""];
        if ([node.view isKindOfClass:UITextField.class]) {
            UITextField *field = (UITextField *)node.view;
            if (![field.text isEqualToString:value]) field.text = value;
        } else if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).text = value;
        else [(UIButton *)node.view setTitle:value forState:UIControlStateNormal];
        YGNodeMarkDirty(node.yoga);
    } else if (op == 4 || op == 5) {
        UiHostNode *parentNode = nodes[@(parent)];
        UIView *parentView = parentNode ? parentNode.view : rootView;
        YGNodeRef parentYoga = parentNode ? parentNode.yoga : rootYoga;
        YGNodeRef oldParent = YGNodeGetParent(node.yoga);
        if (oldParent) YGNodeRemoveChild(oldParent, node.yoga);
        NSUInteger index = before ? [parentView.subviews indexOfObject:nodes[@(before)].view] : parentView.subviews.count;
        [node.view removeFromSuperview];
        // Recompute the index after removal when moving within a parent.
        index = before ? [parentView.subviews indexOfObject:nodes[@(before)].view] : parentView.subviews.count;
        [parentView insertSubview:node.view atIndex:index];
        YGNodeInsertChild(parentYoga, node.yoga, index);
    } else if (op == 6) {
        YGNodeRef parentYoga = YGNodeGetParent(node.yoga);
        if (parentYoga) YGNodeRemoveChild(parentYoga, node.yoga);
        [node.view removeFromSuperview];
        YGNodeFree(node.yoga);
        [nodes removeObjectForKey:@(id)];
    } else if (op == 7 || op == 8) { node.view.userInteractionEnabled = op == 7; }
    else { fprintf(stderr, "ui host: unknown op %d\n", op); abort(); }
}

extern "C" void ui_host_ready(void);
extern "C" void ui_host_commit(const uint8_t *data, int len) {
    ui_commit_each(data, len, ui_host_apply);
    ui_host_ready();
}

extern "C" void ui_host_ready(void) {
    auto start = std::chrono::steady_clock::now();
    YGNodeCalculateLayout(rootYoga, rootView.bounds.size.width, rootView.bounds.size.height, YGDirectionLTR);
    place(rootYoga, rootView);
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
                bool pass = ok && added;
                printf("iOS todo: %s label=%d added=%d\n", pass ? "PASS" : "FAIL", ok, added);
                fflush(stdout);
                ui_stop();
                YGNodeFree(rootYoga);
                exit(pass ? 0 : 1);
            });
        });
    });
}

@interface UiDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation UiDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options { return YES; }
@end

@interface UiSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation UiSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    nodes = [NSMutableDictionary new];
    events = [UiEvents new];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    rootView = [[UIView alloc] initWithFrame:CGRectInset(controller.view.bounds, 12, 80)];
    [controller.view addSubview:rootView];
    rootYoga = YGNodeNew();
    YGNodeStyleSetFlexDirection(rootYoga, YGFlexDirectionColumn);
    ui_start(0);
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
            found = found && commitCount == 2 && frameTimes.size() == 1;
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
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--self-test")) selfTest = true;
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(UiDelegate.class)); }
}
