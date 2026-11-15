#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct BcCpu {
    uint32_t r[16];
} BcCpu;

typedef void (*BcHostFn)(BcCpu *c);

typedef struct BcHost {
    bool (*fnhook_set)(uint32_t addr, uint32_t detour);
    uint32_t (*fnhook_get)(uint32_t addr);
    void (*fnhook_call_original)(BcCpu *c, uint32_t addr);
    uint32_t (*hle_register)(const char *name, BcHostFn fn);
    void (*guest_call)(BcCpu *c, uint32_t addr);
    void *(*dispatch_lookup)(uint32_t addr);
} BcHost;

extern BcHost g_host;
bool bc_host_resolve(void);
