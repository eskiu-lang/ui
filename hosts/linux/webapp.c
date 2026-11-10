// The <ui/webapp> host on Linux: GTK 3 windows holding a WebKitGTK view each
// (webkit2gtk-4.1, or 4.0), for a program that serves a web front end (src/webapp.esk has
// the program's side and the contract of each uiw_ function). The page's messages arrive
// through a script message handler, the program's URL schemes through the web context's
// scheme handlers, and the program's scripts run from GLib's main loop, whatever thread
// asked.
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdkkeysyms.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

// The program's side (src/webapp.esk), on the main thread.
extern void uiw_on_message(int window, const char *text);
extern void uiw_on_request(int window, int64_t request, const char *scheme, const char *url, const char *method, const char *range);
extern int uiw_on_close(int window);
extern void uiw_on_closed(int window);

typedef struct {
    int id;
    GtkWidget *window;
    GtkWidget *web;
    gboolean follow_title;
    gboolean fullscreen;
    gboolean devtools;
    char *origin;
} UiwWindow;

static char *g_app_name;
static char *g_bridge;
static GPtrArray *g_windows;
static GPtrArray *g_schemes;
static int g_next_window;
static int g_exit_code;
static gboolean g_running;
// The scheme requests waiting for uiw_respond, by id.
static GHashTable *g_requests;
static int64_t g_next_request;

static UiwWindow *uiw_find(int id) {
    for (guint i = 0; g_windows && i < g_windows->len; i++) {
        UiwWindow *w = g_ptr_array_index(g_windows, i);
        if (w->id == id) return w;
    }
    return NULL;
}

static UiwWindow *uiw_of_view(WebKitWebView *view) {
    for (guint i = 0; g_windows && i < g_windows->len; i++) {
        UiwWindow *w = g_ptr_array_index(g_windows, i);
        if (WEBKIT_WEB_VIEW(w->web) == view) return w;
    }
    return NULL;
}

// ---- The page's messages ----

static void on_message(WebKitUserContentManager *manager, WebKitJavascriptResult *result, gpointer data) {
    UiwWindow *w = data;
    JSCValue *value = webkit_javascript_result_get_js_value(result);
    if (!jsc_value_is_string(value)) return;
    char *text = jsc_value_to_string(value);
    uiw_on_message(w->id, text);
    g_free(text);
}

// ---- The program's schemes ----

static void on_scheme(WebKitURISchemeRequest *request, gpointer data) {
    (void)data;
    int64_t id = ++g_next_request;
    g_hash_table_insert(g_requests, g_memdup2(&id, sizeof id), g_object_ref(request));
    WebKitWebView *view = webkit_uri_scheme_request_get_web_view(request);
    UiwWindow *w = view ? uiw_of_view(view) : NULL;
    const char *method = "GET";
    const char *range = "";
#if WEBKIT_CHECK_VERSION(2, 36, 0)
    if (webkit_uri_scheme_request_get_http_method(request)) method = webkit_uri_scheme_request_get_http_method(request);
    SoupMessageHeaders *headers = webkit_uri_scheme_request_get_http_headers(request);
    const char *asked = headers ? soup_message_headers_get_one(headers, "Range") : NULL;
    if (asked) range = asked;
#endif
    uiw_on_request(w ? w->id : 0, id, webkit_uri_scheme_request_get_scheme(request), webkit_uri_scheme_request_get_uri(request),
                   method, range);
}

// "scheme://host:port" of a URL, lowercased, from g_malloc.
static char *uiw_origin(const char *url) {
    const char *start = url ? strstr(url, "://") : NULL;
    if (!start) return g_strdup("");
    const char *end = start + 3;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;
    return g_ascii_strdown(url, end - url);
}

static gboolean uiw_scheme_known(const char *url) {
    const char *colon = url ? strchr(url, ':') : NULL;
    if (!colon) return FALSE;
    char *scheme = g_ascii_strdown(url, colon - url);
    gboolean known = strcmp(scheme, "about") == 0 || strcmp(scheme, "blob") == 0 || strcmp(scheme, "data") == 0;
    for (guint i = 0; !known && i < g_schemes->len; i++) known = strcmp(g_ptr_array_index(g_schemes, i), scheme) == 0;
    g_free(scheme);
    return known;
}

