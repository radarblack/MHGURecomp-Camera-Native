#include "safearea.h"

#include <math.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "guest.h"
#include "host.h"
#include "platform.h"

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif

#define BC_MOUSE_DEG_PER_PX 0.15f
#define BC_PITCH_MIN (-89.0f)
#define BC_PITCH_MAX 89.0f

#define BC_CAP_MAX 120
#define BC_CAP_WAIT_FRAMES 60u
#define BC_FIT_MARGIN 1.3f
#define BC_FIT_MAX_RADIUS 3000.0f
#define BC_SETTLE_MAX_MS 20000u
#define BC_STABLE_MS 700u
#define BC_WARP_CM 500.0f
#define BC_WALK_CM 30.0f

#define BC_CAM_FAR 0x30u
#define BC_CAM_NEAR 0x34u
#define BC_CAM_ASPECT 0x38u
#define BC_CAM_FOV 0x3cu
#define BC_CAM_EYE 0x40u
#define BC_CAM_UP 0x50u
#define BC_CAM_TARGET 0x60u

#define BC_FN_SCHED 0xb66164u
#define BC_SC_PAUSED 0x40u
#define BC_SC_LOOP 0x41u
#define BC_SC_RES 0x54u
#define BC_RES_PATH 0x08u
#define BC_EVENT_MS 250u
#define BC_MENU_NS 150000000ull
#define BC_NO_AREA 0xffu

enum { S_OVERLAY, S_CHAT, S_TEXT, S_MODFREE, S_MENU, S_MENUCUR, S_RELEASED, S_VIEW, S_EVENT, S_COUNT };
#define BC_FREEZE_MASK ((1u << S_EVENT) - 1u)

typedef struct BcPose {
    float pos[3];
    float fx, fz;
} BcPose;

static uint32_t g_hooked_fn;
static uint32_t g_hle_id;
static uint32_t g_sched_hook;
static uint32_t g_sched_id;

static uint32_t g_frames;
static uint32_t g_area_enter_frame;
static uint8_t g_last_area = BC_NO_AREA;
static int g_inherit_pending;
static float g_yaw_abs;
static float g_pitch_abs;

static float g_cap_r[BC_CAP_MAX];
static int g_cap_n;
static float g_hunt_radius;
static float g_last_base;
static float g_last_use_r;
static int g_have_radius;

static int g_settle;
static int g_pose_valid;
static uint64_t g_settle_t0;
static uint64_t g_stable_t0;
static float g_pos_ref[3];
static float g_pos_prev[3];
static float g_fwd_ref[2];

static uint64_t g_event_ms;
static int g_seen_relative;
static int g_seen_view;
static int g_frozen_prev;

#if BC_DEBUG > 0
static const char *const g_signal_name[S_COUNT] = {"the port's menu", "chat", "a text box", "a mod's pointer",
    "a game menu", "a game menu cursor", "the mouse released", "the game's menu view", "a game event"};
static unsigned g_signals_prev;
static int g_snap_logged;
static int g_first_call_done;
static int g_cam_logged;
static int g_filter_warned;
static uint32_t g_cam_calls;
static uint32_t g_other_calls;
static int g_last_active;
#endif

static float bc_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int bc_sane_addr(uint32_t a) { return a >= 0x08000000u && a < 0xF0000000u && !(a & 3u); }

