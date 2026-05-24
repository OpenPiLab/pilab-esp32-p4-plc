#include "plc_tags.hpp"
#include "plc_io.hpp"
#include "plc_filesystem.hpp"
#include "script_engine.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <angelscript.h>

static const char* TAG = "PLC_TAGS";
static const char* TAGS_FILE_PATH = PLC_FS_MOUNT_POINT "/tags/tags.JSON";
static const char* TAGS_FILE_TMP_PATH = PLC_FS_MOUNT_POINT "/tags/tags.JSON.tmp";

struct RuntimeTag {
    char name[PLC_TAG_NAME_MAX];
    PlcTagType type;
    bool writable;
    bool retentive;
    bool hmi_visible;
    bool script_visible;
    char description[PLC_TAG_DESC_MAX];
    float min_value;
    float max_value;
    char units[16];
    union {
        bool b;
        int32_t i;
        float f;
    } value;
    // Registry/default value used by /api/tags and flash saves. This is
    // intentionally separate from the live runtime value so HMI writes do not
    // silently change the user's intended startup/initial value.
    union {
        bool b;
        int32_t i;
        float f;
    } initial_value;
};

static void tag_copy_current_to_initial(RuntimeTag& t)
{
    if (t.type == PLC_TAG_BOOL) t.initial_value.b = t.value.b;
    else if (t.type == PLC_TAG_INT) t.initial_value.i = t.value.i;
    else t.initial_value.f = t.value.f;
}

static void tag_copy_initial_to_current(RuntimeTag& t)
{
    if (t.type == PLC_TAG_BOOL) t.value.b = t.initial_value.b;
    else if (t.type == PLC_TAG_INT) t.value.i = t.initial_value.i;
    else t.value.f = t.initial_value.f;
}

static RuntimeTag g_tags[PLC_TAG_MAX_COUNT];
static size_t g_tag_count = 0;
static SemaphoreHandle_t g_tags_mutex = nullptr;
static bool g_loaded = false;

static void set_err(char* err, size_t err_len, const char* msg)
{
    if (err && err_len) snprintf(err, err_len, "%s", msg ? msg : "");
}

static const char* type_to_string(PlcTagType t)
{
    switch (t) {
        case PLC_TAG_BOOL: return "bool";
        case PLC_TAG_INT: return "int";
        case PLC_TAG_FLOAT: return "float";
        default: return "unknown";
    }
}

static bool string_to_type(const char* s, PlcTagType* out)
{
    if (!s || !out) return false;
    if (strcmp(s, "bool") == 0) { *out = PLC_TAG_BOOL; return true; }
    if (strcmp(s, "int") == 0) { *out = PLC_TAG_INT; return true; }
    if (strcmp(s, "float") == 0) { *out = PLC_TAG_FLOAT; return true; }
    return false;
}

static bool is_reserved_word(const char* name)
{
    if (!name) return true;
    const char* words[] = {
        "scan", "Scan", "true", "false", "bool", "int", "float", "void", "uint",
        "if", "else", "for", "while", "return", "break", "continue", "class", "string",
        nullptr
    };
    for (int i = 0; words[i]; ++i) if (strcmp(name, words[i]) == 0) return true;
    return false;
}

static bool all_digits(const char* s)
{
    if (!s || !*s) return false;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return false;
        ++s;
    }
    return true;
}

static bool is_exact_runtime_io_name(const char* name)
{
    if (!name) return false;

    // Reserve only the exact PLC process-image names. Descriptive user tags
    // such as I0_Motor, I1_Start, and Q0_Motor are intentionally allowed.
    if ((name[0] == 'I' || name[0] == 'Q') && all_digits(name + 1)) return true;
    return false;
}


static bool is_legacy_demo_analog_tag_name(const char* name)
{
    // Older demo builds created AI0..AI3 and AO0..AO3 as simulated analog
    // tags. The current PLC has no onboard analog process image, so remove
    // these exact legacy names during tag-list import / file migration.
    // Descriptive user tags such as TankLevel, UserAnalog0, or HMI_Setpoint
    // should be used instead.
    if (!name) return false;
    if ((strncmp(name, "AI", 2) == 0 || strncmp(name, "AO", 2) == 0) &&
        name[2] >= '0' && name[2] <= '3' && name[3] == '\0') {
        return true;
    }
    return false;
}

static bool is_plc_runtime_system_tag_name(const char* name)
{
    // PLC_* is reserved for firmware-provided runtime diagnostics / timing
    // globals such as PLC_DeltaTimeUs and PLC_ScanOverrunCount. These tags are
    // returned by GET /api/tags for discoverability, but they are system-owned.
    return name && strncmp(name, "PLC_", 4) == 0;
}

static bool is_pilab_monitor_tag_name(const char* name)
{
    return name && strncmp(name, "__obj_", 6) == 0;
}

static bool is_system_tag_name(const char* name)
{
    return is_exact_runtime_io_name(name) || is_plc_runtime_system_tag_name(name) || is_pilab_monitor_tag_name(name);
}

static bool is_direct_script_runtime_global(const char* name)
{
    // These are registered directly by script_engine.cpp against live timing
    // variables, not through plc_tags_register_angelscript_globals().
    return name && (
        strcmp(name, "PLC_DeltaTimeUs") == 0 ||
        strcmp(name, "PLC_DeltaTimeMs") == 0 ||
        strcmp(name, "PLC_DeltaTimeSeconds") == 0 ||
        strcmp(name, "PLC_ScanActualPeriodUs") == 0 ||
        strcmp(name, "PLC_ScanBudgetUs") == 0
    );
}

static bool json_script_visible_for_tag(const RuntimeTag& t)
{
    return t.script_visible || is_direct_script_runtime_global(t.name);
}

static bool should_skip_imported_runtime_tag(const char* name)
{
    // /api/tags GET includes read-only runtime/system names so users can
    // discover them. If that same list is posted back, those system names
    // should be ignored rather than rejected or persisted in the user tag table.
    return is_system_tag_name(name) || is_legacy_demo_analog_tag_name(name);
}

bool plc_tags_is_valid_name(const char* name, char* err, size_t err_len)
{
    if (!name || !name[0]) { set_err(err, err_len, "Tag name is empty"); return false; }
    size_t n = strlen(name);
    if (n >= PLC_TAG_NAME_MAX) { set_err(err, err_len, "Tag name is too long"); return false; }
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) {
        set_err(err, err_len, "Tag name must start with A-Z, a-z, or _"); return false;
    }
    for (size_t i = 1; i < n; ++i) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) {
            set_err(err, err_len, "Tag name may only contain A-Z, a-z, 0-9, and _"); return false;
        }
    }
    if (is_reserved_word(name)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Invalid tag '%s': this name is an AngelScript/PLC keyword", name);
        set_err(err, err_len, msg);
        return false;
    }
    if (is_exact_runtime_io_name(name)) {
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "Invalid tag '%s': exact runtime digital I/O names such as I0 and Q0 are reserved. "
                 "Use a descriptive user tag such as %s_Motor or HMI_%s instead.",
                 name, name, name);
        set_err(err, err_len, msg);
        return false;
    }
    if (is_plc_runtime_system_tag_name(name)) {
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "Invalid tag '%s': names starting with PLC_ are reserved for firmware runtime diagnostics/timing globals. "
                 "Use a user prefix such as HMI_, M_, or User_ instead.",
                 name);
        set_err(err, err_len, msg);
        return false;
    }
    set_err(err, err_len, "");
    return true;
}

