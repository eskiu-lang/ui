// host_text.inc: a text input's editing options (props 65 to 72): a secure field, a multiline
// text area that grows, spelling correction, autofill and submit on Enter. The keyboard,
// return key and capitalization props have no AppKit equivalent. Part of host.mm, which
// #includes it (one translation unit).

@interface UiEvents (Input) <NSTextViewDelegate>
@end

static NSString *inputText(NSView *view) {
    if (isTextArea(view)) return ((UiTextArea *)view).textView.string ?: @"";
    if ([view isKindOfClass:NSTextField.class]) return ((NSTextField *)view).stringValue ?: @"";
    return @"";
}

static void setInputText(NSView *view, NSString *text) {
    if ([inputText(view) isEqualToString:text]) return;
    if (isTextArea(view)) {
        UiTextView *textView = ((UiTextArea *)view).textView;
        textView.string = text;
        [textView setNeedsDisplay:YES];
    } else if ([view isKindOfClass:NSTextField.class]) ((NSTextField *)view).stringValue = text;
}

// The view for a text input's options: a field, a secure field or a text area.
static NSView *makeInput(UiHostNode *node) {
    if (node.multiline) {
        UiTextArea *area = [UiTextArea new];
        area.hasVerticalScroller = YES;
        area.autohidesScrollers = YES;
        area.scrollerStyle = NSScrollerStyleOverlay;
        area.borderType = NSBezelBorder;
        area.drawsBackground = NO;
        UiTextView *text = [[UiTextView alloc] initWithFrame:NSMakeRect(0, 0, 240, 40)];
        text.minSize = NSMakeSize(0, 0);
        text.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
        text.verticallyResizable = YES;
        text.horizontallyResizable = NO;
        text.autoresizingMask = NSViewWidthSizable;
        text.textContainer.widthTracksTextView = YES;
        text.richText = NO;
        text.allowsUndo = YES;
        text.font = [NSFont systemFontOfSize:kTextSize];
        text.textContainerInset = NSMakeSize(2, 4);
        text.delegate = events;
        area.documentView = text;
        area.textView = text;
        [viewNodes setObject:node forKey:text];
        return area;
    }
    NSTextField *field = node.secure ? [UiSecureField new] : [UiTextField new];
    field.font = [NSFont systemFontOfSize:kTextSize];
    field.delegate = events;
    field.cell.scrollable = YES;
    field.cell.usesSingleLineMode = YES;
    return field;
}

static void applyPlaceholder(UiHostNode *node) {
    if (isTextArea(node.view)) {
        UiTextView *text = ((UiTextArea *)node.view).textView;
        text.placeholder = node.placeholder;
        [text setNeedsDisplay:YES];
    } else if (isField(node.view)) ((NSTextField *)node.view).placeholderString = node.placeholder;
}

// A text area's spelling: Input.autocorrect off turns checking, correction and replacement
// off; on, it checks as you type and corrects as the system's settings say.
static void applySpelling(NSTextView *text, bool correct) {
    text.continuousSpellCheckingEnabled = correct;
    text.automaticSpellingCorrectionEnabled = correct && NSSpellChecker.isAutomaticSpellingCorrectionEnabled;
    text.automaticTextReplacementEnabled = correct && NSSpellChecker.isAutomaticTextReplacementEnabled;
}

// A window's field editor is shared by its fields: on, it goes back to its own settings (as
// first seen), off turns them off.
static void fieldSpelling(NSTextView *editor, bool correct) {
    static bool kept;
    static BOOL check, fix, replace;
    if (!kept) {
        kept = true;
        check = editor.continuousSpellCheckingEnabled;
        fix = editor.automaticSpellingCorrectionEnabled;
        replace = editor.automaticTextReplacementEnabled;
    }
    editor.continuousSpellCheckingEnabled = correct && check;
    editor.automaticSpellingCorrectionEnabled = correct && fix;
    editor.automaticTextReplacementEnabled = correct && replace;
}

// Autofill (Input.autofill) as the field's content type; the kinds past macOS 11's three
// come with macOS 14.
static void applyTraits(UiHostNode *node) {
    if (isTextArea(node.view)) { applySpelling(((UiTextArea *)node.view).textView, node.autocorrect != 0); return; }
    if (!isField(node.view)) return;
    NSTextField *field = (NSTextField *)node.view;
    NSTextContentType type = nil;
    if (node.autofill == 1) type = NSTextContentTypeUsername;
    else if (node.autofill == 2) type = NSTextContentTypePassword;
    else if (node.autofill == 5) type = NSTextContentTypeOneTimeCode;
    else if (@available(macOS 14.0, *)) {
        if (node.autofill == 3) type = NSTextContentTypeNewPassword;
        else if (node.autofill == 4) type = NSTextContentTypeEmailAddress;
        else if (node.autofill == 6) type = NSTextContentTypeName;
        else if (node.autofill == 7) type = NSTextContentTypeTelephoneNumber;
    }
    field.contentType = type;
    NSText *editor = field.currentEditor;
    if ([editor isKindOfClass:NSTextView.class]) fieldSpelling((NSTextView *)editor, node.autocorrect != 0);
}

