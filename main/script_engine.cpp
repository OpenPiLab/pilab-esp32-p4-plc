#include "script_engine.hpp"
#include "plc_filesystem.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <strings.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <angelscript.h>
#include <scriptarray.h>
#include <scriptmath.h>
#include <scriptstdstring.h>

#include "plc_io.hpp"
#include "plc_tags.hpp"
#include "perf_test.hpp"

static const char* TAG = "AS_ENGINE";

static const char* AS_SCRIPT_TAG = "AS_SCRIPT";

enum PiLabScriptLogLevel : int {
    PILAB_SCRIPT_LOG_NONE = 0,
    PILAB_SCRIPT_LOG_ERROR = 1,
    PILAB_SCRIPT_LOG_WARN = 2,
    PILAB_SCRIPT_LOG_INFO = 3,
    PILAB_SCRIPT_LOG_DEBUG = 4,
    PILAB_SCRIPT_LOG_VERBOSE = 5,
};

static std::atomic<int> g_pilab_script_log_level{PILAB_SCRIPT_LOG_INFO};
static std::atomic<int> g_script_update_mode{SCRIPT_UPDATE_PAUSE_DURING_COMPILE};

static uint32_t g_script_delta_time_us = 5000;
static float g_script_delta_time_ms = 5.0f;
static float g_script_delta_time_s = 0.005f;
static uint32_t g_script_actual_period_us = 5000;
static uint32_t g_script_budget_us = 5000;

extern "C" void script_engine_set_scan_timing(uint32_t delta_us, uint32_t actual_period_us, uint32_t budget_us)
{
    if (delta_us == 0) delta_us = 5000;
    g_script_delta_time_us = delta_us;
    g_script_delta_time_ms = (float)delta_us / 1000.0f;
    g_script_delta_time_s = (float)delta_us / 1000000.0f;
    g_script_actual_period_us = actual_period_us;
    g_script_budget_us = budget_us ? budget_us : 5000;
}


static int parse_pilab_script_log_level(const char* level)
{
    if (!level || !level[0]) return PILAB_SCRIPT_LOG_INFO;
    if (strcasecmp(level, "off") == 0 || strcasecmp(level, "none") == 0) return PILAB_SCRIPT_LOG_NONE;
    if (strcasecmp(level, "error") == 0 || strcasecmp(level, "err") == 0) return PILAB_SCRIPT_LOG_ERROR;
    if (strcasecmp(level, "warn") == 0 || strcasecmp(level, "warning") == 0) return PILAB_SCRIPT_LOG_WARN;
    if (strcasecmp(level, "info") == 0) return PILAB_SCRIPT_LOG_INFO;
    if (strcasecmp(level, "debug") == 0) return PILAB_SCRIPT_LOG_DEBUG;
    if (strcasecmp(level, "verbose") == 0 || strcasecmp(level, "trace") == 0) return PILAB_SCRIPT_LOG_VERBOSE;
    return PILAB_SCRIPT_LOG_INFO;
}

static bool pilab_script_log_enabled(int required_level)
{
    return g_pilab_script_log_level.load() >= required_level;
}


extern "C" void script_engine_set_update_mode(const char* mode)
{
    int next = SCRIPT_UPDATE_PAUSE_DURING_COMPILE;
    if (mode) {
        if (strcasecmp(mode, "stopBeforeCompile") == 0 || strcasecmp(mode, "stop") == 0) next = SCRIPT_UPDATE_STOP_BEFORE_COMPILE;
        else if (strcasecmp(mode, "onlineHotSwap") == 0 || strcasecmp(mode, "online") == 0 || strcasecmp(mode, "hotSwap") == 0) next = SCRIPT_UPDATE_ONLINE_HOT_SWAP;
        else if (strcasecmp(mode, "pauseDuringCompile") == 0 || strcasecmp(mode, "pause") == 0) next = SCRIPT_UPDATE_PAUSE_DURING_COMPILE;
    }
    g_script_update_mode.store(next);
    ESP_LOGI(TAG, "Script update mode set to %s", script_engine_get_update_mode());
}

extern "C" const char* script_engine_get_update_mode(void)
{
    switch (g_script_update_mode.load()) {
        case SCRIPT_UPDATE_STOP_BEFORE_COMPILE: return "stopBeforeCompile";
        case SCRIPT_UPDATE_ONLINE_HOT_SWAP: return "onlineHotSwap";
        case SCRIPT_UPDATE_PAUSE_DURING_COMPILE:
        default: return "pauseDuringCompile";
    }
}

extern "C" void script_engine_set_runtime_log_level(const char* level)
{
    g_pilab_script_log_level.store(parse_pilab_script_log_level(level));
}

extern "C" const char* script_engine_get_runtime_log_level(void)
{
    switch (g_pilab_script_log_level.load()) {
        case PILAB_SCRIPT_LOG_NONE: return "none";
        case PILAB_SCRIPT_LOG_ERROR: return "error";
        case PILAB_SCRIPT_LOG_WARN: return "warn";
        case PILAB_SCRIPT_LOG_INFO: return "info";
        case PILAB_SCRIPT_LOG_DEBUG: return "debug";
        case PILAB_SCRIPT_LOG_VERBOSE: return "verbose";
        default: return "info";
    }
}

// -----------------------------------------------------------------------------
// AngelScript allocator
// -----------------------------------------------------------------------------
// AngelScript allocates heavily during compilation. Prefer PSRAM and fall back to
// internal RAM. Install before the first asCreateScriptEngine().
static bool g_as_memory_functions_installed = false;

