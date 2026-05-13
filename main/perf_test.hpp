#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

void perf_test_start(void);
bool perf_test_is_plc_running(void);
void perf_test_set_plc_running(bool running);
const char *perf_test_get_plc_mode_string(void);

typedef enum PlcOverrunPolicy {
    PLC_OVERRUN_COALESCE_CONTINUE = 0,
    PLC_OVERRUN_WARN_CONTINUE = 1,
    PLC_OVERRUN_FAULT_AFTER_LIMIT = 2,
    PLC_OVERRUN_STOP_IMMEDIATELY = 3,
    PLC_OVERRUN_EDGE_TASK = 4
} PlcOverrunPolicy;

typedef enum PlcScriptTimeMode {
    PLC_TIME_NOMINAL = 0,
    PLC_TIME_ACTUAL = 1,
    PLC_TIME_ACTUAL_CLAMPED = 2
} PlcScriptTimeMode;

typedef struct PlcRuntimePolicy {
    PlcOverrunPolicy overrun_policy;
    PlcScriptTimeMode time_mode;
    uint32_t script_budget_us;
    uint32_t max_delta_us;
    uint32_t fault_after_consecutive_overruns;
    uint32_t fault_after_coalesced_scans;
} PlcRuntimePolicy;

void perf_test_get_runtime_policy(PlcRuntimePolicy *out);
void perf_test_set_runtime_policy(const PlcRuntimePolicy *policy);

#ifdef __cplusplus
}
#endif
