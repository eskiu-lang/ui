// Native code for an Eskiu ui app on Windows (native/windows/*.c and *.cpp, built in by `ui
// build windows` with the Win32 host): views for NativeView, by type, and events on channels
// for use_native_event. The same API as Linux's hosts/linux/ui_native.h, over HWND, in C.
#ifndef UI_NATIVE_H
#define UI_NATIVE_H
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Post `payload` on `channel` to the app's use_native_event handlers. Callable from any
// thread: the event reaches Eskiu on the UI thread.
void ui_native_emit(const char *channel, const char *payload);

// What a native view sends back through: owned by the view, valid while it lives.
typedef struct UiViewEmitter UiViewEmitter;

// Send an event's name and payload to the NativeView's on_event. Callable from any thread.
void ui_view_emit(UiViewEmitter *emitter, const char *name, const char *payload);

// A view type for NativeView. `create` makes a child window of `parent` (it is sized to fill
// the space the layout gives the NativeView); `set` applies a named prop, with "" when the
// app stopped giving it. The notifications a control sends its parent (WM_COMMAND,
// WM_NOTIFY, WM_HSCROLL, WM_VSCROLL) come back to it as UI_REFLECT + the message, as ATL's
// reflected messages do: subclass the control (SetWindowSubclass) to receive them.
#define UI_REFLECT 0x2000
typedef HWND (*UiViewCreate)(HWND parent, UiViewEmitter *emitter);
typedef void (*UiViewSet)(HWND view, const char *name, const char *value);
void ui_register_view(const char *type, UiViewCreate create, UiViewSet set);

// The app's window, to show dialogs from (a file dialog, a picker). Call it on the UI
// thread, once the app has started.
HWND ui_native_window(void);

#ifdef __cplusplus
}
#endif

// A function that runs before the app starts, to register views:
//   UI_NATIVE_SETUP(slider) { ui_register_view("slider", create, set); }
#define UI_NATIVE_SETUP(name) __attribute__((constructor)) static void ui_native_setup_##name(void)

#endif