// The page stays on the app's origin (its schemes, or the URL the window opened): a link
// elsewhere opens in the browser, so no other site gets the bridge.
static gboolean on_decide(WebKitWebView *view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer data) {
    UiwWindow *w = data;
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;
    WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const char *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    char *origin = uiw_origin(uri);
    gboolean inside = uiw_scheme_known(uri) || strcmp(origin, w->origin) == 0;
    g_free(origin);
    if (inside) return FALSE;
    gtk_show_uri_on_window(GTK_WINDOW(w->window), uri, GDK_CURRENT_TIME, NULL);
    webkit_policy_decision_ignore(decision);
    return TRUE;
}

// ---- Windows ----

static gboolean on_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
    UiwWindow *w = data;
    return uiw_on_close(w->id) == 1 ? FALSE : TRUE;
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    UiwWindow *w = data;
    g_ptr_array_remove(g_windows, w);
    uiw_on_closed(w->id);
    g_free(w->origin);
    g_free(w);
    if (g_windows->len == 0 && g_running) gtk_main_quit();
}

static void on_title(GObject *object, GParamSpec *spec, gpointer data) {
    UiwWindow *w = data;
    const char *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(w->web));
    if (w->follow_title && title && title[0]) gtk_window_set_title(GTK_WINDOW(w->window), title);
}

static gboolean on_load_failed(WebKitWebView *view, WebKitLoadEvent event, gchar *uri, GError *error, gpointer data) {
    fprintf(stderr, "webapp: %s did not load: %s\n", uri ? uri : "the page", error ? error->message : "unknown");
    return FALSE;
}

// A link that opens a new window (target="_blank", window.open) goes to the browser.
static GtkWidget *on_create(WebKitWebView *view, WebKitNavigationAction *action, gpointer data) {
    WebKitURIRequest *request = webkit_navigation_action_get_request(action);
    const char *uri = request ? webkit_uri_request_get_uri(request) : NULL;
    if (uri) gtk_show_uri_on_window(GTK_WINDOW(((UiwWindow *)data)->window), uri, GDK_CURRENT_TIME, NULL);
    return NULL;
}

static void uiw_toggle_fullscreen(UiwWindow *w, gboolean on) {
    if (on) gtk_window_fullscreen(GTK_WINDOW(w->window));
    else gtk_window_unfullscreen(GTK_WINDOW(w->window));
    w->fullscreen = on;
}

// F11 full screen; in a build with the developer tools, Ctrl+R and F5 reload and F12 shows
// the inspector.
static gboolean on_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    UiwWindow *w = data;
    gboolean control = (event->state & GDK_CONTROL_MASK) != 0;
    if (event->keyval == GDK_KEY_F11) { uiw_toggle_fullscreen(w, !w->fullscreen); return TRUE; }
    if (!w->devtools) return FALSE;
    if (event->keyval == GDK_KEY_F5 || (control && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R))) {
        webkit_web_view_reload(WEBKIT_WEB_VIEW(w->web));
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F12) {
        webkit_web_inspector_show(webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(w->web)));
        return TRUE;
    }
    return FALSE;
}

// ---- What the program calls ----

void uiw_init(const char *app_name, const char *bridge) {
    gtk_init(NULL, NULL);
    g_app_name = g_strdup(app_name && app_name[0] ? app_name : "app");
    g_bridge = g_strdup(bridge ? bridge : "");
    g_windows = g_ptr_array_new();
    g_schemes = g_ptr_array_new_with_free_func(g_free);
    g_requests = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, g_object_unref);
    g_set_application_name(g_app_name);
}

const char *uiw_platform(void) { return "linux"; }

char *uiw_resource_dir(void) {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return strdup(".");
    path[n] = 0;
    return strdup(dirname(path));
}

void uiw_scheme(const char *name) {
    for (guint i = 0; i < g_schemes->len; i++)
        if (strcmp(g_ptr_array_index(g_schemes, i), name) == 0) return;
    g_ptr_array_add(g_schemes, g_strdup(name));
    WebKitWebContext *context = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(context, name, on_scheme, NULL, NULL);
    WebKitSecurityManager *security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(security, name);
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, name);
}

static void uiw_min_size(UiwWindow *w, int width, int height) {
    GdkGeometry hints = {0};
    hints.min_width = width > 0 ? width : 1;
    hints.min_height = height > 0 ? height : 1;
    gtk_window_set_geometry_hints(GTK_WINDOW(w->window), NULL, &hints, GDK_HINT_MIN_SIZE);
}

