// Native code for an Eskiu ui app on Linux (native/linux/*.c and *.cpp, built in by `ui build
// linux` with the GTK 4 host): views for NativeView, by type, and events on channels for
// use_native_event. The same API as macOS's hosts/macos/ui_native.h, over GtkWidget, in C.
#ifndef UI_NATIVE_H
#define UI_NATIVE_H
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

// Post `payload` on `channel` to the app's use_native_event handlers. Callable from any
// thread: the event reaches Eskiu on the main thread.
void ui_native_emit(const char *channel, const char *payload);

// What a native view sends back through: owned by the view, valid while it lives.
typedef struct UiViewEmitter UiViewEmitter;

// Send an event's name and payload to the NativeView's on_event. Callable from any thread.
void ui_view_emit(UiViewEmitter *emitter, const char *name, const char *payload);

// A view type for NativeView. `create` makes a widget (it fills the space the layout gives the
// NativeView); `set` applies a named prop, with "" when the app stopped giving it.
typedef GtkWidget *(*UiViewCreate)(UiViewEmitter *emitter);
typedef void (*UiViewSet)(GtkWidget *view, const char *name, const char *value);
void ui_register_view(const char *type, UiViewCreate create, UiViewSet set);

// The app's window, to present dialogs from (a file chooser, a picker). Call it on the main
// thread, once the app has started.
GtkWindow *ui_native_window(void);

#ifdef __cplusplus
}
#endif

// A function that runs before the app starts, to register views:
//   UI_NATIVE_SETUP(slider) { ui_register_view("slider", create, set); }
#define UI_NATIVE_SETUP(name) __attribute__((constructor)) static void ui_native_setup_##name(void)

#endif
