// The "slider" view on iOS: a UISlider from 0 to 100.
#import <objc/runtime.h>
#import "ui_native.h"

@interface SliderTarget : NSObject
@property(nonatomic, copy) UiViewEmit emit;
- (void)changed:(UISlider *)slider;
@end
@implementation SliderTarget
- (void)changed:(UISlider *)slider {
    self.emit(@"change", [NSString stringWithFormat:@"%d", (int)lroundf(slider.value)]);
}
@end

UI_NATIVE_SETUP(slider) {
    ui_register_view(@"slider", ^UIView *(UiViewEmit emit) {
        UISlider *slider = [UISlider new];
        slider.minimumValue = 0;
        slider.maximumValue = 100;
        SliderTarget *target = [SliderTarget new];
        target.emit = emit;
        [slider addTarget:target action:@selector(changed:) forControlEvents:UIControlEventValueChanged];
        // The slider keeps its target alive.
        objc_setAssociatedObject(slider, "target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return slider;
    }, ^(UIView *view, NSString *name, NSString *value) {
        if ([name isEqualToString:@"value"]) ((UISlider *)view).value = value.floatValue;
    });
}
