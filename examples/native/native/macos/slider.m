// The "slider" view on macOS: an NSSlider from 0 to 100.
#import <objc/runtime.h>
#import "ui_native.h"

@interface SliderTarget : NSObject
@property(nonatomic, copy) UiViewEmit emit;
- (void)changed:(NSSlider *)slider;
@end
@implementation SliderTarget
- (void)changed:(NSSlider *)slider {
    self.emit(@"change", [NSString stringWithFormat:@"%d", (int)lround(slider.doubleValue)]);
}
@end

UI_NATIVE_SETUP(slider) {
    ui_register_view(@"slider", ^NSView *(UiViewEmit emit) {
        SliderTarget *target = [SliderTarget new];
        target.emit = emit;
        NSSlider *slider = [NSSlider sliderWithValue:0 minValue:0 maxValue:100 target:target action:@selector(changed:)];
        slider.continuous = YES;
        // The slider keeps its target alive (a control's target is weak).
        objc_setAssociatedObject(slider, "target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return slider;
    }, ^(NSView *view, NSString *name, NSString *value) {
        if ([name isEqualToString:@"value"]) ((NSSlider *)view).doubleValue = value.doubleValue;
    });
}
