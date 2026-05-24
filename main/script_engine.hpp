#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ScriptUpdateMode {
    SCRIPT_UPDATE_STOP_BEFORE_COMPILE = 0,
    SCRIPT_UPDATE_PAUSE_DURING_COMPILE = 1,
    SCRIPT_UPDATE_ONLINE_HOT_SWAP = 2
} ScriptUpdateMode;

typedef enum ScriptCompileState {
    SCRIPT_STATE_IDLE = 0,
    SCRIPT_STATE_QUEUED = 1,
    SCRIPT_STATE_COMPILING = 2,
    SCRIPT_STATE_OK = 3,
    SCRIPT_STATE_FAILED = 4,
    SCRIPT_STATE_QUEUE_FULL = 5
} ScriptCompileState;

// Initialize the AngelScript runtime support and start the async compiler task.
bool script_engine_start(void);

// Synchronous compile kept for local tests/debug. Do not call this from httpd callbacks.
bool script_engine_compile_text(const char* script_text, size_t script_len,
                                char* err_buf, size_t err_buf_len);

// Async compile request used by HTTP upload. This function copies the script text
// into heap memory and returns quickly. The dedicated compiler task does the build.
bool script_engine_submit_compile_text(const char* script_text, size_t script_len,
                                       const char* script_filename,
                                       char* response_buf, size_t response_buf_len);

// Fast preflight check used by HTTP upload handler before reading the request body.
// Returns false when a compile is already queued/running or a compiled program is
// waiting to be activated by the scan task. The final submit call still rechecks.
bool script_engine_can_accept_upload(char* response_buf, size_t response_buf_len);

// Called by the deterministic scan task. Executes the currently active compiled scan().
void script_engine_set_scan_timing(uint32_t delta_us, uint32_t actual_period_us, uint32_t budget_us);
bool script_engine_run_scan(void);
uint32_t script_engine_get_vm_last_us(void);
uint32_t script_engine_get_vm_max_us(void);
uint32_t script_engine_get_vm_ema_us(void);
uint32_t script_engine_get_vm_window_avg_us(void);
uint32_t script_engine_get_vm_window_max_us(void);

uint32_t script_engine_get_generation(void);
const char* script_engine_get_last_error(void);
ScriptCompileState script_engine_get_state(void);


// Temporarily pause user script execution while another firmware subsystem
// performs an in-place runtime update that touches registered tag storage.
// This does not destroy or replace the active script; it simply causes
// script_engine_run_scan() to skip user scan execution until resumed.
void script_engine_pause_for_runtime_update(const char* reason);
void script_engine_resume_after_runtime_update(void);
bool script_engine_is_runtime_update_paused(void);

// Runtime script metadata. Names are filenames from /scripts when available.
void script_engine_get_active_script_name(char* out, size_t out_len);
void script_engine_get_pending_script_name(char* out, size_t out_len);

// JSON status helper for /api/script_status.
void script_engine_get_status_json(char* out, size_t out_len);

// PiLab/script runtime log filter. This is intentionally independent from the
// ESP-IDF console log level controlled by esp_log_level_set().
void script_engine_set_runtime_log_level(const char* level);
const char* script_engine_get_runtime_log_level(void);
void script_engine_set_update_mode(const char* mode);
const char* script_engine_get_update_mode(void);

#ifdef __cplusplus
}
#endif