static int find_tag_index_nolock(const char* name)
{
    for (size_t i = 0; i < g_tag_count; ++i) if (strcmp(g_tags[i].name, name) == 0) return (int)i;
    return -1;
}

static void tag_to_info(const RuntimeTag& t, PlcTagInfo* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", t.name);
    out->type = t.type;
    out->writable = t.writable;
    out->retentive = t.retentive;
    out->hmi_visible = t.hmi_visible;
    out->script_visible = t.script_visible;
    snprintf(out->description, sizeof(out->description), "%s", t.description);
    out->min_value = t.min_value;
    out->max_value = t.max_value;
    snprintf(out->units, sizeof(out->units), "%s", t.units);
    if (t.type == PLC_TAG_BOOL) out->value.b = t.value.b;
    else if (t.type == PLC_TAG_INT) out->value.i = t.value.i;
    else out->value.f = t.value.f;
}


static void add_bool_tag_nolock(const char* name, bool value, const char* desc)
{
    if (!name || find_tag_index_nolock(name) >= 0 || g_tag_count >= PLC_TAG_MAX_COUNT) return;
    RuntimeTag& t = g_tags[g_tag_count++];
    memset(&t, 0, sizeof(t));
    snprintf(t.name, sizeof(t.name), "%.*s", (int)(PLC_TAG_NAME_MAX - 1), name);
    t.type = PLC_TAG_BOOL;
    t.writable = true;
    t.retentive = true;
    t.hmi_visible = true;
    t.script_visible = true;
    snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), desc ? desc : "");
    t.value.b = value;
    t.initial_value.b = value;
}


static void add_int_tag_nolock(const char* name, int32_t value, const char* units, const char* desc, bool writable, bool retentive, bool hmi_visible, bool script_visible)
{
    if (!name || find_tag_index_nolock(name) >= 0 || g_tag_count >= PLC_TAG_MAX_COUNT) return;
    RuntimeTag& t = g_tags[g_tag_count++];
    memset(&t, 0, sizeof(t));
    snprintf(t.name, sizeof(t.name), "%.*s", (int)(PLC_TAG_NAME_MAX - 1), name);
    t.type = PLC_TAG_INT;
    t.writable = writable;
    t.retentive = retentive;
    t.hmi_visible = hmi_visible;
    t.script_visible = script_visible;
    snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), desc ? desc : "");
    snprintf(t.units, sizeof(t.units), "%.*s", (int)(sizeof(t.units) - 1), units ? units : "");
    t.value.i = value;
    t.initial_value.i = value;
}

static void add_float_tag_nolock(const char* name, float value, const char* units, const char* desc, bool writable, bool retentive, bool hmi_visible, bool script_visible)
{
    if (!name || find_tag_index_nolock(name) >= 0 || g_tag_count >= PLC_TAG_MAX_COUNT) return;
    RuntimeTag& t = g_tags[g_tag_count++];
    memset(&t, 0, sizeof(t));
    snprintf(t.name, sizeof(t.name), "%.*s", (int)(PLC_TAG_NAME_MAX - 1), name);
    t.type = PLC_TAG_FLOAT;
    t.writable = writable;
    t.retentive = retentive;
    t.hmi_visible = hmi_visible;
    t.script_visible = script_visible;
    snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), desc ? desc : "");
    snprintf(t.units, sizeof(t.units), "%.*s", (int)(sizeof(t.units) - 1), units ? units : "");
    t.value.f = value;
    t.initial_value.f = value;
}