int uiw_window_open(const char *title, const char *url, int width, int height, int min_width, int min_height, int flags) {
    UiwWindow *w = g_new0(UiwWindow, 1);
    w->id = ++g_next_window;
    w->devtools = (flags & 4) != 0;
    WebKitUserContentManager *content = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(content, "eskiu");
    g_signal_connect(content, "script-message-received::eskiu", G_CALLBACK(on_message), w);
    char *source = g_strdup_printf("window.__ESKIU_HOST__={os:\"linux\",window:%d};\n%s", w->id, g_bridge);
    WebKitUserScript *script = webkit_user_script_new(source, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                                      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
    webkit_user_content_manager_add_script(content, script);
    webkit_user_script_unref(script);
    g_free(source);
    w->web = webkit_web_view_new_with_user_content_manager(content);
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(w->web));
    webkit_settings_set_enable_developer_extras(settings, w->devtools);
    webkit_settings_set_media_playback_requires_user_gesture(settings, FALSE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);
    g_signal_connect(w->web, "load-failed", G_CALLBACK(on_load_failed), w);
    g_signal_connect(w->web, "create", G_CALLBACK(on_create), w);
    g_signal_connect(w->web, "decide-policy", G_CALLBACK(on_decide), w);
    w->origin = uiw_origin(url);

    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    w->follow_title = !(title && title[0]);
    gtk_window_set_title(GTK_WINDOW(w->window), w->follow_title ? g_app_name : title);
    gtk_window_set_default_size(GTK_WINDOW(w->window), width, height);
    gtk_window_set_resizable(GTK_WINDOW(w->window), (flags & 1) != 0);
    if (g_windows->len == 0) gtk_window_set_position(GTK_WINDOW(w->window), GTK_WIN_POS_CENTER);
    if (min_width > 0 || min_height > 0) uiw_min_size(w, min_width, min_height);
    char *icon = NULL;
    char *dir = uiw_resource_dir();
    icon = g_build_filename(dir, "icon.png", NULL);
    if (g_file_test(icon, G_FILE_TEST_EXISTS)) gtk_window_set_icon_from_file(GTK_WINDOW(w->window), icon, NULL);
    g_free(icon);
    free(dir);
    gtk_container_add(GTK_CONTAINER(w->window), w->web);
    g_signal_connect(w->window, "delete-event", G_CALLBACK(on_delete), w);
    g_signal_connect(w->window, "destroy", G_CALLBACK(on_destroy), w);
    g_signal_connect(w->window, "key-press-event", G_CALLBACK(on_key), w);
    g_signal_connect(w->web, "notify::title", G_CALLBACK(on_title), w);
    g_ptr_array_add(g_windows, w);
    if (flags & 2) uiw_toggle_fullscreen(w, TRUE);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(w->web), url);
    gtk_widget_show_all(w->window);
    return w->id;
}

void uiw_window_title(int window, const char *title) {
    UiwWindow *w = uiw_find(window);
    if (!w) return;
    w->follow_title = FALSE;
    gtk_window_set_title(GTK_WINDOW(w->window), title && title[0] ? title : g_app_name);
}

void uiw_window_size(int window, int width, int height) {
    UiwWindow *w = uiw_find(window);
    if (w && width > 0 && height > 0) gtk_window_resize(GTK_WINDOW(w->window), width, height);
}

void uiw_window_min_size(int window, int width, int height) {
    UiwWindow *w = uiw_find(window);
    if (w) uiw_min_size(w, width, height);
}

void uiw_window_fullscreen(int window, int on) {
    UiwWindow *w = uiw_find(window);
    if (w) uiw_toggle_fullscreen(w, on != 0);
}

int uiw_window_is_fullscreen(int window) {
    UiwWindow *w = uiw_find(window);
    return w && w->fullscreen ? 1 : 0;
}

void uiw_window_focus(int window) {
    UiwWindow *w = uiw_find(window);
    if (w) gtk_window_present(GTK_WINDOW(w->window));
}

void uiw_window_devtools(int window) {
    UiwWindow *w = uiw_find(window);
    if (w && w->devtools) webkit_web_inspector_show(webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(w->web)));
}

