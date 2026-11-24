#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct __attribute__((aligned(16))) BcCpu {
    uint32_t r[16];
    uint8_t reserved[256 - 64];
    float f[64];
} BcCpu;

_Static_assert(sizeof(BcCpu) == 512, "BcCpu must match the port's Cpu");
_Static_assert(__builtin_offsetof(BcCpu, f) == 256, "BcCpu VFP file must sit at 256");

typedef void (*BcHostFn)(BcCpu *c);

typedef struct BcHost {
    bool (*fnhook_set)(uint32_t addr, uint32_t detour);
    uint32_t (*fnhook_get)(uint32_t addr);
    void (*fnhook_call_original)(BcCpu *c, uint32_t addr);
    uint32_t (*hle_register)(const char *name, BcHostFn fn);
    void *(*dispatch_lookup)(uint32_t addr);
    void (*guest_call)(BcCpu *c, uint32_t addr);
    uint32_t (*mouse_state)(float *dx, float *dy);
    bool (*textinput_active)(void);
    bool (*chat_active)(void);
    bool (*overlay_menu_open)(void);
    bool (*mouse_free)(void);
    bool (*menuptr_open)(void);
    bool (*menuptr_cursor_ran)(uint64_t within_ns);
    const char *(*view_context)(void);
    void *(*sdl_focus)(void);
    bool (*sdl_relmode)(void *window);
    bool (*sdl_add_watch)(bool (*filter)(void *userdata, void *event), void *userdata);
    void (*sdl_remove_watch)(bool (*filter)(void *userdata, void *event), void *userdata);
} BcHost;

extern BcHost g_host;
bool bc_host_resolve(void);