static void* AS_PSRAM_Alloc(size_t size)
{
    if (size == 0) size = 1;
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

static void AS_PSRAM_Free(void* ptr)
{
    heap_caps_free(ptr);
}

static void install_angelscript_psram_allocator_once()
{
    if (!g_as_memory_functions_installed) {
        asSetGlobalMemoryFunctions(AS_PSRAM_Alloc, AS_PSRAM_Free);
        g_as_memory_functions_installed = true;
        ESP_LOGI(TAG, "AngelScript allocator installed: PSRAM preferred, internal fallback");
    }
}

static char* alloc_script_buffer_psram(size_t len)
{
    char* p = static_cast<char*>(heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) p = static_cast<char*>(heap_caps_malloc(len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (p) p[len] = '\0';
    return p;
}

// -----------------------------------------------------------------------------
// External script-visible process image
// -----------------------------------------------------------------------------
// Important: These variables are NOT owned by ScriptProgram. Every compiled
// engine registers its global properties to these same addresses, so hot-reload
// does not zero I/Q/AI/AO memory. Physical I/O remains in plc_io.cpp; this is
// the script-facing mirror copied at scan boundaries.
struct ScriptIoGlobals {
    bool  I[PLC_DI_COUNT] = {};
    bool  Q[PLC_DO_COUNT] = {};
    float AI[PLC_AI_COUNT] = {};
    float AO[PLC_AO_COUNT] = {};
};

static ScriptIoGlobals g_script_io;

static void sync_inputs_to_script_globals()
{
    for (uint32_t i = 0; i < PLC_DI_COUNT; ++i) {
        g_script_io.I[i] = plc_io_get_di(i) != 0;
    }
    for (uint32_t i = 0; i < PLC_AI_COUNT; ++i) {
        g_script_io.AI[i] = plc_io_get_ai(i);
    }
}

static void copy_outputs_from_script_globals()
{
    for (uint32_t i = 0; i < PLC_DO_COUNT; ++i) {
        plc_io_set_do(i, g_script_io.Q[i] ? 1u : 0u);
    }
    for (uint32_t i = 0; i < PLC_AO_COUNT; ++i) {
        plc_io_set_ao(i, g_script_io.AO[i]);
    }
}

// Function wrappers remain for backwards compatibility, but new user scripts can
// directly use globals I0..I15, Q0..Q7, AI0..AI3, AO0..AO3.
static void AS_LogInt_Generic(asIScriptGeneric* gen)
{
    uint32_t value = gen->GetArgDWord(0);
    if (pilab_script_log_enabled(PILAB_SCRIPT_LOG_INFO)) {
        ESP_LOGI(AS_SCRIPT_TAG, "logInt: %lu", (unsigned long)value);
    }
}

static void AS_GetDI_Generic(asIScriptGeneric* gen)
{
    uint32_t index = gen->GetArgDWord(0);
    uint32_t value = (index < PLC_DI_COUNT && g_script_io.I[index]) ? 1u : 0u;
    gen->SetReturnDWord(value);
}

static void AS_SetDO_Generic(asIScriptGeneric* gen)
{
    uint32_t index = gen->GetArgDWord(0);
    uint32_t value = gen->GetArgDWord(1);
    if (index < PLC_DO_COUNT) g_script_io.Q[index] = value != 0;
}

// -----------------------------------------------------------------------------
// Compile / status state
// -----------------------------------------------------------------------------
struct ScriptCompileJob {
    char* source = nullptr;
    size_t length = 0;
    char filename[96] = {};
};

static constexpr size_t SCRIPT_MAX_BYTES = 128 * 1024;
static constexpr uint32_t SCRIPT_COMPILE_TASK_STACK_BYTES = 65536;
static constexpr UBaseType_t SCRIPT_COMPILE_TASK_PRIORITY = 2;
static constexpr BaseType_t SCRIPT_COMPILE_TASK_CORE = 1;

static constexpr uint32_t SCRIPT_CLEANUP_TASK_STACK_BYTES = 8192;
static constexpr UBaseType_t SCRIPT_CLEANUP_TASK_PRIORITY = 1;
static constexpr BaseType_t SCRIPT_CLEANUP_TASK_CORE = 1;
static constexpr TickType_t SCRIPT_CLEANUP_DEFER_TICKS = pdMS_TO_TICKS(100);
static constexpr TickType_t SCRIPT_COMPILE_DEFER_TICKS = pdMS_TO_TICKS(25);

static SemaphoreHandle_t g_program_mutex = nullptr;
static SemaphoreHandle_t g_status_mutex = nullptr;
static QueueHandle_t g_compile_queue = nullptr;
static QueueHandle_t g_retired_queue = nullptr;
static TaskHandle_t g_compile_task_handle = nullptr;
static char g_pending_script_name[96] = {};
static char g_active_script_name[96] = {};
static TaskHandle_t g_cleanup_task_handle = nullptr;

static std::atomic<uint32_t> g_generation{0};
static std::atomic<int> g_state{SCRIPT_STATE_IDLE};
static std::atomic<uint64_t> g_last_compile_us{0};
static std::atomic<uint32_t> g_last_heap_before{0};
static std::atomic<uint32_t> g_last_heap_after{0};
static std::atomic<uint32_t> g_last_heap_min{0};
static std::atomic<uint32_t> g_last_internal_before{0};
static std::atomic<uint32_t> g_last_internal_after{0};
static std::atomic<uint32_t> g_last_internal_min{0};
static std::atomic<uint32_t> g_last_psram_before{0};
static std::atomic<uint32_t> g_last_psram_after{0};
static std::atomic<uint32_t> g_last_psram_min{0};
static std::atomic<uint32_t> g_last_compiler_stack_hwm_words{0};
static std::atomic<uint32_t> g_total_compile_requests{0};
static std::atomic<uint32_t> g_total_compile_ok{0};
static std::atomic<uint32_t> g_total_compile_failed{0};
static std::atomic<uint32_t> g_total_compile_rejected{0};
static std::atomic<bool> g_save_after_compile_busy{false};
static std::atomic<bool> g_compile_busy{false};
static std::atomic<uint32_t> g_activations{0};
static std::atomic<uint32_t> g_script_scans_completed{0};
static std::atomic<uint32_t> g_compile_execute_overlap_scans{0};
static std::atomic<uint32_t> g_retired_destroy_us_max{0};
static std::atomic<uint32_t> g_run_scan_us_max{0};
static std::atomic<uint32_t> g_run_scan_us_last{0};
static std::atomic<uint32_t> g_run_scan_us_ema{0};
static std::atomic<uint32_t> g_run_scan_us_window_avg{0};
static std::atomic<uint32_t> g_run_scan_us_window_max{0};
static std::atomic<uint32_t> g_run_scan_us_window_min{0};
static std::atomic<uint32_t> g_run_scan_us_window_count{0};
static std::atomic<uint32_t> g_run_scan_us_window_over_2500{0};
static std::atomic<uint32_t> g_run_scan_us_window_over_5000{0};

static uint64_t g_run_scan_window_sum_us = 0;
static uint32_t g_run_scan_window_count = 0;
static uint32_t g_run_scan_window_max = 0;
static uint32_t g_run_scan_window_min = UINT32_MAX;
static int64_t g_run_scan_window_start_us = 0;
static uint32_t g_run_scan_window_over_2500 = 0;
static uint32_t g_run_scan_window_over_5000 = 0;

// Experiment: pause active script execution while the compiler is building a
// replacement program. The 1 ms I/O task still runs and applies the most recent
// output image, but the interpreted Scan() function is not executed until the
// compile either fails or the newly compiled program is activated.
static std::atomic<bool> g_pause_script_for_compile{false};
static std::atomic<uint32_t> g_paused_scan_skips{0};
static std::atomic<uint32_t> g_total_pause_windows{0};
static std::atomic<uint64_t> g_last_pause_us{0};
static std::atomic<int64_t> g_pause_start_us{0};

static char g_last_error[1024] = "No script compiled yet";

class ScriptProgram;

struct RetiredProgramJob {
    ScriptProgram* program = nullptr;
    uint32_t generation = 0;
};

static std::unique_ptr<ScriptProgram> g_active_program;
static std::unique_ptr<ScriptProgram> g_pending_program;

static void atomic_max_u32(std::atomic<uint32_t>& target, uint32_t value)
{
    uint32_t old = target.load();
    while (value > old && !target.compare_exchange_weak(old, value)) {}
}

static void set_status_text(const char* text)
{
    if (!text) text = "";
    if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snprintf(g_last_error, sizeof(g_last_error), "%s", text);
        xSemaphoreGive(g_status_mutex);
    } else {
        snprintf(g_last_error, sizeof(g_last_error), "%s", text);
    }
}

static const char* state_to_string(ScriptCompileState state)
{
    switch (state) {
        case SCRIPT_STATE_IDLE: return "idle";
        case SCRIPT_STATE_QUEUED: return "queued";
        case SCRIPT_STATE_COMPILING: return "compiling";
        case SCRIPT_STATE_OK: return "ok";
        case SCRIPT_STATE_FAILED: return "failed";
        case SCRIPT_STATE_QUEUE_FULL: return "queue_full";
        default: return "unknown";
    }
}

static void append_error(std::string& out, const char* text)
{
    if (!text) return;
    if (out.size() < 900) out += text;
}

static void ASMessageCallback(const asSMessageInfo* msg, void* param)
{
    std::string* errors = static_cast<std::string*>(param);
    const char* type = "ERR";
    if (msg->type == asMSGTYPE_WARNING) type = "WARN";
    else if (msg->type == asMSGTYPE_INFORMATION) type = "INFO";

    char line[256];
    snprintf(line, sizeof(line), "%s (%d,%d): %s: %s\n",
             msg->section ? msg->section : "script",
             msg->row,
             msg->col,
             type,
             msg->message ? msg->message : "");
    append_error(*errors, line);

    if (msg->type == asMSGTYPE_ERROR) {
        if (pilab_script_log_enabled(PILAB_SCRIPT_LOG_ERROR)) ESP_LOGE(TAG, "%s", line);
    } else if (msg->type == asMSGTYPE_WARNING) {
        if (pilab_script_log_enabled(PILAB_SCRIPT_LOG_WARN)) ESP_LOGW(TAG, "%s", line);
    } else {
        if (pilab_script_log_enabled(PILAB_SCRIPT_LOG_INFO)) ESP_LOGI(TAG, "%s", line);
    }
}

static bool register_bool_global(asIScriptEngine* engine, const char* name, bool* ptr, std::string& errors)
{
    char decl[32];
    snprintf(decl, sizeof(decl), "bool %s", name);
    int r = engine->RegisterGlobalProperty(decl, ptr);
    if (r < 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "RegisterGlobalProperty failed: %s\n", decl);
        append_error(errors, msg);
        return false;
    }
    return true;
}

static bool register_uint_global(asIScriptEngine* engine, const char* name, uint32_t* ptr, std::string& errors)
{
    char decl[48];
    snprintf(decl, sizeof(decl), "const uint %s", name);
    int r = engine->RegisterGlobalProperty(decl, ptr);
    if (r < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "RegisterGlobalProperty failed: %s\n", decl);
        append_error(errors, msg);
        return false;
    }
    return true;
}

