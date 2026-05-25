#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
class asIScriptEngine;
extern "C" {
#endif

#define PLC_TAG_MAX_COUNT 384
#define PLC_TAG_NAME_MAX 32
#define PLC_TAG_DESC_MAX 64

typedef enum PlcTagType {
    PLC_TAG_BOOL = 0,
    PLC_TAG_INT = 1,
    PLC_TAG_FLOAT = 2
} PlcTagType;

typedef struct PlcTagValueInfo {
    char name[PLC_TAG_NAME_MAX];
    PlcTagType type;
    bool writable;
    union {
        bool b;
        int32_t i;
        float f;
    } value;
} PlcTagValueInfo;


#ifndef PLC_UDP_TAG_GROUP_COUNT
#define PLC_UDP_TAG_GROUP_COUNT 32
#endif
#ifndef PLC_UDP_TAG_VALUE_COUNT
#define PLC_UDP_TAG_VALUE_COUNT 128
#endif

typedef struct PlcUdpTagWriteStats {
    uint32_t write_count;
    uint32_t last_write_us;
    uint32_t max_write_us;
    uint32_t last_values_written;
    uint32_t cache_ready;
} PlcUdpTagWriteStats;

typedef struct PlcTagIndexedValueInfo {
    PlcTagType type;
    union {
        bool b;
        int32_t i;
        float f;
    } value;
} PlcTagIndexedValueInfo;

typedef struct PlcTagInfo {
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
} PlcTagInfo;

void plc_tags_init(void);
bool plc_tags_is_valid_name(const char* name, char* err, size_t err_len);
size_t plc_tags_get_count(void);
size_t plc_tags_copy_all(PlcTagInfo* out, size_t max_count);
size_t plc_tags_copy_hmi_values(PlcTagValueInfo* out, size_t max_count);
size_t plc_tags_copy_hmi_indexed_values(PlcTagIndexedValueInfo* out, size_t max_count);
bool plc_tags_get(const char* name, PlcTagInfo* out);
bool plc_tags_set_value_from_text(const char* name, const char* value_text, char* err, size_t err_len);
bool plc_tags_set_value_bool(const char* name, bool value, char* err, size_t err_len);
bool plc_tags_set_value_int(const char* name, int32_t value, char* err, size_t err_len);
bool plc_tags_set_value_float(const char* name, float value, char* err, size_t err_len);
bool plc_tags_set_internal_bool(const char* name, bool value);
bool plc_tags_set_internal_int(const char* name, int32_t value);
bool plc_tags_set_internal_float(const char* name, float value);
// Creates or reuses a non-retentive, read-only, script-visible monitor tag.
bool plc_tags_ensure_monitor_tag(const char* name, const char* type_name, const char* description, char* err, size_t err_len);
// Creates or reuses a writable, retentive, HMI/script-visible parameter tag.
// Existing values are preserved; metadata updates type validation, visibility, limits, units, and description.
bool plc_tags_ensure_param_tag(const char* name, const char* type_name, const char* default_value,
                               float min_value, float max_value, const char* units,
                               const char* description, char* err, size_t err_len);
void plc_tags_get_json(char* out, size_t out_len);
bool plc_tags_load_json(const char* json, char* err, size_t err_len);
// Loads/merges tags into live RAM only. Does not write LittleFS. Safe to call while PLC is RUN; script scanning is paused during the in-place update.
bool plc_tags_load_json_ram(const char* json, char* err, size_t err_len);
// Explicit persistent save of the current live user tag registry to LittleFS. Caller should enforce STOP/flash policy.
bool plc_tags_save_to_flash(char* err, size_t err_len);
bool plc_tags_write_value_json(const char* json, char* err, size_t err_len);

// Firmware-owned UDP tag bank: UDP_DI0..31, UDP_DO0..31, UDP_AI0..31, UDP_AO0..31.
// Values are written by cached tag indices, not name lookup, from the PLC scan path.
void plc_tags_ensure_udp_tag_bank(void);
uint32_t plc_tags_write_udp_cached_values(const uint32_t* raw_values, size_t value_count);
void plc_tags_get_udp_tag_write_stats(PlcUdpTagWriteStats* out);
void plc_tags_clear_udp_tag_write_stats(void);


#ifdef __cplusplus
}

bool plc_tags_register_angelscript_globals(asIScriptEngine* engine, char* err, size_t err_len);
#endif
