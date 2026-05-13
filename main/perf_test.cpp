#include "perf_test.hpp"

#include <cstdint>
#include <climits>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gptimer.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "script_engine.hpp"
#include "plc_io.hpp"
#include "plc_tags.hpp"

static const char* TAG = "PERF_TEST";

// Hardware timer PLC scan settings
static constexpr uint32_t IO_TICK_PERIOD_US = 1000;       // 1 ms hardware tick
static constexpr uint32_t SCRIPT_SCAN_DIVIDER = 5;           // run AngelScript every 5th tick
static constexpr uint32_t SCRIPT_SCAN_PERIOD_US = IO_TICK_PERIOD_US * SCRIPT_SCAN_DIVIDER;
static constexpr uint32_t REPORT_PERIOD_MS = 5000;       // 5 seconds
static constexpr uint32_t DEFAULT_MAX_DELTA_US = 50000;   // Clamp actual script dt to 50 ms by default

// Artificial CPU load settings
static constexpr bool ENABLE_BACKGROUND_LOAD_TASK = true;
static constexpr uint32_t BACKGROUND_LOAD_WORK_US = 250;
static constexpr uint32_t BACKGROUND_LOAD_PERIOD_MS = 10;

struct PerfStats
{
    uint64_t samples = 0;

    int64_t min_period_us = INT64_MAX;
    int64_t max_period_us = 0;
    int64_t sum_period_us = 0;

    int64_t max_late_us = 0;
    int64_t max_early_us = 0;

    uint64_t over_10us = 0;
    uint64_t over_50us = 0;
    uint64_t over_100us = 0;

    uint64_t missed_notifications = 0;

    int64_t min_work_us = INT64_MAX;
    int64_t max_work_us = 0;
    int64_t sum_work_us = 0;

    uint64_t work_over_100us = 0;
    uint64_t work_over_250us = 0;
    uint64_t work_over_500us = 0;
    uint64_t work_over_900us = 0;

    void reset()
    {
        samples = 0;

        min_period_us = INT64_MAX;
        max_period_us = 0;
        sum_period_us = 0;

        max_late_us = 0;
        max_early_us = 0;

        over_10us = 0;
        over_50us = 0;
        over_100us = 0;

        missed_notifications = 0;

        min_work_us = INT64_MAX;
        max_work_us = 0;
        sum_work_us = 0;

        work_over_100us = 0;
        work_over_250us = 0;
        work_over_500us = 0;
        work_over_900us = 0;
    }

    void add_period_sample(int64_t period_us)
    {
        samples++;

        if (period_us < min_period_us) min_period_us = period_us;
        if (period_us > max_period_us) max_period_us = period_us;

        sum_period_us += period_us;

        const int64_t error_us = period_us - IO_TICK_PERIOD_US;

        if (error_us > max_late_us)
            max_late_us = error_us;

        if (-error_us > max_early_us)
            max_early_us = -error_us;

        const int64_t abs_error = error_us >= 0 ? error_us : -error_us;

        if (abs_error > 10)  over_10us++;
        if (abs_error > 50)  over_50us++;
        if (abs_error > 100) over_100us++;
    }

    void add_work_sample(int64_t work_us)
    {
        if (work_us < min_work_us) min_work_us = work_us;
        if (work_us > max_work_us) max_work_us = work_us;

        sum_work_us += work_us;

        if (work_us > 100) work_over_100us++;
        if (work_us > 250) work_over_250us++;
        if (work_us > 500) work_over_500us++;
        if (work_us > 900) work_over_900us++;
    }

    double average_period_us() const
    {
        if (samples == 0)
            return 0.0;

        return static_cast<double>(sum_period_us) / static_cast<double>(samples);
    }

    double average_work_us() const
    {
        if (samples == 0)
            return 0.0;

        return static_cast<double>(sum_work_us) / static_cast<double>(samples);
    }
};

struct ScriptPerfStats
{
    uint64_t samples = 0;

    int64_t min_total_us = INT64_MAX;
    int64_t max_total_us = 0;
    int64_t sum_total_us = 0;

    uint64_t over_1000us = 0;
    uint64_t over_2500us = 0;
    uint64_t over_5000us = 0;
    uint64_t over_10000us = 0;

    uint64_t missed_notifications = 0;
    uint64_t ran_count = 0;