static bool register_const_float_global(asIScriptEngine* engine, const char* name, float* ptr, std::string& errors)
{
    char decl[48];
    snprintf(decl, sizeof(decl), "const float %s", name);
    int r = engine->RegisterGlobalProperty(decl, ptr);
    if (r < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "RegisterGlobalProperty failed: %s\n", decl);
        append_error(errors, msg);
        return false;
    }
    return true;
}

static bool register_float_global(asIScriptEngine* engine, const char* name, float* ptr, std::string& errors)
{
    char decl[32];
    snprintf(decl, sizeof(decl), "float %s", name);
    int r = engine->RegisterGlobalProperty(decl, ptr);
    if (r < 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "RegisterGlobalProperty failed: %s\n", decl);
        append_error(errors, msg);
        return false;
    }
    return true;
}

class ScriptProgram
{
public:
    ~ScriptProgram()
    {
        // Destruction is intentionally performed only by the scan task after a
        // grace period. Do not delete active/retired programs from compiler task.
        if (ctx) {
            ctx->Release();
            ctx = nullptr;
        }
        if (engine) {
            engine->ShutDownAndRelease();
            engine = nullptr;
        }
    }

    bool compile(const char* text, size_t len, std::string& errors)
    {
        install_angelscript_psram_allocator_once();

        engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
        if (!engine) {
            errors = "asCreateScriptEngine failed";
            return false;
        }

        int r = engine->SetMessageCallback(asFUNCTION(ASMessageCallback), &errors, asCALL_CDECL);
        if (r < 0) return fail(errors, "SetMessageCallback failed\n");

        // Standard AngelScript add-ons. AS_MAX_PORTABILITY is enabled in the
        // angelscript component, so these register through the generic calling
        // convention instead of native RISC-V function bindings.
        RegisterStdString(engine);
        RegisterScriptArray(engine, true);
        RegisterStdStringUtils(engine);
        RegisterScriptMath(engine);

        r = engine->RegisterGlobalFunction("void logInt(uint)", asFUNCTION(AS_LogInt_Generic), asCALL_GENERIC);
        if (r < 0) return fail(errors, "Register logInt() failed\n");

        r = engine->RegisterGlobalFunction("uint getDI(uint)", asFUNCTION(AS_GetDI_Generic), asCALL_GENERIC);
        if (r < 0) return fail(errors, "Register getDI() failed\n");

        r = engine->RegisterGlobalFunction("void setDO(uint,uint)", asFUNCTION(AS_SetDO_Generic), asCALL_GENERIC);
        if (r < 0) return fail(errors, "Register setDO() failed\n");

        for (uint32_t i = 0; i < PLC_DI_COUNT; ++i) {
            char name[8]; snprintf(name, sizeof(name), "I%lu", (unsigned long)i);
            if (!register_bool_global(engine, name, &g_script_io.I[i], errors)) return false;
        }
        for (uint32_t i = 0; i < PLC_DO_COUNT; ++i) {
            char name[8]; snprintf(name, sizeof(name), "Q%lu", (unsigned long)i);
            if (!register_bool_global(engine, name, &g_script_io.Q[i], errors)) return false;
        }
        for (uint32_t i = 0; i < PLC_AI_COUNT; ++i) {
            char name[8]; snprintf(name, sizeof(name), "AI%lu", (unsigned long)i);
            if (!register_float_global(engine, name, &g_script_io.AI[i], errors)) return false;
        }
        for (uint32_t i = 0; i < PLC_AO_COUNT; ++i) {
            char name[8]; snprintf(name, sizeof(name), "AO%lu", (unsigned long)i);
            if (!register_float_global(engine, name, &g_script_io.AO[i], errors)) return false;
        }

        if (!register_uint_global(engine, "PLC_DeltaTimeUs", &g_script_delta_time_us, errors)) return false;
        if (!register_const_float_global(engine, "PLC_DeltaTimeMs", &g_script_delta_time_ms, errors)) return false;
        if (!register_const_float_global(engine, "PLC_DeltaTimeSeconds", &g_script_delta_time_s, errors)) return false;
        if (!register_uint_global(engine, "PLC_ScanActualPeriodUs", &g_script_actual_period_us, errors)) return false;
        if (!register_uint_global(engine, "PLC_ScanBudgetUs", &g_script_budget_us, errors)) return false;

        char tag_err[256] = {};
        if (!plc_tags_register_angelscript_globals(engine, tag_err, sizeof(tag_err))) {
            append_error(errors, tag_err[0] ? tag_err : "Register PLC tags failed\n");
            append_error(errors, "\n");
            return false;
        }

        asIScriptModule* mod = engine->GetModule("control", asGM_ALWAYS_CREATE);
        if (!mod) return fail(errors, "GetModule failed\n");

        r = mod->AddScriptSection("uploaded_script", text, len);
        if (r < 0) return fail(errors, "AddScriptSection failed\n");

        r = mod->Build();
        if (r < 0) {
            append_error(errors, "Build failed\n");
            return false;
        }

        scan_fn = mod->GetFunctionByDecl("void Scan(float)");
        scan_takes_dt = true;
        if (!scan_fn) {
            scan_fn = mod->GetFunctionByDecl("void scan()");
            scan_takes_dt = false;
        }
        if (!scan_fn) return fail(errors, "Script must define either: void Scan(float) or void scan()\n");

        return true;
    }

