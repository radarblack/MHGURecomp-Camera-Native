#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BcConfig g_cfg = {true, 1.0f, -80.0f, 89.0f, 1.60f, 1.90f};

static void parse_pair(char *line, const char *key, size_t klen, const char *value) {
    if (strlen(line) != klen || strncmp(line, key, klen)) return;
    if (!strcmp(key, "SafeAreaCamera")) g_cfg.safe_area_camera = atoi(value) != 0;
    else if (!strcmp(key, "SafeAreaCameraSpeed")) g_cfg.speed = (float)atof(value);
    else if (!strcmp(key, "SafeAreaCameraPitchMin")) g_cfg.pitch_min = (float)atof(value);
    else if (!strcmp(key, "SafeAreaCameraPitchMax")) g_cfg.pitch_max = (float)atof(value);
}

bool bc_config_load(const char *module_dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/BetterCamera.ini", module_dir ? module_dir : ".");
    FILE *f = fopen(path, "r");
    if (!f) return false;
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
        parse_pair(key, "SafeAreaCameraSpeed", 19, value);
        parse_pair(key, "SafeAreaCameraPitchMin", 22, value);
        parse_pair(key, "SafeAreaCameraPitchMax", 22, value);
    }
    fclose(f);
    return true;
}
