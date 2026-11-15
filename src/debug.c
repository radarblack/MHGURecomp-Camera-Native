#include "debug.h"

#if BC_DEBUG > 0

#include <stdio.h>
#include <time.h>

#include "platform.h"

#ifndef BC_VERSION
#define BC_VERSION "0.1.0"
#endif

#define BC_LOG_FILE_NAME "BetterCamera_logs.txt"

static void (*g_sink)(const char *fmt, ...);
static FILE *g_file;
static uint64_t g_t0;

#if defined(_WIN32)
static CRITICAL_SECTION g_lock;
static int g_lock_init;
#define BC_LOCK()                                 \
    do {                                          \
        if (!g_lock_init) {                       \
            InitializeCriticalSection(&g_lock);   \
            g_lock_init = 1;                      \
        }                                         \
        EnterCriticalSection(&g_lock);            \
    } while (0)
#define BC_UNLOCK() LeaveCriticalSection(&g_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define BC_LOCK() pthread_mutex_lock(&g_lock)
#define BC_UNLOCK() pthread_mutex_unlock(&g_lock)
#endif

void bc_debug_set_sink(void (*sink)(const char *fmt, ...)) {
    g_sink = sink;
    g_t0 = bc_now_ms();
}

void bc_debug_open_file(const char *module_dir) {
    char path[520];
    if (module_dir && *module_dir)
        snprintf(path, sizeof path, "%s/%s", module_dir, BC_LOG_FILE_NAME);
    else
        snprintf(path, sizeof path, "%s", BC_LOG_FILE_NAME);
    BC_LOCK();
    if (!g_file) g_file = fopen(path, "ab");
    if (g_file) {
        time_t now = time(NULL);
        char stamp[64];
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(g_file, "\n==== BetterCamera debug session %s (v%s) ====\n", stamp, BC_VERSION);
        fflush(g_file);
    }
    BC_UNLOCK();
    if (g_file)
        bc_debug_log("file logging: %s", path);
    else
        bc_debug_log("file logging FAILED: could not open %s", path);
}

void bc_debug_log(const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    uint64_t dt = bc_now_ms() - g_t0;
    BC_LOCK();
    if (g_file) {
        fprintf(g_file, "[%8.3f] %s\n", (double)dt / 1000.0, msg);
        fflush(g_file);
    }
    if (g_sink) g_sink("[bc %llu.%03llu] %s\n", (unsigned long long)(dt / 1000u), (unsigned long long)(dt % 1000u), msg);
    BC_UNLOCK();
}

#endif