    bool runScan(float dt_seconds)
    {
        if (!engine || !scan_fn) return false;

        if (!ctx) {
            ctx = engine->CreateContext();
            if (!ctx) {
                ESP_LOGE(TAG, "CreateContext failed on PLC task");
                return false;
            }
        }

        int r = ctx->Prepare(scan_fn);
        if (r < 0) return false;

        if (scan_takes_dt) {
            ctx->SetArgFloat(0, dt_seconds);
        }

        r = ctx->Execute();
        if (r != asEXECUTION_FINISHED) {
            if (r == asEXECUTION_EXCEPTION) {
                ESP_LOGE(TAG, "Script exception: %s", ctx->GetExceptionString());
            } else {
                ESP_LOGE(TAG, "Script execution failed: %d", r);
            }
            ctx->Abort();
            ctx->Unprepare();
            return false;
        }

        ctx->Unprepare();
        return true;
    }

private:
    bool fail(std::string& errors, const char* msg)
    {
        append_error(errors, msg);
        return false;
    }

    asIScriptEngine* engine = nullptr;
    asIScriptContext* ctx = nullptr;
    asIScriptFunction* scan_fn = nullptr;
    bool scan_takes_dt = false;
};

static bool compile_to_pending(const char* script_text, size_t script_len, char* err_buf, size_t err_buf_len)
{
    if (!script_text || script_len == 0) {
        if (err_buf && err_buf_len) snprintf(err_buf, err_buf_len, "Empty script");
        return false;
    }

    std::string errors;
    std::unique_ptr<ScriptProgram> next(new (std::nothrow) ScriptProgram());
    if (!next) {
        if (err_buf && err_buf_len) snprintf(err_buf, err_buf_len, "Out of memory creating ScriptProgram");
        return false;
    }

    const uint32_t heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const int64_t start_us = esp_timer_get_time();

    // NO GLOBAL AS API MUTEX HERE.
    // After rebuilding AngelScript without AS_NO_THREADS, this is the actual
    // no-pause test: compile/build a completely separate engine on Core 1 while
    // the active engine continues executing on Core 0.
    const bool ok = next->compile(script_text, script_len, errors);

    const uint64_t compile_us = (uint64_t)(esp_timer_get_time() - start_us);
    const uint32_t heap_after_compile = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const uint32_t internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const uint32_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    g_last_compile_us.store(compile_us);
    g_last_heap_before.store(heap_before);
    g_last_heap_after.store(heap_after_compile);
    g_last_heap_min.store(heap_min);
    g_last_internal_before.store(internal_before);
    g_last_internal_after.store(internal_after);
    g_last_internal_min.store(internal_min);
    g_last_psram_before.store(psram_before);
    g_last_psram_after.store(psram_after);
    g_last_psram_min.store(psram_min);
    g_last_compiler_stack_hwm_words.store(uxTaskGetStackHighWaterMark(nullptr));

    if (!ok) {
        std::string final_error = errors.empty() ? "Compile failed" : errors;
        set_status_text(final_error.c_str());
        if (err_buf && err_buf_len) snprintf(err_buf, err_buf_len, "%s", final_error.c_str());
        ESP_LOGE(TAG,
                 "Compile failed in %llu us, heap8 before=%lu after=%lu min=%lu, internal before=%lu after=%lu min=%lu, psram before=%lu after=%lu min=%lu, compiler stack HWM=%lu words",
                 (unsigned long long)compile_us,
                 (unsigned long)heap_before,
                 (unsigned long)heap_after_compile,
                 (unsigned long)heap_min,
                 (unsigned long)internal_before,
                 (unsigned long)internal_after,
                 (unsigned long)internal_min,
                 (unsigned long)psram_before,
                 (unsigned long)psram_after,
                 (unsigned long)psram_min,
                 (unsigned long)g_last_compiler_stack_hwm_words.load());
        return false;
    }

    xSemaphoreTake(g_program_mutex, portMAX_DELAY);
    if (g_pending_program) {
        xSemaphoreGive(g_program_mutex);
        set_status_text("Compile OK, but pending slot was occupied; script rejected");
        if (err_buf && err_buf_len) snprintf(err_buf, err_buf_len, "Pending slot occupied");
        return false;
    }
    g_pending_program = std::move(next);
    xSemaphoreGive(g_program_mutex);

    char ok_msg[256];
    snprintf(ok_msg, sizeof(ok_msg), "OK. Compile time %llu us, pending activation", (unsigned long long)compile_us);
    set_status_text(ok_msg);
    if (err_buf && err_buf_len) snprintf(err_buf, err_buf_len, "%s", ok_msg);

    ESP_LOGI(TAG,
             "Compile OK in %llu us, pending activation, heap8 before=%lu after=%lu min=%lu, internal before=%lu after=%lu min=%lu, psram before=%lu after=%lu min=%lu, compiler stack HWM=%lu words",
             (unsigned long long)compile_us,
             (unsigned long)heap_before,
             (unsigned long)heap_after_compile,
             (unsigned long)heap_min,
             (unsigned long)internal_before,
             (unsigned long)internal_after,
             (unsigned long)internal_min,
             (unsigned long)psram_before,
             (unsigned long)psram_after,
             (unsigned long)psram_min,
             (unsigned long)g_last_compiler_stack_hwm_words.load());
    return true;
}


