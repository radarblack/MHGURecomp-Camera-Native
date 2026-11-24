#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

BcConfig g_cfg = {1.0f, 1.0f, false, 1.0f, 100.0f, 188.0f, true, 0u, false};

typedef struct BcKey {
    const char *name;
    float *num;
    bool *flag;
    unsigned *mask;
    float lo, hi;
} BcKey;

static const BcKey k_keys[] = {
    {"SafeAreaCameraSpeed", &g_cfg.speed, NULL, NULL, 0.05f, 10.0f},
    {"SafeAreaCameraStickSpeed", &g_cfg.stick_speed, NULL, NULL, 0.05f, 10.0f},
    {"SafeAreaCameraInvertY", NULL, &g_cfg.invert_y, NULL, 0.0f, 0.0f},
    {"SafeAreaCameraDistanceScale", &g_cfg.distance_scale, NULL, NULL, 0.1f, 10.0f},
    {"SafeAreaCameraStartHeight", &g_cfg.start_height, NULL, NULL, 0.0f, 500.0f},
    {"SafeAreaCameraCharacterHeight", &g_cfg.char_height, NULL, NULL, 1.0f, 1000.0f},
    {"SafeAreaCameraCollision", NULL, &g_cfg.collision, NULL, 0.0f, 0.0f},
    {"SafeAreaCameraExtraGroups", NULL, NULL, &g_cfg.extra_groups, 0.0f, 0.0f},
    {"SafeAreaCameraCollisionProbe", NULL, &g_cfg.probe, NULL, 0.0f, 0.0f},
};

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void apply(const char *key, const char *value) {
    for (size_t i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++) {
        const BcKey *k = &k_keys[i];
        if (strcmp(key, k->name)) continue;
        if (k->mask) {
            *k->mask = (unsigned)strtoul(value, NULL, 0);
            BC_LOG("config: %s = 0x%x", k->name, *k->mask);
        } else if (k->flag) {
            *k->flag = atoi(value) != 0;
            BC_LOG("config: %s = %d", k->name, *k->flag);
        } else {
            float v = (float)atof(value);
            *k->num = v < k->lo ? k->lo : v > k->hi ? k->hi : v;
            BC_LOG("config: %s = %.2f", k->name, *k->num);
        }
        return;
    }
}

bool bc_config_load(const char *module_dir) {
    char path[512];
    snprintf(path, sizeof path, "%s/ModernCamera.ini", module_dir ? module_dir : ".");
    FILE *f = fopen(path, "r");
    if (!f) {
        BC_LOG("config: %s not found - defaults in force", path);
        return false;
    }
    unsigned char bom[3];
    if (fread(bom, 1, sizeof bom, f) != sizeof bom || memcmp(bom, "\xEF\xBB\xBF", sizeof bom) != 0) rewind(f);
    BC_LOG("config: reading %s", path);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(line);
        if (*key == ';' || *key == '#') continue;
        apply(key, trim(eq + 1));
    }
    fclose(f);
    BC_LOG("config: loaded (Speed=%.2f StickSpeed=%.2f InvertY=%d DistanceScale=%.2f StartHeight=%.1f CharacterHeight=%.1f Collision=%d ExtraGroups=0x%x Probe=%d)", g_cfg.speed,
        g_cfg.stick_speed, g_cfg.invert_y, g_cfg.distance_scale, g_cfg.start_height, g_cfg.char_height, g_cfg.collision, g_cfg.extra_groups,
        g_cfg.probe);
    return true;
}
