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
    bool vertical;
    bool invert_y;
    float distance_scale;
    bool start_behind;
    float start_yaw_offset;
    bool main_only;
    bool anchor_height;
    float start_height;
    bool fit_character;
    float char_height;
    bool pause_ui;
    bool pause_events;
} BcConfig;

extern BcConfig g_cfg;
bool bc_config_load(const char *module_dir);