static gboolean close_later(gpointer data) {
    UiwWindow *w = uiw_find(GPOINTER_TO_INT(data));
    if (w) gtk_widget_destroy(w->window);
    return G_SOURCE_REMOVE;
}

// After the page's message handler returns: the web view goes with the window.
void uiw_window_close(int window) { g_idle_add(close_later, GINT_TO_POINTER(window)); }

int uiw_run(void) {
    if (g_windows->len == 0) return g_exit_code;
    g_running = TRUE;
    gtk_main();
    g_running = FALSE;
    return g_exit_code;
}

static gboolean quit_later(gpointer data) {
    g_exit_code = GPOINTER_TO_INT(data);
    if (g_running) gtk_main_quit();
    return G_SOURCE_REMOVE;
}

void uiw_quit(int code) { g_idle_add(quit_later, GINT_TO_POINTER(code)); }

typedef struct { int window; char *script; } UiwEval;

static void eval_in(UiwWindow *w, const char *script) {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(w->web), script, -1, NULL, NULL, NULL, NULL, NULL);
#else
    webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(w->web), script, NULL, NULL, NULL);
#endif
}

static gboolean eval_later(gpointer data) {
    UiwEval *e = data;
    if (e->window == 0) {
        for (guint i = 0; i < g_windows->len; i++) eval_in(g_ptr_array_index(g_windows, i), e->script);
    } else {
        UiwWindow *w = uiw_find(e->window);
        if (w) eval_in(w, e->script);
    }
    g_free(e->script);
    g_free(e);
    return G_SOURCE_REMOVE;
}

// From any thread: the script runs on the main loop, in order.
void uiw_eval(int window, const char *script) {
    UiwEval *e = g_new0(UiwEval, 1);
    e->window = window;
    e->script = g_strdup(script);
    g_main_context_invoke(NULL, eval_later, e);
}

// The answer to a request still waiting: a file's part is streamed from the file.
void uiw_respond(int64_t request, int status, const char *headers, const char *file, int64_t offset, int64_t length,
                 const uint8_t *data) {
    WebKitURISchemeRequest *task = g_hash_table_lookup(g_requests, &request);
    if (!task) return;
    g_object_ref(task);
    g_hash_table_remove(g_requests, &request);
    GInputStream *stream = NULL;
    if (file && length > 0) {
        GFile *f = g_file_new_for_path(file);
        GFileInputStream *in = g_file_read(f, NULL, NULL);
        g_object_unref(f);
        GFileInfo *info = in ? g_file_input_stream_query_info(in, G_FILE_ATTRIBUTE_STANDARD_SIZE, NULL, NULL) : NULL;
        int64_t size = info ? (int64_t)g_file_info_get_size(info) : -1;
        if (info) g_object_unref(info);
        if (in && offset == 0 && length == size) {
            // The whole file, streamed.
            stream = G_INPUT_STREAM(in);
        } else if (in) {
            // A part: WebKit reads a stream to its end, so the part is read into memory.
            guint8 *part = g_malloc((gsize)length);
            gsize got = 0;
            gboolean ok = g_seekable_seek(G_SEEKABLE(in), offset, G_SEEK_SET, NULL, NULL)
                && g_input_stream_read_all(G_INPUT_STREAM(in), part, (gsize)length, &got, NULL, NULL) && got == (gsize)length;
            g_object_unref(in);
            if (ok) stream = g_memory_input_stream_new_from_data(part, length, g_free);
            else g_free(part);
        }
    } else {
        stream = g_memory_input_stream_new_from_data(length > 0 && data ? g_memdup2(data, (gsize)length) : g_strdup(""),
                                                     length > 0 && data ? length : 0, g_free);
    }
    if (!stream) {
        GError *error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "the file cannot be read");
        webkit_uri_scheme_request_finish_error(task, error);
        g_error_free(error);
        g_object_unref(task);
        return;
    }
    const char *type = "application/octet-stream";
    char *content_type = NULL;
#if WEBKIT_CHECK_VERSION(2, 36, 0)
    WebKitURISchemeResponse *response = webkit_uri_scheme_response_new(stream, length);
    webkit_uri_scheme_response_set_status(response, (guint)status, NULL);
    SoupMessageHeaders *out = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