static void ensure_runtime_diagnostic_tags_nolock()
{
    // System/runtime diagnostics. These are visible in the tag list so users can
    // discover them, but they are firmware-owned and non-retentive.
    //
    // Most PLC_Scan* diagnostic values are also registered as AngelScript globals
    // through the tag registry. A few timing globals are registered directly by
    // script_engine.cpp against live timing variables; those remain
    // script_visible=false internally to avoid duplicate RegisterGlobalProperty()
    // calls, but plc_tags_get_json() reports them as script_visible=true.
    add_int_tag_nolock("PLC_ScanCoalescedCount", 0, "scans", "System: script scan notifications coalesced/skipped", false, false, true, true);
    add_int_tag_nolock("PLC_ScanOverrunCount", 0, "scans", "System: script scans that exceeded budget", false, false, true, true);
    add_bool_tag_nolock("PLC_ScanOverrunActive", false, "System: script scan is currently over budget");
    int idx = find_tag_index_nolock("PLC_ScanOverrunActive"); if (idx >= 0) { g_tags[idx].writable = false; g_tags[idx].retentive = false; g_tags[idx].script_visible = true; }
    add_bool_tag_nolock("PLC_ScanFaultActive", false, "System: script scan policy fault active");
    idx = find_tag_index_nolock("PLC_ScanFaultActive"); if (idx >= 0) { g_tags[idx].writable = false; g_tags[idx].retentive = false; g_tags[idx].script_visible = true; }
    add_int_tag_nolock("PLC_ScanExecutionTimeUs", 0, "us", "System: last script scan execution time", false, false, true, true);
    add_float_tag_nolock("PLC_ScanLoadPercent", 0.0f, "%", "System: script execution time divided by budget", false, false, true, true);

    // Fine-grained PLC task timing diagnostics. These are firmware-owned tags
    // intended for HMI trend/debug views when chasing jitter or cache stalls.
    add_int_tag_nolock("PLC_IoTaskWorkLastUs", 0, "us", "System: last 1 ms PLC I/O task work duration", false, false, true, true);
    add_int_tag_nolock("PLC_IoTaskWorkMaxUs", 0, "us", "System: max 1 ms PLC I/O task work duration since boot", false, false, true, true);
    add_int_tag_nolock("PLC_IoTaskPeriodLastUs", 0, "us", "System: last 1 ms PLC I/O task period", false, false, true, true);
    add_int_tag_nolock("PLC_IoTaskPeriodMaxUs", 0, "us", "System: max 1 ms PLC I/O task period since boot", false, false, true, true);
    add_int_tag_nolock("PLC_IoTaskMissedCount", 0, "ticks", "System: accumulated missed 1 ms PLC timer notifications", false, false, true, true);

    add_int_tag_nolock("PLC_AsTaskTotalLastUs", 0, "us", "System: last AngelScript task total duration", false, false, true, true);
    add_int_tag_nolock("PLC_AsTaskTotalMaxUs", 0, "us", "System: max AngelScript task total duration since boot", false, false, true, true);
    add_int_tag_nolock("PLC_AsTaskPeriodLastUs", 0, "us", "System: last AngelScript task period", false, false, true, true);
    add_int_tag_nolock("PLC_AsTaskPeriodMaxUs", 0, "us", "System: max AngelScript task period since boot", false, false, true, true);
    add_int_tag_nolock("PLC_AsTaskCoalescedLast", 0, "scans", "System: script notifications coalesced on last scan", false, false, true, true);

    add_int_tag_nolock("PLC_AsVmLastUs", 0, "us", "System: last AngelScript VM ctx->Execute duration", false, false, true, true);
    add_int_tag_nolock("PLC_AsVmMaxUs", 0, "us", "System: max AngelScript VM ctx->Execute duration since boot", false, false, true, true);
    add_int_tag_nolock("PLC_AsVmEmaUs", 0, "us", "System: AngelScript VM execution exponential moving average", false, false, true, true);
    add_int_tag_nolock("PLC_AsVmWindowAvgUs", 0, "us", "System: AngelScript VM 1 second window average", false, false, true, true);
    add_int_tag_nolock("PLC_AsVmWindowMaxUs", 0, "us", "System: AngelScript VM 1 second window maximum", false, false, true, true);

    add_int_tag_nolock("PLC_CacheBuildLastUs", 0, "us", "System: last /api/plc_data cache build total duration", false, false, true, true);
    add_int_tag_nolock("PLC_CacheBuildMaxUs", 0, "us", "System: max /api/plc_data cache build duration since boot", false, false, true, true);
    add_int_tag_nolock("PLC_CacheIoSnapshotUs", 0, "us", "System: cache build I/O snapshot duration", false, false, true, true);
    add_int_tag_nolock("PLC_CacheScriptNameUs", 0, "us", "System: cache build active script name lookup duration", false, false, true, true);
    add_int_tag_nolock("PLC_CacheTagCopyUs", 0, "us", "System: cache build tag table copy duration", false, false, true, true);
    add_int_tag_nolock("PLC_CacheJsonFormatUs", 0, "us", "System: cache build JSON formatting duration", false, false, true, true);
    add_int_tag_nolock("PLC_CachePublishWaitUs", 0, "us", "System: cache task wait time for cache mutex", false, false, true, true);
    add_int_tag_nolock("PLC_CachePublishCopyUs", 0, "us", "System: cache task cache-buffer copy/publish duration", false, false, true, true);
    add_int_tag_nolock("PLC_CachePointCount", 0, "points", "System: cached PLC data point count", false, false, true, true);
    add_int_tag_nolock("PLC_CacheBytes", 0, "bytes", "System: cached PLC data JSON byte count", false, false, true, true);
    add_int_tag_nolock("PLC_CacheVersion", 0, "builds", "System: cached PLC data build version", false, false, true, true);

    add_int_tag_nolock("PLC_HttpPlcDataSendLastUs", 0, "us", "System: last /api/plc_data HTTP send duration", false, false, true, true);
    add_int_tag_nolock("PLC_HttpPlcDataSendMaxUs", 0, "us", "System: max /api/plc_data HTTP send duration since boot", false, false, true, true);

    add_int_tag_nolock("PLC_TagDataBuildLastUs", 0, "us", "System: last compact /api/tag_data build duration", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataBuildMaxUs", 0, "us", "System: max compact /api/tag_data build duration since boot", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataTagCopyUs", 0, "us", "System: compact tag-data value copy duration", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataJsonFormatUs", 0, "us", "System: compact tag-data JSON formatting duration", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataPublishWaitUs", 0, "us", "System: compact tag-data cache publish wait duration", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataPublishCopyUs", 0, "us", "System: compact tag-data cache publish copy duration", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataBytes", 0, "bytes", "System: compact tag-data JSON byte count", false, false, true, true);
    add_int_tag_nolock("PLC_TagDataVersion", 0, "builds", "System: compact tag-data build version", false, false, true, true);
    add_int_tag_nolock("PLC_HttpTagDataSendLastUs", 0, "us", "System: last /api/tag_data HTTP send duration", false, false, true, true);
    add_int_tag_nolock("PLC_HttpTagDataSendMaxUs", 0, "us", "System: max /api/tag_data HTTP send duration since boot", false, false, true, true);

    add_int_tag_nolock("PLC_CompileLastUs", 0, "us", "System: last AngelScript compile duration", false, false, true, true);
    add_int_tag_nolock("PLC_CompileMaxUs", 0, "us", "System: max AngelScript compile duration since boot", false, false, true, true);
    add_int_tag_nolock("PLC_CleanupDestroyLastUs", 0, "us", "System: last retired script cleanup destroy duration", false, false, true, true);
    add_int_tag_nolock("PLC_CleanupDestroyMaxUs", 0, "us", "System: max retired script cleanup destroy duration since boot", false, false, true, true);

    // Direct script globals registered in script_engine.cpp.
    add_int_tag_nolock("PLC_DeltaTimeUs", 5000, "us", "System: clamped elapsed time passed to script", false, false, true, false);
    add_float_tag_nolock("PLC_DeltaTimeMs", 5.0f, "ms", "System: clamped elapsed time passed to script", false, false, true, false);
    add_float_tag_nolock("PLC_DeltaTimeSeconds", 0.005f, "s", "System: clamped elapsed time passed to script", false, false, true, false);
    add_int_tag_nolock("PLC_ScanActualPeriodUs", 0, "us", "System: elapsed time between script executions", false, false, true, false);
    add_int_tag_nolock("PLC_ScanBudgetUs", 5000, "us", "System: configured script scan budget", false, false, true, false);
}

static void ensure_compatibility_tags_nolock()
{
    // Compatibility/default user bit used by the current HMI examples and test scripts.
    // Do not overwrite it if the user already defined it in the tag file; only add it when missing.
    add_bool_tag_nolock("Start", false, "HMI start command bit");
}

static void add_default_tags_nolock()
{
    g_tag_count = 0;
    auto add_bool = [](const char* name, bool value, const char* desc) {
        if (g_tag_count >= PLC_TAG_MAX_COUNT) return;
        RuntimeTag& t = g_tags[g_tag_count++];
        memset(&t, 0, sizeof(t));
        snprintf(t.name, sizeof(t.name), "%s", name);
        t.type = PLC_TAG_BOOL; t.writable = true; t.retentive = true; t.hmi_visible = true; t.script_visible = true;
        snprintf(t.description, sizeof(t.description), "%s", desc);
        t.value.b = value;
        t.initial_value.b = value;
    };
    auto add_float = [](const char* name, float value, const char* units, const char* desc) {
        if (g_tag_count >= PLC_TAG_MAX_COUNT) return;
        RuntimeTag& t = g_tags[g_tag_count++];
        memset(&t, 0, sizeof(t));
        snprintf(t.name, sizeof(t.name), "%s", name);
        t.type = PLC_TAG_FLOAT; t.writable = true; t.retentive = true; t.hmi_visible = true; t.script_visible = true;
        t.min_value = 0.0f; t.max_value = 100.0f;
        snprintf(t.units, sizeof(t.units), "%s", units);
        snprintf(t.description, sizeof(t.description), "%s", desc);
        t.value.f = value;
        t.initial_value.f = value;
    };
    // Default release/demo tags used by the Vue HMI starter screen.
    // These are writable user tags, not physical input pins. The default
    // AngelScript program maps them to Q0..Q3 so the demo works without wiring.
    add_bool("HMI_I0", false, "Demo HMI switch 0 mapped to Q0 by the default script");
    add_bool("HMI_I1", false, "Demo HMI switch 1 mapped to Q1 by the default script");
    add_bool("HMI_I2", false, "Demo HMI switch 2 mapped to Q2 by the default script");
    add_bool("HMI_I3", false, "Demo HMI switch 3 mapped to Q3 by the default script");

    add_bool("AutoMode", false, "Example automatic mode memory bit");
    add_bool("PumpStart", false, "Example HMI pump command bit");
    add_float("TankSetpoint", 50.0f, "%", "Example tank setpoint");
}

static void json_escape_append(char*& p, size_t& rem, const char* s)
{
    if (!s) return;
    while (*s && rem > 2) {
        char c = *s++;
        if (c == '"' || c == '\\') { int n = snprintf(p, rem, "\\%c", c); p += n; rem -= n; }
        else if ((unsigned char)c < 32) { int n = snprintf(p, rem, " "); p += n; rem -= n; }
        else { *p++ = c; *p = 0; rem--; }
    }
}


static size_t runtime_io_virtual_tag_count()
{
    return PLC_DI_COUNT + PLC_DO_COUNT;
}

