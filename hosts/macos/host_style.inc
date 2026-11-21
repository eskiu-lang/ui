// host_style.inc: fonts, text colors, a styled control's look, images, gradients and blur. Part of host.mm, which #includes it (one translation unit).

// An image, fitted in its box or covering it (cropped), smoothed or pixelated.
@interface UiImageView : NSImageView
@property(nonatomic) bool cover;
@property(nonatomic) bool pixelated;
@property(nonatomic, copy) NSString *source;
@end
@implementation UiImageView
- (BOOL)wantsUpdateLayer { return NO; }
- (void)drawRect:(NSRect)dirty {
    NSImage *image = self.image;
    if (!image) return;
    NSSize size = image.size, box = self.bounds.size;
    if (size.width <= 0 || size.height <= 0) return;
    CGFloat sx = box.width / size.width, sy = box.height / size.height;
    CGFloat scale = self.cover ? std::max(sx, sy) : std::min(sx, sy);
    NSRect rect = NSMakeRect((box.width - size.width * scale) / 2, (box.height - size.height * scale) / 2,
                             size.width * scale, size.height * scale);
    [NSGraphicsContext.currentContext saveGraphicsState];
    NSRectClip(self.bounds);
    NSGraphicsContext.currentContext.imageInterpolation = self.pixelated ? NSImageInterpolationNone : NSImageInterpolationHigh;
    [image drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    [NSGraphicsContext.currentContext restoreGraphicsState];
}
@end

static NSFontWeight weightOf(int weight) {
    if (weight <= 0) return NSFontWeightRegular;
    const NSFontWeight weights[] = {NSFontWeightUltraLight, NSFontWeightThin, NSFontWeightLight, NSFontWeightRegular,
        NSFontWeightMedium, NSFontWeightSemibold, NSFontWeightBold, NSFontWeightHeavy, NSFontWeightBlack};
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
        if (postScript && (CTFontManagerRegisterGraphicsFont(cgFont, &error) || [NSFont fontWithName:(__bridge NSString *)postScript size:12]))
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

// The default text size, as the web host's.
static const CGFloat kTextSize = 16;

static NSFont *fontOf(UiHostNode *node) {
    CGFloat size = node.fontSize > 0 ? node.fontSize : kTextSize;
    NSString *custom = node.fontFile ? fontName(node.fontFile) : nil;
    NSFont *font = custom ? [NSFont fontWithName:custom size:size] : nil;
    font = font ?: [NSFont systemFontOfSize:size weight:weightOf(node.fontWeight)];
    return node.italic ? [NSFontManager.sharedFontManager convertFont:font toHaveTrait:NSItalicFontMask] : font;
}

// A button's title: the system's for a push button, else in the node's font and color.
static void applyButtonTitle(UiHostNode *node) {
    NSButton *button = (NSButton *)node.view;
    NSString *title = node.title ?: @"";
    if (!node.styled && !node.textColor && !node.fontSize && !node.fontWeight && !node.fontFile) {
        button.title = title;
        return;
    }
    NSMutableParagraphStyle *paragraph = [NSMutableParagraphStyle new];
    paragraph.alignment = NSTextAlignmentCenter;
    NSColor *color = colorOf(node.textColor) ?: (node.styled ? NSColor.labelColor : NSColor.controlTextColor);
    NSFont *font = node.styled || node.fontSize ? fontOf(node) : [NSFont systemFontOfSize:NSFont.systemFontSize weight:weightOf(node.fontWeight)];
    button.attributedTitle = [[NSAttributedString alloc] initWithString:title attributes:@{
        NSFontAttributeName: font, NSForegroundColorAttributeName: color, NSParagraphStyleAttributeName: paragraph}];
}

// A field or a button the app gives a background or a border loses the system's bezel.
static void applyStyled(UiHostNode *node) {
    bool styled = node.background || node.borderWidth || node.radius;
    if (isTextArea(node.view)) {
        UiTextArea *area = (UiTextArea *)node.view;
        area.borderType = styled ? NSNoBorder : NSBezelBorder;
        area.textView.drawsBackground = !styled;
        area.textView.textContainerInset = styled ? NSMakeSize(node.padding, node.padding) : NSMakeSize(2, 4);
        node.styled = styled;
        YGNodeMarkDirty(node.yoga);
    } else if (isField(node.view)) {
        NSTextField *field = (NSTextField *)node.view;
        field.bezeled = !styled;
        field.bordered = !styled;
        field.drawsBackground = !styled;
        field.focusRingType = styled ? NSFocusRingTypeNone : NSFocusRingTypeDefault;
        ((UiFieldCell *)field.cell).inset = styled ? node.padding : 0;
        node.styled = styled;
        YGNodeMarkDirty(node.yoga);
    } else if ([node.view isKindOfClass:NSButton.class]) {
        NSButton *button = (NSButton *)node.view;
        button.bordered = !styled;
        node.styled = styled;
        applyButtonTitle(node);
        YGNodeMarkDirty(node.yoga);
    }
}

static void applyFont(UiHostNode *node) {
    if ([node.view isKindOfClass:NSButton.class]) applyButtonTitle(node);
    else if ([node.view isKindOfClass:NSTextField.class]) ((NSTextField *)node.view).font = fontOf(node);
    else if (isTextArea(node.view)) ((UiTextArea *)node.view).textView.font = fontOf(node);
    applyLabel(node);
    YGNodeMarkDirty(node.yoga);
}

static void applyColor(UiHostNode *node) {
    NSColor *color = colorOf(node.textColor);
    if ([node.view isKindOfClass:NSButton.class]) applyButtonTitle(node);
    else if ([node.view isKindOfClass:NSTextField.class]) ((NSTextField *)node.view).textColor = color ?: NSColor.labelColor;
    else if (isTextArea(node.view)) ((UiTextArea *)node.view).textView.textColor = color ?: NSColor.labelColor;
    applyLabel(node);
}

// A bundled resource by file name, or an http(s) URL fetched in the background. The image
// changes the node's measured size, so the layout runs again when it arrives. Whether it
// loaded goes to the app when it listens (prop 84), after the commit that set it.
static void imageLoaded(UiHostNode *node, NSString *source, bool loaded) {
    if (!node.imageListening) return;
    int id = node.nodeId, generation = node.generation;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (nodes[@(id)] == node) ui_dispatch_image(id, generation, loaded ? 1 : 0);
    });
}
static void loadImage(UiHostNode *node, NSString *source) {
    UiImageView *view = (UiImageView *)node.view;
    YGNodeRef yoga = node.yoga;
    view.image = nil;
    view.source = source;
    if (!source) return;
    if ([source hasPrefix:@"http://"] || [source hasPrefix:@"https://"]) {
        NSURL *url = [NSURL URLWithString:source];
        [[NSURLSession.sharedSession dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *r, NSError *e) {
            NSImage *image = data ? [[NSImage alloc] initWithData:data] : nil;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (view.source != source) return;
                view.image = image; [view setNeedsDisplay:YES]; YGNodeMarkDirty(yoga); relayout();
                imageLoaded(node, source, image != nil);
            });
        }] resume];
        return;
    }
    NSString *path = [source hasPrefix:@"file://"] ? [NSURL URLWithString:source].path
                   : [source hasPrefix:@"/"] ? source : [NSBundle.mainBundle pathForResource:source ofType:nil];
    view.image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    [view setNeedsDisplay:YES];
    YGNodeMarkDirty(yoga);
    imageLoaded(node, source, view.image != nil);
}

