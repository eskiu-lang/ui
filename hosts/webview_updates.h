// Asset updates in the webview hosts (docs/updates.md): no service worker runs under their
// schemes, so the host keeps the bundle's files in the app's data directory, under
// ui-updates/ by hash, and serves the bundle in use in place of the bundled files. The page
// (hosts/web/updates.mjs) asks it at __ui/updates/OP:
//
//   fetch?url=U&hash=H&path=P   download U, check its SHA-256, keep it as ui-updates/H
//   ready?version=V&file=P%20H  the downloaded bundle, used from the next page load
//   apply                       use it now
//
// Each answer is text: "" or why not. The state (ui-updates/state) is the bundle in use and
// the waiting one: "active V" or "ready V" lines, each followed by its "PATH HASH" lines.
// The rule is the service worker's: a page load makes the waiting bundle the one in use and
// drops the files no bundle needs; a file of the bundle in use is served from ui-updates/,
// any other from the app. Pure C, header-only, for C, Objective-C and C++.
#ifndef UI_WEBVIEW_UPDATES_H
#define UI_WEBVIEW_UPDATES_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *path;
    char hash[65];
} UiUpdateFile;

typedef struct {
    long long version;
    int count;
    UiUpdateFile *files;
} UiUpdateSet;

typedef struct {
    UiUpdateSet active, ready;
} UiUpdateState;

static inline void ui_updates_set_free(UiUpdateSet *set) {
    for (int i = 0; i < set->count; i++) free(set->files[i].path);
    free(set->files);
    set->version = 0;
    set->count = 0;
    set->files = NULL;
}

static inline int ui_updates_hash_ok(const char *hash) {
    if (!hash || strlen(hash) != 64) return 0;
    for (int i = 0; i < 64; i++) {
        char c = hash[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

// A path in assets/, as the updater takes them: no spaces or controls, no backslash, no "..".
static inline int ui_updates_path_ok(const char *path) {
    if (!path || !path[0] || path[0] == '/') return 0;
    for (const char *c = path; *c; c++) {
        if ((unsigned char)*c <= ' ' || *c == '\\' || *c == 127) return 0;
    }
    const char *at = path;
    while (at) {
        if (at[0] == '.' && at[1] == '.' && (at[2] == '/' || at[2] == 0)) return 0;
        at = strchr(at, '/');
        if (at) at++;
    }
    return 1;
}

// Adds "PATH HASH" to a set; false when the line is not one.
static inline int ui_updates_add(UiUpdateSet *set, const char *line, size_t length) {
    const char *space = (const char *)memchr(line, ' ', length);
    if (!space || (size_t)(line + length - space - 1) != 64) return 0;
    char hash[65];
    memcpy(hash, space + 1, 64);
    hash[64] = 0;
    size_t path_length = (size_t)(space - line);
    char *path = (char *)malloc(path_length + 1);
    memcpy(path, line, path_length);
    path[path_length] = 0;
    if (!ui_updates_hash_ok(hash) || !ui_updates_path_ok(path)) {
        free(path);
        return 0;
    }
    UiUpdateFile *files = (UiUpdateFile *)realloc(set->files, sizeof(UiUpdateFile) * (size_t)(set->count + 1));
    if (!files) {
        free(path);
        return 0;
    }
    set->files = files;
    set->files[set->count].path = path;
    memcpy(set->files[set->count].hash, hash, 65);
    set->count++;
    return 1;
}

static inline void ui_updates_free(UiUpdateState *state) {
    ui_updates_set_free(&state->active);
    ui_updates_set_free(&state->ready);
}

static inline void ui_updates_parse(UiUpdateState *state, const char *text) {
    ui_updates_free(state);
    UiUpdateSet *set = NULL;
    while (text && *text) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (length > 7 && strncmp(text, "active ", 7) == 0) {
            set = &state->active;
            set->version = atoll(text + 7);
        } else if (length > 6 && strncmp(text, "ready ", 6) == 0) {
            set = &state->ready;
            set->version = atoll(text + 6);
        } else if (set) {
            ui_updates_add(set, text, length);
        }
        text = end ? end + 1 : NULL;
    }
}

// The state as the text kept in ui-updates/state (malloc'd).
static inline char *ui_updates_format(const UiUpdateState *state) {
    size_t size = 64;
    const UiUpdateSet *sets[2] = {&state->active, &state->ready};
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < sets[k]->count; i++) size += strlen(sets[k]->files[i].path) + 67;
    char *text = (char *)malloc(size);
    if (!text) return NULL;
    size_t at = 0;
    for (int k = 0; k < 2; k++) {
        if (sets[k]->version <= 0) continue;
        at += (size_t)snprintf(text + at, size - at, "%s %lld\n", k == 0 ? "active" : "ready", sets[k]->version);
        for (int i = 0; i < sets[k]->count; i++)
            at += (size_t)snprintf(text + at, size - at, "%s %s\n", sets[k]->files[i].path, sets[k]->files[i].hash);
    }
    text[at] = 0;
    return text;
}

// The hash of the bundle in use's file at `path`, or NULL: the app's own file is served.
static inline const char *ui_updates_lookup(const UiUpdateState *state, const char *path) {
    for (int i = 0; i < state->active.count; i++)
        if (strcmp(state->active.files[i].path, path) == 0) return state->active.files[i].hash;
    return NULL;
}

// The waiting bundle becomes the one in use; false when none waits.
static inline int ui_updates_promote(UiUpdateState *state) {
    if (state->ready.version <= 0) return 0;
    ui_updates_set_free(&state->active);
    state->active = state->ready;
    state->ready.version = 0;
    state->ready.count = 0;
    state->ready.files = NULL;
    return 1;
}

// Whether a name in ui-updates/ is a file a bundle needs (the state and downloads in progress
// are kept too).
static inline int ui_updates_needed(const UiUpdateState *state, const char *name) {
    size_t length = strlen(name);
    if (strcmp(name, "state") == 0 || (length > 5 && strcmp(name + length - 5, ".part") == 0)) return 1;
    const UiUpdateSet *sets[2] = {&state->active, &state->ready};
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < sets[k]->count; i++)
            if (strcmp(sets[k]->files[i].hash, name) == 0) return 1;
    return 0;
}

