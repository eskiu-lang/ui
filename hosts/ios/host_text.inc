// host_text.inc: fonts (scaled with the system text size), text colors, and a text input's
// editing options (props 65 to 72): the keyboard, autofill, secure entry, submit on return,
// and a multiline input as a UITextView that grows. Part of host.mm, which #includes it (one
// translation unit).

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

static void applyLabel(UiHostNode *node);

// The app's sizes follow the system text size (Dynamic Type) through UIFontMetrics; a button
// no font prop reached keeps UIKit's font, scaled too.
static void applyFont(UiHostNode *node) {
    static UIFont *buttonFont;
    if (!buttonFont) buttonFont = [UIButton buttonWithType:UIButtonTypeSystem].titleLabel.font;
    CGFloat size = node.fontSize > 0 ? node.fontSize : 18;
    NSString *custom = node.fontFile ? fontName(node.fontFile) : nil;
    UIFont *font = custom ? [UIFont fontWithName:custom size:size] : nil;
    if (!font) font = node.kind == 4 && !node.fontSet ? buttonFont : [UIFont systemFontOfSize:size weight:weightOf(node.fontWeight)];
    if (node.italic) {
        // A face without an italic (a custom font) stays upright.
        UIFontDescriptor *slanted = [font.fontDescriptor fontDescriptorWithSymbolicTraits:
                                     font.fontDescriptor.symbolicTraits | UIFontDescriptorTraitItalic];
        if (slanted) font = [UIFont fontWithDescriptor:slanted size:font.pointSize];
    }
    font = [UIFontMetrics.defaultMetrics scaledFontForFont:font];
    if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).font = font;
    else if ([node.view isKindOfClass:UIButton.class]) ((UIButton *)node.view).titleLabel.font = font;
    else if ([node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).font = font;
    else if ([node.view isKindOfClass:UITextView.class]) {
        ((UITextView *)node.view).font = font;
        for (UIView *child in node.view.subviews) if ([child isKindOfClass:UILabel.class]) ((UILabel *)child).font = font;
        [node.view setNeedsLayout];
    }
    // A line height is in the attributed text, which holds the font.
    if (node.lineHeight > 0) applyLabel(node);
    YGNodeMarkDirty(node.yoga);
}

// The system text size changed: every text again at the new scale, then a layout.
static void rescaleFonts(void) {
    for (UiHostNode *node in nodes.allValues) {
        if (node.kind == 3 || node.kind == 4 || node.kind == 5) applyFont(node);
    }
    relayout();
}

