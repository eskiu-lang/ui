// A stand-in for the <ui/webapp> hosts (hosts/*/webapp.*) for tests/webapp.esk: each call is
// recorded as a line in a log the test reads back and clears, and the dialogs answer what
// the test sets.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *log_text;
static size_t log_len;
static int next_window;
const char *uiw_test_dialog_paths;
int uiw_test_dialog_choice;
int uiw_test_quit_code = -1;

static void record(const char *line) {
    size_t n = strlen(line);
    log_text = realloc(log_text, log_len + n + 2);
    memcpy(log_text + log_len, line, n);
    log_len += n;
    log_text[log_len++] = '\n';
    log_text[log_len] = 0;
}

static void recordf(const char *format, const char *a, const char *b, long long x, long long y) {
    size_t n = strlen(format) + (a ? strlen(a) : 0) + (b ? strlen(b) : 0) + 64;
    char *line = malloc(n);
    snprintf(line, n, format, a ? a : "", b ? b : "", x, y);
    record(line);
    free(line);
}

const char *uiw_test_log(void) { return log_text ? log_text : ""; }
void uiw_test_clear(void) { log_len = 0; if (log_text) log_text[0] = 0; }

void uiw_init(const char *name, const char *bridge) { recordf("init %s%s", name, "", 0, 0); (void)bridge; }
const char *uiw_platform(void) { return "macos"; }
char *uiw_resource_dir(void) { return strdup("tests/webapp"); }
void uiw_scheme(const char *name) { recordf("scheme %s%s", name, "", 0, 0); }
int uiw_window_open(const char *title, const char *url, int width, int height, int min_width, int min_height, int flags) {
    (void)min_width; (void)min_height;
    recordf("window %s %s %lld %lld", title, url, width, height);
    recordf("flags %s%s%lld", "", "", flags, 0);
    return ++next_window;
}
void uiw_window_title(int window, const char *title) { recordf("title %s%s %lld", title, "", window, 0); }
void uiw_window_size(int window, int width, int height) { recordf("size%s%s %lld %lld", "", "", width, height); (void)window; }
void uiw_window_min_size(int window, int width, int height) { recordf("min%s%s %lld %lld", "", "", width, height); (void)window; }
void uiw_window_fullscreen(int window, int on) { recordf("fullscreen%s%s %lld %lld", "", "", window, on); }
int uiw_window_is_fullscreen(int window) { (void)window; return 1; }
void uiw_window_focus(int window) { recordf("focus%s%s %lld", "", "", window, 0); }
void uiw_window_devtools(int window) { recordf("devtools%s%s %lld", "", "", window, 0); }
void uiw_window_close(int window) { recordf("close%s%s %lld", "", "", window, 0); }
int uiw_run(void) { return 0; }
void uiw_quit(int code) { uiw_test_quit_code = code; recordf("quit%s%s %lld", "", "", code, 0); }
void uiw_eval(int window, const char *script) { recordf("eval %s%s @%lld", script, "", window, 0); }

void uiw_respond(int64_t request, int status, const char *headers, const char *file, int64_t offset, int64_t length, const uint8_t *data) {
    (void)request;
    char line[128];
    snprintf(line, sizeof line, "respond %d", status);
    record(line);
    // The headers, one per line, indented.
    const char *h = headers ? headers : "";
    while (*h) {
        const char *end = strchr(h, '\n');
        size_t n = end ? (size_t)(end - h) : strlen(h);
        char *one = malloc(n + 3);
        one[0] = ' '; one[1] = ' ';
        memcpy(one + 2, h, n);
        one[n + 2] = 0;
        record(one);
        free(one);
        h += n + (end ? 1 : 0);
    }
    if (file) recordf("  file %s%s %lld+%lld", file, "", offset, length);
    else if (data) {
        char *body = malloc((size_t)length + 1);
        memcpy(body, data, (size_t)length);
        body[length] = 0;
        recordf("  body %s%s", body, "", 0, 0);
        free(body);
    }
}

char *uiw_dialog_file(int window, int flags, const char *title, const char *default_path, const char *filters) {
    (void)window;
    recordf("dialog %s|%s", title, default_path, flags, 0);
    recordf("filters %s%s", filters, "", 0, 0);
    return uiw_test_dialog_paths ? strdup(uiw_test_dialog_paths) : NULL;
}

int uiw_dialog_message(int window, int buttons, int level, const char *title, const char *message, const char *ok, const char *cancel) {
    (void)window; (void)ok; (void)cancel;
    recordf("message %s|%s %lld %lld", title, message, buttons, level);
    return uiw_test_dialog_choice;
}