static void append_virtual_tag_json(char*& p, size_t& rem, bool& first, const char* name,
                                    const char* type, bool writable, bool hmi_visible,
                                    bool script_visible, bool system, const char* desc, const char* units,
                                    float min_value, float max_value, const char* value_literal)
{
    if (rem < 96) return;
    int n = snprintf(p, rem, "%s{\"name\":\"%s\",\"type\":\"%s\",\"system\":%s,\"writable\":%s,\"retentive\":false,\"hmi_visible\":%s,\"script_visible\":%s,\"description\":\"",
                     first ? "" : ",", name, type, system ? "true" : "false", writable ? "true" : "false", hmi_visible ? "true" : "false", script_visible ? "true" : "false");
    p += n; rem = (n < (int)rem) ? rem - n : 0;
    json_escape_append(p, rem, desc ? desc : "");
    n = snprintf(p, rem, "\",\"units\":\""); p += n; rem = (n < (int)rem) ? rem - n : 0;
    json_escape_append(p, rem, units ? units : "");
    n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%s}", (double)min_value, (double)max_value, value_literal ? value_literal : "false");
    p += n; rem = (n < (int)rem) ? rem - n : 0;
    first = false;
}

static void append_runtime_io_tags_json(char*& p, size_t& rem, bool& first)
{
    PlcIoSnapshot snap = {};
    plc_io_get_snapshot(&snap);
    char name[12];
    char desc[80];
    char value[32];

    for (uint32_t i = 0; i < PLC_DI_COUNT; ++i) {
        snprintf(name, sizeof(name), "I%lu", (unsigned long)i);
        snprintf(desc, sizeof(desc), "Physical digital input %s from the PLC process image", name);
        snprintf(value, sizeof(value), "%s", snap.debounced_di[i] ? "true" : "false");
        append_virtual_tag_json(p, rem, first, name, "bool", false, true, true, true, desc, "", 0.0f, 1.0f, value);
    }
    for (uint32_t i = 0; i < PLC_DO_COUNT; ++i) {
        snprintf(name, sizeof(name), "Q%lu", (unsigned long)i);
        snprintf(desc, sizeof(desc), "Physical digital output %s command from the PLC process image", name);
        snprintf(value, sizeof(value), "%s", snap.do_cmd[i] ? "true" : "false");
        append_virtual_tag_json(p, rem, first, name, "bool", false, true, true, true, desc, "", 0.0f, 1.0f, value);
    }
}

void plc_tags_get_json(char* out, size_t out_len)
{
    if (!out || !out_len) return;
    out[0] = 0;
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    const size_t reported_count = g_tag_count + runtime_io_virtual_tag_count();
    char* p = out;
    size_t rem = out_len;
    int n = snprintf(p, rem, "{\"max_count\":%u,\"count\":%u,\"tags\":[", (unsigned)PLC_TAG_MAX_COUNT, (unsigned)reported_count);
    p += n; rem = (n < (int)rem) ? rem - n : 0;
    bool first = true;
    for (size_t i = 0; i < g_tag_count && rem > 64; ++i) {
        RuntimeTag& t = g_tags[i];
        n = snprintf(p, rem, "%s{\"name\":\"", first ? "" : ","); p += n; rem = (n < (int)rem) ? rem - n : 0;
        json_escape_append(p, rem, t.name);
        const bool system = is_system_tag_name(t.name);
        const bool script_visible = json_script_visible_for_tag(t);
        n = snprintf(p, rem, "\",\"type\":\"%s\",\"system\":%s,\"writable\":%s,\"retentive\":%s,\"hmi_visible\":%s,\"script_visible\":%s,\"description\":\"",
                     type_to_string(t.type), system?"true":"false", t.writable?"true":"false", t.retentive?"true":"false", t.hmi_visible?"true":"false", script_visible?"true":"false");
        p += n; rem = (n < (int)rem) ? rem - n : 0;
        json_escape_append(p, rem, t.description);
        n = snprintf(p, rem, "\",\"units\":\""); p += n; rem = (n < (int)rem) ? rem - n : 0;
        json_escape_append(p, rem, t.units);
        if (t.type == PLC_TAG_BOOL) {
            const bool out_value = system ? t.value.b : t.initial_value.b;
            n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%s}", (double)t.min_value, (double)t.max_value, out_value ? "true" : "false");
        } else if (t.type == PLC_TAG_INT) {
            const int32_t out_value = system ? t.value.i : t.initial_value.i;
            n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%ld}", (double)t.min_value, (double)t.max_value, (long)out_value);
        } else {
            const float out_value = system ? t.value.f : t.initial_value.f;
            n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%.6g}", (double)t.min_value, (double)t.max_value, (double)out_value);
        }
        p += n; rem = (n < (int)rem) ? rem - n : 0;
        first = false;
    }
    append_runtime_io_tags_json(p, rem, first);
    if (rem < 3) {
        xSemaphoreGive(g_tags_mutex);
        snprintf(out, out_len, "{\"error\":\"tag JSON buffer too small\"}");
        return;
    }
    snprintf(p, rem, "]}");
    xSemaphoreGive(g_tags_mutex);
}

static bool write_runtime_tag_object_json(char*& p, size_t& rem, const RuntimeTag& t, bool first)
{
    if (is_system_tag_name(t.name) || is_legacy_demo_analog_tag_name(t.name)) return true;

    int n = snprintf(p, rem,
                     "%s{\"name\":\"",
                     first ? "" : ",");
    if (n < 0 || (size_t)n >= rem) return false;
    p += n; rem -= (size_t)n;

    json_escape_append(p, rem, t.name);
    n = snprintf(p, rem,
                 "\",\"type\":\"%s\",\"writable\":%s,\"retentive\":%s,\"hmi_visible\":%s,\"script_visible\":%s,\"description\":\"",
                 type_to_string(t.type),
                 t.writable ? "true" : "false",
                 t.retentive ? "true" : "false",
                 t.hmi_visible ? "true" : "false",
                 t.script_visible ? "true" : "false");
    if (n < 0 || (size_t)n >= rem) return false;
    p += n; rem -= (size_t)n;

    json_escape_append(p, rem, t.description);
    n = snprintf(p, rem, "\",\"units\":\"");
    if (n < 0 || (size_t)n >= rem) return false;
    p += n; rem -= (size_t)n;

    json_escape_append(p, rem, t.units);
    if (t.type == PLC_TAG_BOOL) {
        n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%s}",
                     (double)t.min_value, (double)t.max_value, t.initial_value.b ? "true" : "false");
    } else if (t.type == PLC_TAG_INT) {
        n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%ld}",
                     (double)t.min_value, (double)t.max_value, (long)t.initial_value.i);
    } else {
        n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%.6g}",
                     (double)t.min_value, (double)t.max_value, (double)t.initial_value.f);
    }
    if (n < 0 || (size_t)n >= rem) return false;
    p += n; rem -= (size_t)n;
    return true;
}

