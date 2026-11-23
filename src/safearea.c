#include "safearea.h"

#include <math.h>
#include <stdio.h>
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
#define BC_PAD_DEG_PER_S 140.0f
#define BC_PAD_DEADZONE 0.2f
#define BC_PAD_AXIS_RIGHTX 2
#define BC_PAD_AXIS_RIGHTY 3
#define BC_PAD_BUTTONS 26
#define BC_PAD_MAX 8
#define BC_PAD_REFRESH_FRAMES 60u
#define BC_PAD_GRACE_MS 500u
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
#define BC_SDL_EVENT_MOUSE_WHEEL 0x403u
#define BC_SDL_WHEEL_Y 28u
#define BC_SDL_WHEEL_DIRECTION 32u
#define BC_SDL_WHEEL_FLIPPED 1u
#define BC_WHEEL_FIXED 1000.0f
#define BC_ZOOM_STEP 0.9f
#define BC_ZOOM_MIN 0.3f
#define BC_ZOOM_MAX 3.0f
#define BC_ZOOM_TAU_S 0.07f
#define BC_MIN_RADIUS 60.0f

#define BC_FN_WALL 0x383724u
#define BC_FN_WALL2 0x3828fcu
#define BC_WALL2_PROLOGUE 0xe92d4ff0u
#define BC_WALL2_FLAGS 0u
#define BC_G_COLL_MGR_GOT 0x1a39320u
#define BC_G_COLL_GC_GOT 0x1a38bb4u
#define BC_G_COLL_CTRL 0x188ca48u
#define BC_COLL_GROUP_OFF 0x49u
#define BC_WALL_RADIUS 20.0f
#define BC_COLL_MARGIN 2.0f
#define BC_COLL_RELEASE_CM_S 400.0f
#define BC_COLL_FREE 1e9f
#define BC_COLL_LOG_HITS 6
#define BC_WALL_STACK 0x1000u
#define BC_COLL_MAX_GROUPS 8
#define BC_BISECT_ITERS 10
#define BC_BISECT_EPS 1.0f
#define BC_PROBE_WINDOW_MS 3000u
#define BC_FLOOR_MARGIN 20.0f
#define BC_FLOOR_MIN_R 30.0f
#define BC_WALL_PROBE 1.0e6f
#define BC_WALL2_KIND 2u

enum { S_OVERLAY, S_CHAT, S_TEXT, S_MODFREE, S_MENU, S_MENUCUR, S_RELEASED, S_VIEW, S_EVENT, S_COUNT };
#define BC_FREEZE_MASK ((1u << S_EVENT) - 1u)

