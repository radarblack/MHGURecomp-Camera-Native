#include "safearea.h"

#include <math.h>
#include <string.h>

#include "debug.h"

#include <math.h>

#include "config.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif
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
static float g_yaw_abs;
static float g_pitch_abs;
static int g_inherit_pending;
#define BC_CAP_MAX 120
static float g_cap_r[BC_CAP_MAX];
static float g_cap_p[BC_CAP_MAX];
static int g_cap_n;
static uint32_t g_area_enter_frame;
static float g_hunt_radius;
static float g_hunt_pitch_deg;

static float bc_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static float bc_median(float *a, int n) {
    for (int i = 1; i < n; i++) {
        float v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = v;
    }
    return a[n / 2];
}

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
    float pitch_deg_out = -999.0f;
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
                g_inherit_pending ? " [orbit resets to the class's current angle (hunt-style snap)]" : "");
            g_last_area = area;
            g_inherit_pending = 1;
            g_area_enter_frame = g_frames;
            if (area == 0xff) g_cap_n = 0;
        }
        if (area == 0xff) {
            if (g_cfg.hunt_defaults && g_cap_n < BC_CAP_MAX && g_frames - g_area_enter_frame >= 60u &&
                bc_guest_readable(eye_p, 12u) && bc_guest_readable(target_p, 12u)) {
                float qe[3], qt[3];
                for (int i = 0; i < 3; i++) {
                    qe[i] = bc_ldf(eye_p + 4u * (uint32_t)i);
                    qt[i] = bc_ldf(target_p + 4u * (uint32_t)i);
                }
                float ddx = qe[0] - qt[0], ddy = qe[1] - qt[1], ddz = qe[2] - qt[2];
                float qr = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                if (qr > 0.01f && qr < 100000.0f) {
                    float qp = atan2f(ddy, sqrtf(ddx * ddx + ddz * ddz)) * (180.0f / M_PI_F);
                    if (qp > -80.0f && qp < 80.0f) {
                        g_cap_r[g_cap_n] = qr;
                        g_cap_p[g_cap_n] = qp;
                        g_cap_n++;
                        if (g_cap_n == BC_CAP_MAX) {
                            g_hunt_radius = bc_median(g_cap_r, BC_CAP_MAX);
                            g_hunt_pitch_deg = bc_median(g_cap_p, BC_CAP_MAX);
                            BC_LOG("hunt: settings captured from the live quest camera - radius %.2f, pitch %.2f deg",
                                g_hunt_radius, g_hunt_pitch_deg);
                        }
                    }
                }
            }
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
        if (dx != 0.0f || dy != 0.0f) {
            g_yaw_abs -= dx * BC_MOUSE_DEG_PER_PX * g_cfg.speed * (M_PI_F / 180.0f);
            if (g_cfg.vertical) {
                g_pitch_abs += (g_cfg.invert_y ? -dy : dy) * BC_MOUSE_DEG_PER_PX * g_cfg.speed;
                g_pitch_abs = bc_clampf(g_pitch_abs, g_cfg.pitch_min, g_cfg.pitch_max);
            } else if (!g_cfg.level) {
                g_pitch_abs += -dy * BC_MOUSE_DEG_PER_PX * g_cfg.speed;
                g_pitch_abs = bc_clampf(g_pitch_abs, g_cfg.pitch_min, g_cfg.pitch_max);
            }
        }
        if (g_inherit_pending) {
            g_inherit_pending = 0;
            g_yaw_abs = atan2f(dxr, dzr);
            float game_pitch = atan2f(dyr, sqrtf(dxr * dxr + dzr * dzr)) * (180.0f / M_PI_F);
            if (g_cfg.vertical) {
                float start = g_cfg.hunt_pitch_deg != 999.0f ? g_cfg.hunt_pitch_deg
                              : (g_cfg.level ? 0.0f
                                 : (g_cfg.hunt_defaults && g_hunt_pitch_deg != 0.0f ? g_hunt_pitch_deg : game_pitch));
                g_pitch_abs = bc_clampf(start, g_cfg.pitch_min, g_cfg.pitch_max);
            } else {
                g_pitch_abs = g_cfg.level ? 0.0f : game_pitch;
            }
            BC_LOG("camera: orbit anchored at the class's angle (yaw %.1f deg, pitch %.1f deg) - now world-absolute",
                g_yaw_abs * (180.0f / M_PI_F), g_pitch_abs);
        }
        float use_r = g_cfg.hunt_distance > 0.0f ? g_cfg.hunt_distance
                      : (g_cfg.hunt_defaults && g_hunt_radius > 0.0f ? g_hunt_radius : radius);
        float pitch_deg = g_cfg.vertical ? g_pitch_abs
                          : g_cfg.hunt_pitch_deg != 999.0f ? g_cfg.hunt_pitch_deg
                          : (g_cfg.level ? 0.0f
                             : (g_cfg.hunt_defaults && g_hunt_pitch_deg != 0.0f ? g_hunt_pitch_deg : g_pitch_abs));
        if (pitch_deg < g_cfg.pitch_min) pitch_deg = g_cfg.pitch_min;
        if (pitch_deg > g_cfg.pitch_max) pitch_deg = g_cfg.pitch_max;
        float pr = pitch_deg * (M_PI_F / 180.0f);
        eye[0] = target[0] + use_r * sinf(g_yaw_abs) * cosf(pr);
        eye[1] = target[1] + use_r * sinf(pr);
        eye[2] = target[2] + use_r * cosf(g_yaw_abs) * cosf(pr);
        for (int i = 0; i < 3; i++) {
            uint32_t w;
            memcpy(&w, &eye[i], 4);
            bc_st32(eye_p + 4u * (uint32_t)i, w);
        }
        float up_old[3] = {0.0f, 0.0f, 0.0f};
        if (bc_guest_readable(up_p, 12u)) {
            for (int i = 0; i < 3; i++) up_old[i] = bc_ldf(up_p + 4u * (uint32_t)i);
            bc_stf(up_p, 0.0f);
            bc_stf(up_p + 4u, 1.0f);
            bc_stf(up_p + 8u, 0.0f);
        }
        (void)up_old;
        pitch_deg_out = pitch_deg;
        active = (dx != 0.0f || dy != 0.0f);
        reason = R_OK;
        if (sample)
            BC_LOGV("frame %u: mouse %.1f,%.1f yaw %.1f pitch %.1f | eye %.1f,%.1f,%.1f | game up %.3f,%.3f,%.3f",
                (unsigned)g_frames, dx, dy, g_yaw_abs * (180.0f / M_PI_F), pitch_deg_out, eye[0], eye[1], eye[2],
                up_old[0], up_old[1], up_old[2]);
    } while (0);
    if (active != g_last_active) {
        if (active) BC_LOG("camera: ORBITING (mouse-driven, area %u, yaw %.1f pitch %.1f)", (unsigned)g_last_area,
            g_yaw_abs * (180.0f / M_PI_F), pitch_deg_out);
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
    g_yaw_abs = 0.0f;
    g_pitch_abs = 0.0f;
    g_inherit_pending = 0;
    g_cap_n = 0;
    g_hunt_radius = 0.0f;
    g_hunt_pitch_deg = 0.0f;
    BC_LOG("install: armed (gates: area != 255, no UI open; orbit = WORLD-ABSOLUTE yaw/pitch owned by the module, 1:1 the hunt camera's model%s%s; sensitivity %.2f deg/px, pitch %.1f..%.1f)",
        g_cfg.level ? "; LEVEL mode: the view is held even with the ground" : "",
        g_cfg.hunt_defaults ? "; HUNT DEFAULTS: the quest camera's radius/pitch are measured live and re-applied in safe areas" : "",
        BC_MOUSE_DEG_PER_PX * g_cfg.speed, g_cfg.pitch_min, g_cfg.pitch_max);
    return true;
}

void bc_safearea_remove(void) {
    if (!g_hooked_fn) return;
    g_host.fnhook_set(g_hooked_fn, 0);
    g_hooked_fn = 0;
    BC_LOG("install: hook removed");
}