static bool save_to_tags_file_nolock()
{
    const size_t json_cap = 32768;
    char* json = (char*)malloc(json_cap);
    if (!json) return false;

    char* p = json;
    size_t rem = json_cap;
    int n = snprintf(p, rem, "{\"tags\":[");
    if (n < 0 || (size_t)n >= rem) { free(json); return false; }
    p += n; rem -= (size_t)n;

    bool first_saved = true;
    for (size_t i = 0; i < g_tag_count; ++i) {
        RuntimeTag& t = g_tags[i];
        if (is_system_tag_name(t.name) || is_legacy_demo_analog_tag_name(t.name)) continue;
        if (!write_runtime_tag_object_json(p, rem, t, first_saved)) { free(json); return false; }
        first_saved = false;
    }

    if (rem < 3) { free(json); return false; }
    n = snprintf(p, rem, "]}");
    if (n < 0 || (size_t)n >= rem) { free(json); return false; }

    // The /tags directory is normally created at boot by plc_filesystem_init().
    // Try to create it here too so tag save is robust if this module is reused.
    mkdir(PLC_FS_MOUNT_POINT "/tags", 0775);

    FILE* f = fopen(TAGS_FILE_TMP_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed opening %s for write: errno=%d (%s)", TAGS_FILE_TMP_PATH, errno, strerror(errno));
        free(json);
        return false;
    }

    const size_t len = strlen(json);
    bool ok = fwrite(json, 1, len, f) == len;
    if (ok) ok = fflush(f) == 0;
    if (ok) {
        int fd = fileno(f);
        if (fd >= 0) fsync(fd);
    }
    if (fclose(f) != 0) ok = false;

    if (ok) {
        unlink(TAGS_FILE_PATH);
        if (rename(TAGS_FILE_TMP_PATH, TAGS_FILE_PATH) != 0) {
            ESP_LOGE(TAG, "Failed renaming %s to %s: errno=%d (%s)", TAGS_FILE_TMP_PATH, TAGS_FILE_PATH, errno, strerror(errno));
            ok = false;
        }
    }

    if (!ok) unlink(TAGS_FILE_TMP_PATH);
    free(json);
    return ok;
}

static char* read_tags_file(size_t* out_len)
{
    if (out_len) *out_len = 0;
    struct stat st = {};
    if (stat(TAGS_FILE_PATH, &st) != 0) {
        if (errno != ENOENT) ESP_LOGW(TAG, "stat failed for %s: errno=%d (%s)", TAGS_FILE_PATH, errno, strerror(errno));
        return nullptr;
    }
    if (st.st_size <= 2 || st.st_size >= 32768) {
        ESP_LOGW(TAG, "Ignoring %s: invalid size %ld", TAGS_FILE_PATH, (long)st.st_size);
        return nullptr;
    }
    FILE* f = fopen(TAGS_FILE_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Failed opening %s: errno=%d (%s)", TAGS_FILE_PATH, errno, strerror(errno));
        return nullptr;
    }
    char* buf = (char*)malloc((size_t)st.st_size + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if (n != (size_t)st.st_size) {
        free(buf);
        ESP_LOGW(TAG, "Short read from %s", TAGS_FILE_PATH);
        return nullptr;
    }
    buf[n] = 0;
    if (out_len) *out_len = n;
    return buf;
}

static const char* skip_ws(const char* p) { while (p && *p && isspace((unsigned char)*p)) ++p; return p; }
static bool find_string_field(const char* obj, const char* key, char* out, size_t out_len)
{
    if (!obj || !key || !out || !out_len) return false;
    out[0] = 0;
    char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(obj, pat); if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p = skip_ws(p + 1);
    if (*p != '"') return false;
    ++p;
    size_t j = 0;
    while (*p && *p != '"' && j + 1 < out_len) {
        if (*p == '\\' && p[1]) ++p;
        out[j++] = *p++;
    }
    out[j] = 0;
    return true;
}
static bool find_bool_field(const char* obj, const char* key, bool def)
{
    char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(obj, pat); if (!p) return def;
    p = strchr(p + strlen(pat), ':'); if (!p) return def; p = skip_ws(p + 1);
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return def;
}
static float find_float_field(const char* obj, const char* key, float def)
{
    char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(obj, pat); if (!p) return def;
    p = strchr(p + strlen(pat), ':'); if (!p) return def; p = skip_ws(p + 1);
    return strtof(p, nullptr);
}

static void update_existing_tag_from_upload_nolock(RuntimeTag& dst, const RuntimeTag& src)
{
    // IMPORTANT: Do not replace, delete, or reorder RuntimeTag storage while a
    // compiled AngelScript module may still have RegisterGlobalProperty()
    // pointers into dst.value. Only update fields in place.
    dst.writable = src.writable;
    dst.retentive = src.retentive;
    dst.hmi_visible = src.hmi_visible;
    dst.script_visible = src.script_visible;
    snprintf(dst.description, sizeof(dst.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), src.description);
    snprintf(dst.units, sizeof(dst.units), "%.*s", (int)(sizeof(dst.units) - 1), src.units);
    dst.min_value = src.min_value;
    dst.max_value = src.max_value;

    // A full tag upload intentionally updates both the saved initial value and
    // the current live value. Passive /api/tags refreshes do not do this; they
    // only read initial_value.
    if (dst.type == PLC_TAG_BOOL) { dst.initial_value.b = src.initial_value.b; dst.value.b = src.value.b; }
    else if (dst.type == PLC_TAG_INT) { dst.initial_value.i = src.initial_value.i; dst.value.i = src.value.i; }
    else { dst.initial_value.f = src.initial_value.f; dst.value.f = src.value.f; }
}

static bool append_uploaded_tag_nolock(const RuntimeTag& src, char* err, size_t err_len)
{
    if (g_tag_count >= PLC_TAG_MAX_COUNT) {
        set_err(err, err_len, "Tag table full");
        return false;
    }
    RuntimeTag& dst = g_tags[g_tag_count++];
    memset(&dst, 0, sizeof(dst));
    dst = src;
    return true;
}

static bool plc_tags_load_json_internal(const char* json, bool save_file, char* err, size_t err_len)
{
    if (!json) { set_err(err, err_len, "No JSON body"); return false; }
    if (!g_tags_mutex) plc_tags_init();

    // This function is called from the ESP-IDF httpd task. Do not put the
    // whole candidate tag table on that task's stack; it is large enough to
    // trip the stack protector on ESP32-P4 when /api/tags is posted.
    RuntimeTag* uploaded_tags = (RuntimeTag*)calloc(PLC_TAG_MAX_COUNT, sizeof(RuntimeTag));
    if (!uploaded_tags) {
        set_err(err, err_len, "Out of memory allocating tag table");
        return false;
    }
    size_t uploaded_count = 0;

    const char* p = json;
    while ((p = strchr(p, '{')) != nullptr) {
        const char* end = strchr(p, '}'); if (!end) break;
        size_t len = end - p + 1;
        if (len > 700) len = 700;
        char obj[704]; memcpy(obj, p, len); obj[len] = 0; p = end + 1;

        char name[PLC_TAG_NAME_MAX] = {}; char type_s[16] = {};
        if (!find_string_field(obj, "name", name, sizeof(name))) continue;
        if (!find_string_field(obj, "type", type_s, sizeof(type_s))) continue;
        if (should_skip_imported_runtime_tag(name)) {
            // Runtime/system tags are included in GET /api/tags for discovery,
            // but they are not user-defined tags. Ignore them when a full tag
            // list is posted back from the web UI or imported from a bundle.
            // This also migrates away old demo analog tags AI0..AI3/AO0..AO3
            // that may still be present in saved tag files from earlier firmware builds.
            continue;
        }
        char name_err[192];
        if (!plc_tags_is_valid_name(name, name_err, sizeof(name_err))) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Invalid tag '%s': %s", name, name_err);
            set_err(err, err_len, msg);
            free(uploaded_tags);
            return false;
        }
        PlcTagType type;
        if (!string_to_type(type_s, &type)) {
            char msg[192];
            snprintf(msg, sizeof(msg), "Invalid type '%s' for tag '%s'. Expected bool, int, or float", type_s, name);
            set_err(err, err_len, msg);
            free(uploaded_tags);
            return false;
        }
        if (uploaded_count >= PLC_TAG_MAX_COUNT) {
            set_err(err, err_len, "Too many tags in uploaded registry");
            free(uploaded_tags);
            return false;
        }
        for (size_t i = 0; i < uploaded_count; ++i) {
            if (strcmp(uploaded_tags[i].name, name) == 0) {
                char msg[192];
                snprintf(msg, sizeof(msg), "Duplicate tag name '%s' in submitted tag registry", name);
                set_err(err, err_len, msg);
                free(uploaded_tags);
                return false;
            }
        }

        RuntimeTag& t = uploaded_tags[uploaded_count++];
        memset(&t, 0, sizeof(t));
        snprintf(t.name, sizeof(t.name), "%s", name);
        t.type = type;
        t.writable = find_bool_field(obj, "writable", true);
        t.retentive = find_bool_field(obj, "retentive", true);
        t.hmi_visible = find_bool_field(obj, "hmi_visible", true);
        t.script_visible = find_bool_field(obj, "script_visible", true);
        find_string_field(obj, "description", t.description, sizeof(t.description));
        find_string_field(obj, "units", t.units, sizeof(t.units));
        t.min_value = find_float_field(obj, "min", 0.0f);
        t.max_value = find_float_field(obj, "max", 100.0f);
        if (type == PLC_TAG_BOOL) t.value.b = find_bool_field(obj, "value", false);
        else if (type == PLC_TAG_INT) t.value.i = (int32_t)find_float_field(obj, "value", 0.0f);
        else t.value.f = find_float_field(obj, "value", 0.0f);
        tag_copy_current_to_initial(t);
    }

    // First validate against the live table without mutating it. A live tag's
    // type cannot be changed because AngelScript may already have a global
    // property registered to the address and C++ type of its value storage.
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    size_t additions = 0;
    for (size_t i = 0; i < uploaded_count; ++i) {
        int idx = find_tag_index_nolock(uploaded_tags[i].name);
        if (idx >= 0) {
            if (g_tags[idx].type != uploaded_tags[i].type) {
                char msg[224];
                snprintf(msg, sizeof(msg),
                         "Cannot change type of live tag '%s' from %s to %s; create a new tag name instead",
                         uploaded_tags[i].name, type_to_string(g_tags[idx].type), type_to_string(uploaded_tags[i].type));
                xSemaphoreGive(g_tags_mutex);
                set_err(err, err_len, msg);
                free(uploaded_tags);
                return false;
            }
        } else {
            additions++;
        }
    }
    if (g_tag_count + additions > PLC_TAG_MAX_COUNT) {
        xSemaphoreGive(g_tags_mutex);
        set_err(err, err_len, "Tag table full; upload would exceed maximum live tag count");
        free(uploaded_tags);
        return false;
    }
    xSemaphoreGive(g_tags_mutex);

    // Pause the script scan while mutating live tag values/metadata. Existing
    // RuntimeTag storage is updated in place, so registered AngelScript pointers
    // remain valid; the pause avoids reading half-updated values/flags.
    if (g_loaded) script_engine_pause_for_runtime_update("tag registry upload");

    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    for (size_t i = 0; i < uploaded_count; ++i) {
        int idx = find_tag_index_nolock(uploaded_tags[i].name);
        if (idx >= 0) {
            update_existing_tag_from_upload_nolock(g_tags[idx], uploaded_tags[i]);
        } else {
            if (!append_uploaded_tag_nolock(uploaded_tags[i], err, err_len)) {
                xSemaphoreGive(g_tags_mutex);
                if (g_loaded) script_engine_resume_after_runtime_update();
                free(uploaded_tags);
                return false;
            }
        }
    }

    // Never delete tags here. Tags missing from the uploaded JSON remain alive
    // so any existing AngelScript RegisterGlobalProperty() pointers stay valid.
    ensure_runtime_diagnostic_tags_nolock();
    ensure_compatibility_tags_nolock();
    bool saved = save_file ? save_to_tags_file_nolock() : true;
    xSemaphoreGive(g_tags_mutex);

    if (g_loaded) script_engine_resume_after_runtime_update();

    free(uploaded_tags);
    if (!saved) ESP_LOGW(TAG, "Tags updated in RAM but LittleFS tag file save failed");
    set_err(err, err_len, saved ? "OK" : "Tags updated in RAM, LittleFS tag file save failed");
    return true;
}