    void reset()
    {
        samples = 0;

        min_total_us = INT64_MAX;
        max_total_us = 0;
        sum_total_us = 0;

        over_1000us = 0;
        over_2500us = 0;
        over_5000us = 0;
        over_10000us = 0;

        missed_notifications = 0;
        ran_count = 0;
    }

    void add_sample(int64_t total_us, bool ran)
    {
        samples++;

        if (ran)
            ran_count++;

        if (total_us < min_total_us) min_total_us = total_us;
        if (total_us > max_total_us) max_total_us = total_us;

        sum_total_us += total_us;

        if (total_us > 1000)  over_1000us++;
        if (total_us > 2500)  over_2500us++;
        if (total_us > 5000)  over_5000us++;
        if (total_us > 10000) over_10000us++;
    }

    double average_total_us() const
    {
        if (samples == 0)
            return 0.0;

        return static_cast<double>(sum_total_us) / static_cast<double>(samples);
    }
};

static PerfStats g_active_stats;
static ScriptPerfStats g_script_stats;
static portMUX_TYPE g_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_mode_lock = portMUX_INITIALIZER_UNLOCKED;
static bool g_plc_runtime_enabled = true;
static PlcRuntimePolicy g_runtime_policy = {
    PLC_OVERRUN_FAULT_AFTER_LIMIT,
    PLC_TIME_ACTUAL_CLAMPED,
    SCRIPT_SCAN_PERIOD_US,
    DEFAULT_MAX_DELTA_US,
    20,
    100
};
static bool g_scan_fault_active = false;
static uint32_t g_consecutive_overruns = 0;
static uint64_t g_total_coalesced_scans = 0;
static uint64_t g_total_overrun_scans = 0;
static uint32_t g_last_script_actual_period_us = SCRIPT_SCAN_PERIOD_US;
static uint32_t g_last_script_delta_us = SCRIPT_SCAN_PERIOD_US;
static uint32_t g_last_script_execution_us = 0;
static float g_last_script_load_percent = 0.0f;

void perf_test_get_runtime_policy(PlcRuntimePolicy *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&g_mode_lock);
    *out = g_runtime_policy;
    taskEXIT_CRITICAL(&g_mode_lock);
}

void perf_test_set_runtime_policy(const PlcRuntimePolicy *policy)
{
    if (!policy) return;

    PlcRuntimePolicy next = *policy;
    if (next.script_budget_us < 500) next.script_budget_us = SCRIPT_SCAN_PERIOD_US;
    if (next.max_delta_us < next.script_budget_us) next.max_delta_us = next.script_budget_us;
    if (next.fault_after_consecutive_overruns == 0) next.fault_after_consecutive_overruns = 1;

    taskENTER_CRITICAL(&g_mode_lock);
    g_runtime_policy = next;
    g_scan_fault_active = false;
    g_consecutive_overruns = 0;
    taskEXIT_CRITICAL(&g_mode_lock);

    plc_tags_set_internal_bool("PLC_ScanFaultActive", false);

    ESP_LOGI(TAG,
             "PLC runtime policy applied: overrunPolicy=%d timeMode=%d budget=%lu maxDelta=%lu faultAfterOverruns=%lu faultAfterCoalesced=%lu",
             (int)next.overrun_policy,
             (int)next.time_mode,
             (unsigned long)next.script_budget_us,
             (unsigned long)next.max_delta_us,
             (unsigned long)next.fault_after_consecutive_overruns,
             (unsigned long)next.fault_after_coalesced_scans);
}

bool perf_test_is_plc_running(void)
{
    taskENTER_CRITICAL(&g_mode_lock);
    const bool running = g_plc_runtime_enabled;
    taskEXIT_CRITICAL(&g_mode_lock);
    return running;
}

void perf_test_set_plc_running(bool running)
{
    taskENTER_CRITICAL(&g_mode_lock);
    const bool changed = (g_plc_runtime_enabled != running);
    g_plc_runtime_enabled = running;
    taskEXIT_CRITICAL(&g_mode_lock);

    if (changed) {
        ESP_LOGW(TAG, "PLC runtime mode changed to %s", running ? "RUN" : "STOP");
    }
}

const char *perf_test_get_plc_mode_string(void)
{
    return perf_test_is_plc_running() ? "RUN" : "STOP";
}

