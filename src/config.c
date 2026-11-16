#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

BcConfig g_cfg = {true, true, true, 0.0f, 999.0f, 1.0f, -10.0f, 89.0f, true, false, 1.0f, true, 0.0f, true, true, 100.0f, true,
    188.0f, true, true};

static void parse_pair(char *line, const char *key, size_t klen, const char *value) {
    if (strcmp(line, key)) return;
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
    } else if (!strcmp(key, "SafeAreaCameraVertical")) {
        g_cfg.vertical = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraVertical = %d", g_cfg.vertical);
    } else if (!strcmp(key, "SafeAreaCameraInvertY")) {
        g_cfg.invert_y = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraInvertY = %d", g_cfg.invert_y);
    } else if (!strcmp(key, "SafeAreaCameraDistanceScale")) {
        float v = (float)atof(value);
        g_cfg.distance_scale = v < 0.1f ? 0.1f : v > 10.0f ? 10.0f : v;
        BC_LOG("config: SafeAreaCameraDistanceScale = %.2f", g_cfg.distance_scale);
    } else if (!strcmp(key, "SafeAreaCameraStartBehind")) {
        g_cfg.start_behind = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraStartBehind = %d", g_cfg.start_behind);
    } else if (!strcmp(key, "SafeAreaCameraPauseInMenus")) {
        g_cfg.pause_ui = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraPauseInMenus = %d", g_cfg.pause_ui);
    } else if (!strcmp(key, "SafeAreaCameraPauseInEvents")) {
        g_cfg.pause_events = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraPauseInEvents = %d", g_cfg.pause_events);
    } else if (!strcmp(key, "SafeAreaCameraMainOnly")) {
        g_cfg.main_only = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraMainOnly = %d", g_cfg.main_only);
    } else if (!strcmp(key, "SafeAreaCameraStartYawOffset")) {
        g_cfg.start_yaw_offset = (float)atof(value);
        BC_LOG("config: SafeAreaCameraStartYawOffset = %.1f", g_cfg.start_yaw_offset);
    } else if (!strcmp(key, "SafeAreaCameraAnchorHeight")) {
        g_cfg.anchor_height = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraAnchorHeight = %d", g_cfg.anchor_height);
    } else if (!strcmp(key, "SafeAreaCameraStartHeight")) {
        g_cfg.start_height = (float)atof(value);
        BC_LOG("config: SafeAreaCameraStartHeight = %.1f", g_cfg.start_height);
    } else if (!strcmp(key, "SafeAreaCameraFitCharacter")) {
        g_cfg.fit_character = atoi(value) != 0;
        BC_LOG("config: SafeAreaCameraFitCharacter = %d", g_cfg.fit_character);
    } else if (!strcmp(key, "SafeAreaCameraCharacterHeight")) {
        float v = (float)atof(value);
        g_cfg.char_height = v < 1.0f ? 1.0f : v > 1000.0f ? 1000.0f : v;
        BC_LOG("config: SafeAreaCameraCharacterHeight = %.1f", g_cfg.char_height);
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
        parse_pair(key, "SafeAreaCameraVertical", 22, value);
        parse_pair(key, "SafeAreaCameraInvertY", 21, value);
        parse_pair(key, "SafeAreaCameraDistanceScale", 27, value);
        parse_pair(key, "SafeAreaCameraStartBehind", 25, value);
        parse_pair(key, "SafeAreaCameraStartYawOffset", 28, value);
        parse_pair(key, "SafeAreaCameraMainOnly", 22, value);
        parse_pair(key, "SafeAreaCameraAnchorHeight", 26, value);
        parse_pair(key, "SafeAreaCameraStartHeight", 25, value);
        parse_pair(key, "SafeAreaCameraFitCharacter", 26, value);
        parse_pair(key, "SafeAreaCameraCharacterHeight", 29, value);
        parse_pair(key, "SafeAreaCameraPauseInMenus", 26, value);
        parse_pair(key, "SafeAreaCameraPauseInEvents", 27, value);
    }
    fclose(f);
    BC_LOG("config: loaded (SafeAreaCamera=%d Level=%d HuntDefaults=%d Distance=%.1f PitchDeg=%.1f Speed=%.2f Pitch %.1f..%.1f Vertical=%d InvertY=%d DistScale=%.2f StartBehind=%d StartYawOffset=%.1f MainOnly=%d AnchorHeight=%d StartHeight=%.1f Fit=%d CharHeight=%.1f PauseInMenus=%d PauseInEvents=%d)",
        g_cfg.safe_area_camera, g_cfg.level, g_cfg.hunt_defaults, g_cfg.hunt_distance, g_cfg.hunt_pitch_deg, g_cfg.speed,
        g_cfg.pitch_min, g_cfg.pitch_max, g_cfg.vertical, g_cfg.invert_y, g_cfg.distance_scale, g_cfg.start_behind, g_cfg.start_yaw_offset, g_cfg.main_only,
        g_cfg.anchor_height, g_cfg.start_height, g_cfg.fit_character, g_cfg.char_height, g_cfg.pause_ui, g_cfg.pause_events);
    return true;
}