bool plc_tags_load_json(const char* json, char* err, size_t err_len)
{
    return plc_tags_load_json_internal(json, true, err, err_len);
}

bool plc_tags_load_json_ram(const char* json, char* err, size_t err_len)
{
    return plc_tags_load_json_internal(json, false, err, err_len);
}

bool plc_tags_save_to_flash(char* err, size_t err_len)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    bool ok = save_to_tags_file_nolock();
    xSemaphoreGive(g_tags_mutex);
    set_err(err, err_len, ok ? "OK" : "LittleFS tag file save failed");
    return ok;
}

void plc_tags_init(void)
{
    if (!g_tags_mutex) g_tags_mutex = xSemaphoreCreateMutex();
    if (g_loaded || !g_tags_mutex) return;
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    if (!g_loaded) {
        add_default_tags_nolock();
        size_t tags_file_len = 0;
        char* buf = read_tags_file(&tags_file_len);
        if (buf) {
            xSemaphoreGive(g_tags_mutex);
            char err[160] = {};
            if (plc_tags_load_json_internal(buf, false, err, sizeof(err))) {
                ESP_LOGI(TAG, "Loaded tag registry from %s (%u bytes)", TAGS_FILE_PATH, (unsigned)tags_file_len);
            } else {
                ESP_LOGW(TAG, "Failed loading tag registry from %s: %s", TAGS_FILE_PATH, err);
            }
            free(buf);
            xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
        }
        ensure_runtime_diagnostic_tags_nolock();
        ensure_compatibility_tags_nolock();
        g_loaded = true;
        ESP_LOGI(TAG, "Tag registry initialized: %u tags", (unsigned)g_tag_count);
    }
    xSemaphoreGive(g_tags_mutex);
}

size_t plc_tags_get_count(void) { if (!g_tags_mutex) plc_tags_init(); xSemaphoreTake(g_tags_mutex, portMAX_DELAY); size_t n = g_tag_count; xSemaphoreGive(g_tags_mutex); return n; }

size_t plc_tags_copy_all(PlcTagInfo* out, size_t max_count)
{
    if (!out || !max_count) return 0;
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    size_t n = (g_tag_count < max_count) ? g_tag_count : max_count;
    for (size_t i = 0; i < n; ++i) tag_to_info(g_tags[i], &out[i]);
    xSemaphoreGive(g_tags_mutex);
    return n;
}

size_t plc_tags_copy_hmi_values(PlcTagValueInfo* out, size_t max_count)
{
    if (!out || !max_count) return 0;
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    size_t n = 0;
    for (size_t i = 0; i < g_tag_count && n < max_count; ++i) {
        const RuntimeTag& t = g_tags[i];
        if (!t.hmi_visible) continue;
        PlcTagValueInfo& v = out[n++];
        memset(&v, 0, sizeof(v));
        memcpy(v.name, t.name, sizeof(v.name));
        v.name[sizeof(v.name) - 1] = '\0';
        v.type = t.type;
        v.writable = t.writable;
        if (t.type == PLC_TAG_BOOL) v.value.b = t.value.b;
        else if (t.type == PLC_TAG_INT) v.value.i = t.value.i;
        else v.value.f = t.value.f;
    }
    xSemaphoreGive(g_tags_mutex);
    return n;
}

