// The "slider" view on Windows: a trackbar from 0 to 100.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_native.h"
#include <commctrl.h>

// The trackbar's moves come back to it reflected (UI_REFLECT + WM_HSCROLL).
static LRESULT CALLBACK moved(HWND view, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR emitter) {
    if (message == UI_REFLECT + WM_HSCROLL) {
        char value[16];
        snprintf(value, sizeof value, "%d", (int)SendMessageW(view, TBM_GETPOS, 0, 0));
        ui_view_emit((UiViewEmitter *)emitter, "change", value);
        return 0;
    }
    return DefSubclassProc(view, message, w, l);
}

static HWND create(HWND parent, UiViewEmitter *emitter) {
    HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0, parent,
                                  NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(slider, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SetWindowSubclass(slider, moved, 1, (DWORD_PTR)emitter);
    return slider;
}

// The app's value, not the user's: TBM_SETPOS sends no notification.
static void set(HWND view, const char *name, const char *value) {
    if (strcmp(name, "value") == 0) SendMessageW(view, TBM_SETPOS, TRUE, atoi(value));
}

UI_NATIVE_SETUP(slider) { ui_register_view("slider", create, set); }
