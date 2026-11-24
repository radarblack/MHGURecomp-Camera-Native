#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <dlfcn.h>
#include <sys/mman.h>
#endif

#ifdef _WIN32
void bc_sleep_ms(unsigned ms) { Sleep(ms); }
uint64_t bc_now_ns(void) {
    static volatile LONGLONG freq;
    LONGLONG f = freq;
    if (!f) {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        f = q.QuadPart;
        freq = f;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    uint64_t ticks = (uint64_t)t.QuadPart, hz = (uint64_t)f;
    return (ticks / hz) * 1000000000ull + (ticks % hz) * 1000000000ull / hz;
}
uint64_t bc_now_ms(void) { return bc_now_ns() / 1000000ull; }
void *bc_find_symbol(const char *name) {
    HMODULE exe = GetModuleHandleA(NULL);
    return exe ? (void *)GetProcAddress(exe, name) : NULL;
}
void *bc_find_symbol_in_lib(const char *lib) {
    HMODULE m = lib ? GetModuleHandleA(lib) : NULL;
    return (void *)m;
}
void *bc_sym_in(void *lib, const char *name) {
    HMODULE m = (HMODULE)lib;
    return m ? (void *)GetProcAddress(m, name) : NULL;
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
bool bc_guest_readable(uint32_t guest_addr, uint32_t len) {
    const unsigned char *a = (const unsigned char *)(uintptr_t)(0x10000000000ull + guest_addr);
    size_t done = 0;
    while (done < len) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(a + done, &mbi, sizeof mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        size_t off = (size_t)((a + done) - (const unsigned char *)mbi.BaseAddress);
        done = off + mbi.RegionSize;
    }
    return true;
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
uint64_t bc_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
uint64_t bc_now_ms(void) { return bc_now_ns() / 1000000ull; }
void *bc_find_symbol(const char *name) { return dlsym(RTLD_DEFAULT, name); }
void *bc_find_symbol_in_lib(const char *lib) {
    void *h = lib ? dlopen(lib, RTLD_LAZY | RTLD_NOLOAD) : NULL;
    return h;
}
void *bc_sym_in(void *lib, const char *name) {
    return lib ? dlsym(lib, name) : NULL;
}
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
bool bc_guest_readable(uint32_t guest_addr, uint32_t len) {
    const unsigned char *a = (const unsigned char *)(uintptr_t)(0x10000000000ull + guest_addr);
    uintptr_t page = (uintptr_t)a & ~(uintptr_t)0xfffull;
    size_t span = ((uintptr_t)a + len - page + 0xfffull) & ~(size_t)0xfffull;
    return msync((void *)page, span, MS_ASYNC) == 0;
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
