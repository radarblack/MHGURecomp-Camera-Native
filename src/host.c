#include "host.h"

#include <string.h>

#include "platform.h"

BcHost g_host;

bool bc_host_resolve(void) {
    memset(&g_host, 0, sizeof g_host);
    bool ok = true;
    g_host.fnhook_set = (bool (*)(uint32_t, uint32_t))bc_find_symbol("fnhook_set");
    ok &= g_host.fnhook_set != NULL;
    g_host.fnhook_get = (uint32_t (*)(uint32_t))bc_find_symbol("fnhook_get");
    g_host.fnhook_call_original = (void (*)(BcCpu *, uint32_t))bc_find_symbol("fnhook_call_original");
    ok &= g_host.fnhook_call_original != NULL;
    g_host.hle_register = (uint32_t (*)(const char *, BcHostFn))bc_find_symbol("hle_register");
    ok &= g_host.hle_register != NULL;
    g_host.guest_call = (void (*)(BcCpu *, uint32_t))bc_find_symbol("guest_call");
    g_host.dispatch_lookup = (void *(*)(uint32_t))bc_find_symbol("dispatch_lookup");
    return ok;
}
