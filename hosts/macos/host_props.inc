// host_props.inc: props 73 to 91 (padding sides and margins through hosts/layout.h, text
// align, maximum lines, shrink, the image listener, line height, italic, z index, clip, layout
// animation, a modal's presentation and appearing) and a switch's or a slider's accent (prop 9).
// Part of host.mm, which #includes it (one translation unit).

static bool reduceMotion(void) { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion; }

// A switch and a slider in the app's accent (Style.color): the track when on, the filled part
// and the thumb, drawn by the host. AppKit's own (a SwiftUI view inside, on macOS 26) take the
// system's accent only, so while an accent is set that view is hidden and the host draws and
// tracks the mouse.
static void hideSystemLook(NSView *control, bool hide) {
    for (NSView *part in control.subviews) part.hidden = hide;
    control.needsDisplay = YES;
}
static void drawKnob(NSRect dot) {
    NSShadow *shadow = [NSShadow new];
    shadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.3];
    shadow.shadowBlurRadius = 1.5;
    shadow.shadowOffset = NSMakeSize(0, -0.5);
    [NSGraphicsContext saveGraphicsState];
    [shadow set];
    [NSColor.whiteColor setFill];
    [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
    [NSGraphicsContext restoreGraphicsState];
}

@interface UiSwitch : NSSwitch
@property(nonatomic, strong) NSColor *accent;
@end
@implementation UiSwitch
- (BOOL)wantsUpdateLayer { return self.accent == nil && [super wantsUpdateLayer]; }
- (void)setState:(NSControlStateValue)state { [super setState:state]; self.needsDisplay = YES; }
- (void)drawRect:(NSRect)dirty {
    if (!self.accent) { [super drawRect:dirty]; return; }
    for (NSView *part in self.subviews) part.hidden = YES;
    NSRect box = self.bounds;
    CGFloat h = std::min<CGFloat>(box.size.height, 22), w = std::min<CGFloat>(box.size.width, h * 1.75);
    bool on = self.state == NSControlStateValueOn;
    bool rtl = self.userInterfaceLayoutDirection == NSUserInterfaceLayoutDirectionRightToLeft;
    NSRect track = NSMakeRect(rtl ? NSMaxX(box) - w : box.origin.x, box.origin.y + (box.size.height - h) / 2, w, h);
    NSBezierPath *pill = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(track, 0.5, 0.5) xRadius:h / 2 yRadius:h / 2];
    [(on ? self.accent : [NSColor.labelColor colorWithAlphaComponent:0.1]) setFill];
    [pill fill];
    CGFloat knob = h - 4;
    bool right = on != rtl;
    drawKnob(NSMakeRect(right ? NSMaxX(track) - knob - 2 : track.origin.x + 2, track.origin.y + 2, knob, knob));
}
- (void)mouseDown:(NSEvent *)event {
    if (!self.accent) { [super mouseDown:event]; return; }
    if (!self.enabled) return;
    self.state = self.state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn;
    self.needsDisplay = YES;
    [self sendAction:self.action to:self.target];
}
@end