static float bc_dist3(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static int vec_finite(uint32_t p) {
    for (uint32_t i = 0; i < 3u; i++) {
        float v = bc_ldf(p + 4u * i);
        if (!(v == v) || v > 1e9f || v < -1e9f) return 0;
    }
    return 1;
}

static uint32_t bc_main_cam(uint32_t eye_p, uint32_t target_p, uint32_t up_p) {
    if (eye_p < 0x1000u + BC_CAM_EYE || (eye_p & 3u)) return 0;
    if (target_p != eye_p - BC_CAM_EYE + BC_CAM_TARGET || up_p != eye_p - BC_CAM_EYE + BC_CAM_UP) return 0;
    uint32_t cam = eye_p - BC_CAM_EYE;
    if (!bc_guest_readable(cam, 0x70u)) return 0;
    float far_z = bc_ldf(cam + BC_CAM_FAR), near_z = bc_ldf(cam + BC_CAM_NEAR), aspect = bc_ldf(cam + BC_CAM_ASPECT),
          fov = bc_ldf(cam + BC_CAM_FOV);
    if (!(near_z > 0.0f && far_z > near_z && far_z < 1e8f)) return 0;
    if (!(aspect >= 1.2f && aspect <= 4.0f)) return 0;
    if (!(fov > 5.0f && fov < 150.0f)) return 0;
    return cam;
}

static uint32_t bc_local_player(void) {
    uint32_t gc = bc_ld32(BC_G_SGAMECONTROL);
    uint32_t sp = bc_ld32(BC_G_SPLAYER);
    if (!gc || !sp || !bc_guest_readable(gc, 0x40u) || !bc_guest_readable(sp, 0x30u)) return 0;
    uint32_t slot = bc_ld32(gc + BC_GC_SLOT);
    if (slot >= 4u) return 0;
    return bc_ld32(sp + BC_SP_PLAYERS + 4u * slot);
}

static int bc_in_quest(void) {
    uint32_t pl = bc_local_player();
    if (!pl || !bc_guest_readable(pl + BC_PL_ZONE, 4u)) return 0;
    uint8_t zone = bc_ld8(pl + BC_PL_ZONE);
    return zone != 0u && zone != 0xffu;
}

static int bc_player_pose(BcPose *out) {
    uint32_t pl = bc_local_player();
    if (!pl || !bc_guest_readable(pl + BC_PL_POS, 0x20u)) return 0;
    for (uint32_t i = 0; i < 3u; i++) {
        out->pos[i] = bc_ldf(pl + BC_PL_POS + 4u * i);
        if (!(out->pos[i] == out->pos[i]) || out->pos[i] > 1e9f || out->pos[i] < -1e9f) return 0;
    }
    float qx = bc_ldf(pl + BC_PL_ROT), qy = bc_ldf(pl + BC_PL_ROT + 4u), qz = bc_ldf(pl + BC_PL_ROT + 8u),
          qw = bc_ldf(pl + BC_PL_ROT + 12u);
    if (!(qx == qx && qy == qy && qz == qz && qw == qw)) return 0;
    float x = 2.0f * (qx * qz + qw * qy), z = 1.0f - 2.0f * (qx * qx + qy * qy);
    if (!(x * x + z * z > 1e-4f)) return 0;
    float l = sqrtf(x * x + z * z);
    out->fx = x / l;
    out->fz = z / l;
    return 1;
}

static int bc_fit_radius(uint32_t cam, float eye_h, float target_y, float player_y, float *out) {
    float fov = bc_ldf(cam + BC_CAM_FOV), aspect = bc_ldf(cam + BC_CAM_ASPECT);
    float half_v = atanf(tanf(fov * (M_PI_F / 360.0f)) / aspect);
    if (!(half_v > 0.01f && half_v < 1.55f)) return 0;
    float r = (0.5f * g_cfg.char_height * BC_FIT_MARGIN) / tanf(half_v);
    for (int i = 0; i < 8; i++) {
        float pr = asinf(bc_clampf((eye_h - target_y) / r, -0.99f, 0.99f));
        float dxz = r * cosf(pr);
        if (!(dxz > 1.0f)) break;
        float e_axis = atanf((target_y - eye_h) / dxz);
        float e_top = atanf((player_y + g_cfg.char_height - eye_h) / dxz);
        float e_foot = atanf((player_y - eye_h) / dxz);
        float off_top = fabsf(e_top - e_axis), off_foot = fabsf(e_foot - e_axis);
        float worst = off_top > off_foot ? off_top : off_foot;
        if (!(worst > 1e-4f)) break;
        r *= worst / half_v;
    }
    if (!(r > 0.0f && r < BC_FIT_MAX_RADIUS)) return 0;
    *out = r;
    return 1;
}

static int bc_anchor_pitch(float target_y, float radius, float player_y, float *out) {
    float ratio = (player_y + g_cfg.start_height - target_y) / radius;
    if (!(ratio == ratio) || ratio > 0.98f || ratio < -0.98f) return 0;
    *out = asinf(ratio) * (180.0f / M_PI_F);
    return 1;
}

static int bc_settle_step(int fresh, float dx, float dy, const BcPose *pose, float *yaw_out) {
    uint64_t now = bc_now_ms();
    if (fresh) {
        g_settle = 1;
        g_settle_t0 = now;
        g_pose_valid = 0;
#if BC_DEBUG > 0
        g_snap_logged = 0;
#endif
    } else if (!g_settle) {
        return 0;
    } else if (dx != 0.0f || dy != 0.0f) {
        g_settle = 0;
        BC_LOG("camera: start-behind ended (mouse moved)");
        return 0;
    } else if (now - g_settle_t0 > BC_SETTLE_MAX_MS) {
        g_settle = 0;
        BC_LOG("camera: start-behind gave up (no stable hunter pose within %u ms)", (unsigned)BC_SETTLE_MAX_MS);
        return 0;
    }
    if (!pose) return 0;
    if (!g_pose_valid) {
        g_pose_valid = 1;
        memcpy(g_pos_ref, pose->pos, sizeof g_pos_ref);
        memcpy(g_pos_prev, pose->pos, sizeof g_pos_prev);
        g_fwd_ref[0] = pose->fx;
        g_fwd_ref[1] = pose->fz;
        g_stable_t0 = now;
    } else {
        float jump = bc_dist3(pose->pos, g_pos_prev);
        memcpy(g_pos_prev, pose->pos, sizeof g_pos_prev);
        if (jump > BC_WARP_CM) {
            memcpy(g_pos_ref, pose->pos, sizeof g_pos_ref);
            g_fwd_ref[0] = pose->fx;
            g_fwd_ref[1] = pose->fz;
            g_stable_t0 = now;
        } else if (bc_dist3(pose->pos, g_pos_ref) > BC_WALK_CM) {
            g_settle = 0;
            BC_LOG("camera: start-behind ended (the hunter started moving)");
            return 0;
        } else if (pose->fx * g_fwd_ref[0] + pose->fz * g_fwd_ref[1] < 0.9998f) {
            g_fwd_ref[0] = pose->fx;
            g_fwd_ref[1] = pose->fz;
            g_stable_t0 = now;
        } else if (now - g_stable_t0 >= BC_STABLE_MS) {
            g_settle = 0;
            BC_LOG("camera: start-behind settled (hunter pose stable)");
        }
    }
    *yaw_out = atan2f(-pose->fx, -pose->fz);
#if BC_DEBUG > 0
    if (!g_snap_logged) {
        g_snap_logged = 1;
        BC_LOG("camera: started behind the hunter (forward %.2f,%.2f -> yaw %.1f deg)", pose->fx, pose->fz,
            *yaw_out * (180.0f / M_PI_F));
    }
#endif
    return 1;
}

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

static void bc_capture_hunt_radius(uint32_t eye_p, uint32_t target_p) {
    if (g_cap_n >= BC_CAP_MAX || g_frames - g_area_enter_frame < BC_CAP_WAIT_FRAMES) return;
    if (!bc_guest_readable(eye_p, 12u) || !bc_guest_readable(target_p, 12u) || !bc_in_quest()) return;
    float d[3];
    for (uint32_t i = 0; i < 3u; i++) d[i] = bc_ldf(eye_p + 4u * i) - bc_ldf(target_p + 4u * i);
    float r = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(r > 0.01f && r < 100000.0f)) return;
    g_cap_r[g_cap_n++] = r;
    if (g_cap_n == BC_CAP_MAX) {
        g_hunt_radius = bc_median(g_cap_r, BC_CAP_MAX);
        BC_LOG("hunt: radius captured from the live quest camera - %.2f", g_hunt_radius);
    }
}