static bool queue_retired_program_for_cleanup(std::unique_ptr<ScriptProgram>&& retired, uint32_t generation)
{
    if (!retired) return true;

    RetiredProgramJob job{};
    job.program = retired.release();
    job.generation = generation;

    if (g_retired_queue && xQueueSend(g_retired_queue, &job, 0) == pdTRUE) {
        return true;
    }

    // Last-resort fallback: keep the program alive by leaking rather than
    // deleting it from the 5 ms scan task. This should never happen with the
    // queue depth used below, but it is safer for timing than a surprise
    // 10-20 ms destructor pause on the control core.
    ESP_LOGE(TAG,
             "Retired-program cleanup queue full; intentionally leaking generation=%lu to protect scan timing",
             (unsigned long)generation);
    return false;
}

static void script_cleanup_task(void*)
{
    RetiredProgramJob job{};
    while (true) {
        if (xQueueReceive(g_retired_queue, &job, portMAX_DELAY) == pdTRUE) {
            if (!job.program) continue;

            // Give activation and TCP/HTTP response cleanup a chance to settle.
            // Destruction is low-priority and non-real-time.
            vTaskDelay(SCRIPT_CLEANUP_DEFER_TICKS);

            const int64_t destroy_start = esp_timer_get_time();
            delete job.program;
            const uint32_t destroy_us = (uint32_t)(esp_timer_get_time() - destroy_start);
            atomic_max_u32(g_retired_destroy_us_max, destroy_us);

            ESP_LOGI(TAG,
                     "Cleanup task destroyed retired script generation=%lu in %lu us on core %d",
                     (unsigned long)job.generation,
                     (unsigned long)destroy_us,
                     (int)xPortGetCoreID());

            job.program = nullptr;
            job.generation = 0;
        }
    }
}


static bool script_engine_save_compiled_script_to_filesystem(const char* filename,
                                                             const char* script,
                                                             size_t script_len,
                                                             char* saved_rel_path,
                                                             size_t saved_rel_path_len,
                                                             char* err,
                                                             size_t err_len)
{
    if (!filename || !filename[0] || !script || !saved_rel_path || saved_rel_path_len == 0) {
        if (err && err_len) snprintf(err, err_len, "Invalid compiled script save request");
        return false;
    }

    // The upload handler has already sanitized this, but keep this guard here so
    // future callers cannot accidentally turn the script name into a path write.
    if (strstr(filename, "..") || strchr(filename, '/') || strchr(filename, '\\') || filename[0] == '.') {
        if (err && err_len) snprintf(err, err_len, "Invalid compiled script filename");
        return false;
    }

    char dir_path[128] = {0};
    snprintf(dir_path, sizeof(dir_path), "%s/scripts", PLC_FS_MOUNT_POINT);
    mkdir(dir_path, 0755); // harmless if it already exists

    char fs_path[192] = {0};
    int n = snprintf(fs_path, sizeof(fs_path), "%s/scripts/%s", PLC_FS_MOUNT_POINT, filename);
    if (n < 0 || (size_t)n >= sizeof(fs_path)) {
        if (err && err_len) snprintf(err, err_len, "Compiled script path is too long");
        return false;
    }

    FILE* f = fopen(fs_path, "wb");
    if (!f) {
        if (err && err_len) snprintf(err, err_len, "Failed to open /scripts/%s for write", filename);
        return false;
    }

    bool ok = true;
    if (script_len > 0 && fwrite(script, 1, script_len, f) != script_len) {
        if (err && err_len) snprintf(err, err_len, "Failed to write /scripts/%s", filename);
        ok = false;
    }

    if (fclose(f) != 0 && ok) {
        if (err && err_len) snprintf(err, err_len, "Failed to close /scripts/%s", filename);
        ok = false;
    }

    if (!ok) {
        unlink(fs_path);
        return false;
    }

    snprintf(saved_rel_path, saved_rel_path_len, "/scripts/%s", filename);
    return true;
}

static void script_compile_task(void*)
{
    ScriptCompileJob job{};
    while (true) {
        if (xQueueReceive(g_compile_queue, &job, portMAX_DELAY) == pdTRUE) {
            g_state.store(SCRIPT_STATE_COMPILING);

            vTaskDelay(SCRIPT_COMPILE_DEFER_TICKS);

            const int64_t pause_start = esp_timer_get_time();
            const int update_mode = g_script_update_mode.load();
            const bool stop_before_compile = (update_mode == SCRIPT_UPDATE_STOP_BEFORE_COMPILE);
            const bool pause_during_compile = (update_mode == SCRIPT_UPDATE_PAUSE_DURING_COMPILE);

            if (stop_before_compile) {
                perf_test_set_plc_running(false);
            }

            g_pause_start_us.store(pause_start);
            g_pause_script_for_compile.store(pause_during_compile);
            if (pause_during_compile) g_total_pause_windows.fetch_add(1);

            ESP_LOGI(TAG, "Compiler starting deferred job: mode=%s bytes=%u defer_ms=%lu core=%d priority=%u",
                     script_engine_get_update_mode(),
                     (unsigned)job.length,
                     (unsigned long)(SCRIPT_COMPILE_DEFER_TICKS * portTICK_PERIOD_MS),
                     (int)xPortGetCoreID(),
                     (unsigned)uxTaskPriorityGet(nullptr));

            char err[1024] = {};
            g_save_after_compile_busy.store(job.filename[0] != 0);
            bool ok = compile_to_pending(job.source, job.length, err, sizeof(err));

            if (ok && job.filename[0]) {
                char saved_rel_path[128] = {};
                char save_err[192] = {};
                const int64_t save_start_us = esp_timer_get_time();
                if (!script_engine_save_compiled_script_to_filesystem(job.filename,
                                                                      job.source,
                                                                      job.length,
                                                                      saved_rel_path,
                                                                      sizeof(saved_rel_path),
                                                                      save_err,
                                                                      sizeof(save_err))) {
                    ok = false;
                    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        g_pending_program.reset();
                        xSemaphoreGive(g_program_mutex);
                    }
                    if (err[0] == 0) {
                        snprintf(err, sizeof(err), "Compile OK but save failed: %s", save_err);
                    }
                    ESP_LOGE(TAG, "Compiled script save failed; pending program discarded: %s", save_err);
                } else {
                    const int64_t save_us = esp_timer_get_time() - save_start_us;
                    ESP_LOGI(TAG, "Saved compiled script to %s (%u bytes, %lld us)",
                             saved_rel_path,
                             (unsigned)job.length,
                             (long long)save_us);
                    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        snprintf(g_pending_script_name, sizeof(g_pending_script_name), "%s", job.filename);
                        xSemaphoreGive(g_program_mutex);
                    }
                }
            } else if (ok) {
                if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    g_pending_script_name[0] = 0;
                    xSemaphoreGive(g_program_mutex);
                }
            }

            g_save_after_compile_busy.store(false);

            heap_caps_free(job.source);
            job.source = nullptr;
            job.length = 0;

            if (ok) {
                // Keep script execution paused until the 5 ms script task sees
                // and activates the pending program at a scan boundary. That
                // avoids running one more old-program scan after compile.
                g_total_compile_ok.fetch_add(1);
                g_state.store(SCRIPT_STATE_OK);
            } else {
                const int64_t pause_us = esp_timer_get_time() - pause_start;
                g_last_pause_us.store((uint64_t)pause_us);
                if (pause_during_compile) g_pause_script_for_compile.store(false);

                g_total_compile_failed.fetch_add(1);
                g_state.store(SCRIPT_STATE_FAILED);

                ESP_LOGW(TAG, "Compiler failed; script scan unpaused after %llu us",
                         (unsigned long long)pause_us);
            }
            g_compile_busy.store(false);
        }
    }
}

