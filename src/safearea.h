#pragma once
#include <stdbool.h>
#include <stdint.h>

void bc_orbit(float *eye, const float *target, float dyaw_deg, float dpitch_deg, float pitch_min_deg,
              float pitch_max_deg);
bool bc_safearea_install(void);
void bc_safearea_remove(void);