static void detour_sched(BcCpu *c) {
    uint32_t sc = c->r[0];
    if (bc_sane_addr(sc) && !bc_ld8(sc + BC_SC_LOOP) && !bc_ld8(sc + BC_SC_PAUSED)) {
        uint32_t res = bc_ld32(sc + BC_SC_RES);
        if (bc_sane_addr(res)) {
            static const char k_event[] = "event\\";
            int match = 1;
            for (uint32_t i = 0; i < sizeof k_event - 1u; i++)
                if (bc_ld8(res + BC_RES_PATH + i) != (uint8_t)k_event[i]) match = 0;
            if (match) g_event_ms = bc_now_ms();
        }
    }
    g_host.fnhook_call_original(c, BC_FN_SCHED);
}

static unsigned bc_signals(void) {
    unsigned m = 0;
    if (g_host.overlay_menu_open && g_host.overlay_menu_open()) m |= 1u << S_OVERLAY;
    if (g_host.chat_active && g_host.chat_active()) m |= 1u << S_CHAT;
    if (g_host.textinput_active && g_host.textinput_active()) m |= 1u << S_TEXT;
    if (g_host.mouse_free && g_host.mouse_free()) m |= 1u << S_MODFREE;
    if (g_host.menuptr_open && g_host.menuptr_open()) m |= 1u << S_MENU;
    if (g_host.menuptr_cursor_ran && g_host.menuptr_cursor_ran(BC_MENU_NS)) m |= 1u << S_MENUCUR;
    if (g_host.sdl_focus && g_host.sdl_relmode) {
        void *w = g_host.sdl_focus();
        if (w) {
            if (g_host.sdl_relmode(w)) g_seen_relative = 1;
            else if (g_seen_relative) m |= 1u << S_RELEASED;
        }
    }
    if (g_host.view_context) {
        const char *ctx = g_host.view_context();
        if (ctx && (ctx[0] == 'T' || ctx[0] == 'F')) g_seen_view = 1;
        else if (ctx && g_seen_view) m |= 1u << S_VIEW;
    }
    if (g_event_ms && bc_now_ms() - g_event_ms < BC_EVENT_MS) m |= 1u << S_EVENT;
    return m;
}

