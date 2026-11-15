#include <stdio.h>
#include <string.h>

#include "config.h"
#include "engine_module_api.h"
#include "guest.h"
#include "host.h"
#include "platform.h"
#include "safearea.h"

#ifndef BC_VERSION
#define BC_VERSION "0.1.0"
#endif

static const char *const k_checked_ports[] = {"0.16.", "0.17."};

static BcModuleHost g_mh;
static volatile bool g_shutdown_done;

static BcThread g_worker;
static volatile int g_install_state;
#define BC_INSTALL_IDLE 0
#define BC_INSTALL_OK 1
#define BC_INSTALL_FAILED 2

static bool port_version_checked(const char *v) {
    if (!v) return false;
    for (size_t i = 0; i < sizeof k_checked_ports / sizeof k_checked_ports[0]; i++)
        if (!strncmp(v, k_checked_ports[i], strlen(k_checked_ports[i]))) return true;
    return false;
}

static bool game_ready(void) {
    if (!g_host.dispatch_lookup) return false;
    return g_host.dispatch_lookup(BC_FN_LOOKAT) != NULL;
}

static void worker(void *unused) {
    (void)unused;
    uint64_t t0 = bc_now_ms();
    while (bc_now_ms() - t0 < 60000u) {
        if (g_shutdown_done) return;
        if (game_ready()) {
            if (bc_host_resolve() && bc_safearea_install()) {
                g_install_state = BC_INSTALL_OK;
                g_mh.log("[bettercamera] safe-area camera armed (v%s)\n", BC_VERSION);
            } else {
                g_install_state = BC_INSTALL_FAILED;
                g_mh.log("[bettercamera] hook install failed: feature off, boot unaffected\n");
            }
            return;
        }
        bc_sleep_ms(100);
    }
    g_install_state = BC_INSTALL_FAILED;
    g_mh.log("[bettercamera] game code never appeared: feature off, boot unaffected\n");
}

static void module_shutdown(void) {
    if (g_shutdown_done) return;
    g_shutdown_done = true;
    bc_safearea_remove();
    if (g_worker.started) bc_thread_join(&g_worker, 5000u);
}

BC_MODULE_EXPORT bool mhgu_engine_module_entry(const BcModuleHost *host, BcModule *out) {
    if (!host || !out) return false;
    if (host->api < 1 || host->api > BC_MODULE_API) return false;
    if (!port_version_checked(host->port_version)) {
        host->log("[bettercamera] port %s unverified: not loaded\n", host->port_version ? host->port_version : "?");
        return false;
    }
    g_mh = *host;
    g_shutdown_done = false;
    bc_config_load(host->module_dir);
    out->name = "BetterCamera";
    out->shutdown = module_shutdown;
    g_install_state = BC_INSTALL_IDLE;
    if (!bc_thread_start(&g_worker, worker, NULL)) {
        host->log("[bettercamera] worker start failed: feature off, boot unaffected\n");
        return true;
    }
    host->log("[bettercamera] v%s loaded\n", BC_VERSION);
    return true;
}

BC_MODULE_EXPORT const uint32_t mhgu_engine_module_api = BC_MODULE_API;
