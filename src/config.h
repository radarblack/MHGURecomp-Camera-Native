#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct BcConfig {
    bool safe_area_camera;
    bool level;
    bool hunt_defaults;
    float hunt_distance;
    float hunt_pitch_deg;
    float speed;
    float pitch_min;
    float pitch_max;
} BcConfig;

extern BcConfig g_cfg;
bool bc_config_load(const char *module_dir);