// The value of a query's `index`th `key`, percent-decoded (malloc'd), or NULL.
static inline char *ui_updates_query(const char *query, const char *key, int index) {
    size_t key_length = strlen(key);
    while (query && *query) {
        const char *end = strchr(query, '&');
        size_t length = end ? (size_t)(end - query) : strlen(query);
        if (length > key_length && strncmp(query, key, key_length) == 0 && query[key_length] == '=' && index-- == 0) {
            const char *value = query + key_length + 1;
            size_t value_length = length - key_length - 1;
            char *out = (char *)malloc(value_length + 1);
            size_t n = 0;
            for (size_t i = 0; i < value_length; i++) {
                char c = value[i];
                if (c == '+') c = ' ';
                else if (c == '%' && i + 2 < value_length) {
                    char hex[3] = {value[i + 1], value[i + 2], 0};
                    c = (char)strtol(hex, NULL, 16);
                    i += 2;
                }
                out[n++] = c;
            }
            out[n] = 0;
            return out;
        }
        query = end ? end + 1 : NULL;
    }
    return NULL;
}

// `ready`'s query: the waiting bundle; false when it is not one.
static inline int ui_updates_set_ready(UiUpdateState *state, const char *query) {
    char *version = ui_updates_query(query, "version", 0);
    long long number = version ? atoll(version) : 0;
    free(version);
    if (number <= 0) return 0;
    UiUpdateSet set = {number, 0, NULL};
    for (int i = 0;; i++) {
        char *line = ui_updates_query(query, "file", i);
        if (!line) break;
        int ok = ui_updates_add(&set, line, strlen(line));
        free(line);
        if (!ok) {
            ui_updates_set_free(&set);
            return 0;
        }
    }
    ui_updates_set_free(&state->ready);
    state->ready = set;
    return 1;
}

// SHA-256 (FIPS 180-4) of `data`, as 64 lowercase hex digits.
static inline void ui_updates_sha256(const uint8_t *data, size_t length, char out[65]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
#define UI_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
    uint64_t bits = (uint64_t)length * 8;
    size_t blocks = (length + 9 + 63) / 64;
    for (size_t b = 0; b < blocks; b++) {
        uint8_t block[64];
        for (size_t i = 0; i < 64; i++) {
            size_t at = b * 64 + i;
            if (at < length) block[i] = data[at];
            else if (at == length) block[i] = 0x80;
            else block[i] = 0;
        }
        if (b == blocks - 1)
            for (int i = 0; i < 8; i++) block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) | ((uint32_t)block[4 * i + 2] << 8) |
                   block[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = UI_ROTR(w[i - 15], 7) ^ UI_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = UI_ROTR(w[i - 2], 17) ^ UI_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (UI_ROTR(e, 6) ^ UI_ROTR(e, 11) ^ UI_ROTR(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (UI_ROTR(a, 2) ^ UI_ROTR(a, 13) ^ UI_ROTR(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
#undef UI_ROTR
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", (unsigned)h[i]);
    out[64] = 0;
}

// Whether `data` is the file with this hash.
static inline int ui_updates_matches(const uint8_t *data, size_t length, const char *hash) {
    char got[65];
    ui_updates_sha256(data, length, got);
    return strcmp(got, hash) == 0;
}

// The page's console, forwarded by a build that is not a release to the program's standard
// output (the host's message handler `console` on WebKit, chrome.webview on WebView2).
#define UI_WEBVIEW_CONSOLE_SCRIPT                                                                            \
    "(() => { const post = text => { try { (window.webkit?.messageHandlers?.console ?? window.chrome?.webview)" \
    "?.postMessage(text); } catch (e) {} }; for (const level of ['log', 'info', 'warn', 'error']) { "         \
    "const original = console[level]; console[level] = (...args) => { post(args.map(String).join(' ')); "     \
    "original.apply(console, args); }; } })();"

// The type of a file by its extension, for an updated file.
static inline const char *ui_updates_mime(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(slash ? slash : path, '.');
    if (!dot) return "application/octet-stream";
    static const char *types[][2] = {
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
        {".webp", "image/webp"}, {".svg", "image/svg+xml"}, {".json", "application/json"},
        {".txt", "text/plain; charset=utf-8"}, {".css", "text/css"}, {".ttf", "font/ttf"}, {".otf", "font/otf"},
        {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".mp3", "audio/mpeg"}, {".wav", "audio/wav"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".wasm", "application/wasm"},
        {".html", "text/html; charset=utf-8"}, {".ico", "image/x-icon"}};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        const char *a = dot, *b = types[i][0];
        while (*a && *b && (*a | 32) == *b) { a++; b++; }
        if (!*a && !*b) return types[i][1];
    }
    return "application/octet-stream";
}

#endif
