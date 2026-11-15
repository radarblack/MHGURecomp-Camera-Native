#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#ifdef _WIN32
void bc_sleep_ms(unsigned ms) { Sleep(ms); }
uint64_t bc_now_ms(void) { return (uint64_t)GetTickCount64(); }
void *bc_find_symbol(const char *name) {
    HMODULE exe = GetModuleHandleA(NULL);
    return exe ? (void *)GetProcAddress(exe, name) : NULL;
}
typedef struct BcThreadArgs {
    BcThreadFn fn;
    void *arg;
} BcThreadArgs;
static DWORD WINAPI bc_thread_tramp(LPVOID p) {
    BcThreadArgs *a = (BcThreadArgs *)p;
    a->fn(a->arg);
    free(a);
    return 0;
}
bool bc_thread_start(BcThread *t, BcThreadFn fn, void *arg) {
    BcThreadArgs *a = (BcThreadArgs *)malloc(sizeof *a);
    if (!a) return false;
    a->fn = fn;
    a->arg = arg;
    t->handle = CreateThread(NULL, 0, bc_thread_tramp, a, 0, NULL);
    t->started = t->handle != NULL;
    if (!t->started) free(a);
    return t->started;
}
bool bc_thread_join(BcThread *t, unsigned timeout_ms) {
    if (!t->started) return false;
    bool ok = WaitForSingleObject(t->handle, timeout_ms) == WAIT_OBJECT_0;
    if (ok) CloseHandle(t->handle);
    t->started = false;
    return ok;
}
#else
void bc_sleep_ms(unsigned ms) {
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
uint64_t bc_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
void *bc_find_symbol(const char *name) { return dlsym(RTLD_DEFAULT, name); }
typedef struct BcThreadArgs {
    BcThreadFn fn;
    void *arg;
} BcThreadArgs;
static void *bc_thread_tramp(void *p) {
    BcThreadArgs *a = (BcThreadArgs *)p;
    a->fn(a->arg);
    free(a);
    return NULL;
}
bool bc_thread_start(BcThread *t, BcThreadFn fn, void *arg) {
    BcThreadArgs *a = (BcThreadArgs *)malloc(sizeof *a);
    if (!a) return false;
    a->fn = fn;
    a->arg = arg;
    t->started = pthread_create(&t->thread, NULL, bc_thread_tramp, a) == 0;
    if (!t->started) free(a);
    return t->started;
}
bool bc_thread_join(BcThread *t, unsigned timeout_ms) {
    if (!t->started) return false;
    pthread_join(t->thread, NULL);
    t->started = false;
    return true;
}
#endif