// Input.secure or Input.multiline changed the view's class: the new view takes the old one's
// place, frame, layer, gestures and text, as the same node; focus stays with it.
static void swapInput(UiHostNode *node) {
    NSView *old = node.view;
    bool want = node.multiline ? isTextArea(old) : node.secure ? [old isKindOfClass:UiSecureField.class]
                                                               : [old isKindOfClass:UiTextField.class];
    if (want) return;
    NSWindow *window = old.window;
    NSResponder *first = window.firstResponder;
    bool focused = first == old || (isTextArea(old) && first == ((UiTextArea *)old).textView) ||
                   ([first isKindOfClass:NSTextView.class] && (__bridge void *)((NSTextView *)first).delegate == (__bridge void *)old);
    NSString *text = inputText(old);
    NSView *view = makeInput(node);
    view.wantsLayer = YES;
    view.autoresizesSubviews = NO;
    view.frame = old.frame;
    view.hidden = old.hidden;
    view.alphaValue = old.alphaValue;
    view.accessibilityLabel = old.accessibilityLabel;
    if (isTextArea(view)) ((UiTextArea *)view).textView.accessibilityLabel = old.accessibilityLabel;
    CALayer *from = old.layer, *to = view.layer;
    to.backgroundColor = from.backgroundColor;
    to.borderWidth = from.borderWidth;
    to.borderColor = from.borderColor;
    to.cornerRadius = from.cornerRadius;
    to.masksToBounds = from.masksToBounds;
    to.shadowColor = from.shadowColor;
    to.shadowOpacity = from.shadowOpacity;
    to.shadowRadius = from.shadowRadius;
    to.shadowOffset = from.shadowOffset;
    to.opacity = from.opacity;
    to.transform = from.transform;
    if (node.gradient) [to insertSublayer:node.gradient atIndex:0];
    if (node.blur) [view addSubview:node.blur positioned:NSWindowBelow relativeTo:nil];
    for (NSGestureRecognizer *g in old.gestureRecognizers.copy) {
        [old removeGestureRecognizer:g];
        [view addGestureRecognizer:g];
    }
    if (focused) [window makeFirstResponder:nil];
    [old.superview addSubview:view positioned:NSWindowAbove relativeTo:old];
    [old removeFromSuperview];
    [viewNodes removeObjectForKey:old];
    if (isTextArea(old)) [viewNodes removeObjectForKey:((UiTextArea *)old).textView];
    [viewNodes setObject:node forKey:view];
    node.view = view;
    setInputText(view, text);
    bool access = node.access;
    node.access = false;
    applyFont(node);
    applyColor(node);
    applyStyled(node);
    applyPlaceholder(node);
    applyTraits(node);
    applyDirection(node);
    applyInputAlign(node);
    applyAreaLineHeight(node);
    if (access) applyAccess(node);
    refreshEnabled(node);
    YGNodeMarkDirty(node.yoga);
    relayout();
    if (focused) [window makeFirstResponder:isTextArea(view) ? ((UiTextArea *)view).textView : view];
}

// Props 65 to 72 on a text input; false when the code is not one of them. 66 (keyboard), 67
// (return key) and 69 (capitalize) do not apply to a desktop text field.
static bool inputProp(UiHostNode *node, int code, int value) {
    if (code < 65 || code > 72 || node.kind != 5) return false;
    if (code == 65) { node.secure = value; swapInput(node); }
    else if (code == 68) { node.multiline = value; swapInput(node); }
    else if (code == 70) { node.autocorrect = value; applyTraits(node); }
    else if (code == 71) { node.autofill = value; applyTraits(node); }
    else if (code == 72) node.submit = value;
    return true;
}

@implementation UiEvents (Input)
// Enter in a one-line field with a submit listener reports its text (the field keeps its
// usual Enter behavior too).
- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)command {
    UiHostNode *node = nodeOf(control);
    if (command == @selector(insertNewline:) && node && node.submit && isField(control))
        ui_dispatch_submit(node.nodeId, node.generation, ((NSTextField *)control).stringValue.UTF8String ?: "");
    return NO;
}
// The field editor is shared: the field's spelling options go on it as editing starts.
- (void)controlTextDidBeginEditing:(NSNotification *)note {
    UiHostNode *node = nodeOf(note.object);
    NSText *editor = ((NSTextField *)note.object).currentEditor;
    if (node && [editor isKindOfClass:NSTextView.class]) fieldSpelling((NSTextView *)editor, node.autocorrect != 0);
}
- (void)textDidChange:(NSNotification *)note {
    UiTextView *text = note.object;
    UiHostNode *node = nodeOf(text);
    [text setNeedsDisplay:YES];
    if (!node) return;
    YGNodeMarkDirty(node.yoga);
    ui_dispatch_text(node.nodeId, node.generation, text.string.UTF8String ?: "");
    relayout();
}
@end