@interface UiSlider : NSSlider
@property(nonatomic, strong) NSColor *accent;
@end
@implementation UiSlider
- (BOOL)wantsUpdateLayer { return self.accent == nil && [super wantsUpdateLayer]; }
- (CGFloat)fraction {
    double span = self.maxValue - self.minValue;
    return span > 0 ? std::min(1.0, std::max(0.0, (self.doubleValue - self.minValue) / span)) : 0;
}
- (bool)rtl { return self.userInterfaceLayoutDirection == NSUserInterfaceLayoutDirectionRightToLeft; }
- (void)setDoubleValue:(double)value { [super setDoubleValue:value]; self.needsDisplay = YES; }
- (void)drawRect:(NSRect)dirty {
    if (!self.accent) { [super drawRect:dirty]; return; }
    for (NSView *part in self.subviews) part.hidden = YES;
    NSRect box = self.bounds;
    CGFloat knob = std::min<CGFloat>(box.size.height, 20), bar = 4, mid = NSMidY(box);
    CGFloat from = box.origin.x + knob / 2, span = box.size.width - knob;
    CGFloat at = from + span * ([self rtl] ? 1 - [self fraction] : [self fraction]);
    [[NSColor.labelColor colorWithAlphaComponent:0.15] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(from, mid - bar / 2, span, bar) xRadius:bar / 2 yRadius:bar / 2] fill];
    [self.accent setFill];
    NSRect filled = [self rtl] ? NSMakeRect(at, mid - bar / 2, from + span - at, bar) : NSMakeRect(from, mid - bar / 2, at - from, bar);
    [[NSBezierPath bezierPathWithRoundedRect:filled xRadius:bar / 2 yRadius:bar / 2] fill];
    drawKnob(NSMakeRect(at - knob / 2 + 1, mid - knob / 2 + 1, knob - 2, knob - 2));
}
- (void)track:(NSEvent *)event {
    NSPoint at = [self convertPoint:event.locationInWindow fromView:nil];
    CGFloat knob = std::min<CGFloat>(self.bounds.size.height, 20), span = std::max<CGFloat>(1, self.bounds.size.width - knob);
    CGFloat f = std::min<CGFloat>(1, std::max<CGFloat>(0, (at.x - knob / 2) / span));
    if ([self rtl]) f = 1 - f;
    self.doubleValue = self.minValue + f * (self.maxValue - self.minValue);
    self.needsDisplay = YES;
    [self sendAction:self.action to:self.target];
}
- (void)mouseDown:(NSEvent *)event {
    if (!self.accent) { [super mouseDown:event]; return; }
    if (!self.enabled) return;
    [self track:event];
    for (;;) {
        NSEvent *next = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        if (next.type == NSEventTypeLeftMouseUp) break;
        [self track:next];
    }
}
@end

static void applyAccent(UiHostNode *node) {
    NSColor *color = colorOf(node.textColor);
    if ([node.view isKindOfClass:UiSwitch.class]) ((UiSwitch *)node.view).accent = color;
    else if ([node.view isKindOfClass:UiSlider.class]) ((UiSlider *)node.view).accent = color;
    else return;
    hideSystemLook(node.view, color != nil);
}

// Style.text_align: the start (the default, as on the web) and the end follow the layout's
// direction.
static NSTextAlignment alignmentOf(int align) {
    bool rtl = ui_layout_rtl();
    if (align == 2) return NSTextAlignmentCenter;
    if (align == 3) return rtl ? NSTextAlignmentLeft : NSTextAlignmentRight;
    if (align == 4) return NSTextAlignmentJustified;
    return rtl ? NSTextAlignmentRight : NSTextAlignmentLeft;
}

// The layout's direction on a view (AppKit's own follows the app's localizations, not the
// system's language), and on every view when it changes.
static void applyDirection(UiHostNode *node) {
    NSUserInterfaceLayoutDirection direction = ui_layout_rtl() ? NSUserInterfaceLayoutDirectionRightToLeft
                                                               : NSUserInterfaceLayoutDirectionLeftToRight;
    if (node.view.userInterfaceLayoutDirection != direction) node.view.userInterfaceLayoutDirection = direction;
    if (isTextArea(node.view)) ((UiTextArea *)node.view).textView.baseWritingDirection =
        ui_layout_rtl() ? NSWritingDirectionRightToLeft : NSWritingDirectionLeftToRight;
}
static void directionChanged(void) {
    for (UiHostNode *node in nodes.allValues) {
        applyDirection(node);
        applyLabel(node);
        if (isField(node.view)) ((NSTextField *)node.view).alignment = alignmentOf(0);
        node.view.needsDisplay = YES;
    }
}

