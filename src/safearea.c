#include "safearea.h"

#include <math.h>

#include "safearea_math.h"

#include "config.h"
#include "guest.h"
#include "host.h"
#include "platform.h"

static uint32_t g_hooked_fn;
static uint32_t g_hle_id;

static bool gc_ready(void) {
    uint32_t gcp = bc_ld32(BC_G_SGAMECONTROL);
    return bc_sane(gcp);
}

static uint8_t area_type(void) {
    uint32_t gcp = bc_ld32(BC_G_SGAMECONTROL);
    if (!bc_sane(gcp)) return 0xff;
    return bc_ld8(gcp + BC_GC_AREA_TYPE);
}

static uint32_t local_player(void) {
    uint32_t gcp = bc_ld32(BC_G_SGAMECONTROL);
    uint32_t spp = bc_ld32(BC_G_SPLAYER);
    if (!bc_sane(gcp) || !bc_sane(spp)) return 0;
    uint32_t slot = bc_ld8(gcp + BC_GC_PLAYER_SLOT);
    if (slot > 3) return 0;
    uint32_t player = bc_ld32(spp + BC_SC_PLAYER_SLOTS + 4u * slot);
    return bc_sane(player) ? player : 0;
}

static uint32_t vfunc(uint32_t obj, uint32_t off) {
    uint32_t vt = bc_ld32(obj);
    if (!bc_sane(vt)) return 0;
    uint32_t fn = bc_ld32(vt + off);
    return bc_sane(fn) ? fn : 0;
}

static uint32_t query_action(uint32_t player, uint32_t id) {
    uint32_t fn = vfunc(player, BC_VT_CAM_ACTION);
    if (!fn) return 0;
    BcCpu c;
    memset(&c, 0, sizeof c);
    c.r[0] = player;
    c.r[1] = id;
    g_host.guest_call(&c, fn);
    return c.r[0];
}

static uint32_t query_tilt(uint32_t player) {
    uint32_t fn = vfunc(player, BC_VT_TILT);
    if (!fn) return 0;
    BcCpu c;
    memset(&c, 0, sizeof c);
    c.r[0] = player;
    g_host.guest_call(&c, fn);
    return c.r[0];
}

static float stick_axis(uint32_t player, uint32_t pos_id, uint32_t neg_id, uint8_t invert_bit, uint8_t invert) {
    float v = 0.0f;
    if (query_action(player, pos_id)) v += 1.0f;
    if (query_action(player, neg_id)) v -= 1.0f;
    if (invert & invert_bit) v = -v;
    return v;
}

static void detour_lookat(BcCpu *c) {
    uint32_t cam = c->r[1];
    if (g_cfg.safe_area_camera && bc_sane(cam) && area_type() != 0xff && gc_ready()) {
        float aspect = bc_ldf(cam + BC_CAM_ASPECT);
        if (aspect >= g_cfg.aspect_min && aspect <= g_cfg.aspect_max) {
            uint32_t player = local_player();
            if (player) {
                uint32_t tilt = query_tilt(player);
                float mag = 0.0f;
                if (tilt > 0x1eu) mag = (float)(tilt < 0x200u ? tilt : 0x200u) - 30.0f;
                if (mag > 0.0f) {
                    uint8_t invert = bc_ld8(bc_ld32(BC_G_SGAMECONTROL) + BC_GC_INVERT);
                    float yaw_in = stick_axis(player, 3, 4, 0x02, invert);
                    float pitch_in = stick_axis(player, 1, 2, 0x01, invert);
                    float speed_scale = 1.0f;
                    uint8_t cs = bc_ld8(bc_ld32(BC_G_SGAMECONTROL) + BC_GC_CAM_SPEED);
                    if (cs == 0) speed_scale = 0.75f;
                    else if (cs > 1) speed_scale = 1.25f;
                    float deg = mag / 482.0f * g_cfg.speed * speed_scale;
                    float eye[3], target[3];
                    for (int i = 0; i < 3; i++) {
                        eye[i] = bc_ldf(cam + BC_CAM_EYE + 4u * (uint32_t)i);
                        target[i] = bc_ldf(cam + BC_CAM_TARGET + 4u * (uint32_t)i);
                    }
                    bc_orbit(eye, target, yaw_in * deg, -pitch_in * deg, g_cfg.pitch_min, g_cfg.pitch_max);
                    for (int i = 0; i < 3; i++) {
                        uint32_t w;
                        memcpy(&w, &eye[i], 4);
                        bc_st32(cam + BC_CAM_EYE + 4u * (uint32_t)i, w);
                    }
                }
            }
        }
    }
    g_host.fnhook_call_original(c, BC_FN_LOOKAT);
}

bool bc_safearea_install(void) {
    if (!g_host.fnhook_set || !g_host.hle_register || !g_host.guest_call) return false;
    if (g_hooked_fn) return true;
    g_hle_id = g_host.hle_register("bettercamera:lookat", detour_lookat);
    if (!g_hle_id) return false;
    if (!g_host.fnhook_set(BC_FN_LOOKAT, g_hle_id)) return false;
    g_hooked_fn = BC_FN_LOOKAT;
    return true;
}

void bc_safearea_remove(void) {
    if (!g_hooked_fn) return;
    g_host.fnhook_set(g_hooked_fn, 0);
    g_hooked_fn = 0;
}
