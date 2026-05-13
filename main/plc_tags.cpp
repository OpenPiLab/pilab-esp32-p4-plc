#include "plc_tags.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <angelscript.h>

static const char* TAG = "PLC_TAGS";
static const char* NVS_NS = "plc_tags";
static const char* NVS_KEY = "tags_json";

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
};

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

static bool is_reserved_name(const char* name)
{
    if (!name) return true;
    const char* words[] = {
        "scan", "Scan", "true", "false", "bool", "int", "float", "void", "uint",
        "if", "else", "for", "while", "return", "break", "continue", "class", "string",
        nullptr
    };
    for (int i = 0; words[i]; ++i) if (strcmp(name, words[i]) == 0) return true;

    if ((name[0] == 'I' || name[0] == 'Q') && isdigit((unsigned char)name[1])) return true;
    if (name[0] == 'A' && (name[1] == 'I' || name[1] == 'O') && isdigit((unsigned char)name[2])) return true;
    return false;
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
    if (is_reserved_name(name)) { set_err(err, err_len, "Tag name is reserved by the PLC runtime"); return false; }
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
}

static void ensure_runtime_diagnostic_tags_nolock()
{
    add_int_tag_nolock("PLC_ScanCoalescedCount", 0, "scans", "Script scan notifications coalesced/skipped", false, false, true, false);
    add_int_tag_nolock("PLC_ScanOverrunCount", 0, "scans", "Script scans that exceeded budget", false, false, true, false);
    add_bool_tag_nolock("PLC_ScanOverrunActive", false, "Script scan is currently over budget");
    int idx = find_tag_index_nolock("PLC_ScanOverrunActive"); if (idx >= 0) { g_tags[idx].writable = false; g_tags[idx].retentive = false; g_tags[idx].script_visible = false; }
    add_bool_tag_nolock("PLC_ScanFaultActive", false, "Script scan policy fault active");
    idx = find_tag_index_nolock("PLC_ScanFaultActive"); if (idx >= 0) { g_tags[idx].writable = false; g_tags[idx].retentive = false; g_tags[idx].script_visible = false; }
    add_int_tag_nolock("PLC_ScanActualPeriodUs", 0, "us", "Elapsed time between script executions", false, false, true, false);
    add_int_tag_nolock("PLC_ScanExecutionTimeUs", 0, "us", "Last script scan execution time", false, false, true, false);
    add_float_tag_nolock("PLC_ScanLoadPercent", 0.0f, "%", "Script execution time divided by budget", false, false, true, false);
    add_int_tag_nolock("PLC_DeltaTimeUs", 5000, "us", "Clamped elapsed time passed to script", false, false, true, false);
    add_float_tag_nolock("PLC_DeltaTimeMs", 5.0f, "ms", "Clamped elapsed time passed to script", false, false, true, false);
}

static void ensure_compatibility_tags_nolock()
{
    // Compatibility/default user bit used by the current HMI examples and test scripts.
    // Do not overwrite it if the user already defined it in NVS; only add it when missing.
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

void plc_tags_get_json(char* out, size_t out_len)
{
    if (!out || !out_len) return;
    out[0] = 0;
    if (!g_tags_mutex) plc_tags_init();
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    char* p = out;
    size_t rem = out_len;
    int n = snprintf(p, rem, "{\"max_count\":%u,\"count\":%u,\"tags\":[", (unsigned)PLC_TAG_MAX_COUNT, (unsigned)g_tag_count);
    p += n; rem = (n < (int)rem) ? rem - n : 0;
    for (size_t i = 0; i < g_tag_count && rem > 64; ++i) {
        RuntimeTag& t = g_tags[i];
        n = snprintf(p, rem, "%s{\"name\":\"", i ? "," : ""); p += n; rem -= n;
        json_escape_append(p, rem, t.name);
        n = snprintf(p, rem, "\",\"type\":\"%s\",\"writable\":%s,\"retentive\":%s,\"hmi_visible\":%s,\"script_visible\":%s,\"description\":\"",
                     type_to_string(t.type), t.writable?"true":"false", t.retentive?"true":"false", t.hmi_visible?"true":"false", t.script_visible?"true":"false");
        p += n; rem -= n;
        json_escape_append(p, rem, t.description);
        n = snprintf(p, rem, "\",\"units\":\""); p += n; rem -= n;
        json_escape_append(p, rem, t.units);
        if (t.type == PLC_TAG_BOOL) n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%s}", (double)t.min_value, (double)t.max_value, t.value.b?"true":"false");
        else if (t.type == PLC_TAG_INT) n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%ld}", (double)t.min_value, (double)t.max_value, (long)t.value.i);
        else n = snprintf(p, rem, "\",\"min\":%.3f,\"max\":%.3f,\"value\":%.6g}", (double)t.min_value, (double)t.max_value, (double)t.value.f);
        p += n; rem = (n < (int)rem) ? rem - n : 0;
    }
    snprintf(p, rem, "]}");
    xSemaphoreGive(g_tags_mutex);
}