bool script_engine_start(void)
{
    if (!g_program_mutex) g_program_mutex = xSemaphoreCreateMutex();
    if (!g_status_mutex) g_status_mutex = xSemaphoreCreateMutex();
    if (!g_compile_queue) g_compile_queue = xQueueCreate(1, sizeof(ScriptCompileJob));
    if (!g_retired_queue) g_retired_queue = xQueueCreate(8, sizeof(RetiredProgramJob));

    if (!g_program_mutex || !g_status_mutex || !g_compile_queue || !g_retired_queue) {
        set_status_text("Script engine init failed: mutex/queue allocation failed");
        return false;
    }

    if (!g_compile_task_handle) {
        BaseType_t ret = xTaskCreatePinnedToCore(
            script_compile_task,
            "script_compile",
            SCRIPT_COMPILE_TASK_STACK_BYTES,
            nullptr,
            SCRIPT_COMPILE_TASK_PRIORITY,
            &g_compile_task_handle,
            SCRIPT_COMPILE_TASK_CORE);
        if (ret != pdPASS) {
            set_status_text("Script engine init failed: compiler task create failed");
            return false;
        }
        ESP_LOGI(TAG, "Async compiler task started: stack=%lu bytes core=%d priority=%u",
                 (unsigned long)SCRIPT_COMPILE_TASK_STACK_BYTES,
                 (int)SCRIPT_COMPILE_TASK_CORE,
                 (unsigned)SCRIPT_COMPILE_TASK_PRIORITY);
    }

    if (!g_cleanup_task_handle) {
        BaseType_t ret = xTaskCreatePinnedToCore(
            script_cleanup_task,
            "script_cleanup",
            SCRIPT_CLEANUP_TASK_STACK_BYTES,
            nullptr,
            SCRIPT_CLEANUP_TASK_PRIORITY,
            &g_cleanup_task_handle,
            SCRIPT_CLEANUP_TASK_CORE);
        if (ret != pdPASS) {
            set_status_text("Script engine init failed: cleanup task create failed");
            return false;
        }
        ESP_LOGI(TAG, "Async cleanup task started: stack=%lu bytes core=%d priority=%u",
                 (unsigned long)SCRIPT_CLEANUP_TASK_STACK_BYTES,
                 (int)SCRIPT_CLEANUP_TASK_CORE,
                 (unsigned)SCRIPT_CLEANUP_TASK_PRIORITY);
    }

    ESP_LOGI(TAG, "Memory: heap8=%lu internal=%lu psram=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    return true;
}

bool script_engine_compile_text(const char* script_text, size_t script_len, char* err_buf, size_t err_buf_len)
{
    if (!script_engine_start()) return false;
    g_state.store(SCRIPT_STATE_COMPILING);
    bool ok = compile_to_pending(script_text, script_len, err_buf, err_buf_len);
    g_state.store(ok ? SCRIPT_STATE_OK : SCRIPT_STATE_FAILED);
    g_compile_busy.store(false);
    return ok;
}


bool script_engine_can_accept_upload(char* response_buf, size_t response_buf_len)
{
    if (!script_engine_start()) {
        if (response_buf && response_buf_len) {
            snprintf(response_buf, response_buf_len, "Script engine failed to start");
        }
        return false;
    }

    if (g_compile_busy.load()) {
        if (response_buf && response_buf_len) {
            snprintf(response_buf, response_buf_len, "Compile already queued or running");
        }
        return false;
    }

    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        bool pending_exists = (g_pending_program != nullptr);
        xSemaphoreGive(g_program_mutex);
        if (pending_exists) {
            if (response_buf && response_buf_len) {
                snprintf(response_buf, response_buf_len, "Compiled script is pending activation");
            }
            return false;
        }
    }

    if (response_buf && response_buf_len) {
        snprintf(response_buf, response_buf_len, "Ready for upload");
    }
    return true;
}

