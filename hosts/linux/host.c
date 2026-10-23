#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

// A real Linux desktop host for the shared wasm/DOM renderer using GTK 3 and WebKitGTK.
typedef struct {
    GtkWidget *window;
    GtkWidget *webview;
    char *screenshot;
    gboolean testing;
    gboolean is_fullscreen;
} LinuxHost;

static void on_title_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    GtkWindow *window = GTK_WINDOW(user_data);
    WebKitWebView *webview = WEBKIT_WEB_VIEW(object);
    const gchar *title = webkit_web_view_get_title(webview);
    if (title && *title) {
        gtk_window_set_title(window, title);
    }
}

static gboolean on_load_failed(WebKitWebView *webview, WebKitLoadEvent load_event, gchar *failing_uri, GError *error, gpointer user_data) {
    fprintf(stderr, "desktop load failed: %s (%s)\n", error ? error->message : "unknown", failing_uri ? failing_uri : "");
    LinuxHost *host = (LinuxHost *)user_data;
    if (host->testing) {
        exit(1);
    }
    return FALSE;
}

static void on_snapshot_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    cairo_surface_t *surface = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), res, &error);
    LinuxHost *host = (LinuxHost *)user_data;
    if (!surface || error) {
        fprintf(stderr, "snapshot failed: %s\n", error ? error->message : "unknown");
        exit(1);
    }
    if (cairo_surface_write_to_png(surface, host->screenshot) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "snapshot write failed: %s\n", host->screenshot);
        exit(1);
    }
    cairo_surface_destroy(surface);
    gtk_main_quit();
}

static void on_script_message_received(WebKitUserContentManager *manager, WebKitJavascriptResult *js_result, gpointer user_data) {
    LinuxHost *host = (LinuxHost *)user_data;
#if WEBKIT_CHECK_VERSION(2, 22, 0)
    JSCValue *value = webkit_javascript_result_get_js_value(js_result);
    char *json = jsc_value_to_json(value, 2);
#else
    char *json = NULL;
#endif
    if (json) {
        printf("%s\n", json);
        fflush(stdout);
        gboolean passed = strstr(json, "\"status\": \"PASS\"") != NULL;
        g_free(json);
        if (host->testing && host->screenshot) {
            webkit_web_view_get_snapshot(WEBKIT_WEB_VIEW(host->webview),
                                         WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT,
                                         WEBKIT_SNAPSHOT_OPTIONS_NONE,
                                         NULL,
                                         on_snapshot_ready,
                                         host);
            return;
        }
        if (host->testing) {
            exit(passed ? 0 : 1);
        }
    }
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    LinuxHost *host = (LinuxHost *)user_data;
    guint state = event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK);

    // Ctrl+Q or Ctrl+W: Quit
    if (state == GDK_CONTROL_MASK && (event->keyval == GDK_KEY_q || event->keyval == GDK_KEY_Q ||
                                      event->keyval == GDK_KEY_w || event->keyval == GDK_KEY_W)) {
        gtk_main_quit();
        return TRUE;
    }

    // Ctrl+R or F5: Reload
    if ((state == GDK_CONTROL_MASK && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R)) ||
        event->keyval == GDK_KEY_F5) {
        webkit_web_view_reload(WEBKIT_WEB_VIEW(host->webview));
        return TRUE;
    }

    // F11: Fullscreen toggle
    if (event->keyval == GDK_KEY_F11) {
        if (host->is_fullscreen) {
            gtk_window_unfullscreen(GTK_WINDOW(host->window));
            host->is_fullscreen = FALSE;
        } else {
            gtk_window_fullscreen(GTK_WINDOW(host->window));
            host->is_fullscreen = TRUE;
        }
        return TRUE;
    }

    return FALSE;
}

// The app bundled in web/ beside the executable, served as app://localhost/: a path that is
// a file of web/ gets it, any other path without an extension (a route) gets index.html.
static char g_web_dir[1024];

static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    static const char *types[][2] = {
        {".html", "text/html"}, {".js", "text/javascript"}, {".mjs", "text/javascript"},
        {".wasm", "application/wasm"}, {".css", "text/css"}, {".json", "application/json"},
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
        {".webp", "image/webp"}, {".svg", "image/svg+xml"}, {".ico", "image/x-icon"},
        {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".ttf", "font/ttf"}, {".otf", "font/otf"},
        {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".mp4", "video/mp4"}, {".webm", "video/webm"},
        {".txt", "text/plain"}, {".webmanifest", "application/manifest+json"}};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (g_ascii_strcasecmp(dot, types[i][0]) == 0) return types[i][1];
    return "application/octet-stream";
}

