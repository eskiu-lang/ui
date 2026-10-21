// The clock module on iOS: the externs Eskiu calls, over an NSTimer on the main run loop.
#import "ui_native.h"

static NSTimer *timer;

void clock_start(int ms) {
    [timer invalidate];
    timer = [NSTimer scheduledTimerWithTimeInterval:ms / 1000.0 repeats:YES block:^(NSTimer *t) {
        ui_native_emit("clock", "tick");
    }];
}

void clock_stop(void) {
    [timer invalidate];
    timer = nil;
}