static TaskHandle_t g_plc_task_handle = nullptr;
static TaskHandle_t g_script_task_handle = nullptr;
static gptimer_handle_t g_plc_timer = nullptr;

static bool IRAM_ATTR plc_timer_alarm_callback(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t* edata,
    void* user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;

    BaseType_t higher_priority_task_woken = pdFALSE;

    if (g_plc_task_handle != nullptr)
    {
        vTaskNotifyGiveFromISR(g_plc_task_handle, &higher_priority_task_woken);
    }

    return higher_priority_task_woken == pdTRUE;
}

static void plc_scan_task(void* arg)
{
    (void)arg;

    ESP_LOGI(TAG, "PLC scan task started");
    ESP_LOGI(TAG, "esp_timer_get_time at PLC task start: %lld us",
             static_cast<long long>(esp_timer_get_time()));

    int64_t last_time_us = 0;
    bool first_sample = true;
    uint32_t script_scan_divider = 0;

    while (true)
    {
        const uint32_t notify_count = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        const int64_t now_us = esp_timer_get_time();

        const int64_t work_start_us = esp_timer_get_time();


        // 1 ms deterministic firmware side only:
        //   - read physical inputs
        //   - debounce into the process image
        //   - apply the most recently published output image
        //
        // IMPORTANT: This task must never call AngelScript, create/destroy
        // AngelScript objects, or wait on any AngelScript mutex. Script compile,
        // activation, execution and cleanup all happen in lower-priority tasks.
        //
        // RUN/STOP note:
        // STOP is a maintenance/programming gate. It prevents PLC I/O ticking
        // and AngelScript scans while flash writes are allowed from the web UI.
        // It is not a safety-rated emergency stop. Add explicit safe-output
        // handling later when the output policy is finalized.
        if (perf_test_is_plc_running())
        {
            plc_io_tick_1ms();
            plc_io_apply_outputs();

            // Wake the soft AngelScript task every 5 hardware ticks. Do not use
            // vTaskDelayUntil() for this because the FreeRTOS tick rate may be
            // 100 Hz, making pdMS_TO_TICKS(5) equal to zero and causing an assert.
            script_scan_divider++;
            if (script_scan_divider >= SCRIPT_SCAN_DIVIDER)
            {
                script_scan_divider = 0;
                if (g_script_task_handle != nullptr)
                {
                    xTaskNotifyGive(g_script_task_handle);
                }
            }
        }

        const int64_t work_end_us = esp_timer_get_time();

        const int64_t work_us = work_end_us - work_start_us;

        taskENTER_CRITICAL(&g_stats_lock);

        if (notify_count > 1)
        {
            g_active_stats.missed_notifications += notify_count - 1;
        }

        if (!first_sample)
        {
            const int64_t period_us = now_us - last_time_us;
            g_active_stats.add_period_sample(period_us);
            g_active_stats.add_work_sample(work_us);
        }
        else
        {
            first_sample = false;
        }

        taskEXIT_CRITICAL(&g_stats_lock);

        last_time_us = now_us;
    }
}


