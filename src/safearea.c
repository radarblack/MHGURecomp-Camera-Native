#include "safearea.h"

#include <math.h>
#include <string.h>

#include "debug.h"
#include "safearea_math.h"

#include "config.h"
#include "guest.h"
#include "host.h"
#include "platform.h"

static uint32_t g_hooked_fn;
static uint32_t g_hle_id;
static uint32_t g_frames;
static int g_last_active;
static uint8_t g_last_area = 0xff;
static int g_vt_dumped;
static int g_gc_logged;

enum { R_OK, R_CFG_OFF, R_CAM_BAD, R_GC_BAD, R_QUEST, R_ASPECT, R_NOPLAYER, R_TILT_IDLE };
static const char *const g_reason[] = {
    "ok", "feature off (SafeAreaCamera=0)", "camera pointer not readable (not mapped or not a camera object)", "sGameControl not ready (pointer null or object not readable)",
    "quest area (the game's own camera runs)", "aspect outside the gameplay range", "no local player",
    "stick below the dead zone"};

static uint32_t local_player(void) {
    uint32_t gcp = bc_ld32(BC_G_SGAMECONTROL);
    uint32_t spp = bc_ld32(BC_G_SPLAYER);
    if (!gcp || !spp) return 0;
    uint32_t slot = bc_ld8(gcp + BC_GC_PLAYER_SLOT);
    if (slot > 3) return 0;
    uint32_t slot_addr = spp + BC_SC_PLAYER_SLOTS + 4u * slot;
    if (!bc_guest_readable(slot_addr, 4u)) return 0;
    uint32_t player = bc_ld32(slot_addr);
    if (!player || !bc_guest_readable(player, 4u)) return 0;
    return player;
}

