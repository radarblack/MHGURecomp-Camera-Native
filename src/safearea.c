#include "safearea.h"

#include <math.h>
#include <string.h>

#include "debug.h"
#include "safearea_math.h"

#include "config.h"
#include "guest.h"
#include "host.h"
#include "platform.h"

#ifndef BC_MOUSE_DEG_PER_PX
#define BC_MOUSE_DEG_PER_PX 0.15f
#endif

static uint32_t g_hooked_fn;
static uint32_t g_hle_id;
static uint32_t g_frames;
static int g_last_active;
static uint8_t g_last_area = 0xff;
static int g_gc_logged;
static int g_first_call_done;
static float g_acc_yaw;
static float g_acc_pitch;

enum { R_OK, R_CFG_OFF, R_VEC_BAD, R_GC_BAD, R_QUEST, R_UI_OPEN, R_MOUSE_IDLE };
static const char *const g_reason[] = {"ok", "feature off (SafeAreaCamera=0)",
    "view vectors not sane (not readable, not finite, or degenerate)", "sGameControl not ready (pointer null or object not readable)",
    "quest area (the game's own camera runs)", "a text input or chat box is open", "no orbit (accumulator zero, mouse idle)"};

static float vec_finite(uint32_t p) {
    for (uint32_t i = 0; i < 3u; i++) {
        float v = bc_ldf(p + 4u * i);
        if (!(v == v) || v > 1e9f || v < -1e9f) return 0.0f;
    }
    return 1.0f;
}

static void detour_builder(BcCpu *c) {
    uint32_t eye_p = c->r[1], target_p = c->r[2], up_p = c->r[3];
    g_frames++;
    int sample = (g_frames % 30u) == 1u;
    int active = 0;
    int reason = R_MOUSE_IDLE;
    float eye[3], target[3];
    do {
        if (!g_cfg.safe_area_camera) {
            reason = R_CFG_OFF;
            break;
        }
        uint32_t gcp = bc_ld32(BC_G_SGAMECONTROL);
        if (!gcp || !bc_guest_readable(gcp, 0x80u)) {
            reason = R_GC_BAD;
            break;
        }
        if (!g_gc_logged) {
            g_gc_logged = 1;
            BC_LOG("guest: sGameControl = %08x", (unsigned)gcp);
        }
        uint8_t area = bc_ld8(gcp + BC_GC_AREA_TYPE);
        if (!g_first_call_done) {
            g_first_call_done = 1;
            BC_LOG("detour: FIRST CALL (eye*=%08x target*=%08x, area byte = %u)", (unsigned)eye_p,
                (unsigned)target_p, (unsigned)area);
        }
        if (area != g_last_area) {
            BC_LOG("area: %u -> %u (%s)%s", (unsigned)g_last_area, (unsigned)area,
                area == 0xff ? "QUEST - module idle" : "safe area - module armed",
                (g_acc_yaw != 0.0f || g_acc_pitch != 0.0f) ? " [orbit accumulator reset]" : "");
            g_last_area = area;
            g_acc_yaw = 0.0f;
            g_acc_pitch = 0.0f;
        }
        if (area == 0xff) {
            reason = R_QUEST;
            break;
        }
        if (g_host.textinput_active && g_host.textinput_active()) {
            reason = R_UI_OPEN;
            break;
        }
        if (g_host.chat_active && g_host.chat_active()) {
            reason = R_UI_OPEN;
            break;
        }
        if (!eye_p || !target_p || !up_p || !bc_guest_readable(eye_p, 12u) || !bc_guest_readable(target_p, 12u) ||
            !vec_finite(eye_p) || !vec_finite(target_p)) {
            reason = R_VEC_BAD;
            break;
        }
        float dx = 0.0f, dy = 0.0f;
        if (g_host.mouse_state) g_host.mouse_state(&dx, &dy);
        for (int i = 0; i < 3; i++) {
            eye[i] = bc_ldf(eye_p + 4u * (uint32_t)i);
            target[i] = bc_ldf(target_p + 4u * (uint32_t)i);
        }
        float dxr = eye[0] - target[0], dyr = eye[1] - target[1], dzr = eye[2] - target[2];
        float radius = sqrtf(dxr * dxr + dyr * dyr + dzr * dzr);
        if (!(radius > 0.01f) || radius > 100000.0f) {
            reason = R_VEC_BAD;
            break;
        }
        float deg = BC_MOUSE_DEG_PER_PX * g_cfg.speed;
        if (dx != 0.0f || dy != 0.0f) {
            g_acc_yaw += dx * deg;
            if (!g_cfg.level) {
                g_acc_pitch += -dy * deg;
                if (g_acc_pitch < g_cfg.pitch_min) g_acc_pitch = g_cfg.pitch_min;
                if (g_acc_pitch > g_cfg.pitch_max) g_acc_pitch = g_cfg.pitch_max;
            }
        }
        float dpitch = g_acc_pitch;
        if (g_cfg.level) {
            float base_pitch = atan2f(dyr, sqrtf(dxr * dxr + dzr * dzr)) * (180.0f / M_PI_F);
            dpitch = -base_pitch;
        }
        bc_orbit(eye, target, g_acc_yaw, dpitch, g_cfg.pitch_min, g_cfg.pitch_max);
        for (int i = 0; i < 3; i++) {
            uint32_t w;
            memcpy(&w, &eye[i], 4);
            bc_st32(eye_p + 4u * (uint32_t)i, w);
        }
        active = (dx != 0.0f || dy != 0.0f);
        reason = R_OK;
        if (sample)
            BC_LOGV("frame %u: mouse %.1f,%.1f acc %.1f/%.1f | eye %.1f,%.1f,%.1f", (unsigned)g_frames, dx, dy,
                g_acc_yaw, g_acc_pitch, eye[0], eye[1], eye[2]);
    } while (0);
    if (active != g_last_active) {
        if (active) BC_LOG("camera: ORBITING (mouse-driven, area %u, acc %.1f/%.1f)", (unsigned)g_last_area, g_acc_yaw,
            g_acc_pitch);
        else BC_LOG("camera: idle (%s)", g_reason[reason]);
        g_last_active = active;
    } else if (!active && sample) {
        BC_LOGV("frame %u: skip - %s", (unsigned)g_frames, g_reason[reason]);
    }
    (void)sample;
    (void)reason;
    g_host.fnhook_call_original(c, BC_FN_BUILDER);
}