static void script_scan_task(void* arg)
{
    (void)arg;

    ESP_LOGI(TAG, "AngelScript scan task started: period=%u us, triggered by 1 ms PLC task", SCRIPT_SCAN_PERIOD_US);

    int64_t last_script_start_us = 0;

    while (true)
    {
        const uint32_t notify_count = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        const int64_t start_us = esp_timer_get_time();
        uint32_t actual_period_us = SCRIPT_SCAN_PERIOD_US;
        if (last_script_start_us > 0 && start_us > last_script_start_us) {
            actual_period_us = (uint32_t)(start_us - last_script_start_us);
        }
        last_script_start_us = start_us;

        PlcRuntimePolicy policy;
        bool fault_active;
        taskENTER_CRITICAL(&g_mode_lock);
        policy = g_runtime_policy;
        fault_active = g_scan_fault_active;
        taskEXIT_CRITICAL(&g_mode_lock);

        uint32_t delta_us = SCRIPT_SCAN_PERIOD_US;
        if (policy.time_mode == PLC_TIME_ACTUAL) {
            delta_us = actual_period_us;
        } else if (policy.time_mode == PLC_TIME_ACTUAL_CLAMPED) {
            delta_us = actual_period_us;
            if (delta_us > policy.max_delta_us) delta_us = policy.max_delta_us;
        }

        script_engine_set_scan_timing(delta_us, actual_period_us, policy.script_budget_us);

        // Measure the entire soft PLC script cycle, not just ctx->Execute().
        // This includes input sync, pending script activation, AngelScript
        // execution, output copyback, and retired program cleanup.
        const bool can_run = perf_test_is_plc_running() && !fault_active;
        const bool ran = can_run ? script_engine_run_scan() : false;
        const int64_t total_us_i64 = esp_timer_get_time() - start_us;
        const uint32_t total_us = total_us_i64 > 0 ? (uint32_t)total_us_i64 : 0;

        uint32_t drained = 0;
        if (total_us > policy.script_budget_us || notify_count > 1) {
            drained = ulTaskNotifyTake(pdTRUE, 0);
        }
        const uint32_t coalesced = (notify_count > 1 ? (notify_count - 1) : 0) + drained;
        const bool overrun = total_us > policy.script_budget_us;
        const float load = policy.script_budget_us ? ((float)total_us * 100.0f / (float)policy.script_budget_us) : 0.0f;

        bool stop_now = false;
        bool new_fault = false;

        taskENTER_CRITICAL(&g_mode_lock);
        g_last_script_actual_period_us = actual_period_us;
        g_last_script_delta_us = delta_us;
        g_last_script_execution_us = total_us;
        g_last_script_load_percent = load;
        if (coalesced) g_total_coalesced_scans += coalesced;
        if (overrun) {
            g_total_overrun_scans++;
            g_consecutive_overruns++;
        } else {
            g_consecutive_overruns = 0;
        }

        switch (policy.overrun_policy) {
            case PLC_OVERRUN_STOP_IMMEDIATELY:
                if (overrun || coalesced) { g_scan_fault_active = true; stop_now = true; new_fault = true; }
                break;
            case PLC_OVERRUN_FAULT_AFTER_LIMIT:
                if (g_consecutive_overruns >= policy.fault_after_consecutive_overruns ||
                    (policy.fault_after_coalesced_scans > 0 && coalesced >= policy.fault_after_coalesced_scans)) {
                    g_scan_fault_active = true;
                    stop_now = true;
                    new_fault = true;
                }
                break;
            case PLC_OVERRUN_WARN_CONTINUE:
            case PLC_OVERRUN_COALESCE_CONTINUE:
            case PLC_OVERRUN_EDGE_TASK:
            default:
                break;
        }
        fault_active = g_scan_fault_active;
        taskEXIT_CRITICAL(&g_mode_lock);

        if (stop_now) {
            perf_test_set_plc_running(false);
            ESP_LOGE(TAG, "PLC stopped by script overrun policy: total_us=%lu budget_us=%lu coalesced=%lu",
                     (unsigned long)total_us,
                     (unsigned long)policy.script_budget_us,
                     (unsigned long)coalesced);
        } else if (new_fault) {
            ESP_LOGE(TAG, "PLC scan fault set by overrun policy");
        }

        plc_tags_set_internal_int("PLC_ScanCoalescedCount", (int32_t)(g_total_coalesced_scans > INT32_MAX ? INT32_MAX : g_total_coalesced_scans));
        plc_tags_set_internal_int("PLC_ScanOverrunCount", (int32_t)(g_total_overrun_scans > INT32_MAX ? INT32_MAX : g_total_overrun_scans));
        plc_tags_set_internal_bool("PLC_ScanOverrunActive", overrun || coalesced > 0);
        plc_tags_set_internal_bool("PLC_ScanFaultActive", fault_active);
        plc_tags_set_internal_int("PLC_ScanActualPeriodUs", (int32_t)actual_period_us);
        plc_tags_set_internal_int("PLC_ScanExecutionTimeUs", (int32_t)total_us);
        plc_tags_set_internal_float("PLC_ScanLoadPercent", load);
        plc_tags_set_internal_int("PLC_DeltaTimeUs", (int32_t)delta_us);
        plc_tags_set_internal_float("PLC_DeltaTimeMs", (float)delta_us / 1000.0f);

        taskENTER_CRITICAL(&g_stats_lock);

        if (coalesced > 0)
        {
            g_script_stats.missed_notifications += coalesced;
        }

        g_script_stats.add_sample(total_us, ran);

        taskEXIT_CRITICAL(&g_stats_lock);

        // Always yield after a script scan. When overloaded, this prevents the
        // 5 ms script task from monopolizing CPU0 and starving IDLE0 / watchdog.
        taskYIELD();
    }
}