static uint32_t vfunc(uint32_t obj, uint32_t off) {
    if (!bc_guest_readable(obj, 4u)) return 0;
    uint32_t vt = bc_ld32(obj);
    if (!vt || !bc_guest_readable(vt + off, 4u)) return 0;
    return bc_ld32(vt + off);
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

static void dump_player_vtables(uint32_t player) {
    uint32_t vt = bc_ld32(player);
    if (!bc_sane(vt)) {
        BC_LOG("guest: player %08x vtable %08x not sane - stick queries will fail", (unsigned)player, (unsigned)vt);
        return;
    }
    BC_LOG("guest: player %08x vt %08x | tilt slot 0x238 -> %08x | action slot 0x2e8 -> %08x", (unsigned)player,
        (unsigned)vt, (unsigned)bc_ld32(vt + BC_VT_TILT), (unsigned)bc_ld32(vt + BC_VT_CAM_ACTION));
}

static void detour_lookat(BcCpu *c) {
    uint32_t cam = c->r[1];
    g_frames++;
    int sample = (g_frames % 30u) == 1u;
    int active = 0;
    int reason = R_TILT_IDLE;
    do {
        if (!g_cfg.safe_area_camera) {
            reason = R_CFG_OFF;
            break;
        }
        if (!bc_guest_readable(cam, 0x70u)) {
            reason = R_CAM_BAD;
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
        if (area != g_last_area) {
            BC_LOG("area: %u -> %u (%s)", (unsigned)g_last_area, (unsigned)area,
                area == 0xff ? "QUEST - module idle" : "safe area - module armed");
            g_last_area = area;
        }
        if (area == 0xff) {
            reason = R_QUEST;
            break;
        }
        float aspect = bc_ldf(cam + BC_CAM_ASPECT);
        if (aspect < g_cfg.aspect_min || aspect > g_cfg.aspect_max) {
            reason = R_ASPECT;
            break;
        }
        uint32_t player = local_player();
        if (!player) {
            reason = R_NOPLAYER;
            break;
        }
        if (!g_vt_dumped) {
            g_vt_dumped = 1;
            dump_player_vtables(player);
        }
        uint32_t tilt = query_tilt(player);
        float mag = 0.0f;
        if (tilt > 0x1eu) mag = (float)(tilt < 0x200u ? tilt : 0x200u) - 30.0f;
        if (mag <= 0.0f) {
            reason = R_TILT_IDLE;
            break;
        }
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
        active = 1;
        reason = R_OK;
        if (sample)
            BC_LOGV("frame %u: tilt=%u deg=%.3f (yaw %.0f pitch %.0f, speed x%.2f) eye %.1f,%.1f,%.1f",
                (unsigned)g_frames, (unsigned)tilt, deg, yaw_in, pitch_in, speed_scale, eye[0], eye[1], eye[2]);
    } while (0);
    if (active != g_last_active) {
        if (active) BC_LOG("camera: ORBITING (area %u, tilt active)", (unsigned)g_last_area);
        else BC_LOG("camera: idle (%s)", g_reason[reason]);
        g_last_active = active;
    } else if (!active && sample) {
        BC_LOGV("frame %u: skip - %s", (unsigned)g_frames, g_reason[reason]);
    }
    (void)sample;
    (void)reason;
    (void)g_reason;
    g_host.fnhook_call_original(c, BC_FN_LOOKAT);
}

bool bc_safearea_install(void) {
    if (!g_host.fnhook_set || !g_host.hle_register || !g_host.guest_call) {
        BC_LOG("install: FAIL - host functions missing (fnhook_set=%p hle_register=%p guest_call=%p)",
            (void *)g_host.fnhook_set, (void *)g_host.hle_register, (void *)g_host.guest_call);
        return false;
    }
    if (g_hooked_fn) {
        BC_LOG("install: already armed at 0x21c33c");
        return true;
    }
    if (!bc_guest_readable(BC_FN_LOOKAT, 16u)) {
        BC_LOG("install: FAIL - the look-at 0x21c33c is not readable yet (the game code is not mapped)");
        return false;
    }
    uint32_t w0 = bc_ld32(BC_FN_LOOKAT), w1 = bc_ld32(BC_FN_LOOKAT + 4u), w2 = bc_ld32(BC_FN_LOOKAT + 8u),
             w3 = bc_ld32(BC_FN_LOOKAT + 12u);
    BC_LOG("install: look-at 0x21c33c words: %08x %08x %08x %08x (expected %08x = push {r11,lr})", (unsigned)w0,
        (unsigned)w1, (unsigned)w2, (unsigned)w3, 0xe92d4800u);
    (void)w1;
    (void)w2;
    (void)w3;
    if (w0 != 0xe92d4800u) {
        BC_LOG("install: FAIL - target bytes do not match (code moved or a different game build): hooking would "
               "corrupt the port - feature off");
        return false;
    }
    g_hle_id = g_host.hle_register("bettercamera:lookat", detour_lookat);
    if (!g_hle_id) {
        BC_LOG("install: FAIL - hle_register returned 0");
        return false;
    }
    BC_LOG("install: detour registered as hle id %u", (unsigned)g_hle_id);
    if (!g_host.fnhook_set(BC_FN_LOOKAT, g_hle_id)) {
        BC_LOG("install: FAIL - fnhook_set(0x21c33c, %u) returned false", (unsigned)g_hle_id);
        return false;
    }
    if (g_host.fnhook_get) {
        uint32_t rb = g_host.fnhook_get(BC_FN_LOOKAT);
        if (rb == g_hle_id)
            BC_LOG("install: HOOK VERIFIED - fnhook_get(0x21c33c) == %u (the look-at now runs BetterCamera)",
                (unsigned)rb);
        else
            BC_LOG("install: WARN - fnhook_get(0x21c33c) = %u, expected %u", (unsigned)rb, (unsigned)g_hle_id);
    } else {
        BC_LOG("install: fnhook_set accepted (no fnhook_get to read back - verification by field test)");
    }
    g_hooked_fn = BC_FN_LOOKAT;
    BC_LOG("install: armed (gates: area != 255, aspect %.2f..%.2f, tilt > 30)", g_cfg.aspect_min, g_cfg.aspect_max);
    return true;
}

void bc_safearea_remove(void) {
    if (!g_hooked_fn) return;
    g_host.fnhook_set(g_hooked_fn, 0);
    g_hooked_fn = 0;
    BC_LOG("install: hook removed");
}