#if BC_DEBUG > 0
static void bc_log_signals(unsigned m) {
    if (m == g_signals_prev) return;
    g_signals_prev = m;
    char buf[160];
    buf[0] = 0;
    for (int i = 0; i < S_COUNT; i++) {
        if (!(m & (1u << i))) continue;
        if (buf[0]) strncat(buf, ", ", sizeof buf - strlen(buf) - 1);
        strncat(buf, g_signal_name[i], sizeof buf - strlen(buf) - 1);
    }
    BC_LOG("camera: gate signals -> %s", buf[0] ? buf : "none");
}
#endif

static void bc_discard_mouse(void) {
    float jx, jy;
    if (g_host.mouse_state) g_host.mouse_state(&jx, &jy);
}

static void detour_builder(BcCpu *c) {
    uint32_t eye_p = c->r[1], target_p = c->r[2], up_p = c->r[3];
    uint32_t cam = bc_main_cam(eye_p, target_p, up_p);
    if (!cam) {
#if BC_DEBUG > 0
        g_other_calls++;
        if (!g_cam_calls && g_other_calls == 30000u && !g_filter_warned) {
            g_filter_warned = 1;
            BC_LOG("camera: WARNING - 30000 view builds and none look like the main camera (eye*=%08x target*=%08x up*=%08x)",
                (unsigned)eye_p, (unsigned)target_p, (unsigned)up_p);
        }
#endif
        g_host.fnhook_call_original(c, BC_FN_BUILDER);
        return;
    }
#if BC_DEBUG > 0
    g_cam_calls++;
    if (!g_cam_logged) {
        g_cam_logged = 1;
        BC_LOG("camera: main camera recognised (object %08x; %u other view builds skipped before it)", (unsigned)cam,
            (unsigned)g_other_calls);
    }
#endif
    g_frames++;
    do {
        uint32_t gcp = bc_ld32(BC_G_SGAMECONTROL);
        if (!gcp || !bc_guest_readable(gcp, 0x80u)) break;
        uint8_t area = bc_ld8(gcp + BC_GC_AREA_TYPE);
#if BC_DEBUG > 0
        if (!g_first_call_done) {
            g_first_call_done = 1;
            BC_LOG("detour: first main-camera call (sGameControl %08x, area byte %u)", (unsigned)gcp, (unsigned)area);
        }
#endif
        if (area != g_last_area) {
            BC_LOG("area: %u -> %u (%s)", (unsigned)g_last_area, (unsigned)area,
                area == BC_NO_AREA ? "quest - module idle" : "safe area - module armed");
            g_last_area = area;
            g_inherit_pending = 1;
            g_area_enter_frame = g_frames;
            g_have_radius = 0;
            if (area == BC_NO_AREA) {
                g_cap_n = 0;
                g_settle = 0;
            }
        }
        if (area == BC_NO_AREA) {
            bc_capture_hunt_radius(eye_p, target_p);
            break;
        }

        unsigned signals = bc_signals();
#if BC_DEBUG > 0
        bc_log_signals(signals);
#endif
        if (signals & (1u << S_EVENT)) {
            bc_discard_mouse();
            break;
        }
        int frozen = (signals & BC_FREEZE_MASK) != 0;
        if (frozen != g_frozen_prev) {
            g_frozen_prev = frozen;
            BC_LOG("camera: mouse look %s", frozen ? "paused (menu, dialogue or UI open)" : "resumed");
        }

        if (!bc_guest_readable(eye_p, 12u) || !bc_guest_readable(target_p, 12u) || !vec_finite(eye_p) ||
            !vec_finite(target_p))
            break;
        float dx = 0.0f, dy = 0.0f;
        if (g_host.mouse_state) g_host.mouse_state(&dx, &dy);
        if (frozen) dx = dy = 0.0f;

        float eye[3], target[3];
        for (uint32_t i = 0; i < 3u; i++) {
            eye[i] = bc_ldf(eye_p + 4u * i);
            target[i] = bc_ldf(target_p + 4u * i);
        }
        float dxr = eye[0] - target[0], dyr = eye[1] - target[1], dzr = eye[2] - target[2];
        float radius = sqrtf(dxr * dxr + dyr * dyr + dzr * dzr);
        if (!(radius > 0.01f) || radius > 100000.0f) break;

        if (dx != 0.0f || dy != 0.0f) {
            g_yaw_abs -= dx * BC_MOUSE_DEG_PER_PX * g_cfg.speed * (M_PI_F / 180.0f);
            g_pitch_abs += (g_cfg.invert_y ? -dy : dy) * BC_MOUSE_DEG_PER_PX * g_cfg.speed;
            g_pitch_abs = bc_clampf(g_pitch_abs, BC_PITCH_MIN, BC_PITCH_MAX);
        }

        int fresh = g_inherit_pending;
        if (fresh) {
            g_inherit_pending = 0;
            g_yaw_abs = atan2f(dxr, dzr);
            g_pitch_abs = 0.0f;
            BC_LOG("camera: orbit anchored at the game camera's angle (yaw %.1f deg) - now world-absolute",
                g_yaw_abs * (180.0f / M_PI_F));
        }

        BcPose pose;
        int have_pose = bc_player_pose(&pose);
        int settling = fresh;
        float behind_yaw;
        if (bc_settle_step(fresh, dx, dy, have_pose ? &pose : NULL, &behind_yaw)) {
            g_yaw_abs = behind_yaw;
            settling = 1;
        }

        float base_r = (g_have_radius && fabsf(radius - g_last_use_r) < 0.05f) ? g_last_base
                       : (g_hunt_radius > 0.0f ? g_hunt_radius : radius);
        float use_r = base_r * g_cfg.distance_scale;
        if (have_pose) {
            float h_eye = settling ? pose.pos[1] + g_cfg.start_height : eye[1];
            float rmin;
            if (bc_fit_radius(cam, h_eye, target[1], pose.pos[1], &rmin) && use_r < rmin) {
                if (fresh) BC_LOG("camera: radius raised to fit the character (%.1f -> %.1f)", use_r, rmin);
                use_r = rmin;
            }
        }
        if (fresh)
            BC_LOG("camera: distance - game %.1f, hunt capture %.1f, scale %.2f -> using %.1f", radius, g_hunt_radius,
                g_cfg.distance_scale, use_r);
        g_last_base = base_r;
        g_last_use_r = use_r;
        g_have_radius = 1;

        if (settling && have_pose) {
            float ap;
            if (bc_anchor_pitch(target[1], use_r, pose.pos[1], &ap)) g_pitch_abs = bc_clampf(ap, BC_PITCH_MIN, BC_PITCH_MAX);
        }

        float pr = g_pitch_abs * (M_PI_F / 180.0f);
        eye[0] = target[0] + use_r * sinf(g_yaw_abs) * cosf(pr);
        eye[1] = target[1] + use_r * sinf(pr);
        eye[2] = target[2] + use_r * cosf(g_yaw_abs) * cosf(pr);
        for (uint32_t i = 0; i < 3u; i++) bc_stf(eye_p + 4u * i, eye[i]);
        if (bc_guest_readable(up_p, 12u)) {
            bc_stf(up_p, 0.0f);
            bc_stf(up_p + 4u, 1.0f);
            bc_stf(up_p + 8u, 0.0f);
        }

#if BC_DEBUG > 0
        int active = dx != 0.0f || dy != 0.0f;
        if (active != g_last_active) {
            g_last_active = active;
            BC_LOG("camera: %s (yaw %.1f pitch %.1f)", active ? "mouse look" : "idle",
                g_yaw_abs * (180.0f / M_PI_F), g_pitch_abs);
        }
#endif
#if BC_DEBUG > 1
        if (g_frames % 30u == 1u)
            BC_LOGV("frame %u: yaw %.1f pitch %.1f | eye %.1f,%.1f,%.1f | radius game %.1f used %.1f | hunter %s%.2f,%.2f",
                (unsigned)g_frames, g_yaw_abs * (180.0f / M_PI_F), g_pitch_abs, eye[0], eye[1], eye[2], radius, use_r,
                have_pose ? "" : "n/a ", have_pose ? pose.fx : 0.0f, have_pose ? pose.fz : 0.0f);
#endif
    } while (0);
    g_host.fnhook_call_original(c, BC_FN_BUILDER);
}