bool script_engine_submit_compile_text(const char* script_text, size_t script_len,
                                       const char* script_filename,
                                       char* response_buf, size_t response_buf_len)
{
    if (!script_text || script_len == 0 || script_len > SCRIPT_MAX_BYTES) {
        if (response_buf && response_buf_len) {
            snprintf(response_buf, response_buf_len, "Script must be 1..%u bytes", (unsigned)SCRIPT_MAX_BYTES);
        }
        return false;
    }

    if (!script_engine_start()) {
        if (response_buf && response_buf_len) snprintf(response_buf, response_buf_len, "Script engine failed to start");
        return false;
    }

    bool expected = false;
    if (!g_compile_busy.compare_exchange_strong(expected, true)) {
        g_total_compile_rejected.fetch_add(1);
        if (response_buf && response_buf_len) snprintf(response_buf, response_buf_len, "Compile already queued or running");
        g_state.store(SCRIPT_STATE_QUEUE_FULL);
        return false;
    }

    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        bool pending_exists = (g_pending_program != nullptr);
        xSemaphoreGive(g_program_mutex);
        if (pending_exists) {
            g_compile_busy.store(false);
            g_total_compile_rejected.fetch_add(1);
            if (response_buf && response_buf_len) snprintf(response_buf, response_buf_len, "Compiled script is pending activation");
            g_state.store(SCRIPT_STATE_QUEUE_FULL);
            return false;
        }
    }

    char* copy = alloc_script_buffer_psram(script_len);
    if (!copy) {
        g_compile_busy.store(false);
        if (response_buf && response_buf_len) snprintf(response_buf, response_buf_len, "Out of memory copying script");
        return false;
    }
    memcpy(copy, script_text, script_len);

    ScriptCompileJob job{};
    job.source = copy;
    job.length = script_len;
    if (script_filename && script_filename[0]) {
        snprintf(job.filename, sizeof(job.filename), "%s", script_filename);
    }
    if (xQueueSend(g_compile_queue, &job, 0) != pdTRUE) {
        heap_caps_free(copy);
        g_compile_busy.store(false);
        g_total_compile_rejected.fetch_add(1);
        if (response_buf && response_buf_len) snprintf(response_buf, response_buf_len, "Compile queue full");
        g_state.store(SCRIPT_STATE_QUEUE_FULL);
        return false;
    }

    g_total_compile_requests.fetch_add(1);
    g_state.store(SCRIPT_STATE_QUEUED);
    set_status_text("Script accepted for async compile");

    if (response_buf && response_buf_len) {
        snprintf(response_buf, response_buf_len, "Script accepted for async compile");
    }
    return true;
}

static void update_run_scan_window_stats(uint32_t run_us)
{
    g_run_scan_us_last.store(run_us, std::memory_order_relaxed);

    uint32_t ema = g_run_scan_us_ema.load(std::memory_order_relaxed);
    if (ema == 0) {
        ema = run_us;
    } else {
        ema = (uint32_t)(((uint64_t)ema * 7u + run_us) / 8u);
    }
    g_run_scan_us_ema.store(ema, std::memory_order_relaxed);

    const int64_t now_us = esp_timer_get_time();
    if (g_run_scan_window_start_us == 0) {
        g_run_scan_window_start_us = now_us;
    }

    g_run_scan_window_sum_us += run_us;
    g_run_scan_window_count++;
    if (run_us > g_run_scan_window_max) g_run_scan_window_max = run_us;
    if (run_us < g_run_scan_window_min) g_run_scan_window_min = run_us;
    if (run_us > 2500u) g_run_scan_window_over_2500++;
    if (run_us > 5000u) g_run_scan_window_over_5000++;

    if ((now_us - g_run_scan_window_start_us) >= 1000000) {
        const uint32_t count = g_run_scan_window_count ? g_run_scan_window_count : 1;
        const uint32_t avg = (uint32_t)(g_run_scan_window_sum_us / count);
        const uint32_t minv = (g_run_scan_window_min == UINT32_MAX) ? 0 : g_run_scan_window_min;

        g_run_scan_us_window_avg.store(avg, std::memory_order_relaxed);
        g_run_scan_us_window_max.store(g_run_scan_window_max, std::memory_order_relaxed);
        g_run_scan_us_window_min.store(minv, std::memory_order_relaxed);
        g_run_scan_us_window_count.store(count, std::memory_order_relaxed);
        g_run_scan_us_window_over_2500.store(g_run_scan_window_over_2500, std::memory_order_relaxed);
        g_run_scan_us_window_over_5000.store(g_run_scan_window_over_5000, std::memory_order_relaxed);

        g_run_scan_window_start_us = now_us;
        g_run_scan_window_sum_us = 0;
        g_run_scan_window_count = 0;
        g_run_scan_window_max = 0;
        g_run_scan_window_min = UINT32_MAX;
        g_run_scan_window_over_2500 = 0;
        g_run_scan_window_over_5000 = 0;
    }
}

bool script_engine_run_scan(void)
{
    if (!g_program_mutex) return false;

    bool ran = false;
    bool activated = false;

    // If a compile is active, deliberately pause user script execution. The
    // firmware-side 1 ms I/O task continues to read inputs and apply the last
    // published outputs. This experiment lets us see how much jitter is caused
    // by compiler-vs-interpreter contention.
    const bool paused = g_pause_script_for_compile.load();

    if (!paused) {
        sync_inputs_to_script_globals();

        if (g_active_program) {
            const int64_t start_us = esp_timer_get_time();
            ran = g_active_program->runScan(g_script_delta_time_s);
            const uint32_t run_us = (uint32_t)(esp_timer_get_time() - start_us);
            atomic_max_u32(g_run_scan_us_max, run_us);
            update_run_scan_window_stats(run_us);

            if (ran) {
                copy_outputs_from_script_globals();
                plc_io_note_script_scan();
                g_script_scans_completed.fetch_add(1);
                if (g_compile_busy.load()) g_compile_execute_overlap_scans.fetch_add(1);
            }
        }
    } else {
        g_paused_scan_skips.fetch_add(1);
    }

    // Activate pending program at scan boundary. This still runs while paused,
    // so successful compile causes a clean swap and then unpauses execution.
    if (xSemaphoreTake(g_program_mutex, 0) == pdTRUE) {
        if (g_pending_program && !g_save_after_compile_busy.load()) {
            std::unique_ptr<ScriptProgram> retired_program = std::move(g_active_program);
            g_active_program = std::move(g_pending_program);
            snprintf(g_active_script_name, sizeof(g_active_script_name), "%s", g_pending_script_name);
            g_pending_script_name[0] = 0;

            uint32_t new_generation = g_generation.fetch_add(1) + 1;
            g_activations.fetch_add(1);
            activated = true;

            if (retired_program) {
                const uint32_t retired_generation = (new_generation > 0) ? (new_generation - 1) : 0;
                queue_retired_program_for_cleanup(std::move(retired_program), retired_generation);
            }

            if (g_status_mutex && xSemaphoreTake(g_status_mutex, 0) == pdTRUE) {
                snprintf(g_last_error, sizeof(g_last_error), "Active script generation=%lu", (unsigned long)new_generation);
                xSemaphoreGive(g_status_mutex);
            }

            ESP_LOGI(TAG, "PLC activated pending script generation=%lu after completed scan; retired cleanup queued on core %d",
                     (unsigned long)new_generation,
                     (int)SCRIPT_CLEANUP_TASK_CORE);
        }
        xSemaphoreGive(g_program_mutex);
    }

    if (activated && paused) {
        const int64_t now_us = esp_timer_get_time();
        const int64_t start_us = g_pause_start_us.load();
        const uint64_t pause_us = (start_us > 0 && now_us > start_us) ? (uint64_t)(now_us - start_us) : 0;
        g_last_pause_us.store(pause_us);
        g_pause_script_for_compile.store(false);

        ESP_LOGI(TAG,
                 "Script scan unpaused after compile/activation pause=%llu us, paused_scan_skips=%lu",
                 (unsigned long long)pause_us,
                 (unsigned long)g_paused_scan_skips.load());
    }

    return ran;
}

