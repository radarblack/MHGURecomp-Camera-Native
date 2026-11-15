#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BC_GUEST_BASE 0x10000000000ull
#define BC_GPTR(a) ((void *)(uintptr_t)(BC_GUEST_BASE + (uint32_t)(a)))

static inline uint8_t bc_ld8(uint32_t a) { return *(const uint8_t *)BC_GPTR(a); }
static inline uint16_t bc_ld16(uint32_t a) {
    uint16_t v;
    memcpy(&v, BC_GPTR(a), sizeof v);
    return v;
}
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
static inline bool bc_sane(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3); }

#define BC_FN_LOOKAT 0x21c33cu
#define BC_G_SGAMECONTROL 0x1a8ca48u
#define BC_G_SPLAYER 0x1a85b58u
#define BC_FN_GET_MY_PLAYER 0x276278u
#define BC_GC_AREA_TYPE 0x4cu
#define BC_GC_PLAYER_SLOT 0x30u
#define BC_GC_INVERT 0x64u
#define BC_GC_CAM_SPEED 0x65u
#define BC_SC_PLAYER_SLOTS 0x1cu
#define BC_CAM_ASPECT 0x38u
#define BC_CAM_EYE 0x40u
#define BC_CAM_UP 0x50u
#define BC_CAM_TARGET 0x60u
#define BC_VT_CAM_ACTION 0x2e8u
#define BC_VT_TILT 0x238u