static void report_task(void* arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Report task started");

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));

        PerfStats snapshot;
        ScriptPerfStats script_snapshot;

        taskENTER_CRITICAL(&g_stats_lock);
        snapshot = g_active_stats;
        script_snapshot = g_script_stats;
        g_active_stats.reset();
        g_script_stats.reset();
        taskEXIT_CRITICAL(&g_stats_lock);

        const size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        const size_t min_free_8bit = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        ESP_LOGI(TAG, "---------------- Performance Report ----------------");
        ESP_LOGI(TAG, "PLC scan source: GPTimer hardware interrupt");
        ESP_LOGI(TAG, "I/O tick target period: %u us, AngelScript task period: %u us", IO_TICK_PERIOD_US, SCRIPT_SCAN_PERIOD_US);
        ESP_LOGI(TAG, "Samples: %llu", static_cast<unsigned long long>(snapshot.samples));

        ESP_LOGI(TAG, "Period min: %lld us", static_cast<long long>(snapshot.min_period_us));
        ESP_LOGI(TAG, "Period max: %lld us", static_cast<long long>(snapshot.max_period_us));
        ESP_LOGI(TAG, "Period avg: %.2f us", snapshot.average_period_us());

        ESP_LOGI(TAG, "Worst late jitter:  +%lld us", static_cast<long long>(snapshot.max_late_us));
        ESP_LOGI(TAG, "Worst early jitter: -%lld us", static_cast<long long>(snapshot.max_early_us));

        ESP_LOGI(TAG, "Samples >10 us error:  %llu", static_cast<unsigned long long>(snapshot.over_10us));
        ESP_LOGI(TAG, "Samples >50 us error:  %llu", static_cast<unsigned long long>(snapshot.over_50us));
        ESP_LOGI(TAG, "Samples >100 us error: %llu", static_cast<unsigned long long>(snapshot.over_100us));

        ESP_LOGI(TAG, "Missed/coalesced notifications: %llu",
                 static_cast<unsigned long long>(snapshot.missed_notifications));

        ESP_LOGI(TAG, "---------------- PLC Work Timing -------------------");
        ESP_LOGI(TAG, "Work min: %lld us", static_cast<long long>(snapshot.min_work_us));
        ESP_LOGI(TAG, "Work max: %lld us", static_cast<long long>(snapshot.max_work_us));
        ESP_LOGI(TAG, "Work avg: %.2f us", snapshot.average_work_us());

        ESP_LOGI(TAG, "Work >100 us: %llu", static_cast<unsigned long long>(snapshot.work_over_100us));
        ESP_LOGI(TAG, "Work >250 us: %llu", static_cast<unsigned long long>(snapshot.work_over_250us));
        ESP_LOGI(TAG, "Work >500 us: %llu", static_cast<unsigned long long>(snapshot.work_over_500us));
        ESP_LOGI(TAG, "Work >900 us: %llu", static_cast<unsigned long long>(snapshot.work_over_900us));

        ESP_LOGI(TAG, "------------- AngelScript Total Timing -------------");
        ESP_LOGI(TAG, "Script samples: %llu", static_cast<unsigned long long>(script_snapshot.samples));
        ESP_LOGI(TAG, "Script ran count: %llu", static_cast<unsigned long long>(script_snapshot.ran_count));

        if (script_snapshot.samples > 0)
        {
            ESP_LOGI(TAG, "Script total min: %lld us", static_cast<long long>(script_snapshot.min_total_us));
            ESP_LOGI(TAG, "Script total max: %lld us", static_cast<long long>(script_snapshot.max_total_us));
            ESP_LOGI(TAG, "Script total avg: %.2f us", script_snapshot.average_total_us());
        }
        else
        {
            ESP_LOGI(TAG, "Script total min: n/a");
            ESP_LOGI(TAG, "Script total max: n/a");
            ESP_LOGI(TAG, "Script total avg: n/a");
        }

        ESP_LOGI(TAG, "Script >1 ms:   %llu", static_cast<unsigned long long>(script_snapshot.over_1000us));
        ESP_LOGI(TAG, "Script >2.5 ms: %llu", static_cast<unsigned long long>(script_snapshot.over_2500us));
        ESP_LOGI(TAG, "Script >5 ms:   %llu", static_cast<unsigned long long>(script_snapshot.over_5000us));
        ESP_LOGI(TAG, "Script >10 ms:  %llu", static_cast<unsigned long long>(script_snapshot.over_10000us));
        ESP_LOGI(TAG, "Script missed/coalesced notifications: %llu",
                 static_cast<unsigned long long>(script_snapshot.missed_notifications));
        ESP_LOGI(TAG, "Script runtime: actual_period=%lu us delta=%lu us exec=%lu us load=%.1f%% fault=%d",
                 (unsigned long)g_last_script_actual_period_us,
                 (unsigned long)g_last_script_delta_us,
                 (unsigned long)g_last_script_execution_us,
                 (double)g_last_script_load_percent,
                 g_scan_fault_active ? 1 : 0);

        ESP_LOGI(TAG, "---------------- Memory / Stack --------------------");
        ESP_LOGI(TAG, "Heap free 8-bit: %u bytes", static_cast<unsigned>(free_8bit));
        ESP_LOGI(TAG, "Heap minimum free 8-bit: %u bytes", static_cast<unsigned>(min_free_8bit));
        ESP_LOGI(TAG, "Heap free internal: %u bytes", static_cast<unsigned>(free_internal));

        if (g_plc_task_handle != nullptr)
        {
            ESP_LOGI(TAG, "PLC I/O task stack high water mark: %u words",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_plc_task_handle)));
        }
        if (g_script_task_handle != nullptr)
        {
            ESP_LOGI(TAG, "AngelScript task stack high water mark: %u words",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_script_task_handle)));
        }

        ESP_LOGI(TAG, "----------------------------------------------------");
    }
}

