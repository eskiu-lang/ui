#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int64_t live;
static int64_t peak;
static int64_t calls;
void *ui_test_malloc(size_t bytes) {
    void *p = calloc(1, bytes ? (size_t)bytes : 1);
    if (!p) abort();
    calls++;
    live++;
    if (live > peak) peak = live;
    return p;
}
void ui_test_free(void *p) {
    if (p) { live--; free(p); }
}
void *ui_test_calloc(size_t count, size_t bytes) {
    if (bytes && count > SIZE_MAX / bytes) abort();
    return ui_test_malloc(count * bytes);
}
int64_t ui_live_allocations(void) { return live; }
// False when the test IR was not rewritten to these allocators (tools/ci.sh).
_Bool ui_allocations_counted(void) { return calls > 0; }
int64_t ui_peak_allocations(void) { return peak; }
double ui_clock_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