static bool applyAccent(UiHostNode *node, UIColor *color);
static void applyColor(UiHostNode *node, UIColor *color) {
    if (applyAccent(node, color)) return;
    if ([node.view isKindOfClass:UILabel.class]) ((UILabel *)node.view).textColor = color ?: UIColor.labelColor;
    else if ([node.view isKindOfClass:UIButton.class]) [(UIButton *)node.view setTitleColor:color forState:UIControlStateNormal];
    else if ([node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).textColor = color ?: UIColor.labelColor;
    else if ([node.view isKindOfClass:UITextView.class]) ((UITextView *)node.view).textColor = color ?: UIColor.labelColor;
    if (node.lineHeight > 0) applyLabel(node);
}

@interface UiEvents (Input) <UITextViewDelegate>
- (void)submit:(UITextField *)sender;
@end

// A multiline input: a text view with a placeholder, scrolling only once it is taller than
// the layout lets it be (Style.max_height).
@interface UiTextView : UITextView
@property(nonatomic, strong) UILabel *placeholderLabel;
@end
@implementation UiTextView
- (void)layoutSubviews {
    [super layoutSubviews];
    UIEdgeInsets inset = self.textContainerInset;
    CGFloat pad = self.textContainer.lineFragmentPadding;
    CGFloat width = self.bounds.size.width - inset.left - inset.right - 2 * pad;
    CGSize size = [self.placeholderLabel sizeThatFits:CGSizeMake(std::max<CGFloat>(0, width), CGFLOAT_MAX)];
    self.placeholderLabel.frame = CGRectMake(inset.left + pad, inset.top, std::max<CGFloat>(0, width), size.height);
    self.placeholderLabel.hidden = self.text.length > 0;
    CGFloat need = [self sizeThatFits:CGSizeMake(self.bounds.size.width, CGFLOAT_MAX)].height;
    bool scroll = need > self.bounds.size.height + 1;
    if (self.scrollEnabled != scroll) self.scrollEnabled = scroll;
}
@end

static bool isTextInput(UIView *view) {
    return [view isKindOfClass:UITextField.class] || [view isKindOfClass:UITextView.class];
}

static NSString *inputText(UIView *view) {
    if ([view isKindOfClass:UITextField.class]) return ((UITextField *)view).text ?: @"";
    if ([view isKindOfClass:UITextView.class]) return ((UITextView *)view).text ?: @"";
    return @"";
}

static void setInputText(UIView *view, NSString *text) {
    if ([inputText(view) isEqualToString:text]) return;
    if ([view isKindOfClass:UITextField.class]) ((UITextField *)view).text = text;
    else if ([view isKindOfClass:UITextView.class]) {
        ((UITextView *)view).text = text;
        [view setNeedsLayout];
    }
}

static UITextField *makeTextField(int id) {
    UITextField *field = [UITextField new];
    field.borderStyle = UITextBorderStyleRoundedRect;
    field.font = [UIFontMetrics.defaultMetrics scaledFontForFont:[UIFont systemFontOfSize:18]];
    field.tag = id;
    [field addTarget:events action:@selector(edit:) forControlEvents:UIControlEventEditingChanged];
    [field addTarget:events action:@selector(focus:) forControlEvents:UIControlEventEditingDidBegin];
    [field addTarget:events action:@selector(blur:) forControlEvents:UIControlEventEditingDidEnd];
    return field;
}

static UiTextView *makeTextView(int id) {
    UiTextView *view = [UiTextView new];
    view.tag = id;
    view.delegate = events;
    view.scrollEnabled = NO;
    view.backgroundColor = UIColor.clearColor;
    view.textContainerInset = UIEdgeInsetsMake(8, 0, 8, 0);
    view.placeholderLabel = [UILabel new];
    view.placeholderLabel.numberOfLines = 0;
    view.placeholderLabel.textColor = UIColor.placeholderTextColor;
    view.placeholderLabel.userInteractionEnabled = NO;
    [view addSubview:view.placeholderLabel];
    // The rounded border a one-line field draws, until the app gives one.
    view.layer.borderWidth = 1 / UIScreen.mainScreen.scale;
    view.layer.borderColor = UIColor.systemGray4Color.CGColor;
    view.layer.cornerRadius = 5;
    return view;
}

// The node's padding inside the input: a field's text from its left edge, a text view's on
// both sides.
static void applyInputPadding(UiHostNode *node) {
    int left = node.padLeft ?: node.padding, right = node.padRight ?: node.padding;
    if ([node.view isKindOfClass:UITextField.class]) {
        UITextField *field = (UITextField *)node.view;
        field.leftView = left ? [[UIView alloc] initWithFrame:CGRectMake(0, 0, left, 1)] : nil;
        field.leftViewMode = left ? UITextFieldViewModeAlways : UITextFieldViewModeNever;
    } else if ([node.view isKindOfClass:UITextView.class]) {
        ((UITextView *)node.view).textContainerInset = UIEdgeInsetsMake(8, left, 8, right);
    }
}

static void applyPlaceholder(UiHostNode *node) {
    if ([node.view isKindOfClass:UITextField.class]) ((UITextField *)node.view).placeholder = node.placeholder;
    else if ([node.view isKindOfClass:UiTextView.class]) {
        UiTextView *view = (UiTextView *)node.view;
        view.placeholderLabel.text = node.placeholder;
        view.placeholderLabel.font = view.font;
        [view setNeedsLayout];
    }
}

// The keyboard, return key, capitalization, correction, autofill and secure entry.
static void applyTraits(UiHostNode *node) {
    if (!isTextInput(node.view)) return;
    UIView<UITextInputTraits> *input = (UIView<UITextInputTraits> *)node.view;
    const UIKeyboardType keyboards[] = {UIKeyboardTypeDefault, UIKeyboardTypeEmailAddress, UIKeyboardTypeNumberPad,
        UIKeyboardTypeDecimalPad, UIKeyboardTypePhonePad, UIKeyboardTypeURL, UIKeyboardTypeWebSearch};
    const UIReturnKeyType returns[] = {UIReturnKeyDefault, UIReturnKeyDone, UIReturnKeyGo, UIReturnKeyNext,
        UIReturnKeySearch, UIReturnKeySend};
    const UITextAutocapitalizationType caps[] = {UITextAutocapitalizationTypeSentences, UITextAutocapitalizationTypeNone,
        UITextAutocapitalizationTypeWords, UITextAutocapitalizationTypeSentences, UITextAutocapitalizationTypeAllCharacters};
    NSArray<UITextContentType> *fills = @[@"", UITextContentTypeUsername, UITextContentTypePassword,
        UITextContentTypeNewPassword, UITextContentTypeEmailAddress, UITextContentTypeOneTimeCode, UITextContentTypeName,
        UITextContentTypeTelephoneNumber];
    input.keyboardType = keyboards[std::min(6, std::max(0, node.keyboard))];
    input.returnKeyType = returns[std::min(5, std::max(0, node.returnKey))];
    input.autocapitalizationType = caps[std::min(4, std::max(0, node.capitalize))];
    bool correct = node.autocorrect != 0;
    input.autocorrectionType = correct ? UITextAutocorrectionTypeDefault : UITextAutocorrectionTypeNo;
    input.spellCheckingType = correct ? UITextSpellCheckingTypeDefault : UITextSpellCheckingTypeNo;
    if (@available(iOS 17.0, *)) input.inlinePredictionType = correct ? UITextInlinePredictionTypeDefault : UITextInlinePredictionTypeNo;
    int fill = std::min(7, std::max(0, node.autofill));
    input.textContentType = fill ? fills[fill] : nil;
    // A text view does not hide what is typed: secure entry is for one-line fields.
    if ([node.view isKindOfClass:UITextField.class]) input.secureTextEntry = node.secure != 0;
    if (node.view.isFirstResponder) [node.view reloadInputViews];
}

// Return in a one-line field with a submit listener reports the text (and ends editing, as
// UIKit's return does).
static void applySubmit(UiHostNode *node) {
    if (![node.view isKindOfClass:UITextField.class]) return;
    UITextField *field = (UITextField *)node.view;
    [field removeTarget:events action:@selector(submit:) forControlEvents:UIControlEventEditingDidEndOnExit];
    if (node.submit) [field addTarget:events action:@selector(submit:) forControlEvents:UIControlEventEditingDidEndOnExit];
}

// Input.multiline: the field becomes a text view, or back, in the same place, as the same
// node (id, value, props, focus).
static void setMultiline(UiHostNode *node) {
    UIView *old = node.view;
    bool multi = node.multiline != 0;
    if (multi == [old isKindOfClass:UITextView.class]) return;
    UIView *view = multi ? (UIView *)makeTextView(node.nodeId) : (UIView *)makeTextField(node.nodeId);
    bool focused = old.isFirstResponder;
    view.bounds = old.bounds;
    view.center = old.center;
    view.transform = old.transform;
    view.alpha = old.alpha;
    view.hidden = old.hidden;
    view.userInteractionEnabled = old.userInteractionEnabled;
    view.accessibilityLabel = old.accessibilityLabel;
    if (old.backgroundColor) view.backgroundColor = old.backgroundColor;
    CALayer *from = old.layer, *to = view.layer;
    // What the app set on the layer (a field draws its default border itself).
    if (from.borderWidth > 0 || !multi) { to.borderWidth = from.borderWidth; to.borderColor = from.borderColor; }
    if (from.cornerRadius > 0 || !multi) to.cornerRadius = from.cornerRadius;
    to.shadowColor = from.shadowColor;
    to.shadowOpacity = from.shadowOpacity;
    to.shadowRadius = from.shadowRadius;
    to.shadowOffset = from.shadowOffset;
    view.clipsToBounds = old.clipsToBounds;
    if (node.gradient) [to insertSublayer:node.gradient atIndex:0];
    if (node.blur) [view insertSubview:node.blur atIndex:0];
    // The pan, hover and long press handlers the host added (not the views' own recognizers).
    for (UIGestureRecognizer *g in old.gestureRecognizers.copy) {
        Class kind = g.class;
        if (kind != UIPanGestureRecognizer.class && kind != UIHoverGestureRecognizer.class &&
            kind != UILongPressGestureRecognizer.class) continue;
        [old removeGestureRecognizer:g];
        [view addGestureRecognizer:g];
    }
    NSString *text = inputText(old);
    if (focused) [old resignFirstResponder];
    [old.superview insertSubview:view aboveSubview:old];
    [old removeFromSuperview];
    node.view = view;
    setInputText(view, text);
    bool access = node.access;
    node.access = false;
    applyFont(node);
    applyColor(node, colorOf(node.textColor));
    applyInputPadding(node);
    applyLabel(node);
    applyPlaceholder(node);
    applyTraits(node);
    applySubmit(node);
    if (access) applyAccess(node);
    applyEnabled(node);
    YGNodeMarkDirty(node.yoga);
    if (focused) [view becomeFirstResponder];
}

// Props 65 to 72 on a text input; false when the code is not one of them.
static bool inputProp(UiHostNode *node, int code, int value) {
    if (code < 65 || code > 72 || node.kind != 5) return false;
    if (code == 65) node.secure = value;
    else if (code == 66) node.keyboard = value;
    else if (code == 67) node.returnKey = value;
    else if (code == 69) node.capitalize = value;
    else if (code == 70) node.autocorrect = value;
    else if (code == 71) node.autofill = value;
    else if (code == 72) { node.submit = value; applySubmit(node); return true; }
    else { node.multiline = value; setMultiline(node); return true; }
    applyTraits(node);
    return true;
}

@implementation UiEvents (Input)
- (void)submit:(UITextField *)sender {
    UiHostNode *node = nodes[@(sender.tag)];
    if (node && node.submit) ui_dispatch_submit((int)sender.tag, node.generation, sender.text.UTF8String ?: "");
}
- (void)textViewDidChange:(UITextView *)view {
    UiHostNode *node = nodes[@(view.tag)];
    [view setNeedsLayout];
    if (!node) return;
    YGNodeMarkDirty(node.yoga);
    ui_dispatch_text((int)view.tag, node.generation, view.text.UTF8String ?: "");
    relayout();
}
- (void)textViewDidBeginEditing:(UITextView *)view {
    UiHostNode *node = nodes[@(view.tag)];
    if (node) ui_dispatch_focus((int)view.tag, node.generation, 1);
    dispatch_async(dispatch_get_main_queue(), ^{ revealFocused(); });
}
- (void)textViewDidEndEditing:(UITextView *)view {
    UiHostNode *node = nodes[@(view.tag)];
    if (node) ui_dispatch_focus((int)view.tag, node.generation, 0);
}
@end
