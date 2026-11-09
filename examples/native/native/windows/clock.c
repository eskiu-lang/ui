// The clock module on Windows: the externs Eskiu calls, over a thread timer on the UI thread.
#include "ui_native.h"

static UINT_PTR timer;

static void CALLBACK tick(HWND hwnd, UINT message, UINT_PTR id, DWORD time) {
    (void)hwnd; (void)message; (void)id; (void)time;
    ui_native_emit("clock", "tick");
}

void clock_start(int ms) {
    if (timer) KillTimer(NULL, timer);
    timer = SetTimer(NULL, 0, (UINT)ms, tick);
}

void clock_stop(void) {
    if (timer) KillTimer(NULL, timer);
    timer = 0;
}
