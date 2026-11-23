#include <math.h>
#include <string.h>

#include "collision.h"
#include "config.h"
#include "debug.h"
#include "guest.h"
#include "host.h"
#include "platform.h"

#define BC_COL_FN_WALLQ 0x3828fcu
#define BC_COL_WALL_MGR_SLOT 0x1a39320u
#define BC_COL_WALL_YOFF_SLOT 0x1a3c41cu
#define BC_COL_RADIUS 20.0f

#define BC_COL_SCRATCH 0x2000u

#define BC_COL_ARGS 0x00u
#define BC_COL_START 0x20u
#define BC_COL_ZVAL 0x30u
#define BC_COL_TARGET 0x40u
#define BC_COL_SIZE 0x50u

static uint32_t bc_col_local_player(void) {
    uint32_t gc = bc_ld32(BC_G_SGAMECONTROL);
    uint32_t sp = bc_ld32(BC_G_SPLAYER);
    if (!gc || !sp || !bc_guest_readable(gc, 0x40u) || !bc_guest_readable(sp, 0x30u)) return 0;
    uint32_t slot = bc_ld32(gc + BC_GC_SLOT);
    if (slot >= 4u) return 0;
    return bc_ld32(sp + BC_SP_PLAYERS + 4u * slot);
}

static unsigned g_first_hit_logged;
static unsigned g_first_skip_logged;

static int bc_finite3(const float *v) {
    for (uint32_t i = 0; i < 3u; i++)
        if (!(v[i] == v[i]) || v[i] > 1e9f || v[i] < -1e9f) return 0;
    return 1;
}

int bc_collision_clamp(BcCpu *c, const float pivot[3], float eye[3]) {
    BcCpuFull *cpu = (BcCpuFull *)(void *)c;
    if (!bc_finite3(pivot) || !bc_finite3(eye)) return 0;

    uint32_t pl = bc_col_local_player();
    if (!pl || !bc_guest_readable(pl + BC_PL_ZONE, 1u)) return 0;
    uint32_t zone = bc_ld8(pl + BC_PL_ZONE);
    if (zone == 0xffu || zone > 7u) return 0;

    if (!bc_guest_readable(BC_COL_WALL_MGR_SLOT, 4u)) return 0;
    uint32_t mgr_var = bc_ld32(BC_COL_WALL_MGR_SLOT);
    if (!mgr_var || !bc_guest_readable(mgr_var, 4u)) return 0;
    uint32_t mgr = bc_ld32(mgr_var);
    if (!mgr || !bc_guest_readable(mgr, 0x40u)) return 0;

    float yoff = 0.0f;
    if (bc_guest_readable(BC_COL_WALL_YOFF_SLOT, 4u)) {
        uint32_t yp = bc_ld32(BC_COL_WALL_YOFF_SLOT);
        if (yp && bc_guest_readable(yp, 4u)) yoff = bc_ldf(yp);
    }

    uint32_t s = (cpu->r[13] - BC_COL_SCRATCH) & ~7u;
    if (!bc_guest_readable(s, BC_COL_SIZE)) {
        if (!g_first_skip_logged++) BC_LOG("collision: scratch at %08X unreadable, skipped", s);
        return 0;
    }

    bc_st32(s + BC_COL_ARGS + 0u, s + BC_COL_TARGET);
    bc_st32(s + BC_COL_ARGS + 4u, 2u);
    bc_st32(s + BC_COL_ARGS + 8u, 0u);
    bc_stf(s + BC_COL_START + 0u, pivot[0]);
    bc_stf(s + BC_COL_START + 4u, pivot[1]);
    bc_st32(s + BC_COL_START + 8u, s + BC_COL_ZVAL);
    bc_stf(s + BC_COL_ZVAL + 0u, pivot[2]);
    bc_st32(s + BC_COL_ZVAL + 4u, 0u);
    for (uint32_t i = 0; i < 3u; i++) bc_stf(s + BC_COL_TARGET + 4u * i, eye[i]);
    bc_st32(s + BC_COL_TARGET + 12u, 0u);

    BcCpuFull call = *cpu;
    call.r[0] = mgr;
    call.r[1] = pl;
    call.r[2] = zone;
    call.r[3] = s + BC_COL_START;
    call.v.f[0] = pivot[1] + yoff;
    call.r[13] = s;
    call.r[14] = 0u;
    g_host.fnhook_call_original((BcCpu *)&call, BC_COL_FN_WALLQ);

    if (call.r[0] != 1u) return 0;

    float hx = bc_ldf(s + BC_COL_START + 0u);
    float hz = bc_ldf(s + BC_COL_ZVAL + 0u);
    float dv[2] = {hx - pivot[0], hz - pivot[2]};
    if (!(dv[0] == dv[0]) || !(dv[1] == dv[1]) || dv[0] > 1e9f || dv[0] < -1e9f || dv[1] > 1e9f ||
        dv[1] < -1e9f)
        return 0;
    float ex = eye[0] - pivot[0], ez = eye[2] - pivot[2];
    float elen = sqrtf(ex * ex + ez * ez);
    if (elen < 1e-3f) return 0;
    if (dv[0] * ex + dv[1] * ez <= 0.0f) return 0;

    float m = g_cfg.collision_margin;
    if (m < 0.0f) m = 0.0f;
    eye[0] = hx - (ex / elen) * m;
    eye[2] = hz - (ez / elen) * m;

    if (!g_first_hit_logged++) BC_LOG("collision: wall hit at (%.1f, %.1f) group %u, clamped", hx, hz, zone);
    BC_LOGV("collision: hit group %u at (%.1f, %.1f) -> eye (%.1f, %.1f)", zone, hx, hz, eye[0], eye[2]);
    return 1;
}
