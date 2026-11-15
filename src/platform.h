#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

void bc_sleep_ms(unsigned ms);
uint64_t bc_now_ms(void);
void *bc_find_symbol(const char *name);

typedef void (*BcThreadFn)(void *arg);
typedef struct BcThread {
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t thread;
#endif
    BcThreadFn fn;
    void *arg;
    bool started;
} BcThread;

bool bc_thread_start(BcThread *t, BcThreadFn fn, void *arg);
bool bc_thread_join(BcThread *t, unsigned timeout_ms);
