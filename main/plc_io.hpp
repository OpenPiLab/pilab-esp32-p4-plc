#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ESP32-P4 PLC process image size.
// AngelScript sees these as integer-tagged arrays, not physical GPIOs.
// Physical onboard DIO is intentionally limited to 8 DI + 8 DO using
// low-conflict ESP32-P4 Module DEV KIT header GPIOs.
#define PLC_DI_COUNT 8
#define PLC_DO_COUNT 8
#define PLC_AI_COUNT 4
#define PLC_AO_COUNT 4

// 1 ms hardware tick input/update path.
void plc_io_init(void);
void plc_io_tick_1ms(void);
void plc_io_apply_outputs(void);

// Called by AngelScript host functions. These operate on the process image only.
uint32_t plc_io_get_di(uint32_t index);
void     plc_io_set_do(uint32_t index, uint32_t value);
float    plc_io_get_ai(uint32_t index);
void     plc_io_set_ao(uint32_t index, float value);


// Full process-image snapshot for web/HMI endpoints.
// This keeps HTTP JSON generation from repeatedly locking live I/O data.
typedef struct PlcIoSnapshot {
    uint8_t  raw_di[PLC_DI_COUNT];
    uint8_t  debounced_di[PLC_DI_COUNT];
    uint8_t  do_cmd[PLC_DO_COUNT];
    float    ai[PLC_AI_COUNT];
    float    ao[PLC_AO_COUNT];
    uint32_t raw_di_mask;
    uint32_t di_mask;
    uint32_t do_mask;
    uint32_t tick_count;
    uint32_t script_scan_count;
    uint32_t output_write_count;
} PlcIoSnapshot;

void plc_io_get_snapshot(PlcIoSnapshot* snapshot);

// Diagnostics / web status helpers.
uint32_t plc_io_get_raw_di_mask(void);
uint32_t plc_io_get_debounced_di_mask(void);
uint32_t plc_io_get_do_mask(void);
uint32_t plc_io_get_tick_count(void);
uint32_t plc_io_get_script_scan_count(void);
uint32_t plc_io_get_output_write_count(void);
void plc_io_note_script_scan(void);
void plc_io_get_status_json(char* out, size_t out_len);

#ifdef __cplusplus
}
#endif
