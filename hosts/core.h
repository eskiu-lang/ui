// The core's entry points (hosts/bridge.esk), for the native hosts. A development build
// (UI_RELOAD, from `ui serve`) reaches them through pointers into the app's library, which
// loadCore loads again after each build; a release build links the app in.
#pragma once
#define UI_CORE(X) \
    X(void, ui_start, (int example)) \
    X(void, ui_dispatch, (int id, int generation)) \
    X(void, ui_dispatch_text, (int id, int generation, const char *text)) \
    X(void, ui_dispatch_focus, (int id, int generation, int focused)) \
    X(void, ui_dispatch_toggle, (int id, int generation, int on)) \
    X(void, ui_dispatch_scroll, (int id, int generation, int offset, int viewport)) \
    X(void, ui_dispatch_alert, (int request, int choice)) \
    X(void, ui_dispatch_refresh, (int id, int generation)) \
    X(void, ui_report_link, (const char *url)) \
    X(void, ui_report_window, (int width, int height, int scale)) \
    X(int, ui_edge_to_edge, (void)) \
    X(void, ui_report_insets, (int top, int right, int bottom, int left, int keyboard)) \
    X(void, ui_report_active, (int active)) \
    X(void, ui_report_appearance, (int dark)) \
    X(void, ui_report_accessibility, (int screen_reader, int reduce_motion, int font_scale)) \
    X(void, ui_report_locale, (const char *locale, int rtl)) \
    X(const char *, ui_conformance_log, (void)) \
    X(void, ui_tick, (void)) \
    X(void, ui_stop, (void)) \
    X(void, ui_dispatch_native, (const char *channel, const char *payload)) \
    X(int, ui_report_back, (void)) \
    X(void, ui_dispatch_layout, (int id, int generation, int x, int y, int width, int height, int window_x, int window_y)) \
    X(void, ui_dispatch_timer, (int token)) \
    X(void, ui_dispatch_hover, (int id, int generation, int inside)) \
    X(void, ui_dispatch_value, (int id, int generation, int value)) \
    X(void, ui_dispatch_answer, (int request, const char *text)) \
    X(void, ui_dispatch_pan, (int id, int generation, int phase, int dx, int dy, int vx, int vy)) \
    X(void, ui_report_data_dir, (const char *path)) \
    X(void, ui_dispatch_fetch, (int request, int status, const char *body, const char *headers)) \
    X(void, ui_dispatch_view, (int id, int generation, const char *name, const char *payload))
#ifdef UI_RELOAD
// Where a failed load is reported (stderr, Android's log).
static void ui_core_error(const char *message);
#include <dlfcn.h>
#include <cstdio>
#define UI_CORE_POINTER(ret, name, params) static ret (*name) params;
UI_CORE(UI_CORE_POINTER)
// The app's library at `path`, its entry points into the pointers; false if it does not load.
static bool loadCore(const char *path) {
    void *core = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!core) { ui_core_error(dlerror()); return false; }
#define UI_CORE_LOOKUP(ret, name, params) name = (ret (*) params)dlsym(core, #name);
    UI_CORE(UI_CORE_LOOKUP)
    return true;
}
#else
#define UI_CORE_EXTERN(ret, name, params) extern "C" ret name params;
UI_CORE(UI_CORE_EXTERN)
#endif
