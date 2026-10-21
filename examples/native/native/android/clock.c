// The clock module on Android: the externs Eskiu calls, over a thread that sleeps. The ticks
// come from that thread; ui_native_emit takes them to the UI thread.
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "ui_native.h"

static atomic_int running;
static atomic_int period;
static atomic_int generation;

static void *tick(void *arg) {
    int mine = (int)(long)arg;
    while (atomic_load(&running) && atomic_load(&generation) == mine) {
        usleep(atomic_load(&period) * 1000);
        if (atomic_load(&running) && atomic_load(&generation) == mine) ui_native_emit("clock", "tick");
    }
    return 0;
}

void clock_start(int ms) {
    atomic_store(&period, ms);
    atomic_store(&running, 1);
    // A new generation: an older thread still asleep sees it and ends.
    int mine = atomic_fetch_add(&generation, 1) + 1;
    pthread_t thread;
    pthread_create(&thread, 0, tick, (void *)(long)mine);
    pthread_detach(thread);
}

void clock_stop(void) { atomic_store(&running, 0); }