size_t plc_tags_copy_hmi_indexed_values(PlcTagIndexedValueInfo* out, size_t max_count)
{
    if (!out || !max_count) return 0;
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    size_t n = 0;
    for (size_t i = 0; i < g_tag_count && n < max_count; ++i) {
        const RuntimeTag& t = g_tags[i];
        if (!t.hmi_visible) continue;
        PlcTagIndexedValueInfo& v = out[n++];
        v.type = t.type;
        if (t.type == PLC_TAG_BOOL) v.value.b = t.value.b;
        else if (t.type == PLC_TAG_INT) v.value.i = t.value.i;
        else v.value.f = t.value.f;
    }
    xSemaphoreGive(g_tags_mutex);
    return n;
}

bool plc_tags_get(const char* name, PlcTagInfo* out)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    int idx = find_tag_index_nolock(name);
    if (idx >= 0) tag_to_info(g_tags[idx], out);
    xSemaphoreGive(g_tags_mutex);
    return idx >= 0;
}


static bool plc_tags_set_internal_value_nolock(const char* name, PlcTagType type, bool b, int32_t i, float f)
{
    int idx = find_tag_index_nolock(name);
    if (idx < 0) return false;
    RuntimeTag& t = g_tags[idx];
    if (t.type != type) return false;
    if (type == PLC_TAG_BOOL) t.value.b = b;
    else if (type == PLC_TAG_INT) t.value.i = i;
    else t.value.f = f;
    return true;
}

bool plc_tags_set_internal_bool(const char* name, bool value)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    bool ok = plc_tags_set_internal_value_nolock(name, PLC_TAG_BOOL, value, 0, 0.0f);
    xSemaphoreGive(g_tags_mutex);
    return ok;
}

bool plc_tags_set_internal_int(const char* name, int32_t value)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    bool ok = plc_tags_set_internal_value_nolock(name, PLC_TAG_INT, false, value, 0.0f);
    xSemaphoreGive(g_tags_mutex);
    return ok;
}

bool plc_tags_set_internal_float(const char* name, float value)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    bool ok = plc_tags_set_internal_value_nolock(name, PLC_TAG_FLOAT, false, 0, value);
    xSemaphoreGive(g_tags_mutex);
    return ok;
}

bool plc_tags_set_value_bool(const char* name, bool value, char* err, size_t err_len)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    int idx = find_tag_index_nolock(name);
    if (idx < 0) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Unknown tag"); return false; }
    RuntimeTag& t = g_tags[idx];
    if (!t.writable) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Tag is not writable"); return false; }
    if (t.type != PLC_TAG_BOOL) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Tag is not bool"); return false; }
    t.value.b = value;
    // Runtime value writes update RAM only. They must never commit LittleFS/flash
    // from the HTTP task because flash writes can create PLC scan jitter spikes.
    xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "OK"); return true;
}
bool plc_tags_set_value_int(const char* name, int32_t value, char* err, size_t err_len)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    int idx = find_tag_index_nolock(name);
    if (idx < 0) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Unknown tag"); return false; }
    RuntimeTag& t = g_tags[idx];
    if (!t.writable) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Tag is not writable"); return false; }
    if (t.type != PLC_TAG_INT) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Tag is not int"); return false; }
    if (t.max_value > t.min_value) {
        if ((float)value < t.min_value) value = (int32_t)t.min_value;
        if ((float)value > t.max_value) value = (int32_t)t.max_value;
    }
    t.value.i = value;
    // Runtime value writes update RAM only. They must never commit LittleFS/flash.
    xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "OK"); return true;
}
bool plc_tags_set_value_float(const char* name, float value, char* err, size_t err_len)
{
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    int idx = find_tag_index_nolock(name);
    if (idx < 0) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Unknown tag"); return false; }
    RuntimeTag& t = g_tags[idx];
    if (!t.writable) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Tag is not writable"); return false; }
    if (t.type != PLC_TAG_FLOAT) { xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "Tag is not float"); return false; }
    if (t.max_value > t.min_value) {
        if (value < t.min_value) value = t.min_value;
        if (value > t.max_value) value = t.max_value;
    }
    t.value.f = value;
    // Runtime value writes update RAM only. They must never commit LittleFS/flash.
    xSemaphoreGive(g_tags_mutex); set_err(err, err_len, "OK"); return true;
}

bool plc_tags_set_value_from_text(const char* name, const char* value_text, char* err, size_t err_len)
{
    PlcTagInfo info;
    if (!plc_tags_get(name, &info)) { set_err(err, err_len, "Unknown tag"); return false; }
    if (info.type == PLC_TAG_BOOL) return plc_tags_set_value_bool(name, (strcmp(value_text, "true") == 0 || strcmp(value_text, "1") == 0), err, err_len);
    if (info.type == PLC_TAG_INT) return plc_tags_set_value_int(name, (int32_t)strtol(value_text, nullptr, 10), err, err_len);
    return plc_tags_set_value_float(name, strtof(value_text, nullptr), err, err_len);
}

bool plc_tags_write_value_json(const char* json, char* err, size_t err_len)
{
    char name[PLC_TAG_NAME_MAX] = {};
    if (!find_string_field(json, "tag", name, sizeof(name)) && !find_string_field(json, "name", name, sizeof(name))) {
        set_err(err, err_len, "Missing tag/name"); return false;
    }
    PlcTagInfo info;
    if (!plc_tags_get(name, &info)) { set_err(err, err_len, "Unknown tag"); return false; }
    char pat[] = "\"value\"";
    const char* p = strstr(json, pat); if (!p) { set_err(err, err_len, "Missing value"); return false; }
    p = strchr(p + strlen(pat), ':'); if (!p) { set_err(err, err_len, "Bad value"); return false; }
    p = skip_ws(p + 1);
    if (info.type == PLC_TAG_BOOL) return plc_tags_set_value_bool(name, strncmp(p, "true", 4) == 0 || strncmp(p, "1", 1) == 0, err, err_len);
    if (info.type == PLC_TAG_INT) return plc_tags_set_value_int(name, (int32_t)strtol(p, nullptr, 10), err, err_len);
    return plc_tags_set_value_float(name, strtof(p, nullptr), err, err_len);
}

bool plc_tags_register_angelscript_globals(asIScriptEngine* engine, char* err, size_t err_len)
{
    if (!engine) { set_err(err, err_len, "No AngelScript engine"); return false; }
    if (!g_tags_mutex) plc_tags_init();

    // Do not hold the tag mutex while calling into AngelScript. RegisterGlobalProperty
    // can take AngelScript internal locks, so we first copy the small registration list
    // while the tag table is locked, then release our mutex before touching the engine.
    struct GlobalReg {
        char name[PLC_TAG_NAME_MAX];
        PlcTagType type;
        void* ptr;
    } regs[PLC_TAG_MAX_COUNT];

    size_t reg_count = 0;
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    for (size_t i = 0; i < g_tag_count && reg_count < PLC_TAG_MAX_COUNT; ++i) {
        RuntimeTag& t = g_tags[i];
        if (!t.script_visible) continue;

        GlobalReg& r = regs[reg_count++];
        snprintf(r.name, sizeof(r.name), "%.*s", (int)(PLC_TAG_NAME_MAX - 1), t.name);
        r.type = t.type;
        if (t.type == PLC_TAG_BOOL) {
            r.ptr = &t.value.b;
        } else if (t.type == PLC_TAG_INT) {
            r.ptr = &t.value.i;
        } else {
            r.ptr = &t.value.f;
        }
    }
    xSemaphoreGive(g_tags_mutex);

    for (size_t i = 0; i < reg_count; ++i) {
        const char* type_name = "float";
        if (regs[i].type == PLC_TAG_BOOL) type_name = "bool";
        else if (regs[i].type == PLC_TAG_INT) type_name = "int";

        char decl[96];
        snprintf(decl, sizeof(decl), "%s %.*s", type_name, (int)(PLC_TAG_NAME_MAX - 1), regs[i].name);

        int r = engine->RegisterGlobalProperty(decl, regs[i].ptr);
        if (r < 0) {
            snprintf(err, err_len, "RegisterGlobalProperty failed for user tag: %s", decl);
            return false;
        }
    }

    set_err(err, err_len, "OK");
    return true;
}



