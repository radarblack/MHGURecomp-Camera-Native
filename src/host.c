#include "host.h"

#include <string.h>

#include "debug.h"
#include "platform.h"

BcHost g_host;

static void *sym(const char *name, int required) {
    void *p = bc_find_symbol(name);
    if (p) BC_LOG("host: %s found at %p", name, p);
    else if (required) BC_LOG("host: %s MISSING (required) - the module cannot work on this port build", name);
    else BC_LOG("host: %s missing (optional)", name);
    return p;
}

static void *sdl_sym(const char *name) {
    void *lib = bc_find_symbol_in_lib("SDL3.dll");
#ifndef _WIN32
    if (!lib) lib = bc_find_symbol_in_lib("libSDL3.so.0");
#endif
    void *r = lib ? bc_sym_in(lib, name) : 0;
    if (!r) r = bc_find_symbol(name);
    if (r) BC_LOG("host: %s found (SDL)", name);
    else BC_LOG("host: %s missing (SDL)", name);
    return r;
}

bool bc_host_resolve(void) {
    memset(&g_host, 0, sizeof g_host);
    bool ok = true;
    g_host.fnhook_set = (bool (*)(uint32_t, uint32_t))sym("fnhook_set", 1);
    ok &= g_host.fnhook_set != NULL;
    g_host.fnhook_get = (uint32_t (*)(uint32_t))sym("fnhook_get", 0);
    g_host.fnhook_call_original = (void (*)(BcCpu *, uint32_t))sym("fnhook_call_original", 1);
    ok &= g_host.fnhook_call_original != NULL;
    g_host.hle_register = (uint32_t (*)(const char *, BcHostFn))sym("hle_register", 1);
    ok &= g_host.hle_register != NULL;
    g_host.guest_call = (void (*)(BcCpu *, uint32_t))sym("guest_call", 0);
    g_host.dispatch_lookup = (void *(*)(uint32_t))sym("dispatch_lookup", 0);
    g_host.mouse_state = (uint32_t (*)(float *, float *))sdl_sym("SDL_GetRelativeMouseState");
    g_host.textinput_active = (bool (*)(void))sym("textinput_active", 0);
    g_host.chat_active = (bool (*)(void))sym("chat_active", 0);
    g_host.overlay_menu_open = (bool (*)(void))sym("overlay_menu_open", 0);
    g_host.mouse_free = (bool (*)(void))sym("host_input_mouse_free", 0);
    g_host.menuptr_open = (bool (*)(void))sym("menuptr_open", 0);
    g_host.menuptr_cursor_ran = (bool (*)(uint64_t))sym("menuptr_cursor_ran", 0);
    g_host.view_context = (const char *(*)(void))sym("input_view_context", 0);
    g_host.sdl_focus = (void *(*)(void))sdl_sym("SDL_GetKeyboardFocus");
    g_host.sdl_relmode = (bool (*)(void *))sdl_sym("SDL_GetWindowRelativeMouseMode");
    if (!ok) BC_LOG("host: resolution FAILED (a required symbol is missing)");
    return ok;
}