// A label's text with its alignment, its maximum lines (cut with an ellipsis) and its line
// height (the text in the middle of each line, as CSS's line-height puts it).
static void applyLabel(UiHostNode *node) {
    if (node.kind != 3) return;
    NSTextField *label = (NSTextField *)node.view;
    NSString *text = node.text ?: @"";
    label.alignment = alignmentOf(node.textAlign);
    label.maximumNumberOfLines = node.maxLines;
    label.cell.truncatesLastVisibleLine = node.maxLines > 0;
    if (node.lineHeight <= 0) {
        if (label.attributedStringValue.length && [label.attributedStringValue attribute:NSParagraphStyleAttributeName atIndex:0 effectiveRange:nil])
            label.attributedStringValue = [[NSAttributedString alloc] initWithString:@""];
        if (![label.stringValue isEqualToString:text]) label.stringValue = text;
    } else {
        NSFont *font = fontOf(node);
        NSMutableParagraphStyle *paragraph = [NSMutableParagraphStyle new];
        paragraph.alignment = label.alignment;
        paragraph.lineBreakMode = NSLineBreakByWordWrapping;
        paragraph.minimumLineHeight = paragraph.maximumLineHeight = node.lineHeight;
        CGFloat natural = font.ascender - font.descender;
        label.attributedStringValue = [[NSAttributedString alloc] initWithString:text attributes:@{
            NSFontAttributeName: font, NSForegroundColorAttributeName: colorOf(node.textColor) ?: NSColor.labelColor,
            NSParagraphStyleAttributeName: paragraph, NSBaselineOffsetAttributeName: @((node.lineHeight - natural) / 4)}];
    }
    YGNodeMarkDirty(node.yoga);
}

// A text area's line height, for what is typed.
static void applyAreaLineHeight(UiHostNode *node) {
    if (!isTextArea(node.view)) return;
    UiTextView *text = ((UiTextArea *)node.view).textView;
    NSMutableParagraphStyle *paragraph = [NSMutableParagraphStyle new];
    paragraph.minimumLineHeight = paragraph.maximumLineHeight = std::max(0, node.lineHeight);
    text.defaultParagraphStyle = paragraph;
    [text.textStorage addAttribute:NSParagraphStyleAttributeName value:paragraph range:NSMakeRange(0, text.textStorage.length)];
    NSMutableDictionary *typing = [text.typingAttributes mutableCopy];
    typing[NSParagraphStyleAttributeName] = paragraph;
    text.typingAttributes = typing;
    YGNodeMarkDirty(node.yoga);
}

// What a layer clips: Style.clip, else a radius without a shadow (a shadow draws outside).
static void applyMasks(UiHostNode *node) {
    node.view.layer.masksToBounds = node.clip || (node.radius > 0 && !node.shadow);
}

// Whether any element set a z index (until then the children's order is the views').
static bool stacking;

// Siblings by Style.z_index, then in the children's order; views that are no node's (a blur
// view, a native view) stay under them.
static void restack(NSView *parent) {
    if (!parent || !stacking) return;
    UiHostNode *owner = nodeOf(parent);
    YGNodeRef yoga = owner ? owner.yoga : rootYoga;
    [parent sortSubviewsUsingFunction:[](__kindof NSView *a, __kindof NSView *b, void *context) -> NSComparisonResult {
        UiHostNode *na = nodeOf(a), *nb = nodeOf(b);
        if (!na || !nb) return !na && !nb ? NSOrderedSame : !na ? NSOrderedAscending : NSOrderedDescending;
        if (na.zIndex != nb.zIndex) return na.zIndex < nb.zIndex ? NSOrderedAscending : NSOrderedDescending;
        YGNodeRef parentYoga = (YGNodeRef)context;
        size_t ia = 0, ib = 0, n = YGNodeGetChildCount(parentYoga);
        for (size_t i = 0; i < n; i++) {
            YGNodeRef child = YGNodeGetChild(parentYoga, i);
            if (child == na.yoga) ia = i;
            if (child == nb.yoga) ib = i;
        }
        return ia < ib ? NSOrderedAscending : ia > ib ? NSOrderedDescending : NSOrderedSame;
    } context:yoga];
}

