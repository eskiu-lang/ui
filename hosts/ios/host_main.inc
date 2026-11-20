// host_main.inc: the system's settings, the controller and the app. Part of host.mm, which #includes it (one translation unit).

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
- (void)systemChanged:(NSNotification *)note {
    // A new system text size: the app's fonts again at the new scale (Dynamic Type).
    if ([note.name isEqualToString:UIContentSizeCategoryDidChangeNotification]) rescaleFonts();
    reportSystem(self.traitCollection);
}
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
    if ([view isKindOfClass:UITextField.class] || [view isKindOfClass:UITextView.class]) return @"textfield";
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
        } else if (isTextInput(view)) [view becomeFirstResponder];
        else if (node.kind == 12) ui_dispatch(id, node.generation);
        else if ([view isKindOfClass:UIControl.class]) [(UIControl *)view sendActionsForControlEvents:UIControlEventTouchUpInside];
        else return "error: the view takes no press";
        return "";
    }
    if (op == "type") {
        if (!isTextInput(view)) return "error: not a text field";
        [view becomeFirstResponder];
        setInputText(view, @(driverText(d, "text").c_str()));
        if ([view isKindOfClass:UITextView.class]) [events textViewDidChange:(UITextView *)view];
        else [(UITextField *)view sendActionsForControlEvents:UIControlEventEditingChanged];
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
