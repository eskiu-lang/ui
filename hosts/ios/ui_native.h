// Native code for an Eskiu ui app on iOS (native/ios/*.m and *.mm, built in by `ui build ios`):
// views for NativeView, by type, and events on channels for use_native_event.
#ifndef UI_NATIVE_H
#define UI_NATIVE_H
#import <UIKit/UIKit.h>

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
void ui_register_view(NSString *type, UIView *(^create)(UiViewEmit emit),
                      void (^set)(UIView *view, NSString *name, NSString *value));

// The view controller to present system screens from (a picker, the camera): the app's,
// or the one it presents on top. Call it on the main thread, once the app has started.
UIViewController *ui_view_controller(void);

#ifdef __cplusplus
}
#endif

// A function that runs before the app starts, to register views:
//   UI_NATIVE_SETUP(slider) { ui_register_view(@"slider", ..., ...); }
// Swift has no such functions: a class @objc(Name) in Name.swift with `@objc static func
// setup()` is called at launch instead (ui build lists it in Info.plist's UiNativeClasses).
#define UI_NATIVE_SETUP(name) __attribute__((constructor)) static void ui_native_setup_##name(void)

#endif