typedef struct BcPose {
    uint32_t avatar;
    float pos[3];
    float fx, fz;
    int zone;
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

static int g_wheel_acc;
static int g_wheel_watching;
static float g_zoom = 1.0f;
static float g_zoom_target = 1.0f;
static float g_dt = 0.016f;
static uint64_t g_tick_ms;

static uint32_t g_wall_hook;
static uint32_t g_wall_id;
static int g_coll_state;
static int g_coll_slots_ok;
static int g_coll_confirmed;
static int g_coll_tested;
static uint32_t g_coll_queries;
static uint32_t g_coll_hits;
static uint32_t g_coll_seen[4];
static float g_coll_cap = BC_COLL_FREE;
#if BC_DEBUG > 0
static int g_coll_hit_logs;
#endif

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
    out->avatar = pl;
    out->zone = bc_guest_readable(pl + BC_PL_ZONE, 4u) ? (int)bc_ld8(pl + BC_PL_ZONE) : -1;
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
        BC_LOG("camera: start-behind ended (look input)");
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

static bool bc_wheel_watch(void *userdata, void *event) {
    (void)userdata;
    const unsigned char *e = (const unsigned char *)event;
    uint32_t type, dir;
    float y;
    memcpy(&type, e, sizeof type);
    if (type != BC_SDL_EVENT_MOUSE_WHEEL) return true;
    memcpy(&y, e + BC_SDL_WHEEL_Y, sizeof y);
    memcpy(&dir, e + BC_SDL_WHEEL_DIRECTION, sizeof dir);
    if (!(y == y) || y > 100.0f || y < -100.0f) return true;
    if (dir == BC_SDL_WHEEL_FLIPPED) y = -y;
    __atomic_fetch_add(&g_wheel_acc, (int)lroundf(y * BC_WHEEL_FIXED), __ATOMIC_RELAXED);
    return true;
}

static float bc_take_wheel(void) { return (float)__atomic_exchange_n(&g_wheel_acc, 0, __ATOMIC_RELAXED) / BC_WHEEL_FIXED; }

static void bc_tick(void) {
    uint64_t now = bc_now_ms();
    g_dt = g_tick_ms ? bc_clampf((float)(now - g_tick_ms) * 0.001f, 0.001f, 0.1f) : 0.016f;
    g_tick_ms = now;
}

static void bc_zoom_reset(void) { g_zoom = g_zoom_target = 1.0f; }

static void bc_zoom_step(float notches) {
    if (notches != 0.0f) {
        g_zoom_target = bc_clampf(g_zoom_target * powf(BC_ZOOM_STEP, notches), BC_ZOOM_MIN, BC_ZOOM_MAX);
        BC_LOG("camera: zoom x%.2f", g_zoom_target);
    }
    g_zoom += (g_zoom_target - g_zoom) * (1.0f - expf(-g_dt / BC_ZOOM_TAU_S));
    if (fabsf(g_zoom - g_zoom_target) < 0.0005f) g_zoom = g_zoom_target;
}


static int bc_coll_read(uint32_t *got_mgr, uint32_t *got_gc, uint32_t *mgr, uint32_t *gc, uint32_t *group) {
    if (!g_coll_slots_ok) {
        if (!bc_guest_readable(BC_G_COLL_MGR_GOT, 4u) || !bc_guest_readable(BC_G_COLL_GC_GOT, 4u)) return 0;
        g_coll_slots_ok = 1;
    }
    uint32_t mv = bc_ld32(BC_G_COLL_MGR_GOT), gv = bc_ld32(BC_G_COLL_GC_GOT);
    *got_mgr = mv;
    *got_gc = gv;
    if (!mv || !gv || (mv & 3u) || (gv & 3u) || !bc_guest_readable(mv, 4u) || !bc_guest_readable(gv, 4u)) return 0;
    uint32_t m = bc_ld32(mv), g = bc_ld32(gv);
    if (!m || !g || (m & 3u) || (g & 3u) || !bc_guest_readable(m, 0x40u) || !bc_guest_readable(g, 0x80u)) return 0;
    *mgr = m;
    *gc = g;
    *group = bc_ld8(g + BC_COLL_GROUP_OFF);
    return 1;
}

static int bc_coll_args(uint32_t *mgr_out) {
    if (g_coll_state < 0 || !g_host.guest_call) return 0;
    uint32_t mv, gv, m, g, grp;
    if (!bc_coll_read(&mv, &gv, &m, &g, &grp)) return 0;
    if (g_coll_state == 0) {
        int ok = gv == BC_G_COLL_CTRL && bc_sane_addr(m);
        if (!ok) {
            if (g_coll_seen[0] != mv || g_coll_seen[1] != gv || g_coll_seen[2] != m || g_coll_seen[3] != g) {
                g_coll_seen[0] = mv;
                g_coll_seen[1] = gv;
                g_coll_seen[2] = m;
                g_coll_seen[3] = g;
                BC_LOG("collision: waiting - table %08x->%08x->%08x, table %08x->%08x->%08x not accepted yet",
                    (unsigned)BC_G_COLL_MGR_GOT, (unsigned)mv, (unsigned)m, (unsigned)BC_G_COLL_GC_GOT, (unsigned)gv,
                    (unsigned)g);
            }
            return 0;
        }
        g_coll_state = 1;
        BC_LOG("collision: ready - manager %08x (via %08x), game control %08x (via %08x), game-control zone byte %u",
            (unsigned)m, (unsigned)mv, (unsigned)g, (unsigned)gv, (unsigned)grp);
    }
    *mgr_out = m;
    return 1;
}

static void detour_wall(BcCpu *c) {
    if (!g_coll_confirmed && g_coll_state >= 0) {
        uint32_t mv, gv, m, g, grp;
        uint32_t r0 = c->r[0], group = c->r[3] & 0xffu;
        (void)group;
        if (bc_coll_read(&mv, &gv, &m, &g, &grp)) {
            if (r0 == m) {
                g_coll_confirmed = 1;
                g_coll_state = 1;
                BC_LOG("collision: manager confirmed by the game's own camera query (manager %08x, its group %u, ours %u)",
                    (unsigned)r0, (unsigned)group, (unsigned)grp);
            } else {
                g_coll_state = -1;
                BC_LOG("collision: DISABLED - the game's own query used manager %08x, the tables give %08x", (unsigned)r0,
                    (unsigned)m);
            }
        }
    }
    g_host.fnhook_call_original(c, BC_FN_WALL);
}

static int bc_wall2_hit(BcCpu *live, uint32_t mgr, uint32_t avatar, uint32_t group, const float *from, const float *to) {
    uint32_t sp = (live->r[13] - 0x100u) & ~15u;
    if (!bc_guest_readable(sp - BC_WALL_STACK, BC_WALL_STACK + 0x100u)) return -1;
    const uint32_t a_start = sp + 0x20u, a_target = sp + 0x30u;
    for (uint32_t i = 0; i < 3u; i++) {
        bc_stf(a_start + 4u * i, from[i]);
        bc_stf(a_target + 4u * i, to[i]);
    }
    bc_stf(a_start + 12u, 0.0f);
    bc_stf(a_target + 12u, 0.0f);
    bc_st32(sp, a_target);
    bc_st32(sp + 4u, BC_WALL2_KIND);
    bc_st32(sp + 8u, 0u);
    BcCpu cc;
    memcpy(&cc, live, sizeof cc);
    cc.r[0] = mgr;
    cc.r[1] = avatar;
    cc.r[2] = group;
    cc.r[3] = a_start;
    cc.r[13] = sp;
    cc.f[0] = from[1];
    g_host.guest_call(&cc, BC_FN_WALL2);
    if (cc.r[0] > 1u) {
        g_coll_state = -1;
        BC_LOG("collision: DISABLED - the wall query returned %u, expected 0 or 1", (unsigned)cc.r[0]);
        return -1;
    }
    return (int)cc.r[0];
}

static int bc_wall2_selftest(BcCpu *live, uint32_t mgr, uint32_t avatar) {
    const float a[3] = {BC_WALL_PROBE, BC_WALL_PROBE, BC_WALL_PROBE};
    const float b[3] = {BC_WALL_PROBE + 300.0f, BC_WALL_PROBE, BC_WALL_PROBE};
    int h = bc_wall2_hit(live, mgr, avatar, 0u, a, b);
    return h < 0 ? -1 : h == 0 ? 1 : 0;
}

static float bc_wall2_cap(BcCpu *live, uint32_t mgr, uint32_t avatar, const uint32_t *groups, int n, const float *target,
    const float *dir, float want_r) {
    float best = want_r;
    for (int gi = 0; gi < n; gi++) {
        float lo = BC_MIN_RADIUS, hi = best;
        if (hi <= lo) break;
        float end[3] = {target[0] + dir[0] * hi, target[1] + dir[1] * hi, target[2] + dir[2] * hi};
        int h = bc_wall2_hit(live, mgr, avatar, groups[gi], target, end);
        if (h < 0) return want_r;
        if (h == 0) continue;
        for (int i = 0; i < 3; i++) end[i] = target[i] + dir[i] * lo;
        h = bc_wall2_hit(live, mgr, avatar, groups[gi], target, end);
        if (h < 0) return want_r;
        if (h > 0) {
            best = lo;
            break;
        }
        for (int it = 0; it < BC_BISECT_ITERS && hi - lo > BC_BISECT_EPS; it++) {
            float mid = 0.5f * (lo + hi);
            for (int i = 0; i < 3; i++) end[i] = target[i] + dir[i] * mid;
            h = bc_wall2_hit(live, mgr, avatar, groups[gi], target, end);
            if (h < 0) return want_r;
            if (h > 0) hi = mid;
            else lo = mid;
        }
        best = lo;
    }
    return best;
}

static int bc_wall_groups(const BcPose *pose, uint32_t *groups) {
    unsigned mask = 0x5u | g_cfg.extra_groups;
    if (pose && pose->zone >= 0 && pose->zone < 32) mask |= 1u << pose->zone;
    int n = 0;
    for (uint32_t g = 0; g < 32u && n < BC_COLL_MAX_GROUPS; g++)
        if (mask & (1u << g)) groups[n++] = g;
    return n;
}

static float bc_floor_cap(float target_y, float dir_y, float floor_y) {
    if (dir_y > -0.001f) return BC_COLL_FREE;
    float room = target_y - floor_y;
    float cap = room > 0.0f ? room / -dir_y : 0.0f;
    return cap < BC_FLOOR_MIN_R ? BC_FLOOR_MIN_R : cap;
}

#if BC_DEBUG > 0
static uint32_t g_probe_hits[32];
static float g_probe_free[32];
static uint32_t g_probe_seen;
static uint32_t g_probe_frames;
static uint64_t g_probe_t0;

static void bc_probe_reset_area(void) { g_probe_seen = 0; }

static void bc_probe(BcCpu *live, uint32_t mgr, uint32_t avatar, const float *target, const float *dir, float want_r) {
    float to[3] = {target[0] + dir[0] * want_r, target[1] + dir[1] * want_r, target[2] + dir[2] * want_r};
    for (uint32_t g = 0; g < 32u; g++) {
        if (bc_wall2_hit(live, mgr, avatar, g, target, to) != 1) continue;
        g_probe_hits[g]++;
        if (!(g_probe_seen & (1u << g))) {
            g_probe_seen |= 1u << g;
            g_probe_free[g] = bc_wall2_cap(live, mgr, avatar, &g, 1, target, dir, want_r);
            BC_LOG("probe: group %u blocks the camera line (first hit in this area): free up to %.1f of %.1f, from %.1f,%.1f,%.1f dir %.2f,%.2f,%.2f",
                (unsigned)g, g_probe_free[g], want_r, target[0], target[1], target[2], dir[0], dir[1], dir[2]);
        }
    }
    g_probe_frames++;
    uint64_t now = bc_now_ms();
    if (!g_probe_t0) g_probe_t0 = now;
    if (now - g_probe_t0 >= BC_PROBE_WINDOW_MS) {
        char buf[320];
        buf[0] = 0;
        for (uint32_t g = 0; g < 32u; g++) {
            if (!g_probe_hits[g]) continue;
            char item[48];
            snprintf(item, sizeof item, "%sg%u x%u", buf[0] ? ", " : "", (unsigned)g, (unsigned)g_probe_hits[g]);
            strncat(buf, item, sizeof buf - strlen(buf) - 1);
        }
        BC_LOG("probe: last %us (%u frames) at %.0f,%.0f,%.0f, camera yaw %.0f: %s", BC_PROBE_WINDOW_MS / 1000u,
            (unsigned)g_probe_frames, target[0], target[1], target[2], atan2f(dir[0], dir[2]) * (180.0f / M_PI_F),
            buf[0] ? buf : "no group blocks the line");
        memset(g_probe_hits, 0, sizeof g_probe_hits);
        g_probe_frames = 0;
        g_probe_t0 = now;
    }
}
#endif

static float bc_collide(BcCpu *live, const float *target, const float *dir, float want_r, int fresh, const BcPose *pose) {
    if (fresh) {
        if (g_coll_queries) BC_LOG("collision: previous area - %u queries, %u blocked frames", (unsigned)g_coll_queries,
            (unsigned)g_coll_hits);
        g_coll_queries = g_coll_hits = 0;
        g_coll_cap = BC_COLL_FREE;
#if BC_DEBUG > 0
        bc_probe_reset_area();
#endif
    }
    float goal = BC_COLL_FREE;
    uint32_t mgr;
    if (g_cfg.collision && pose && bc_coll_args(&mgr)) {
        uint32_t groups[BC_COLL_MAX_GROUPS];
        int n = bc_wall_groups(pose, groups);
        if (fresh) BC_LOG("collision: this area - manager %08x, hunter %08x zone byte %d, %d groups queried (first %u)",
            (unsigned)mgr, (unsigned)pose->avatar, pose->zone, n, n ? (unsigned)groups[0] : 0u);
        if (!g_coll_tested) {
            int t = bc_wall2_selftest(live, mgr, pose->avatar);
            if (t > 0) {
                g_coll_tested = 1;
                BC_LOG("collision: self-test passed (a far-away sweep misses)");
            } else if (t == 0) {
                g_coll_tested = 1;
                g_coll_state = -1;
                BC_LOG("collision: DISABLED - the self-test failed (a far-away sweep reported a hit)");
            }
        }
        if (g_coll_state >= 0 && g_coll_tested) {
            float cap = bc_wall2_cap(live, mgr, pose->avatar, groups, n, target, dir, want_r);
            g_coll_queries++;
            if (cap < want_r) {
                g_coll_hits++;
                goal = cap - BC_COLL_MARGIN;
                if (goal < BC_MIN_RADIUS) goal = BC_MIN_RADIUS;
#if BC_DEBUG > 0
                if (g_coll_hit_logs < BC_COLL_LOG_HITS) {
                    g_coll_hit_logs++;
                    BC_LOG("collision: blocked #%d from %.1f,%.1f,%.1f dir %.2f,%.2f,%.2f: free up to %.1f of %.1f (groups queried %d)",
                        g_coll_hit_logs, target[0], target[1], target[2], dir[0], dir[1], dir[2], cap, want_r, n);
                }
#endif
            }
#if BC_DEBUG > 0
            if (g_cfg.probe) bc_probe(live, mgr, pose->avatar, target, dir, want_r);
#endif
        }
    }
    if (goal < g_coll_cap) g_coll_cap = goal;
    else {
        g_coll_cap += BC_COLL_RELEASE_CM_S * g_dt;
        if (g_coll_cap > goal) g_coll_cap = goal;
    }
    return g_coll_cap < want_r ? g_coll_cap : want_r;
}

static void bc_discard_mouse(void) {
    float jx, jy;
    if (g_host.mouse_state) g_host.mouse_state(&jx, &jy);
}

typedef struct BcPadSlot {
    uint32_t id;
    uint32_t held;
    uint64_t opened_ms;
} BcPadSlot;

static BcPadSlot g_pad_slots[BC_PAD_MAX];
static int g_pad_count;
static uint32_t g_pad_active;
static uint32_t g_pad_next_refresh;

static uint32_t bc_pad_buttons(void *pad) {
    uint32_t mask = 0u;
    for (int b = 0; b < BC_PAD_BUTTONS; b++)
        if (g_host.sdl_gamepad_button(pad, b)) mask |= 1u << b;
    return mask;
}

static void bc_pad_refresh(void) {
    int n = 0;
    uint32_t *ids = g_host.sdl_gamepads(&n);
    BcPadSlot next[BC_PAD_MAX];
    int count = 0;
    uint64_t now = bc_now_ms();
    for (int i = 0; ids && i < n && count < BC_PAD_MAX; i++) {
        BcPadSlot slot = {ids[i], 0u, now};
        int known = 0;
        for (int j = 0; j < g_pad_count; j++) {
            if (g_pad_slots[j].id == ids[i]) {
                slot = g_pad_slots[j];
                known = 1;
                break;
            }
        }
        if (!known) {
            void *pad = g_host.sdl_gamepad_from_id(ids[i]);
            if (pad) slot.held = bc_pad_buttons(pad);
        }
        next[count++] = slot;
    }
    if (ids) g_host.sdl_free(ids);
    memcpy(g_pad_slots, next, sizeof(BcPadSlot) * (size_t)count);
    g_pad_count = count;
}

static void bc_pad_look(float *sx, float *sy) {
    *sx = *sy = 0.0f;
    if (!g_host.sdl_gamepads || !g_host.sdl_gamepad_from_id || !g_host.sdl_gamepad_axis ||
        !g_host.sdl_gamepad_button || !g_host.sdl_free)
        return;
    if ((int32_t)(g_frames - g_pad_next_refresh) >= 0) {
        g_pad_next_refresh = g_frames + BC_PAD_REFRESH_FRAMES;
        bc_pad_refresh();
    }
    uint64_t now = bc_now_ms();
    uint32_t first = 0u;
    for (int i = 0; i < g_pad_count; i++) {
        BcPadSlot *slot = &g_pad_slots[i];
        void *pad = g_host.sdl_gamepad_from_id(slot->id);
        if (!pad) continue;
        if (!first) first = slot->id;
        uint32_t held = bc_pad_buttons(pad);
        if ((held & ~slot->held) && now - slot->opened_ms >= BC_PAD_GRACE_MS) g_pad_active = slot->id;
        slot->held = held;
    }
    void *pad = g_pad_active ? g_host.sdl_gamepad_from_id(g_pad_active) : NULL;
    if (!pad) {
        g_pad_active = first;
        pad = first ? g_host.sdl_gamepad_from_id(first) : NULL;
    }
    if (!pad) return;
    float x = (float)g_host.sdl_gamepad_axis(pad, BC_PAD_AXIS_RIGHTX) / 32767.0f;
    float y = (float)g_host.sdl_gamepad_axis(pad, BC_PAD_AXIS_RIGHTY) / 32767.0f;
    float mag = sqrtf(x * x + y * y);
    if (!(mag > BC_PAD_DEADZONE)) return;
    float scale = bc_clampf((mag - BC_PAD_DEADZONE) / (1.0f - BC_PAD_DEADZONE), 0.0f, 1.0f);
    scale *= scale;
    *sx = x / mag * scale;
    *sy = y / mag * scale;
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
    bc_tick();
    float wheel = bc_take_wheel();
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

        if (frozen) wheel = 0.0f;
        if (!bc_guest_readable(eye_p, 12u) || !bc_guest_readable(target_p, 12u) || !vec_finite(eye_p) ||
            !vec_finite(target_p))
            break;
        float dx = 0.0f, dy = 0.0f;
        if (g_host.mouse_state) g_host.mouse_state(&dx, &dy);
        float sx = 0.0f, sy = 0.0f;
        bc_pad_look(&sx, &sy);
        if (frozen) dx = dy = sx = sy = 0.0f;

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
        if (sx != 0.0f || sy != 0.0f) {
            float step = BC_PAD_DEG_PER_S * g_cfg.stick_speed * g_dt;
            g_yaw_abs -= sx * step * (M_PI_F / 180.0f);
            g_pitch_abs += (g_cfg.invert_y ? -sy : sy) * step;
            g_pitch_abs = bc_clampf(g_pitch_abs, BC_PITCH_MIN, BC_PITCH_MAX);
        }

        int fresh = g_inherit_pending;
        if (fresh) {
            g_inherit_pending = 0;
            g_yaw_abs = atan2f(dxr, dzr);
            g_pitch_abs = 0.0f;
            bc_zoom_reset();
            BC_LOG("camera: orbit anchored at the game camera's angle (yaw %.1f deg) - now world-absolute",
                g_yaw_abs * (180.0f / M_PI_F));
        }

        BcPose pose;
        int have_pose = bc_player_pose(&pose);
        int settling = fresh;
        float behind_yaw;
        if (bc_settle_step(fresh, dx + sx, dy + sy, have_pose ? &pose : NULL, &behind_yaw)) {
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
        bc_zoom_step(wheel);
        use_r *= g_zoom;
        if (use_r < BC_MIN_RADIUS) use_r = BC_MIN_RADIUS;
        g_last_base = base_r;
        g_have_radius = 1;

        if (settling && have_pose) {
            float ap;
            if (bc_anchor_pitch(target[1], use_r, pose.pos[1], &ap)) g_pitch_abs = bc_clampf(ap, BC_PITCH_MIN, BC_PITCH_MAX);
        }

        float pr = g_pitch_abs * (M_PI_F / 180.0f);
        float cp = cosf(pr);
        float dir[3] = {sinf(g_yaw_abs) * cp, sinf(pr), cosf(g_yaw_abs) * cp};
        float r_final = bc_collide(c, target, dir, use_r, fresh, have_pose ? &pose : NULL);
        if (have_pose) {
            float r_floor = bc_floor_cap(target[1], dir[1], pose.pos[1] + BC_FLOOR_MARGIN);
            if (r_floor < r_final) r_final = r_floor;
        }
        for (int i = 0; i < 3; i++) eye[i] = target[i] + r_final * dir[i];
        if (have_pose && eye[1] < pose.pos[1] + BC_FLOOR_MARGIN) eye[1] = pose.pos[1] + BC_FLOOR_MARGIN;
        {
            float ex = eye[0] - target[0], ey = eye[1] - target[1], ez = eye[2] - target[2];
            g_last_use_r = sqrtf(ex * ex + ey * ey + ez * ez);
        }
        for (uint32_t i = 0; i < 3u; i++) bc_stf(eye_p + 4u * i, eye[i]);
        if (bc_guest_readable(up_p, 12u)) {
            bc_stf(up_p, 0.0f);
            bc_stf(up_p + 4u, 1.0f);
            bc_stf(up_p + 8u, 0.0f);
        }

#if BC_DEBUG > 0
        int active = dx != 0.0f || dy != 0.0f || sx != 0.0f || sy != 0.0f;
        if (active != g_last_active) {
            g_last_active = active;
            BC_LOG("camera: %s (yaw %.1f pitch %.1f)", active ? "look" : "idle",
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
    if (!g_host.mouse_state) BC_LOG("install: WARN - no SDL relative-mouse source: the mouse will not orbit");
    if (!g_host.sdl_gamepads || !g_host.sdl_gamepad_from_id || !g_host.sdl_gamepad_axis || !g_host.sdl_gamepad_button ||
        !g_host.sdl_free)
        BC_LOG("install: WARN - no SDL gamepad source: the controller will not orbit");
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

    g_coll_state = 0;
    g_coll_confirmed = 0;
    g_coll_tested = 0;
    if (!g_cfg.collision) {
        BC_LOG("install: collision off (SafeAreaCameraCollision=0)");
    } else if (!g_host.guest_call) {
        g_coll_state = -1;
        BC_LOG("install: WARN - the port does not export guest_call: no camera collision");
    } else if (!bc_guest_readable(BC_FN_WALL2, 8u)) {
        g_coll_state = -1;
        BC_LOG("install: WARN - the wall-slide query is not readable: no camera collision");
    } else {
        uint32_t w0 = bc_ld32(BC_FN_WALL2), w1 = bc_ld32(BC_FN_WALL2 + 4u);
        (void)w1;
        if (w0 != BC_WALL2_PROLOGUE) {
            g_coll_state = -1;
            BC_LOG("install: WARN - 0x%x starts %08x %08x, expected %08x (push {r4-r8,sb,sl,fp,lr}): the collision address may not match this build",
                (unsigned)BC_FN_WALL2, (unsigned)w0, (unsigned)w1, (unsigned)BC_WALL2_PROLOGUE);
        } else {
            if (bc_guest_readable(BC_FN_WALL, 8u) && (bc_ld32(BC_FN_WALL) & 0xffff0000u) == 0xe92d0000u) {
                g_wall_id = g_host.hle_register("moderncamera:wallquery", detour_wall);
                if (g_wall_id && g_host.fnhook_set(BC_FN_WALL, g_wall_id)) g_wall_hook = BC_FN_WALL;
            }
            BC_LOG("install: camera collision armed (wall-slide query 0x%x, kind 2; manager %s against the game's own hunt call)",
                (unsigned)BC_FN_WALL2, g_wall_hook ? "cross-checked" : "not cross-checked");
        }
    }
    if (g_host.sdl_add_watch && g_host.sdl_remove_watch && g_host.sdl_add_watch(bc_wheel_watch, NULL)) {
        g_wheel_watching = 1;
        BC_LOG("install: mouse wheel zoom armed");
    } else {
        BC_LOG("install: WARN - no SDL event watch: the mouse wheel will not zoom");
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
    if (g_coll_queries) BC_LOG("collision: last area - %u queries, %u blocked frames", (unsigned)g_coll_queries,
        (unsigned)g_coll_hits);
    if (g_wall_hook) {
        g_host.fnhook_set(g_wall_hook, 0);
        g_wall_hook = 0;
    }
    if (g_wheel_watching) {
        g_host.sdl_remove_watch(bc_wheel_watch, NULL);
        g_wheel_watching = 0;
    }
    BC_LOG("install: hooks removed");
}
