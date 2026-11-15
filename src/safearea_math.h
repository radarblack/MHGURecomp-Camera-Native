#include "safearea.h"

#include <math.h>

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif

static float bc_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void bc_orbit(float *eye, const float *target, float dyaw_deg, float dpitch_deg, float pitch_min_deg,
              float pitch_max_deg) {
    float dx = eye[0] - target[0], dy = eye[1] - target[1], dz = eye[2] - target[2];
    float r = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(r > 0.0001f) || r > 100000.0f) return;
    float yaw = atan2f(dx, dz);
    float pitch = asinf(bc_clampf(dy / r, -1.0f, 1.0f));
    yaw += dyaw_deg * (M_PI_F / 180.0f);
    pitch = bc_clampf(pitch + dpitch_deg * (M_PI_F / 180.0f), pitch_min_deg * (M_PI_F / 180.0f),
                      pitch_max_deg * (M_PI_F / 180.0f));
    float cp = cosf(pitch);
    eye[0] = target[0] + r * sinf(yaw) * cp;
    eye[1] = target[1] + r * sinf(pitch);
    eye[2] = target[2] + r * cosf(yaw) * cp;
}