static bool save_to_nvs_nolock()
{
    char* json = (char*)malloc(8192);
    if (!json) return false;
    // Avoid recursive mutex use by writing JSON inline using public helper pattern.
    char* p = json; size_t rem = 8192; int n = snprintf(p, rem, "{\"tags\":["); p += n; rem -= n;
    bool first_saved = true;
    for (size_t i = 0; i < g_tag_count && rem > 64; ++i) {
        RuntimeTag& t = g_tags[i];
        if (!t.retentive) continue;
        n = snprintf(p, rem, "%s{\"name\":\"%s\",\"type\":\"%s\",\"writable\":%s,\"retentive\":%s,\"hmi_visible\":%s,\"script_visible\":%s,\"description\":\"%s\",\"units\":\"%s\",\"min\":%.3f,\"max\":%.3f,",
                     first_saved ? "" : ",", t.name, type_to_string(t.type), t.writable?"true":"false", t.retentive?"true":"false", t.hmi_visible?"true":"false", t.script_visible?"true":"false", t.description, t.units, (double)t.min_value, (double)t.max_value);
        p += n; rem -= n;
        if (t.type == PLC_TAG_BOOL) n = snprintf(p, rem, "\"value\":%s}", t.value.b?"true":"false");
        else if (t.type == PLC_TAG_INT) n = snprintf(p, rem, "\"value\":%ld}", (long)t.value.i);
        else n = snprintf(p, rem, "\"value\":%.6g}", (double)t.value.f);
        p += n; rem -= n;
        first_saved = false;
    }
    snprintf(p, rem, "]}");
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_str(h, NVS_KEY, json);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    free(json);
    return e == ESP_OK;
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

bool plc_tags_load_json(const char* json, char* err, size_t err_len)
{
    if (!json) { set_err(err, err_len, "No JSON body"); return false; }
    if (!g_tags_mutex) plc_tags_init();

    // This function is called from the ESP-IDF httpd task. Do not put the
    // whole candidate tag table on that task's stack; it is large enough to
    // trip the stack protector on ESP32-P4 when /api/tags is posted.
    RuntimeTag* new_tags = (RuntimeTag*)calloc(PLC_TAG_MAX_COUNT, sizeof(RuntimeTag));
    if (!new_tags) {
        set_err(err, err_len, "Out of memory allocating tag table");
        return false;
    }
    size_t new_count = 0;

    const char* p = json;
    while ((p = strchr(p, '{')) != nullptr) {
        const char* end = strchr(p, '}'); if (!end) break;
        size_t len = end - p + 1;
        if (len > 700) len = 700;
        char obj[704]; memcpy(obj, p, len); obj[len] = 0; p = end + 1;

        char name[PLC_TAG_NAME_MAX] = {}; char type_s[16] = {};
        if (!find_string_field(obj, "name", name, sizeof(name))) continue;
        if (!find_string_field(obj, "type", type_s, sizeof(type_s))) continue;
        char name_err[96];
        if (!plc_tags_is_valid_name(name, name_err, sizeof(name_err))) {
            set_err(err, err_len, name_err);
            free(new_tags);
            return false;
        }
        PlcTagType type;
        if (!string_to_type(type_s, &type)) {
            set_err(err, err_len, "Invalid tag type");
            free(new_tags);
            return false;
        }
        if (new_count >= PLC_TAG_MAX_COUNT) {
            set_err(err, err_len, "Too many tags");
            free(new_tags);
            return false;
        }
        for (size_t i = 0; i < new_count; ++i) {
            if (strcmp(new_tags[i].name, name) == 0) {
                set_err(err, err_len, "Duplicate tag name");
                free(new_tags);
                return false;
            }
        }

        RuntimeTag& t = new_tags[new_count++];
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
    }

    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    memcpy(g_tags, new_tags, PLC_TAG_MAX_COUNT * sizeof(RuntimeTag));
    g_tag_count = new_count;
    ensure_runtime_diagnostic_tags_nolock();
    bool saved = save_to_nvs_nolock();
    xSemaphoreGive(g_tags_mutex);
    free(new_tags);
    if (!saved) ESP_LOGW(TAG, "Tags updated in RAM but NVS save failed");
    set_err(err, err_len, saved ? "OK" : "Tags updated in RAM, NVS save failed");
    return true;
}

void plc_tags_init(void)
{
    if (!g_tags_mutex) g_tags_mutex = xSemaphoreCreateMutex();
    if (g_loaded || !g_tags_mutex) return;
    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
    if (!g_loaded) {
        add_default_tags_nolock();
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t len = 0;
            if (nvs_get_str(h, NVS_KEY, nullptr, &len) == ESP_OK && len > 2 && len < 8192) {
                char* buf = (char*)malloc(len);
                if (buf && nvs_get_str(h, NVS_KEY, buf, &len) == ESP_OK) {
                    xSemaphoreGive(g_tags_mutex);
                    char err[128]; plc_tags_load_json(buf, err, sizeof(err));
                    xSemaphoreTake(g_tags_mutex, portMAX_DELAY);
                }
                free(buf);
            }
            nvs_close(h);
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
    // Runtime writes must never commit NVS/flash from the HTTP task.
    // Flash/NVS commits can block long enough to create PLC scan jitter spikes.
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
    t.value.i = value;
    // Runtime writes must never commit NVS/flash from the HTTP task.
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
    t.value.f = value;
    // Runtime writes must never commit NVS/flash from the HTTP task.
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
