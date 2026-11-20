// host_views.inc: the host's calls from the core: scheduling, timers, nodes and native views. Part of host.mm, which #includes it (one translation unit).

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
            node.view = makeTextField(id);
            node.autocorrect = 1;
        } else if (kind == 3) {
            UILabel *label = [UILabel new];
            label.numberOfLines = 0;
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
        // After the measure function: marking the node dirty needs one.
        if (kind == 3 || kind == 4) applyFont(node);
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
        if (accessProp(node, kind, before) || inputProp(node, kind, before)) {}
        else if (kind == 1) YGNodeStyleSetGap(node.yoga, YGGutterAll, before);
        else if (kind == 2) {
            YGNodeStyleSetPadding(node.yoga, YGEdgeAll, before);
            // An input draws its text from its edge: inset it by the padding.
            node.padding = before;
            applyInputPadding(node);
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
        else if (kind == 9) { node.textColor = before; applyColor(node, colorOf(before)); }
        else if (kind == 10) { node.fontSize = before; node.fontSet = true; applyFont(node); }
        else if (kind == 11) { node.fontWeight = before; node.fontSet = true; applyFont(node); }
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
        else if (kind == 1) {
            node.labelled = value != nil;
            node.view.accessibilityLabel = value;
            if (node.access) applyAccess(node);
        } else if (kind == 5) { node.fontFile = value; node.fontSet = true; applyFont(node); }
        else if (kind == 2) { node.placeholder = value; applyPlaceholder(node); }
        else if (accessText(node, kind, value)) {}
        else if (kind == 3 && [node.view isKindOfClass:UIImageView.class]) loadImage((UIImageView *)node.view, node.yoga, value);
    } else if (op == 3) {
        NSString *value = [NSString stringWithUTF8String:text ? text : ""];
        if (isTextInput(node.view)) setInputText(node.view, value);
        else if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).text = value;
        else [(UIButton *)node.view setTitle:value forState:UIControlStateNormal];
        YGNodeMarkDirty(node.yoga);
    } else if ((op == 4 || op == 5) && node.kind == 12) {
        // Over everything, outside the parent's layout.
        node.parentId = parent;
        if (disabledCount) refreshEnabled(node);
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
        // Under a disabled element, what comes in is disabled too.
        node.parentId = parent;
        if (disabledCount) refreshEnabled(node);
    } else if (op == 6) {
        if (node.disabled) disabledCount--;
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
