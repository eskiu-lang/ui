// The "slider" view on Linux: a GtkScale from 0 to 100.
#include <stdio.h>
#include <stdlib.h>
#include "ui_native.h"

static void changed(GtkRange *range, gpointer emitter) {
    char value[16];
    snprintf(value, sizeof value, "%d", (int)(gtk_range_get_value(range) + 0.5));
    ui_view_emit((UiViewEmitter *)emitter, "change", value);
}

static GtkWidget *create(UiViewEmitter *emitter) {
    GtkWidget *slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
    g_signal_connect(slider, "value-changed", G_CALLBACK(changed), emitter);
    return slider;
}

static void set(GtkWidget *view, const char *name, const char *value) {
    if (g_strcmp0(name, "value") != 0) return;
    // The app's value, not the user's: no change event for it.
    g_signal_handlers_block_matched(view, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, (gpointer)changed, NULL);
    gtk_range_set_value(GTK_RANGE(view), atof(value));
    g_signal_handlers_unblock_matched(view, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, (gpointer)changed, NULL);
}

UI_NATIVE_SETUP(slider) { ui_register_view("slider", create, set); }
