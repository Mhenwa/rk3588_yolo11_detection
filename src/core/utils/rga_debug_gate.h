#ifndef CORE_UTILS_RGA_DEBUG_GATE_H_
#define CORE_UTILS_RGA_DEBUG_GATE_H_

#ifdef __cplusplus
extern "C" {
#endif

// DISABLE_PREPROCESS_RGA=1: force CPU preprocess path.
int rga_debug_preprocess_disabled(void);

// DISABLE_DISPLAY_RGA=1: skip display-side RGA blit path.
int rga_debug_display_disabled(void);

// RGA_GLOBAL_LOCK=1: serialize RGA calls across modules.
int rga_debug_global_lock_enabled(void);
void rga_debug_lock(void);
void rga_debug_unlock(void);

// RGA_GUARD_CHECK=1: enable guard-canary checks for RGA-related buffers.
int rga_debug_guard_check_enabled(void);

#ifdef __cplusplus
}
#endif

#endif  // CORE_UTILS_RGA_DEBUG_GATE_H_