#endif
    char **lines = g_strsplit(headers ? headers : "", "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *colon = strchr(lines[i], ':');
        if (!colon) continue;
        *colon = 0;
        const char *value = colon + 1;
        while (*value == ' ') value++;
        if (g_ascii_strcasecmp(lines[i], "Content-Type") == 0) content_type = g_strdup(value);
#if WEBKIT_CHECK_VERSION(2, 36, 0)
        soup_message_headers_append(out, lines[i], value);
#endif
    }
    g_strfreev(lines);
    if (content_type) type = content_type;
#if WEBKIT_CHECK_VERSION(2, 36, 0)
    webkit_uri_scheme_response_set_content_type(response, type);
    webkit_uri_scheme_response_set_http_headers(response, out);
    webkit_uri_scheme_request_finish_with_response(task, response);
    g_object_unref(response);
#else
    // Without responses (WebKitGTK before 2.36) the status and headers cannot be set.
    webkit_uri_scheme_request_finish(task, stream, length, type);
#endif
    g_free(content_type);
    g_object_unref(stream);
    g_object_unref(task);
}

static GtkWindow *uiw_parent(int window) {
    UiwWindow *w = uiw_find(window);
    return w ? GTK_WINDOW(w->window) : NULL;
}

char *uiw_dialog_file(int window, int flags, const char *title, const char *default_path, const char *filters) {
    GtkFileChooserAction action = (flags & 4) ? GTK_FILE_CHOOSER_ACTION_SAVE
        : (flags & 2) ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN;
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new(title && title[0] ? title : NULL, uiw_parent(window), action,
                                                               (flags & 4) ? "_Save" : "_Open", "_Cancel");
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_select_multiple(chooser, (flags & 1) != 0);
    if (flags & 4) gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    if (default_path && default_path[0]) {
        if (g_file_test(default_path, G_FILE_TEST_IS_DIR)) gtk_file_chooser_set_current_folder(chooser, default_path);
        else {
            char *dir = g_path_get_dirname(default_path);
            char *base = g_path_get_basename(default_path);
            if (g_path_is_absolute(default_path)) gtk_file_chooser_set_current_folder(chooser, dir);
            if (flags & 4) gtk_file_chooser_set_current_name(chooser, base);
            g_free(dir);
            g_free(base);
        }
    }
    char **lines = g_strsplit(filters ? filters : "", "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *tab = strchr(lines[i], '\t');
        if (!tab) continue;
        *tab = 0;
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, lines[i][0] ? lines[i] : tab + 1);
        char **extensions = g_strsplit(tab + 1, ",", -1);
        for (int j = 0; extensions[j]; j++) {
            char *pattern = g_strdup_printf("*.%s", extensions[j]);
            gtk_file_filter_add_pattern(filter, strcmp(extensions[j], "*") == 0 ? "*" : pattern);
            g_free(pattern);
        }
        g_strfreev(extensions);
        gtk_file_chooser_add_filter(chooser, filter);
    }
    g_strfreev(lines);
    char *result = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GString *out = g_string_new("");
        GSList *files = gtk_file_chooser_get_filenames(chooser);
        for (GSList *f = files; f; f = f->next) {
            g_string_append_printf(out, "%s\n", (char *)f->data);
            g_free(f->data);
        }
        g_slist_free(files);
        if (out->len) result = strdup(out->str);
        g_string_free(out, TRUE);
    }
    g_object_unref(dialog);
    return result;
}

int uiw_dialog_message(int window, int buttons, int level, const char *title, const char *message, const char *ok,
                       const char *cancel) {
    GtkMessageType type = level == 2 ? GTK_MESSAGE_ERROR : level == 1 ? GTK_MESSAGE_WARNING
        : buttons == 1 ? GTK_MESSAGE_QUESTION : GTK_MESSAGE_INFO;
    gboolean titled = title && title[0];
    GtkWidget *dialog = gtk_message_dialog_new(uiw_parent(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type,
                                               GTK_BUTTONS_NONE, "%s", titled ? title : (message ? message : ""));
    if (titled) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message ? message : "");
    gtk_window_set_title(GTK_WINDOW(dialog), titled ? title : g_app_name);
    if (buttons == 1) gtk_dialog_add_button(GTK_DIALOG(dialog), cancel && cancel[0] ? cancel : "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), ok && ok[0] ? ok : "OK", GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    int answer = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return answer == GTK_RESPONSE_OK ? 1 : 0;
}
