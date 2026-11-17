#include <stdio.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "engine_module_api.h"
#include "guest.h"
#include "host.h"
#include "platform.h"
#include "safearea.h"

#ifndef BC_VERSION
#define BC_VERSION "0.1.0"
#endif

static const char *const k_checked_ports[] = {"0.16.", "0.17."};

static MhguHost g_mh;
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
    if (g_host.dispatch_lookup) return g_host.dispatch_lookup(BC_FN_BUILDER) != NULL;
    return bc_guest_readable(BC_FN_BUILDER, 16u) && bc_ld32(BC_FN_BUILDER) == BC_BUILDER_PROLOGUE;
}

static void worker(void *unused) {
    (void)unused;
    uint64_t t0 = bc_now_ms();
    unsigned polls = 0;
    BC_LOG("boot: resolving the port's exports");
    if (!bc_host_resolve()) {
        BC_LOG("boot: host resolve FAILED - a required port symbol is missing");
        g_install_state = BC_INSTALL_FAILED;
        g_mh.log("[bettercamera] required port symbols missing: feature off, boot unaffected\n");
        return;
    }
    BC_LOG("boot: waiting for the game code (dispatch target 0x9c5e4c = the view-matrix builder)");
    while (bc_now_ms() - t0 < 60000u) {
        if (g_shutdown_done) return;
        if (game_ready()) {
            BC_LOG("boot: game code visible after %llu ms", (unsigned long long)(bc_now_ms() - t0));
            if (bc_host_resolve() && bc_safearea_install()) {
                g_install_state = BC_INSTALL_OK;
                g_mh.log("[bettercamera] safe-area camera armed (v%s)\n", BC_VERSION);
            } else {
                g_install_state = BC_INSTALL_FAILED;
                g_mh.log("[bettercamera] hook install failed: feature off, boot unaffected\n");
            }
            return;
        }
        if (++polls % 20u == 0u)
            BC_LOG("boot: %llu s - the look-at is not in the dispatch table yet",
                (unsigned long long)((bc_now_ms() - t0) / 1000u));
        bc_sleep_ms(100);
    }
    BC_LOG("boot: TIMEOUT after 60 s - the port never exposed the look-at; feature off");
    g_install_state = BC_INSTALL_FAILED;
    g_mh.log("[bettercamera] game code never appeared: feature off, boot unaffected\n");
}

static void module_shutdown(void) {
    if (g_shutdown_done) return;
    g_shutdown_done = true;
    BC_LOG("module: shutdown (install state %d)", g_install_state);
    bc_safearea_remove();
    if (g_worker.started) bc_thread_join(&g_worker, 5000u);
}

MHGU_ENGINE_MODULE_EXPORT bool mhgu_engine_module_entry(const MhguHost *host, MhguEngineModule *out) {
    if (!host || !out) return false;
    if (host->api < 1 || host->api > MHGU_ENGINE_MODULE_API) {
        host->log("[bettercamera] built for engine module API %u, this port has %u: not loaded\n",
            (unsigned)MHGU_ENGINE_MODULE_API, host->api ? (unsigned)host->api : 0u);
        return false;
    }
    if (!port_version_checked(host->port_version)) {
        host->log("[bettercamera] port %s unverified (checked: 0.16., 0.17.): not loaded\n",
            host->port_version ? host->port_version : "?");
        return false;
    }
    g_mh = *host;
    g_shutdown_done = false;
#if BC_DEBUG > 0
    bc_debug_set_sink(g_mh.log);
    bc_debug_open_file(host->module_dir);
#endif
    BC_LOG("module: BetterCamera v%s entry (module api %u, port %s, dir %s)", BC_VERSION, (unsigned)host->api,
        host->port_version ? host->port_version : "?", host->module_dir ? host->module_dir : "?");
    bool cfg = bc_config_load(host->module_dir);
    BC_LOG("module: ini %s", cfg ? "loaded" : "not found (defaults in force)");
    (void)cfg;
    out->name = "BetterCamera";
    out->shutdown = module_shutdown;
    g_install_state = BC_INSTALL_IDLE;
    if (!bc_thread_start(&g_worker, worker, NULL)) {
        host->log("[bettercamera] worker start failed: feature off, boot unaffected\n");
        BC_LOG("module: FAIL - worker thread could not start");
        return true;
    }
    host->log("[bettercamera] v%s loaded\n", BC_VERSION);
    return true;
}

MHGU_ENGINE_MODULE_EXPORT const uint32_t mhgu_engine_module_api = MHGU_ENGINE_MODULE_API;