static void applyGradient(UiHostNode *node) {
    if (!node.gradientTo) { [node.gradient removeFromSuperlayer]; node.gradient = nil; return; }
    if (!node.gradient) {
        node.gradient = [CAGradientLayer layer];
        [node.view.layer insertSublayer:node.gradient atIndex:0];
        node.gradient.frame = node.view.bounds;
    }
    NSColor *from = node.background ? colorOf(node.background) : NSColor.clearColor;
    node.gradient.colors = @[(id)from.CGColor, (id)colorOf(node.gradientTo).CGColor];
    // The layer is flipped with its view: y grows down, as CSS's angles assume.
    double a = node.gradientAngle * M_PI / 180.0;
    node.gradient.startPoint = CGPointMake(0.5 - sin(a) / 2, 0.5 + cos(a) / 2);
    node.gradient.endPoint = CGPointMake(0.5 + sin(a) / 2, 0.5 - cos(a) / 2);
}

// A backdrop blur: the system material nearest the radius (they follow the appearance).
static void applyBlur(UiHostNode *node, int radius) {
    if (radius <= 0) {
        [node.blur removeFromSuperview];
        node.blur = nil;
        if (!node.gradient) node.view.layer.backgroundColor = colorOf(node.background).CGColor;
        return;
    }
    if (!node.blur) {
        node.blur = [NSVisualEffectView new];
        node.blur.blendingMode = NSVisualEffectBlendingModeWithinWindow;
        node.blur.state = NSVisualEffectStateActive;
        node.blur.frame = node.view.bounds;
        node.blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        node.view.autoresizesSubviews = YES;
        [node.view addSubview:node.blur positioned:NSWindowBelow relativeTo:nil];
    }
    node.blur.material = radius <= 10 ? NSVisualEffectMaterialHUDWindow : radius <= 20 ? NSVisualEffectMaterialPopover
                       : radius <= 35 ? NSVisualEffectMaterialMenu : NSVisualEffectMaterialUnderWindowBackground;
    if (!node.gradient) node.view.layer.backgroundColor = colorOf(node.background).CGColor;
}

// The inner view of a box with Style.clip and a shadow (applyMasks): it clips the children to
// the box's radius while the box's own layer, unclipped, draws the shadow. A point on it and
// on no child is the box's.
@interface UiClipView : NSView
@end
@implementation UiClipView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (NSView *)hitTest:(NSPoint)point {
    NSView *hit = [super hitTest:point];
    return hit == self ? nil : hit;
}
@end

// The view children go into: a scroll's document view, the inner view of a box that clips
// and has a shadow, else the node's own.
static NSView *container(UiHostNode *node) {
    if ([node.view isKindOfClass:NSScrollView.class]) return ((NSScrollView *)node.view).documentView;
    return node.clipView ?: node.view;
}

// The inner view and the gradient of a box that clips and has a shadow follow its bounds and
// its radius (the box's layer does not clip them).
static void applyInnerRadius(UiHostNode *node) {
    CGFloat radius = node.view.layer.cornerRadius;
    if (node.clipView) {
        node.clipView.frame = node.view.bounds;
        node.clipView.layer.cornerRadius = radius;
    }
    if (node.gradient) {
        node.gradient.cornerRadius = radius;
        node.gradient.masksToBounds = node.clipView != nil;
    }
}