// Props 73 to 91; false for any other code.
static bool styleProp(UiHostNode *node, int code, int value) {
    if (ui_layout_prop(node.yoga, code, value)) return true;
    switch (code) {
    case 81: node.textAlign = value; applyLabel(node); return true;
    case 82: node.maxLines = value; applyLabel(node); return true;
    case 83:
        node.shrinkSet = value != 0;
        if (value) YGNodeStyleSetFlexShrink(node.yoga, value);
        else if (![node.view isKindOfClass:NSScrollView.class] || isTextArea(node.view))
            YGNodeStyleSetFlexShrink(node.yoga, YGNodeStyleGetFlexGrow(node.yoga) > 0 ? 1 : 0);
        else YGNodeStyleSetFlexShrink(node.yoga, 1);
        return true;
    case 84: node.imageListening = value != 0; return true;
    case 85: node.lineHeight = value; applyLabel(node); applyAreaLineHeight(node); return true;
    case 86: node.italic = value != 0; applyFont(node); return true;
    case 87: node.zIndex = value; stacking = stacking || value; restack(node.view.superview); return true;
    case 88: node.clip = value != 0; applyMasks(node); return true;
    case 89: node.presentation = value; return true;
    case 90: node.layoutMs = value; return true;
    case 91: node.appear = value; return true;
    }
    return false;
}

// A view's frame, moved there over its layout animation (prop 90) when it was on screen and
// the layout changed it; at once otherwise.
static bool moveAnimated(UiHostNode *node, NSRect frame) {
    NSRect was = node.view.frame;
    if (node.layoutMs <= 0 || !node.view.window || NSEqualRects(was, frame) || NSIsEmptyRect(was) || reduceMotion()) return false;
    NSString *curves[] = {kCAMediaTimingFunctionEaseInEaseOut, kCAMediaTimingFunctionEaseOut, kCAMediaTimingFunctionEaseIn,
                          kCAMediaTimingFunctionLinear};
    CAMediaTimingFunction *curve = node.easing == 4 ? [CAMediaTimingFunction functionWithControlPoints:0.34 :1.56 :0.64 :1]
        : [CAMediaTimingFunction functionWithName:curves[std::min(3, std::max(0, node.easing))]];
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = node.layoutMs / 1000.0;
        context.allowsImplicitAnimation = YES;
        context.timingFunction = curve;
        node.view.animator.frame = frame;
    }];
    return true;
}

// A modal over the window as it opens (props 89 and 91): the layer fades in, a sheet's panel
// slides up from the bottom edge, a dialog's grows a little; nothing when it appears at once
// or the system asks for reduced motion. A Modal (91 = 0) appears as it always did.
static void appearModals(void) {
    for (UiHostNode *modal in modals) {
        if (modal.appeared || modal.sheet || !modal.view.superview) continue;
        modal.appeared = true;
        if (modal.appear != 1 || reduceMotion() || !modal.view.layer) continue;
        CAMediaTimingFunction *curve = [CAMediaTimingFunction functionWithControlPoints:0.2 :0.8 :0.2 :1];
        CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
        fade.fromValue = @0;
        fade.toValue = @1;
        fade.duration = 0.22;
        fade.timingFunction = curve;
        [modal.view.layer addAnimation:fade forKey:@"appear"];
        UiHostNode *panel = nil;
        for (NSView *view in modal.view.subviews) if ((panel = nodeOf(view))) break;
        if (!panel.view.layer || modal.presentation == 0) continue;
        NSSize size = panel.view.bounds.size;
        CABasicAnimation *move = [CABasicAnimation animationWithKeyPath:@"transform"];
        CATransform3D from = modal.presentation == 1 ? CATransform3DMakeTranslation(0, size.height, 0)
            : CATransform3DTranslate(CATransform3DScale(CATransform3DMakeTranslation(size.width / 2, size.height / 2, 0), 0.96, 0.96, 1),
                                     -size.width / 2, -size.height / 2, 0);
        move.fromValue = [NSValue valueWithCATransform3D:from];
        move.toValue = [NSValue valueWithCATransform3D:panel.view.layer.transform];
        move.duration = 0.22;
        move.timingFunction = curve;
        [panel.view.layer addAnimation:move forKey:@"appear"];
    }
}
