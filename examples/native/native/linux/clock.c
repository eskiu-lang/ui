// The clock module on Linux: the externs Eskiu calls, over a GLib timeout on the main loop.
#include "ui_native.h"

static guint timer;

static gboolean tick(gpointer data) {
    (void)data;
    ui_native_emit("clock", "tick");
    return G_SOURCE_CONTINUE;
}

void clock_start(int ms) {
    if (timer) g_source_remove(timer);
    timer = g_timeout_add((guint)ms, tick, NULL);
}

void clock_stop(void) {
    if (timer) g_source_remove(timer);
    timer = 0;
}