static void on_app_request(WebKitURISchemeRequest *request, gpointer data) {
    (void)data;
    const char *raw = webkit_uri_scheme_request_get_path(request);
    gchar *rel = g_uri_unescape_string(raw && raw[0] ? raw : "/", NULL);
    if (!rel || strstr(rel, "..")) {
        GError *error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "not in the app");
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        g_free(rel);
        return;
    }
    gchar *file = g_build_filename(g_web_dir, rel, NULL);
    const char *base = strrchr(rel, '/');
    if (!g_file_test(file, G_FILE_TEST_IS_REGULAR) && !strchr(base ? base : rel, '.')) {
        g_free(file);
        file = g_build_filename(g_web_dir, "index.html", NULL);
    }
    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;
    if (g_file_get_contents(file, &contents, &length, &error)) {
        GInputStream *stream = g_memory_input_stream_new_from_data(contents, (gssize)length, g_free);
        webkit_uri_scheme_request_finish(request, stream, (gint64)length, mime_for(file));
        g_object_unref(stream);
    } else {
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
    }
    g_free(file);
    g_free(rel);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    LinuxHost host;
    memset(&host, 0, sizeof(host));

    char local_url[2048] = {0};
    const char *url = NULL;
    char icon_path[1024] = {0};

    char exe_path[1024] = {0};
    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len > 0) {
        exe_path[exe_len] = '\0';
        char *dir = dirname(exe_path);
        snprintf(g_web_dir, sizeof(g_web_dir), "%s/web", dir);
        snprintf(local_url, sizeof(local_url), "%s/web/index.html", dir);
        snprintf(icon_path, sizeof(icon_path), "%s/icon.png", dir);
    }

    if (argc > 1) {
        url = argv[1];
    } else if (local_url[0] && g_file_test(local_url, G_FILE_TEST_EXISTS)) {
        url = "app://localhost/";
    } else {
        fprintf(stderr, "There is no web/index.html next to this program; build it with `ui build linux`.\n");
        return 1;
    }

    host.testing = argc > 2;
    host.screenshot = argc > 2 ? argv[2] : NULL;

    int width = 1100, height = 860;
    if (argc > 3) {
        sscanf(argv[3], "%dx%d", &width, &height);
    }

    host.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(host.window), width, height);
    gtk_window_set_position(GTK_WINDOW(host.window), GTK_WIN_POS_CENTER);
    gtk_window_set_title(GTK_WINDOW(host.window), exe_len > 0 ? g_path_get_basename(argv[0]) : "app");

    g_signal_connect(host.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(host.window, "key-press-event", G_CALLBACK(on_key_press), &host);

    if (icon_path[0] && g_file_test(icon_path, G_FILE_TEST_EXISTS)) {
        gtk_window_set_icon_from_file(GTK_WINDOW(host.window), icon_path, NULL);
    } else if (g_file_test("icon.png", G_FILE_TEST_EXISTS)) {
        gtk_window_set_icon_from_file(GTK_WINDOW(host.window), "icon.png", NULL);
    }

    WebKitWebContext *context = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(context, "app", on_app_request, NULL, NULL);
    WebKitSecurityManager *security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(security, "app");
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, "app");

    WebKitUserContentManager *ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(ucm, "results");
    g_signal_connect(ucm, "script-message-received::results", G_CALLBACK(on_script_message_received), &host);

    host.webview = webkit_web_view_new_with_user_content_manager(ucm);

#ifndef UI_RELEASE
    // The inspector, for development builds.
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(host.webview));
    webkit_settings_set_enable_developer_extras(settings, TRUE);
#endif

    gtk_container_add(GTK_CONTAINER(host.window), host.webview);

    g_signal_connect(host.webview, "notify::title", G_CALLBACK(on_title_changed), host.window);
    g_signal_connect(host.webview, "load-failed", G_CALLBACK(on_load_failed), &host);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(host.webview), url);

    gtk_widget_show_all(host.window);
    gtk_main();

    return 0;
}
