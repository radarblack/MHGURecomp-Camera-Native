#pragma once
#include <stddef.h>
#include <stdint.h>

#include "host.h"

typedef struct BcCpuFull {
    uint32_t r[16];
    uint32_t pc;
    uint32_t nf, zf, cf, vf;
    uint32_t qf;
    uint32_t ge;
    uint32_t fpscr;
    uint32_t tpidruro;
    uint32_t tpidrurw;
    uint32_t excl_addr;
    uint32_t _pad0;
    uint64_t excl_val;
    void *thread;
    uint8_t _pad[256 - 128];
    union {
        uint64_t d[32];
        uint32_t s[64];
        float f[64];
    } v;
} BcCpuFull;

_Static_assert(offsetof(BcCpuFull, pc) == 64, "cpu: pc offset");
_Static_assert(offsetof(BcCpuFull, tpidruro) == 96, "cpu: tls offset");
_Static_assert(offsetof(BcCpuFull, v) == 256, "cpu: vfp offset");
_Static_assert(sizeof(BcCpuFull) == 512, "cpu: total size");

int bc_collision_clamp(BcCpu *c, const float pivot[3], float eye[3]);
