// host_tests.inc: the host's own test runs (--self-test, --native-test). Part of host.mm, which
// #includes it (one translation unit).

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
