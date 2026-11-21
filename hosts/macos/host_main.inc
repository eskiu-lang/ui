// host_main.inc: the core's commands, the window and the app. Part of host.mm, which #includes it (one translation unit).

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
    appearModals();
    commitCount++;
}

// A sheet is as big as what the modal holds: laid out in the window's size first, then in
// the box its children take.
static void layoutSheet(UiHostNode *modal) {
    NSSize window = contentView.bounds.size;
    ui_grid_calculate(modal.yoga, window.width, window.height, ui_layout_direction());
    CGFloat right = 0, bottom = 0;
    for (size_t i = 0; i < YGNodeGetChildCount(modal.yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(modal.yoga, i);
        right = std::max(right, (CGFloat)(YGNodeLayoutGetLeft(child) + YGNodeLayoutGetWidth(child)));
        bottom = std::max(bottom, (CGFloat)(YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child)));
    }
    float pad = YGNodeLayoutGetPadding(modal.yoga, YGEdgeRight);
    NSSize size = NSMakeSize(std::max<CGFloat>(1, std::ceil(right + pad)), std::max<CGFloat>(1, std::ceil(bottom + pad)));
    ui_grid_calculate(modal.yoga, size.width, size.height, ui_layout_direction());
    if (!NSEqualSizes(modal.sheet.contentView.bounds.size, size)) [modal.sheet setContentSize:size];
    modal.view.frame = NSMakeRect(0, 0, size.width, size.height);
    place(modal.yoga);
}

static void relayout(void) {
    ui_grid_calculate(rootYoga, rootView.bounds.size.width, rootView.bounds.size.height, ui_layout_direction());
    place(rootYoga);
    for (UiHostNode *modal in modals) {
        if (modal.sheet) { layoutSheet(modal); continue; }
        NSRect screen = contentView.bounds;
        modal.view.frame = screen;
        ui_grid_calculate(modal.yoga, screen.size.width, screen.size.height, ui_layout_direction());
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
- (void)refresh:(id)sender { refreshShown(); }
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
    // Right to left: Yoga mirrors rows, start and end; the views their controls and text.
    if (rtl != ui_layout_rtl()) {
        ui_layout_rtl() = rtl;
        directionChanged();
        relayout();
    }
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
        } else if (isField(view)) [view.window makeFirstResponder:view];
        else if (isTextArea(view)) [view.window makeFirstResponder:((UiTextArea *)view).textView];
        else if (node.kind == 12) ui_dispatch(id, node.generation);
        else if ([view isKindOfClass:NSButton.class]) [(NSButton *)view sendAction:((NSButton *)view).action to:((NSButton *)view).target];
        else if ([view isKindOfClass:UiPressable.class]) [view accessibilityPerformPress];
        else return "error: the view takes no press";
        return "";
    }
    if (op == "type") {
        if (isTextArea(view)) {
            UiTextView *area = ((UiTextArea *)view).textView;
            [area.window makeFirstResponder:area];
            area.string = @(driverText(d, "text").c_str());
            [NSNotificationCenter.defaultCenter postNotificationName:NSTextDidChangeNotification object:area];
            return "";
        }
        if (!isField(view)) return "error: not a text field";
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
    menuItem(view, @"Refresh", @selector(refresh:), @"r", 0);
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
