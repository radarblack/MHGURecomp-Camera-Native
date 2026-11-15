#include "debug.h"

#if BC_DEBUG > 0

#include <stdio.h>

#include "platform.h"

static void (*g_sink)(const char *fmt, ...);
static uint64_t g_t0;

void bc_debug_set_sink(void (*sink)(const char *fmt, ...)) {
    g_sink = sink;
    g_t0 = bc_now_ms();
}

void bc_debug_log(const char *fmt, ...) {
    if (!g_sink) return;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    uint64_t dt = bc_now_ms() - g_t0;
    g_sink("[bc %llu.%03llu] %s\n", (unsigned long long)(dt / 1000u), (unsigned long long)(dt % 1000u), msg);
}

#endif