static bool pilab_param_type_from_name(const char* type_name, PlcTagType* out)
{
    if (!type_name || !out) return false;
    if (strcmp(type_name, "bool") == 0) { *out = PLC_TAG_BOOL; return true; }
    if (strcmp(type_name, "int") == 0 || strcmp(type_name, "uint") == 0) { *out = PLC_TAG_INT; return true; }
    if (strcmp(type_name, "float") == 0 || strcmp(type_name, "double") == 0) { *out = PLC_TAG_FLOAT; return true; }
    return false;
}

bool plc_tags_ensure_param_tag(const char* name, const char* type_name, const char* default_value,
                               float min_value, float max_value, const char* units,
                               const char* description, char* err, size_t err_len)
{
    if (!g_tags_mutex) plc_tags_init();
    if (!plc_tags_is_valid_name(name, err, err_len)) return false;

    PlcTagType type = PLC_TAG_INT;
    if (!pilab_param_type_from_name(type_name, &type)) {
        set_err(err, err_len, "Unsupported parameter tag type");
        return false;
    }

    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    int idx = find_tag_index_nolock(name);
    if (idx >= 0) {
        RuntimeTag& t = g_tags[idx];
        if (t.type != type) {
            xSemaphoreGive(g_tags_mutex);
            set_err(err, err_len, "Existing parameter tag has different type");
            return false;
        }
        // Preserve the live value, but refresh the metadata/visibility contract.
        t.writable = true;
        t.retentive = true;
        t.hmi_visible = true;
        t.script_visible = true;
        t.min_value = min_value;
        t.max_value = max_value;
        if (units) snprintf(t.units, sizeof(t.units), "%.*s", (int)(sizeof(t.units) - 1), units);
        if (description && description[0]) snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), description);
        // Clamp existing numeric values into the declared range if a real range was provided.
        if (t.max_value > t.min_value) {
            if (t.type == PLC_TAG_INT) {
                if ((float)t.value.i < t.min_value) t.value.i = (int32_t)t.min_value;
                if ((float)t.value.i > t.max_value) t.value.i = (int32_t)t.max_value;
            } else if (t.type == PLC_TAG_FLOAT) {
                if (t.value.f < t.min_value) t.value.f = t.min_value;
                if (t.value.f > t.max_value) t.value.f = t.max_value;
            }
        }
        xSemaphoreGive(g_tags_mutex);
        set_err(err, err_len, "OK");
        return true;
    }

    if (g_tag_count >= PLC_TAG_MAX_COUNT) {
        xSemaphoreGive(g_tags_mutex);
        set_err(err, err_len, "Tag table full");
        return false;
    }

    RuntimeTag& t = g_tags[g_tag_count++];
    memset(&t, 0, sizeof(t));
    snprintf(t.name, sizeof(t.name), "%.*s", (int)(PLC_TAG_NAME_MAX - 1), name);
    t.type = type;
    t.writable = true;
    t.retentive = true;
    t.hmi_visible = true;
    t.script_visible = true;
    t.min_value = min_value;
    t.max_value = max_value;
    snprintf(t.units, sizeof(t.units), "%.*s", (int)(sizeof(t.units) - 1), units ? units : "");
    snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), description ? description : "PiLab parameter tag");

    if (type == PLC_TAG_BOOL) {
        t.value.b = default_value && (strcmp(default_value, "true") == 0 || strcmp(default_value, "1") == 0);
    } else if (type == PLC_TAG_INT) {
        int32_t v = default_value ? (int32_t)strtol(default_value, nullptr, 10) : 0;
        if (t.max_value > t.min_value) {
            if ((float)v < t.min_value) v = (int32_t)t.min_value;
            if ((float)v > t.max_value) v = (int32_t)t.max_value;
        }
        t.value.i = v;
    } else {
        float v = default_value ? strtof(default_value, nullptr) : 0.0f;
        if (t.max_value > t.min_value) {
            if (v < t.min_value) v = t.min_value;
            if (v > t.max_value) v = t.max_value;
        }
        t.value.f = v;
    }
    tag_copy_current_to_initial(t);

    xSemaphoreGive(g_tags_mutex);
    set_err(err, err_len, "OK");
    return true;
}

bool plc_tags_ensure_monitor_tag(const char* name, const char* type_name, const char* description, char* err, size_t err_len)
{
    if (!g_tags_mutex) plc_tags_init();
    if (!plc_tags_is_valid_name(name, err, err_len)) return false;

    PlcTagType type = PLC_TAG_FLOAT;
    if (!type_name || strcmp(type_name, "float") == 0) type = PLC_TAG_FLOAT;
    else if (strcmp(type_name, "bool") == 0) type = PLC_TAG_BOOL;
    else if (strcmp(type_name, "int") == 0 || strcmp(type_name, "uint") == 0) type = PLC_TAG_INT;
    else {
        set_err(err, err_len, "Unsupported monitor tag type");
        return false;
    }

    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    int idx = find_tag_index_nolock(name);
    if (idx >= 0) {
        RuntimeTag& t = g_tags[idx];
        if (t.type != type) {
            xSemaphoreGive(g_tags_mutex);
            set_err(err, err_len, "Existing tag has different type");
            return false;
        }
        t.writable = false;
        t.retentive = false;
        t.hmi_visible = true;
        t.script_visible = true;
        if (description && description[0]) snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), description);
        xSemaphoreGive(g_tags_mutex);
        set_err(err, err_len, "OK");
        return true;
    }

    if (g_tag_count >= PLC_TAG_MAX_COUNT) {
        xSemaphoreGive(g_tags_mutex);
        set_err(err, err_len, "Tag table full");
        return false;
    }

    RuntimeTag& t = g_tags[g_tag_count++];
    memset(&t, 0, sizeof(t));
    snprintf(t.name, sizeof(t.name), "%.*s", (int)(PLC_TAG_NAME_MAX - 1), name);
    t.type = type;
    t.writable = false;
    t.retentive = false;
    t.hmi_visible = true;
    t.script_visible = true;
    snprintf(t.description, sizeof(t.description), "%.*s", (int)(PLC_TAG_DESC_MAX - 1), description ? description : "PiLab monitor tag");
    if (type == PLC_TAG_BOOL) t.value.b = false;
    else if (type == PLC_TAG_INT) t.value.i = 0;
    else t.value.f = 0.0f;
    tag_copy_current_to_initial(t);
    xSemaphoreGive(g_tags_mutex);
    set_err(err, err_len, "OK");
    return true;
}