void script_engine_get_active_script_name(char* out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';

    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        snprintf(out, out_len, "%s", g_active_script_name[0] ? g_active_script_name : "");
        xSemaphoreGive(g_program_mutex);
    }
}

void script_engine_get_pending_script_name(char* out, size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';

    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        snprintf(out, out_len, "%s", g_pending_script_name[0] ? g_pending_script_name : "");
        xSemaphoreGive(g_program_mutex);
    }
}

uint32_t script_engine_get_generation(void)
{
    return g_generation.load();
}

const char* script_engine_get_last_error(void)
{
    return g_last_error;
}

ScriptCompileState script_engine_get_state(void)
{
    return static_cast<ScriptCompileState>(g_state.load());
}

void script_engine_get_status_json(char* out, size_t out_len)
{
    if (!out || out_len == 0) return;

    // Keep this small: this function may be called from the ESP-IDF httpd task.
    // Large stack buffers here caused httpd stack protection faults.
    char msg[384] = {};
    if (g_status_mutex && xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        const size_t copy_len_raw = strlen(g_last_error);
        const size_t copy_len = (copy_len_raw >= sizeof(msg)) ? (sizeof(msg) - 1) : copy_len_raw;
        memcpy(msg, g_last_error, copy_len);
        msg[copy_len] = '\0';
        xSemaphoreGive(g_status_mutex);
    } else {
        const size_t copy_len_raw = strlen(g_last_error);
        const size_t copy_len = (copy_len_raw >= sizeof(msg)) ? (sizeof(msg) - 1) : copy_len_raw;
        memcpy(msg, g_last_error, copy_len);
        msg[copy_len] = '\0';
    }

    char escaped[512] = {};
    size_t j = 0;
    for (size_t i = 0; msg[i] != '\0' && j + 2 < sizeof(escaped); ++i) {
        char c = msg[i];
        if (c == '"' || c == '\\') {
            escaped[j++] = '\\';
            escaped[j++] = c;
        } else if (c == '\n' || c == '\r') {
            escaped[j++] = ' ';
        } else {
            escaped[j++] = c;
        }
    }
    escaped[j] = '\0';

    ScriptCompileState state = script_engine_get_state();
    bool busy = g_compile_busy.load();
    char active_script_name[96] = {};
    char pending_script_name[96] = {};
    bool pending = false;
    if (g_program_mutex && xSemaphoreTake(g_program_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        pending = (g_pending_program != nullptr);
        snprintf(active_script_name, sizeof(active_script_name), "%s", g_active_script_name);
        snprintf(pending_script_name, sizeof(pending_script_name), "%s", g_pending_script_name);
        xSemaphoreGive(g_program_mutex);
    }

    snprintf(out, out_len,
             "{\"state\":\"%s\",\"generation\":%lu,\"active_script_name\":\"%s\",\"active_script_path\":\"%s%s\",\"pending_script_name\":\"%s\",\"pending\":%s,\"compile_busy\":%s,"
             "\"pause_during_compile_experiment\":true,\"activations\":%lu,\"script_scans_completed\":%lu,"
             "\"compile_execute_overlap_scans\":%lu,\"script_scan_paused\":%s,"
             "\"paused_scan_skips\":%lu,\"pause_windows\":%lu,\"last_pause_us\":%llu,"
             "\"run_scan_us_max\":%lu,\"run_scan_us_last\":%lu,\"run_scan_us_ema\":%lu,"
             "\"run_scan_us_window_avg\":%lu,\"run_scan_us_window_max\":%lu,"
             "\"run_scan_us_window_min\":%lu,\"run_scan_us_window_count\":%lu,"
             "\"run_scan_us_window_over_2500\":%lu,\"run_scan_us_window_over_5000\":%lu,"
             "\"retired_destroy_us_max\":%lu,\"last_compile_us\":%llu,"
             "\"heap_before\":%lu,\"heap_after\":%lu,\"heap_min\":%lu,"
             "\"internal_before\":%lu,\"internal_after\":%lu,\"internal_min\":%lu,"
             "\"psram_before\":%lu,\"psram_after\":%lu,\"psram_min\":%lu,"
             "\"heap_now\":%lu,\"internal_now\":%lu,\"psram_now\":%lu,"
             "\"compiler_stack_hwm_words\":%lu,\"requests\":%lu,\"ok\":%lu,\"failed\":%lu,\"rejected\":%lu,"
             "\"last_result\":\"%s\"}",
             state_to_string(state),
             (unsigned long)g_generation.load(),
             active_script_name,
             active_script_name[0] ? "/scripts/" : "",
             active_script_name,
             pending_script_name,
             pending ? "true" : "false",
             busy ? "true" : "false",
             (unsigned long)g_activations.load(),
             (unsigned long)g_script_scans_completed.load(),
             (unsigned long)g_compile_execute_overlap_scans.load(),
             g_pause_script_for_compile.load() ? "true" : "false",
             (unsigned long)g_paused_scan_skips.load(),
             (unsigned long)g_total_pause_windows.load(),
             (unsigned long long)g_last_pause_us.load(),
             (unsigned long)g_run_scan_us_max.load(),
             (unsigned long)g_run_scan_us_last.load(),
             (unsigned long)g_run_scan_us_ema.load(),
             (unsigned long)g_run_scan_us_window_avg.load(),
             (unsigned long)g_run_scan_us_window_max.load(),
             (unsigned long)g_run_scan_us_window_min.load(),
             (unsigned long)g_run_scan_us_window_count.load(),
             (unsigned long)g_run_scan_us_window_over_2500.load(),
             (unsigned long)g_run_scan_us_window_over_5000.load(),
             (unsigned long)g_retired_destroy_us_max.load(),
             (unsigned long long)g_last_compile_us.load(),
             (unsigned long)g_last_heap_before.load(),
             (unsigned long)g_last_heap_after.load(),
             (unsigned long)g_last_heap_min.load(),
             (unsigned long)g_last_internal_before.load(),
             (unsigned long)g_last_internal_after.load(),
             (unsigned long)g_last_internal_min.load(),
             (unsigned long)g_last_psram_before.load(),
             (unsigned long)g_last_psram_after.load(),
             (unsigned long)g_last_psram_min.load(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned long)g_last_compiler_stack_hwm_words.load(),
             (unsigned long)g_total_compile_requests.load(),
             (unsigned long)g_total_compile_ok.load(),
             (unsigned long)g_total_compile_failed.load(),
             (unsigned long)g_total_compile_rejected.load(),
             escaped);
}
