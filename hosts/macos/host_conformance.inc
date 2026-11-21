// host_conformance.inc: the host's own run of the conformance scene (tests/conformance built
// with UI_TEST=1, started with UI_CONFORMANCE=1): what the driver's ops cannot reach, read from
// AppKit (each labeled element's role, what the layers drew, the modal's cover) after AppKit's
// own input (a control's action, a field's edit, the alert's answer, a pull past the feed's
// top, Escape). It prints one line, CONFORMANCE and a JSON object, which tools/conformance.py
// compares with the other hosts'. Part of host.mm, which #includes it in a test build only.

static UiHostNode *labeled(NSString *label) {
    for (UiHostNode *node in nodes.allValues) {
        NSString *name = [node.view isKindOfClass:NSButton.class] && node.kind == 4 ? node.title : node.view.accessibilityLabel;
        if ([name isEqualToString:label]) return node;
    }
    return nil;
}

// The role a labeled element has for assistive technologies, in the suite's words.
static NSString *conformanceRole(NSView *view) {
    NSString *role = view.accessibilityRole, *subrole = view.accessibilitySubrole;
    // A cell-based control speaks through its cell.
    if ((!role || [role isEqualToString:NSAccessibilityUnknownRole]) && [view isKindOfClass:NSControl.class]) {
        role = ((NSControl *)view).cell.accessibilityRole;
        subrole = ((NSControl *)view).cell.accessibilitySubrole;
    }
    if ([view isKindOfClass:NSSwitch.class] || [subrole isEqualToString:NSAccessibilitySwitchSubrole]) return @"switch";
    if ([role isEqualToString:NSAccessibilityButtonRole]) return @"button";
    if ([role isEqualToString:NSAccessibilitySliderRole]) return @"slider";
    if ([role isEqualToString:NSAccessibilityProgressIndicatorRole]) return @"progressbar";
    if ([role isEqualToString:NSAccessibilityImageRole]) return @"image";
    if ([role isEqualToString:NSAccessibilityScrollAreaRole]) return @"scroll";
    if ([role isEqualToString:NSAccessibilityTextFieldRole]) return @"textfield";
    if ([role isEqualToString:NSAccessibilityStaticTextRole]) return @"text";
    if ([role isEqualToString:NSAccessibilityGroupRole]) return @"group";
    return role ?: @"none";
}

static NSString *hexOf(CGColorRef cg) {
    NSColor *color = cg ? [[NSColor colorWithCGColor:cg] colorUsingColorSpace:NSColorSpace.sRGBColorSpace] : nil;
    if (!color) return @"00000000";
    return [NSString stringWithFormat:@"%02x%02x%02x%02x", (int)std::lround(color.redComponent * 255),
            (int)std::lround(color.greenComponent * 255), (int)std::lround(color.blueComponent * 255),
            (int)std::lround(color.alphaComponent * 255)];
}

static NSArray *boxIn(NSView *view, NSView *space) {
    NSRect r = [view convertRect:view.bounds toView:space];
    auto one = [](CGFloat v) { return @(std::round(v * 10) / 10); };
    return @[one(r.origin.x), one(r.origin.y), one(r.size.width), one(r.size.height)];
}

