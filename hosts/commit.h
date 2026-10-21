// Decodes an Eskiu ui commit (the byte format documented in src/ui.esk) into one call
// per record. Shared by the native hosts; header-only, C and C++.
#ifndef UI_COMMIT_H
#define UI_COMMIT_H
#include <stdint.h>

typedef void (*ui_commit_fn)(int op, int id, int generation, int parent, int before,
                             int kind, const char *text);
// A command (op 10): its request id, the command (1 alert: title, message, buttons; 2 open
// a URL) and its strings.
typedef void (*ui_command_fn)(int request, int command, int count, const char **strings);
// A native view's named prop (op 11): the node, the prop's name and its value ("" removed).
typedef void (*ui_named_fn)(int id, const char *name, const char *value);

static inline int ui_commit_u32(const uint8_t *p) {
    return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                 ((uint32_t)p[3] << 24));
}

// Props arrive as op 2 with the prop code in `kind` and its value in `before`; string props
// (op 9) with the prop code in `kind` and the value in `text`.
static inline void ui_commit_each(const uint8_t *data, int len, ui_commit_fn apply, ui_command_fn command,
                                  ui_named_fn named) {
    int at = 0;
    while (at < len) {
        int op = data[at];
        int id = ui_commit_u32(data + at + 1);
        at += 5;
        int generation = 0, parent = 0, before = 0, kind = 0;
        const char *text = 0;
        if (op == 1) { generation = ui_commit_u32(data + at); kind = data[at + 4]; at += 5; }
        else if (op == 2) { kind = data[at]; before = ui_commit_u32(data + at + 1); at += 5; }
        else if (op == 3) {
            int n = ui_commit_u32(data + at);
            text = (const char *)(data + at + 4);
            at += 4 + n + 1;
        } else if (op == 4 || op == 5) {
            parent = ui_commit_u32(data + at); before = ui_commit_u32(data + at + 4); at += 8;
        } else if (op == 10) {
            int which = data[at], count = data[at + 1];
            at += 2;
            const char *strings[32];
            for (int i = 0; i < count; i++) {
                int n = ui_commit_u32(data + at);
                if (i < 32) strings[i] = (const char *)(data + at + 4);
                at += 4 + n + 1;
            }
            command(id, which, count < 32 ? count : 32, strings);
            continue;
        } else if (op == 11) {
            int n = ui_commit_u32(data + at);
            const char *name = (const char *)(data + at + 4);
            at += 4 + n + 1;
            int v = ui_commit_u32(data + at);
            const char *value = (const char *)(data + at + 4);
            at += 4 + v + 1;
            named(id, name, value);
            continue;
        } else if (op == 9) {
            kind = data[at];
            int n = ui_commit_u32(data + at + 1);
            text = (const char *)(data + at + 5);
            at += 5 + n + 1;
        }
        apply(op, id, generation, parent, before, kind, text);
    }
}
#endif
