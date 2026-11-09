// Native code for an Eskiu ui app on macOS (native/macos/*.m and *.mm, built in by `ui build
// macos` with the AppKit host): views for NativeView, by type, and events on channels for
// use_native_event. The same API as iOS's hosts/ios/ui_native.h, over NSView.
#ifndef UI_NATIVE_H
#define UI_NATIVE_H
#import <Cocoa/Cocoa.h>

#ifdef __cplusplus
extern "C" {
#endif

// Post `payload` on `channel` to the app's use_native_event handlers. Callable from any
// thread: the event reaches Eskiu on the main thread.
void ui_native_emit(const char *channel, const char *payload);

// What a native view sends back: an event's name and payload (NativeView's on_event).
typedef void (^UiViewEmit)(NSString *name, NSString *payload);

// A view type for NativeView. `create` makes a view (it fills the space the layout gives the
// NativeView); `set` applies a named prop, with "" when the app stopped giving it.
void ui_register_view(NSString *type, NSView *(^create)(UiViewEmit emit),
                      void (^set)(NSView *view, NSString *name, NSString *value));

// The app's window, to present sheets and panels from (an open panel, a picker). Call it on
// the main thread, once the app has started.
NSWindow *ui_native_window(void);

#ifdef __cplusplus
}
#endif

// A function that runs before the app starts, to register views:
//   UI_NATIVE_SETUP(slider) { ui_register_view(@"slider", ..., ...); }
#define UI_NATIVE_SETUP(name) __attribute__((constructor)) static void ui_native_setup_##name(void)

#endif
