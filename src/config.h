#pragma once
#include <stdbool.h>

typedef struct BcConfig {
    float speed;
    bool invert_y;
    float distance_scale;
    float start_height;
    float char_height;
} BcConfig;

extern BcConfig g_cfg;
bool bc_config_load(const char *module_dir);
