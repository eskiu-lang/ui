// host_style.inc: the layout and text props since 73 (the sides' padding, margins, text align,
// maximum lines, shrink, an image's listener, line height, italic, z index, clip, layout
// animation, a modal's presentation and appearing), a switch's or slider's accent, and the
// layout's direction. Part of host.mm, which #includes it (one translation unit).

// A label's text with its alignment, lines and line height (props 81, 82 and 85). A line
// height needs an attributed string; without one the label keeps plain text.
static void applyLabel(UiHostNode *node) {
    const NSTextAlignment aligns[] = {NSTextAlignmentNatural, NSTextAlignmentNatural, NSTextAlignmentCenter,
        layoutRTL ? NSTextAlignmentLeft : NSTextAlignmentRight, NSTextAlignmentJustified};
    NSTextAlignment align = aligns[std::min(4, std::max(0, node.textAlign))];
    NSLineBreakMode breaks = node.maxLines > 0 ? NSLineBreakByTruncatingTail : NSLineBreakByWordWrapping;
    if ([node.view isKindOfClass:UIButton.class]) {
        UIButton *button = (UIButton *)node.view;
        // A title too long for its button wraps (a large text size), unless max_lines cuts it.
        button.titleLabel.numberOfLines = node.maxLines > 0 ? node.maxLines : 0;
        button.titleLabel.lineBreakMode = breaks;
        button.titleLabel.textAlignment = align;
        const UIControlContentHorizontalAlignment places[] = {UIControlContentHorizontalAlignmentCenter,
            UIControlContentHorizontalAlignmentLeading, UIControlContentHorizontalAlignmentCenter,
            UIControlContentHorizontalAlignmentTrailing, UIControlContentHorizontalAlignmentFill};
        button.contentHorizontalAlignment = places[std::min(4, std::max(0, node.textAlign))];
        return;
    }
    if ([node.view isKindOfClass:UITextField.class]) { ((UITextField *)node.view).textAlignment = align; return; }
    if ([node.view isKindOfClass:UITextView.class]) { ((UITextView *)node.view).textAlignment = align; return; }
    if (![node.view isKindOfClass:UILabel.class]) return;
    UILabel *label = (UILabel *)node.view;
    label.numberOfLines = node.maxLines;
    label.lineBreakMode = breaks;
    label.textAlignment = align;
    NSString *text = node.text ?: @"";
    if (node.lineHeight <= 0) {
        label.text = text;
        return;
    }
    // In points that follow the text size, as the font does.
    CGFloat height = [UIFontMetrics.defaultMetrics scaledValueForValue:node.lineHeight];
    NSMutableParagraphStyle *paragraph = [NSMutableParagraphStyle new];
    paragraph.minimumLineHeight = height;
    paragraph.maximumLineHeight = height;
    paragraph.alignment = align;
    paragraph.lineBreakMode = breaks;
    // The extra space splits above and below the glyphs, as CSS's line-height does.
    CGFloat offset = std::max<CGFloat>(0, (height - label.font.lineHeight) / 4);
    label.attributedText = [[NSAttributedString alloc] initWithString:text attributes:@{
        NSFontAttributeName: label.font, NSForegroundColorAttributeName: label.textColor ?: UIColor.labelColor,
        NSParagraphStyleAttributeName: paragraph, NSBaselineOffsetAttributeName: @(offset)}];
}

// Whether a view hides what falls outside it: Style.clip, a radius without a shadow, a covering
// image. A clipped view's shadow is clipped too (a layer cannot do both).
static void applyClip(UiHostNode *node) {
    node.view.clipsToBounds = node.clip || node.cover || (node.radius > 0 && !node.view.layer.shadowOpacity) ||
                              node.kind == 13;
}

// Siblings drawn and hit in z index order (prop 87), the children's order among equals.
static void restack(UIView *parent) {
    NSArray<UIView *> *views = parent.subviews;
    bool any = false;
    for (UIView *view in views) if (view.layer.zPosition != 0) { any = true; break; }
    if (!any) return;
    NSArray<UIView *> *sorted = [views sortedArrayWithOptions:NSSortStable usingComparator:^NSComparisonResult(UIView *a, UIView *b) {
        CGFloat za = a.layer.zPosition, zb = b.layer.zPosition;
        return za < zb ? NSOrderedAscending : za > zb ? NSOrderedDescending : NSOrderedSame;
    }];
    if ([sorted isEqualToArray:views]) return;
    for (UIView *view in sorted) [parent bringSubviewToFront:view];
}