static void background_load_task(void* arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Background load task started");

    while (true)
    {
        const int64_t start_us = esp_timer_get_time();

        uint32_t dummy = 0;

        while ((esp_timer_get_time() - start_us) < BACKGROUND_LOAD_WORK_US)
        {
            dummy = dummy + 1;
        }

        asm volatile("" : : "r"(dummy) : "memory");

        vTaskDelay(pdMS_TO_TICKS(BACKGROUND_LOAD_PERIOD_MS));
    }
}

static void start_plc_hardware_timer()
{
    ESP_LOGI(TAG, "Starting GPTimer hardware interrupt at %u us", IO_TICK_PERIOD_US);

    gptimer_config_t timer_config = {};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1000000; // 1 MHz = 1 us per count

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &g_plc_timer));

    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = plc_timer_alarm_callback;

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(g_plc_timer, &callbacks, nullptr));

    gptimer_alarm_config_t alarm_config = {};
    alarm_config.alarm_count = IO_TICK_PERIOD_US;
    alarm_config.reload_count = 0;
    alarm_config.flags.auto_reload_on_alarm = true;

    ESP_ERROR_CHECK(gptimer_set_alarm_action(g_plc_timer, &alarm_config));

    ESP_ERROR_CHECK(gptimer_enable(g_plc_timer));
    ESP_ERROR_CHECK(gptimer_start(g_plc_timer));
}

void perf_test_start()
{
    ESP_LOGI(TAG, "ESP32-P4 hardware-timer performance test starting");
    ESP_LOGI(TAG, "esp_timer_get_time at app_main: %lld us",
             static_cast<long long>(esp_timer_get_time()));

    ESP_LOGI(TAG, "Heap free 8-bit: %u bytes",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));

    script_engine_start();
    plc_io_init();

    xTaskCreatePinnedToCore(
        plc_scan_task,
        "plc_io_1ms",
        4096,
        nullptr,
        24,
        &g_plc_task_handle,
        0
    );

    xTaskCreatePinnedToCore(
        script_scan_task,
        "as_scan_5ms",
        16384,
        nullptr,
        12,
        &g_script_task_handle,
        0
    );

    xTaskCreatePinnedToCore(
        report_task,
        "report",
        4096,
        nullptr,
        4,
        nullptr,
        1
    );

    if constexpr (ENABLE_BACKGROUND_LOAD_TASK)
    {
        xTaskCreatePinnedToCore(
            background_load_task,
            "background_load",
            4096,
            nullptr,
            3,
            nullptr,
            1
        );
    }

    start_plc_hardware_timer();
}