static void runConformance(void) {
    __block NSMutableDictionary *frames = [NSMutableDictionary new], *roles = [NSMutableDictionary new], *styles = nil;
    __block NSDictionary *modalReport = nil;
    __block NSMutableArray *refresh = [NSMutableArray new];
    NSArray<void (^)(void)> *steps = @[
        ^{
            NSView *suite = labeled(@"Suite").view;
            for (UiHostNode *node in nodes.allValues) {
                NSString *label = node.kind == 4 ? node.title : node.view.accessibilityLabel;
                if (!label.length || !node.view.window) continue;
                frames[label] = boxIn(node.view, suite);
                roles[label] = conformanceRole(node.kind == 5 && isTextArea(node.view) ? ((UiTextArea *)node.view).textView : node.view);
            }
            CALayer *painted = labeled(@"Painted").view.layer;
            NSTextField *title = (NSTextField *)labeled(@"Title").view;
            UiHostNode *moved = labeled(@"Moved");
            CATransform3D t = moved.view.layer.transform;
            NSSize size = moved.view.bounds.size;
            NSNumber *weight = [title.font.fontDescriptor objectForKey:NSFontTraitsAttribute][NSFontWeightTrait];
            int weightValue = 400;
            const CGFloat weights[] = {NSFontWeightUltraLight, NSFontWeightThin, NSFontWeightLight, NSFontWeightRegular,
                NSFontWeightMedium, NSFontWeightSemibold, NSFontWeightBold, NSFontWeightHeavy, NSFontWeightBlack};
            for (int i = 0; i < 9; ++i) if (std::fabs(weights[i] - weight.doubleValue) < 0.01) weightValue = (i + 1) * 100;
            // The translation is where the transform takes the view's center.
            CGFloat cx = size.width / 2, cy = size.height / 2;
            styles = [@{
                @"Painted": @{@"background": hexOf(painted.backgroundColor), @"radius": @(painted.cornerRadius),
                              @"border": @(painted.borderWidth), @"borderColor": hexOf(painted.borderColor),
                              @"opacity": @(std::round(labeled(@"Painted").view.alphaValue * 100) / 100)},
                @"Title": @{@"color": hexOf(title.textColor.CGColor), @"fontSize": @(title.font.pointSize), @"fontWeight": @(weightValue)},
                @"Moved": @{@"translateX": @(std::round(t.m11 * cx + t.m21 * cy + t.m41 - cx)),
                            @"translateY": @(std::round(t.m12 * cx + t.m22 * cy + t.m42 - cy)),
                            @"scale": @(std::round(std::hypot(t.m11, t.m12) * 100) / 100),
                            @"rotate": @(std::round(std::atan2(t.m12, t.m11) * 180 / M_PI))},
            } mutableCopy];
            NSButton *tap = (NSButton *)labeled(@"Tap").view;
            [tap sendAction:tap.action to:tap.target];
            [tap sendAction:tap.action to:tap.target];
            NSTextField *field = (NSTextField *)labeled(@"Field").view;
            field.stringValue = @"abc";
            [NSNotificationCenter.defaultCenter postNotificationName:NSControlTextDidChangeNotification object:field];
        },
        ^{
            [labeled(@"Press").view accessibilityPerformPress];
            NSSwitch *toggle = (NSSwitch *)labeled(@"Toggle").view;
            toggle.state = NSControlStateValueOn;
            [toggle sendAction:toggle.action to:toggle.target];
        },
        ^{
            NSSlider *volume = (NSSlider *)labeled(@"Volume").view;
            volume.doubleValue = 70;
            [volume sendAction:volume.action to:volume.target];
        },
        ^{
            NSButton *copy = (NSButton *)labeled(@"Copy").view;
            [copy sendAction:copy.action to:copy.target];
        },
        ^{
            NSButton *ask = (NSButton *)labeled(@"Ask").view;
            [ask sendAction:ask.action to:ask.target];
        },
        ^{
            // The alert's second button.
            if (shownAlert) [mainWindow endSheet:shownAlert.window returnCode:NSAlertFirstButtonReturn + 1];
        },
        ^{
            NSButton *open = (NSButton *)labeled(@"Open sheet").view;
            [open sendAction:open.action to:open.target];
        },
        ^{
            // A Modal is a sheet on the window: AppKit takes the input of the whole window.
            UiHostNode *modal = modals.lastObject;
            NSView *sheet = labeled(@"Sheet").view;
            if (modal && sheet) {
                NSView *content = mainWindow.contentView;
                NSRect cover = modal.sheet ? content.bounds : [modal.view convertRect:modal.view.bounds toView:nil];
                NSSize window = content.bounds.size;
                modalReport = @{@"cover": @[@(cover.origin.x), @(cover.origin.y), @(cover.size.width), @(cover.size.height)],
                                @"window": @[@(window.width), @(window.height)],
                                @"sheet": boxIn(sheet, sheet.window.contentView)};
            }
            NSButton *close = (NSButton *)labeled(@"Close").view;
            [close sendAction:close.action to:close.target];
        },
        ^{
            NSButton *open = (NSButton *)labeled(@"Open sheet").view;
            [open sendAction:open.action to:open.target];
        },
        ^{
            // Escape in the sheet asks its Modal to close.
            UiHostNode *modal = modals.lastObject;
            if (modal.sheet) [modal.sheet cancelOperation:nil];
            else ui_report_back();
        },
        ^{
            // Pull the feed past its top, as the trackpad's elastic scroll does.
            UiHostNode *feed = labeled(@"Feed");
            pullChecked(feed, -kPull - 1);
            pullChecked(feed, 0);
        },
        ^{
            [refresh addObject:@(labeled(@"Feed").refreshing)];
            NSButton *loaded = (NSButton *)labeled(@"Loaded").view;
            [loaded sendAction:loaded.action to:loaded.target];
        },
        ^{
            [refresh addObject:@(labeled(@"Feed").refreshing)];
            NSScrollView *feed = (NSScrollView *)labeled(@"Feed").view;
            [feed.contentView scrollToPoint:NSMakePoint(0, 2000)];
            [feed reflectScrolledClipView:feed.contentView];
        },
        ^{
            NSView *suite = labeled(@"Suite").view;
            NSMutableDictionary *scrolled = [NSMutableDictionary new];
            for (UiHostNode *node in nodes.allValues) {
                NSString *label = node.view.accessibilityLabel;
                if ([label hasPrefix:@"Row "] || [label isEqualToString:@"Feed"]) scrolled[label] = boxIn(node.view, suite);
            }
            NSDictionary *result = @{@"status": @"DONE", @"frames": frames, @"roles": roles, @"styles": styles ?: @{},
                                     @"scrolled": scrolled, @"modal": modalReport ?: [NSNull null], @"refresh": refresh,
                                     @"log": @(ui_conformance_log())};
            NSData *json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil];
            printf("CONFORMANCE %s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
            fflush(stdout);
            ui_stop();
            exit(0);
        },
    ];
    // One step every 300 ms, after the scene's first frame and its timer.
    for (NSUInteger i = 0; i < steps.count; i++) {
        void (^step)(void) = steps[i];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((0.5 + 0.3 * i) * NSEC_PER_SEC)), dispatch_get_main_queue(), step);
    }
}