// A view's direction: right to left when the user's language is (UIKit follows the app's own
// localizations, which an app may not have).
static void applyDirection(UIView *view) {
    view.semanticContentAttribute = layoutRTL ? UISemanticContentAttributeForceRightToLeft : UISemanticContentAttributeUnspecified;
}

// The user's language changed direction: every view and the layout again.
static void setDirection(bool rtl) {
    if (rtl == layoutRTL) return;
    layoutRTL = rtl;
    applyDirection(rootView);
    for (UiHostNode *node in nodes.allValues) {
        applyDirection(node.view);
        if (node.kind == 3 || node.kind == 4 || node.kind == 5) applyLabel(node);
    }
    if (rootYoga) relayout();
}

// A modal's appearing (props 89 and 91), once it is laid out: a sheet's content slides up, a
// full screen modal or a dialog fades in; at once with 2, under reduce motion, and with 0 (the
// host's default, which shows it at once).
static void appearModal(UiHostNode *node) {
    if (!node.appearing) return;
    node.appearing = false;
    if (node.appear != 1 || UIAccessibilityIsReduceMotionEnabled()) return;
    UIView *layer = node.view;
    if (node.presentation == 1) {
        UIColor *dim = layer.backgroundColor;
        layer.backgroundColor = [dim colorWithAlphaComponent:0];
        for (UIView *panel in layer.subviews)
            panel.transform = CGAffineTransformMakeTranslation(0, layer.bounds.size.height - CGRectGetMinY(panel.frame));
        [UIView animateWithDuration:0.3 delay:0 options:UIViewAnimationOptionCurveEaseOut animations:^{
            layer.backgroundColor = dim;
            for (UIView *panel in layer.subviews) panel.transform = CGAffineTransformIdentity;
        } completion:nil];
        return;
    }
    layer.alpha = 0;
    [UIView animateWithDuration:0.2 animations:^{ layer.alpha = 1; }];
}

// Props 73 to 91 and a switch's or slider's accent; false when the code is not one of them.
static bool styleProp(UiHostNode *node, int code, int value) {
    if (code >= 73 && code <= 76) {
        const YGEdge edges[] = {YGEdgeTop, YGEdgeRight, YGEdgeBottom, YGEdgeLeft};
        YGNodeStyleSetPadding(node.yoga, edges[code - 73], value ? (float)value : YGUndefined);
        if (code == 73) node.padTop = value; else if (code == 74) node.padRight = value;
        else if (code == 75) node.padBottom = value; else node.padLeft = value;
        applyInputPadding(node);
    } else if (code >= 77 && code <= 80) {
        const YGEdge edges[] = {YGEdgeTop, YGEdgeRight, YGEdgeBottom, YGEdgeLeft};
        YGNodeStyleSetMargin(node.yoga, edges[code - 77], value);
    } else if (code == 81 || code == 82 || code == 85) {
        if (code == 81) node.textAlign = value; else if (code == 82) node.maxLines = value; else node.lineHeight = value;
        applyLabel(node);
        YGNodeMarkDirty(node.yoga);
    } else if (code == 83) {
        node.shrink = value;
        if (![node.view isKindOfClass:UIScrollView.class])
            YGNodeStyleSetFlexShrink(node.yoga, std::max<float>(value, YGNodeStyleGetFlexGrow(node.yoga) > 0 ? 1 : 0));
    } else if (code == 84) node.imageListening = value != 0;
    else if (code == 86) { node.italic = value != 0; applyFont(node); }
    else if (code == 87) {
        node.view.layer.zPosition = value;
        if (node.view.superview) restack(node.view.superview);
    } else if (code == 88) { node.clip = value != 0; applyClip(node); }
    else if (code == 89) node.presentation = value;
    else if (code == 90) node.layoutMs = value;
    else if (code == 91) node.appear = value;
    else return false;
    return true;
}

// A switch's or slider's accent (prop 9): the track when on, the filled part and the thumb.
static bool applyAccent(UiHostNode *node, UIColor *color) {
    if ([node.view isKindOfClass:UISwitch.class]) { ((UISwitch *)node.view).onTintColor = color; return true; }
    if (![node.view isKindOfClass:UISlider.class]) return false;
    UISlider *slider = (UISlider *)node.view;
    slider.minimumTrackTintColor = color;
    slider.thumbTintColor = color;
    return true;
}
