#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BC_GUEST_BASE 0x10000000000ull
#define BC_GPTR(a) ((void *)(uintptr_t)(BC_GUEST_BASE + (uint32_t)(a)))

static inline uint8_t bc_ld8(uint32_t a) { return *(const uint8_t *)BC_GPTR(a); }
static inline uint32_t bc_ld32(uint32_t a) {
    uint32_t v;
    memcpy(&v, BC_GPTR(a), sizeof v);
    return v;
}
static inline float bc_ldf(uint32_t a) {
    float v;
    memcpy(&v, BC_GPTR(a), sizeof v);
    return v;
}
static inline void bc_st32(uint32_t a, uint32_t v) { memcpy(BC_GPTR(a), &v, sizeof v); }
static inline void bc_stf(uint32_t a, float v) { memcpy(BC_GPTR(a), &v, sizeof v); }

#define BC_FN_BUILDER 0x9c5e4cu
#define BC_BUILDER_PROLOGUE 0xe92d4070u
#define BC_G_SGAMECONTROL 0x1a8ca48u
#define BC_GC_AREA_TYPE 0x4cu
#define BC_G_SPLAYER 0x1a85b58u
#define BC_GC_SLOT 0x30u
#define BC_SP_PLAYERS 0x1cu
#define BC_PL_ZONE 0x1054u
#define BC_PL_POS 0x40u
#define BC_PL_ROT 0x50u
