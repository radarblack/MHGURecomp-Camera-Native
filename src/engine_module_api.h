#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BC_MODULE_API 1
#define BC_MODULE_API_NAME "mhgu_engine_module_api"
#define BC_MODULE_ENTRY_NAME "mhgu_engine_module_entry"

#ifdef __cplusplus
#define BC_MODULE_LINK extern "C"
#else
#define BC_MODULE_LINK
#endif
#ifdef _WIN32
#define BC_MODULE_EXPORT BC_MODULE_LINK __declspec(dllexport)
#else
#define BC_MODULE_EXPORT BC_MODULE_LINK __attribute__((visibility("default")))
#endif

typedef struct BcModuleHost {
    uint32_t api;
    void (*log)(const char *fmt, ...);
    bool (*setting)(const char *key, char *out, size_t out_size);
    const char *module_dir;
    const char *port_version;
} BcModuleHost;

typedef struct BcModule {
    const char *name;
    void (*shutdown)(void);
} BcModule;

typedef bool (*BcModuleEntry)(const BcModuleHost *host, BcModule *out);