bool bc_safearea_install(void) {
    if (!g_host.fnhook_set || !g_host.hle_register || !g_host.fnhook_call_original) {
        BC_LOG("install: FAIL - host functions missing");
        return false;
    }
    if (!g_host.mouse_state) BC_LOG("install: WARN - no SDL relative-mouse source: the module will never orbit");
    if (g_hooked_fn) {
        BC_LOG("install: already armed");
        return true;
    }
    if (!bc_guest_readable(BC_FN_BUILDER, 16u)) {
        BC_LOG("install: FAIL - the view builder is not readable yet");
        return false;
    }
    uint32_t w0 = bc_ld32(BC_FN_BUILDER);
    if (w0 != BC_BUILDER_PROLOGUE) {
        BC_LOG("install: FAIL - view builder prologue %08x, expected %08x (a different game build?)", (unsigned)w0,
            (unsigned)BC_BUILDER_PROLOGUE);
        return false;
    }
    g_hle_id = g_host.hle_register("moderncamera:viewbuilder", detour_builder);
    if (!g_hle_id || !g_host.fnhook_set(BC_FN_BUILDER, g_hle_id)) {
        BC_LOG("install: FAIL - could not hook the view builder");
        return false;
    }
    if (g_host.fnhook_get && g_host.fnhook_get(BC_FN_BUILDER) != g_hle_id)
        BC_LOG("install: WARN - the hook read back as %u, expected %u", (unsigned)g_host.fnhook_get(BC_FN_BUILDER),
            (unsigned)g_hle_id);
    g_hooked_fn = BC_FN_BUILDER;

    g_sched_id = g_host.hle_register("moderncamera:scheduler", detour_sched);
    if (g_sched_id && g_host.fnhook_set(BC_FN_SCHED, g_sched_id)) {
        g_sched_hook = BC_FN_SCHED;
        BC_LOG("install: event hook armed (the camera steps aside while a game event plays)");
    } else {
        BC_LOG("install: WARN - could not hook the event scheduler: events will not pause the camera");
    }

    g_yaw_abs = 0.0f;
    g_pitch_abs = 0.0f;
    g_inherit_pending = 0;
    g_settle = 0;
    g_cap_n = 0;
    g_hunt_radius = 0.0f;
    BC_LOG("install: armed (speed %.2f, pitch %.0f..%.0f)", g_cfg.speed, BC_PITCH_MIN, BC_PITCH_MAX);
    return true;
}

void bc_safearea_remove(void) {
    if (!g_hooked_fn) return;
    g_host.fnhook_set(g_hooked_fn, 0);
    g_hooked_fn = 0;
    if (g_sched_hook) {
        g_host.fnhook_set(g_sched_hook, 0);
        g_sched_hook = 0;
    }
    BC_LOG("install: hooks removed");
}