bool bc_safearea_install(void) {
    if (!g_host.fnhook_set || !g_host.hle_register || !g_host.fnhook_call_original) {
        BC_LOG("install: FAIL - host functions missing (fnhook_set=%p hle_register=%p call_original=%p)",
            (void *)g_host.fnhook_set, (void *)g_host.hle_register, (void *)g_host.fnhook_call_original);
        return false;
    }
    if (!g_host.mouse_state)
        BC_LOG("install: WARN - no SDL relative-mouse source: the module will never orbit (KB+M builds need SDL3)");
    if (g_hooked_fn) {
        BC_LOG("install: already armed at 0x9c5e4c");
        return true;
    }
    if (!bc_guest_readable(BC_FN_BUILDER, 16u)) {
        BC_LOG("install: FAIL - the builder 0x9c5e4c is not readable yet (the game code is not mapped)");
        return false;
    }
    uint32_t w0 = bc_ld32(BC_FN_BUILDER), w1 = bc_ld32(BC_FN_BUILDER + 4u), w2 = bc_ld32(BC_FN_BUILDER + 8u),
             w3 = bc_ld32(BC_FN_BUILDER + 12u);
    BC_LOG("install: builder 0x9c5e4c words: %08x %08x %08x %08x (expected %08x = push {r4-r6, lr})", (unsigned)w0,
        (unsigned)w1, (unsigned)w2, (unsigned)w3, 0xe92d4070u);
    (void)w1;
    (void)w2;
    (void)w3;
    if (w0 != 0xe92d4070u) {
        BC_LOG("install: FAIL - target bytes do not match (code moved or a different game build): hooking would "
               "corrupt the port - feature off");
        return false;
    }
    g_hle_id = g_host.hle_register("bettercamera:viewbuilder", detour_builder);
    if (!g_hle_id) {
        BC_LOG("install: FAIL - hle_register returned 0");
        return false;
    }
    BC_LOG("install: detour registered as hle id %u", (unsigned)g_hle_id);
    if (!g_host.fnhook_set(BC_FN_BUILDER, g_hle_id)) {
        BC_LOG("install: FAIL - fnhook_set(0x9c5e4c, %u) returned false", (unsigned)g_hle_id);
        return false;
    }
    if (g_host.fnhook_get) {
        uint32_t rb = g_host.fnhook_get(BC_FN_BUILDER);
        if (rb == g_hle_id)
            BC_LOG("install: HOOK VERIFIED - fnhook_get(0x9c5e4c) == %u (the view builder now runs BetterCamera)",
                (unsigned)rb);
        else
            BC_LOG("install: WARN - fnhook_get(0x9c5e4c) = %u, expected %u", (unsigned)rb, (unsigned)g_hle_id);
    } else {
        BC_LOG("install: fnhook_set accepted (no fnhook_get to read back - verification by field test)");
    }
    g_hooked_fn = BC_FN_BUILDER;
    g_acc_yaw = 0.0f;
    g_acc_pitch = 0.0f;
    BC_LOG("install: armed (gates: area != 255, no UI open; orbit ACCUMULATED and re-applied over the class's re-derived base every frame%s; sensitivity %.2f deg/px, pitch %.1f..%.1f)",
        g_cfg.level ? "; LEVEL mode: the view is held even with the ground" : "",
        BC_MOUSE_DEG_PER_PX * g_cfg.speed, g_cfg.pitch_min, g_cfg.pitch_max);
    return true;
}

void bc_safearea_remove(void) {
    if (!g_hooked_fn) return;
    g_host.fnhook_set(g_hooked_fn, 0);
    g_hooked_fn = 0;
    BC_LOG("install: hook removed");
}
