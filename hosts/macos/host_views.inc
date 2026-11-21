// host_views.inc: layout, events, timers and the node views. Part of host.mm, which #includes it (one translation unit).

static void place(YGNodeRef yoga) {
    for (size_t i = 0; i < YGNodeGetChildCount(yoga); ++i) {
        YGNodeRef child = YGNodeGetChild(yoga, i);
        if (YGNodeStyleGetDisplay(child) == YGDisplayNone) continue;
        UiHostNode *node = (__bridge UiHostNode *)YGNodeGetContext(child);
        CGFloat x = YGNodeLayoutGetLeft(child), y = YGNodeLayoutGetTop(child);
        CGFloat w = YGNodeLayoutGetWidth(child), h = YGNodeLayoutGetHeight(child);
        bool animated = moveAnimated(node, NSMakeRect(x, y, w, h));
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        if (!animated) node.view.frame = NSMakeRect(x, y, w, h);
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
    if (owner && [owner.view isKindOfClass:NSScrollView.class] && !isTextArea(owner.view)) {
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
            node.autocorrect = 1;
            node.view = makeInput(node);
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
            node.view.accessibilityElement = YES;
            node.view.accessibilityRole = NSAccessibilityButtonRole;
            YGNodeStyleSetFlexDirection(node.yoga, YGFlexDirectionColumn);
            YGNodeStyleSetGap(node.yoga, YGGutterAll, 8);
        } else if (kind == 15) {
            NSSlider *slider = [UiSlider sliderWithTarget:events action:@selector(slide:)];
            slider.continuous = YES;
            node.view = slider;
        } else if (kind == 9) {
            NSSwitch *toggle = [UiSwitch new];
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
        if (ui_layout_rtl()) { applyDirection(node); applyLabel(node); }
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
        if (accessProp(node, kind, before) || inputProp(node, kind, before) || styleProp(node, kind, before)) {}
        else if (kind == 1) YGNodeStyleSetGap(node.yoga, YGGutterAll, before);
        else if (kind == 2) {
            YGNodeStyleSetPadding(node.yoga, YGEdgeAll, before);
            node.padding = before;
            if (isField(node.view) || isTextArea(node.view)) applyStyled(node);
        } else if (kind == 3) {
            // A text area's focus is its text view's; a field's is its field editor's.
            NSView *target = isTextArea(node.view) ? ((UiTextArea *)node.view).textView : node.view;
            NSResponder *first = node.view.window.firstResponder;
            if (before) [node.view.window makeFirstResponder:target];
            else if (first == target || (first && [first isKindOfClass:NSTextView.class] &&
                     (__bridge void *)((NSTextView *)first).delegate == (__bridge void *)node.view))
                [node.view.window makeFirstResponder:nil];
        } else if (kind == 4) {
            if (before) YGNodeStyleSetWidth(node.yoga, before); else YGNodeStyleSetWidthAuto(node.yoga);
        } else if (kind == 5) {
            if (before) YGNodeStyleSetHeight(node.yoga, before); else YGNodeStyleSetHeightAuto(node.yoga);
        } else if (kind == 6) {
            YGNodeStyleSetFlexGrow(node.yoga, before);
            if (![node.view isKindOfClass:NSScrollView.class] && !node.shrinkSet) YGNodeStyleSetFlexShrink(node.yoga, before > 0 ? 1 : 0);
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
        else if (kind == 9) { node.textColor = before; applyColor(node); applyAccent(node); }
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
            applyMasks(node);
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
            applyMasks(node);
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
            node.labelled = value != nil;
            node.view.accessibilityLabel = value;
            if (isTextArea(node.view)) ((UiTextArea *)node.view).textView.accessibilityLabel = value;
            if (node.kind == 1 || node.kind == 2 || node.kind == 13) {
                node.view.accessibilityElement = value != nil;
                node.baseElement = value != nil;
            }
            if (node.access) applyAccess(node);
        } else if (kind == 5) { node.fontFile = value; applyFont(node); }
        else if (kind == 2) { node.placeholder = value; applyPlaceholder(node); }
        else if (accessText(node, kind, value)) {}
        else if (kind == 3 && [node.view isKindOfClass:UiImageView.class]) loadImage(node, value);
    } else if (op == 3) {
        NSString *value = [NSString stringWithUTF8String:text ? text : ""];
        if ([node.view isKindOfClass:NSButton.class]) { node.title = value; applyButtonTitle(node); }
        else if (node.kind == 5) setInputText(node.view, value);
        else if (node.kind == 3) { node.text = value; applyLabel(node); }
        YGNodeMarkDirty(node.yoga);
    } else if ((op == 4 || op == 5) && node.kind == 12) {
        // A Modal is a sheet; one with a background of its own (an Overlay: popovers, menus)
        // is a layer over the window's content, outside the parent's layout.
        node.parentId = parent;
        if (disabledCount) refreshEnabled(node);
        if ([modals containsObject:node]) return;
        [modals addObject:node];
        // A ModalWith's sheet or dialog (prop 89) is laid out by the core over the window too.
        if (!node.background && !node.presentation) showSheet(node);
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
        restack(parentView);
        // Under a disabled element, what comes in is disabled too.
        node.parentId = parent;
        if (disabledCount) refreshEnabled(node);
    } else if (op == 6) {
        if (node.disabled) disabledCount--;
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
