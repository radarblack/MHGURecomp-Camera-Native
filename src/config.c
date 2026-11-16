#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

BcConfig g_cfg = {true, true, true, 0.0f, 999.0f, 1.0f, -80.0f, 89.0f};

static void parse_pair(char *line, const char *key, size_t klen, const char *value) {
    if (strlen(line) != klen || strncmp(line, key, klen)) return;
    if (!strcmp(key, "SafeAreaCamera")) {
        g_cfg.safe_area_camera = atoi(value) != 0;
        BC_LOG("config: SafeAreaCamera = %d", g_cfg.safe_area_camera);
    } else if (!strcmp(key, "SafeAreaCameraLevel")) {
        g_cfg.level = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraLevel = %d", g_cfg.level);
    } else if (!strcmp(key, "SafeAreaCameraHuntDefaults")) {
        g_cfg.hunt_defaults = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraHuntDefaults = %d", g_cfg.hunt_defaults);
    } else if (!strcmp(key, "SafeAreaCameraDistance")) {
        g_cfg.hunt_distance = (float)atof(value);
        BC_LOG("config: SafeAreaCameraDistance = %.2f", g_cfg.hunt_distance);
    } else if (!strcmp(key, "SafeAreaCameraPitchDeg")) {
        g_cfg.hunt_pitch_deg = (float)atof(value);
        BC_LOG("config: SafeAreaCameraPitchDeg = %.1f", g_cfg.hunt_pitch_deg);
    } else if (!strcmp(key, "SafeAreaCameraSpeed")) {
        g_cfg.speed = (float)atof(value);
        BC_LOG("config: SafeAreaCameraSpeed = %.2f", g_cfg.speed);
    } else if (!strcmp(key, "SafeAreaCameraPitchMin")) {
        g_cfg.pitch_min = (float)atof(value);
        BC_LOG("config: SafeAreaCameraPitchMin = %.1f", g_cfg.pitch_min);
    } else if (!strcmp(key, "SafeAreaCameraPitchMax")) {
        g_cfg.pitch_max = (float)atof(value);
        BC_LOG("config: SafeAreaCameraPitchMax = %.1f", g_cfg.pitch_max);
    }
}

bool bc_config_load(const char *module_dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/BetterCamera.ini", module_dir ? module_dir : ".");
    FILE *f = fopen(path, "r");
    if (!f) {
        BC_LOG("config: %s not found - defaults in force (SafeAreaCamera=1 Speed=1.0 Pitch %.1f..%.1f)", path,
            g_cfg.pitch_min, g_cfg.pitch_max);
        return false;
    }
    BC_LOG("config: reading %s", path);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *value = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        parse_pair(key, "SafeAreaCamera", 15, value);
        parse_pair(key, "SafeAreaCameraHuntDefaults", 24, value);
        parse_pair(key, "SafeAreaCameraDistance", 22, value);
        parse_pair(key, "SafeAreaCameraPitchDeg", 22, value);
        parse_pair(key, "SafeAreaCameraLevel", 19, value);
        parse_pair(key, "SafeAreaCameraSpeed", 19, value);
        parse_pair(key, "SafeAreaCameraPitchMin", 22, value);
        parse_pair(key, "SafeAreaCameraPitchMax", 22, value);
    }
    fclose(f);
    BC_LOG("config: loaded (SafeAreaCamera=%d Level=%d HuntDefaults=%d Distance=%.1f PitchDeg=%.1f Speed=%.2f Pitch %.1f..%.1f)",
        g_cfg.safe_area_camera, g_cfg.level, g_cfg.hunt_defaults, g_cfg.hunt_distance, g_cfg.hunt_pitch_deg, g_cfg.speed,
        g_cfg.pitch_min, g_cfg.pitch_max);
    return true;
}
