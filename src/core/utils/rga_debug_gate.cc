#include "core/utils/rga_debug_gate.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>

namespace {

pthread_mutex_t g_rga_global_mtx = PTHREAD_MUTEX_INITIALIZER;
std::once_flag g_init_once;
int g_preprocess_disabled = 0;
int g_display_disabled = 0;
int g_global_lock_enabled = 0;
int g_guard_check_enabled = 0;

int env_true(const char* value)
{
    if (value == nullptr)
        return 0;
    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "TRUE") == 0 ||
           strcmp(value, "on") == 0 ||
           strcmp(value, "ON") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "YES") == 0;
}

void init_once()
{
    g_preprocess_disabled = env_true(getenv("DISABLE_PREPROCESS_RGA"));
    g_display_disabled = env_true(getenv("DISABLE_DISPLAY_RGA"));
    g_global_lock_enabled = env_true(getenv("RGA_GLOBAL_LOCK"));
    g_guard_check_enabled = env_true(getenv("RGA_GUARD_CHECK"));
}

void ensure_init()
{
    std::call_once(g_init_once, init_once);
}

}  // namespace

extern "C" int rga_debug_preprocess_disabled(void)
{
    ensure_init();
    return g_preprocess_disabled;
}

extern "C" int rga_debug_display_disabled(void)
{
    ensure_init();
    return g_display_disabled;
}

extern "C" int rga_debug_global_lock_enabled(void)
{
    ensure_init();
    return g_global_lock_enabled;
}

extern "C" int rga_debug_guard_check_enabled(void)
{
    ensure_init();
    return g_guard_check_enabled;
}

extern "C" void rga_debug_lock(void)
{
    ensure_init();
    if (g_global_lock_enabled)
    {
        pthread_mutex_lock(&g_rga_global_mtx);
    }
}

extern "C" void rga_debug_unlock(void)
{
    ensure_init();
    if (g_global_lock_enabled)
    {
        pthread_mutex_unlock(&g_rga_global_mtx);
    }
}
