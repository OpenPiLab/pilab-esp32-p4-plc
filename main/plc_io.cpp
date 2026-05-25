#include "plc_io.hpp"
#include "plc_tags.hpp"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_timer.h"
#include "esp_attr.h"

static const char* TAG = "PLC_IO";

// -----------------------------------------------------------------------------
// ESP32-P4 Module DEV KIT 40-pin header map from the uploaded pinout image.
// -----------------------------------------------------------------------------
// Header GPIOs shown in the image:
//   Left:  GPIO7/SDA, GPIO8/SCL, GPIO23, GPIO21, GPIO20, GPIO6, GPIO3,
//          GPIO2, GPIO0, GPIO24, GPIO33, GPIO26, GPIO48, GPIO53, GPIO47
//   Right: GPIO37/TXD, GPIO38/RXD, GPIO22, GPIO5, GPIO4, GPIO1, GPIO36,
//          GPIO32, GPIO25, GPIO54, GPIO46, GPIO27, GPIO45
//
// Low-drama PLC allocation used here:
//   I0..I7 = 8 digital inputs
//   Q0..Q7 = 8 digital outputs
//
// This map intentionally avoids the header pins most likely to collide with
// onboard peripherals or current troubleshooting issues:
//   GPIO7/GPIO8   = I2C labels
//   GPIO24/GPIO25 = USB-related pins
//   GPIO37/GPIO38 = USB-UART labels
//   GPIO45/GPIO46 = avoided due to SD-card/control suspicion and observed DO issue
//   GPIO53        = audio amplifier control
//
// GPIO47 and GPIO48 are intentionally left as spare clean-looking GPIOs.

static constexpr bool PLC_INPUT_ACTIVE_LOW = true;
static constexpr uint8_t PLC_DEBOUNCE_TICKS = 3;     // 3 x 1 ms = 3 ms

static constexpr gpio_num_t k_di_pins[PLC_DI_COUNT] = {
    GPIO_NUM_0,  // I0
    GPIO_NUM_1,  // I1
    GPIO_NUM_2,  // I2
    GPIO_NUM_3,  // I3
    GPIO_NUM_4,  // I4
    GPIO_NUM_5,  // I5
    GPIO_NUM_6,  // I6
    GPIO_NUM_20, // I7
};

static constexpr gpio_num_t k_do_pins[PLC_DO_COUNT] = {
    GPIO_NUM_21, // Q0
    GPIO_NUM_22, // Q1
    GPIO_NUM_23, // Q2
    GPIO_NUM_26, // Q3
    GPIO_NUM_27, // Q4
    GPIO_NUM_32, // Q5
    GPIO_NUM_33, // Q6
    GPIO_NUM_54, // Q7
};

struct PlcIoImage {
    uint8_t  raw_di[PLC_DI_COUNT];
    uint8_t  debounced_di[PLC_DI_COUNT];
    uint8_t  candidate_di[PLC_DI_COUNT];
    uint8_t  stable_count[PLC_DI_COUNT];
    uint8_t  do_cmd[PLC_DO_COUNT];
    float    ai[PLC_AI_COUNT];
    float    ao[PLC_AO_COUNT];
    uint32_t tick_count;
    uint32_t script_scan_count;
    uint32_t output_write_count;
};

static PlcIoImage g_io = {};
static portMUX_TYPE g_io_lock = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint32_t PLC_UDP_IO_STALE_US = 1000000u; // keep last remote image active for 1 s

struct PlcUdpInputImage {
    uint32_t seq;
    uint32_t di_mask;
    float ai[PLC_AI_COUNT];
    uint64_t rx_time_us;
};

struct PlcUdpTagImage {
    uint32_t seq;
    uint64_t rx_time_us;
    uint32_t values[PLC_UDP_TAG_VALUE_COUNT];
};

static DRAM_ATTR PlcUdpInputImage g_udp_img[2] = {};
static DRAM_ATTR PlcUdpInputImage g_udp_active_img = {};
static DRAM_ATTR PlcUdpTagImage g_udp_tag_img[2] = {};
static DRAM_ATTR PlcUdpTagImage g_udp_tag_active_img = {};
static DRAM_ATTR PlcUdpIoStats g_udp_stats = {};
static uint32_t g_udp_publish_index = 0;
static uint32_t g_udp_publish_generation = 0;
static uint32_t g_udp_consumed_generation = 0;
static uint32_t g_udp_tag_publish_index = 0;
static uint32_t g_udp_tag_publish_generation = 0;
static uint32_t g_udp_tag_consumed_generation = 0;
static portMUX_TYPE g_udp_lock = portMUX_INITIALIZER_UNLOCKED;

static inline bool valid_gpio(gpio_num_t pin)
{
    return pin >= GPIO_NUM_0 && pin < GPIO_NUM_MAX;
}

static uint32_t make_mask_u8(const uint8_t* bits, uint32_t count)
{
    uint32_t mask = 0;
    for (uint32_t i = 0; i < count && i < 32; ++i) {
        if (bits[i]) mask |= (1u << i);
    }
    return mask;
}

void plc_io_init(void)
{
    memset(&g_io, 0, sizeof(g_io));
    plc_io_clear_udp_io_stats();

    for (int i = 0; i < PLC_DI_COUNT; ++i) {
        if (!valid_gpio(k_di_pins[i])) continue;
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << k_di_pins[i];
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }

    for (int i = 0; i < PLC_DO_COUNT; ++i) {
        if (!valid_gpio(k_do_pins[i])) continue;
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << k_do_pins[i];
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        gpio_set_level(k_do_pins[i], 0);
    }

    ESP_LOGI(TAG, "Process image initialized: %d DI, %d DO, debounce=%u ms, inputs active-%s",
             PLC_DI_COUNT, PLC_DO_COUNT, (unsigned)PLC_DEBOUNCE_TICKS,
             PLC_INPUT_ACTIVE_LOW ? "low" : "high");
    for (int i = 0; i < PLC_DI_COUNT; ++i) {
        ESP_LOGI(TAG, "I%d -> GPIO%d", i, (int)k_di_pins[i]);
    }
    for (int i = 0; i < PLC_DO_COUNT; ++i) {
        ESP_LOGI(TAG, "Q%d -> GPIO%d", i, (int)k_do_pins[i]);
    }
}

static void plc_io_consume_udp_input_image_locked(uint64_t now_us)
{
    bool active = false;
    PlcUdpInputImage active_img = {};

    // Take one short UDP-buffer critical section: copy the latest complete image
    // if the UDP task published a newer one, then update diagnostics. The normal
    // PLC process-image lock is already held by the caller, so the copied image
    // is mirrored into g_io after this section exits.
    taskENTER_CRITICAL(&g_udp_lock);

    const uint32_t pub_gen = g_udp_publish_generation;
    const uint32_t pub_index = g_udp_publish_index;

    if (pub_gen != g_udp_consumed_generation) {
        g_udp_active_img = g_udp_img[pub_index & 1u];
        if (pub_gen > g_udp_consumed_generation + 1u && g_udp_consumed_generation != 0) {
            g_udp_stats.missed_update_count += pub_gen - g_udp_consumed_generation - 1u;
        }
        g_udp_consumed_generation = pub_gen;
        g_udp_stats.consume_count++;
        g_udp_stats.consumed_seq = g_udp_active_img.seq;
        g_udp_stats.last_di_mask = g_udp_active_img.di_mask;
    }

    active_img = g_udp_active_img;

    if (active_img.rx_time_us == 0) {
        g_udp_stats.active = 0;
    } else {
        const uint64_t age64 = now_us >= active_img.rx_time_us ? now_us - active_img.rx_time_us : 0;
        const uint32_t age_us = age64 > UINT32_MAX ? UINT32_MAX : (uint32_t)age64;
        g_udp_stats.last_age_us = age_us;

        if (age_us > PLC_UDP_IO_STALE_US) {
            g_udp_stats.active = 0;
            g_udp_stats.stale_count++;
        } else {
            g_udp_stats.active = 1;
            g_udp_stats.last_di_mask = active_img.di_mask;
            if (age_us > g_udp_stats.max_age_us) {
                g_udp_stats.max_age_us = age_us;
            }
            for (int i = 0; i < PLC_AI_COUNT; ++i) {
                g_udp_stats.last_ai[i] = active_img.ai[i];
            }
            active = true;
        }
    }

    taskEXIT_CRITICAL(&g_udp_lock);

    if (!active) {
        return;
    }

    // Simulate a complete remote input process image. This deliberately mirrors
    // the received UDP input image into the normal PLC process image, so ladder,
    // AngelScript, and the HMI all see the same data path they would see from a
    // real remote I/O coupler.
    for (int i = 0; i < PLC_DI_COUNT; ++i) {
        const uint8_t bit = (active_img.di_mask & (1u << i)) ? 1u : 0u;
        g_io.raw_di[i] = bit;
        g_io.candidate_di[i] = bit;
        g_io.stable_count[i] = PLC_DEBOUNCE_TICKS;
        g_io.debounced_di[i] = bit;
    }
    for (int i = 0; i < PLC_AI_COUNT; ++i) {
        g_io.ai[i] = active_img.ai[i];
    }
}

static void plc_io_consume_udp_tag_image_scan(uint64_t now_us)
{
    bool active_new_image = false;
    PlcUdpTagImage active_img = {};

    taskENTER_CRITICAL(&g_udp_lock);

    const uint32_t pub_gen = g_udp_tag_publish_generation;
    const uint32_t pub_index = g_udp_tag_publish_index;

    if (pub_gen != g_udp_tag_consumed_generation) {
        g_udp_tag_active_img = g_udp_tag_img[pub_index & 1u];
        if (pub_gen > g_udp_tag_consumed_generation + 1u && g_udp_tag_consumed_generation != 0) {
            g_udp_stats.tag_missed_update_count += pub_gen - g_udp_tag_consumed_generation - 1u;
        }
        g_udp_tag_consumed_generation = pub_gen;
        g_udp_stats.tag_consume_count++;
        g_udp_stats.tag_consumed_seq = g_udp_tag_active_img.seq;

        active_img = g_udp_tag_active_img;
        if (active_img.rx_time_us != 0) {
            const uint64_t age64 = now_us >= active_img.rx_time_us ? now_us - active_img.rx_time_us : 0;
            const uint32_t age_us = age64 > UINT32_MAX ? UINT32_MAX : (uint32_t)age64;
            g_udp_stats.tag_last_age_us = age_us;
            if (age_us <= PLC_UDP_IO_STALE_US) {
                if (age_us > g_udp_stats.tag_max_age_us) g_udp_stats.tag_max_age_us = age_us;
                active_new_image = true;
            }
        }
    }

    taskEXIT_CRITICAL(&g_udp_lock);

    if (!active_new_image) return;

    const uint64_t t0 = (uint64_t)esp_timer_get_time();
    const uint32_t written = plc_tags_write_udp_cached_values(active_img.values, PLC_UDP_TAG_VALUE_COUNT);
    const uint64_t t1 = (uint64_t)esp_timer_get_time();
    const uint32_t write_us = (t1 >= t0) ? (uint32_t)(t1 - t0) : 0u;

    PlcUdpTagWriteStats tag_stats = {};
    plc_tags_get_udp_tag_write_stats(&tag_stats);

    taskENTER_CRITICAL(&g_udp_lock);
    g_udp_stats.tag_values_written = written;
    g_udp_stats.tag_write_last_us = write_us;
    if (write_us > g_udp_stats.tag_write_max_us) g_udp_stats.tag_write_max_us = write_us;
    g_udp_stats.tag_cache_ready = tag_stats.cache_ready;
    taskEXIT_CRITICAL(&g_udp_lock);
}

void plc_io_tick_1ms(void)
{
    uint8_t raw[PLC_DI_COUNT];

    for (int i = 0; i < PLC_DI_COUNT; ++i) {
        int level = 0;
        if (valid_gpio(k_di_pins[i])) {
            level = gpio_get_level(k_di_pins[i]);
        }
        raw[i] = PLC_INPUT_ACTIVE_LOW ? (level == 0) : (level != 0);
    }

    const uint64_t now_us = (uint64_t)esp_timer_get_time();

    taskENTER_CRITICAL(&g_io_lock);

    for (int i = 0; i < PLC_DI_COUNT; ++i) {
        g_io.raw_di[i] = raw[i];

        if (raw[i] == g_io.candidate_di[i]) {
            if (g_io.stable_count[i] < PLC_DEBOUNCE_TICKS) {
                g_io.stable_count[i]++;
            }
            if (g_io.stable_count[i] >= PLC_DEBOUNCE_TICKS) {
                g_io.debounced_di[i] = raw[i];
            }
        } else {
            g_io.candidate_di[i] = raw[i];
            g_io.stable_count[i] = 1;
        }
    }

    // Synthetic analogs for early testing until real ADC/field I/O is added.
    g_io.ai[0] = (g_io.tick_count % 1000) * 0.001f;
    g_io.ai[1] = (float)make_mask_u8(g_io.debounced_di, PLC_DI_COUNT);

    // If validated UDP remote-I/O packets are arriving, overwrite the physical
    // test inputs with the latest complete UDP process image. This is the key
    // simulation path for testing whether UDP I/O traffic affects PLC timing.
    plc_io_consume_udp_input_image_locked(now_us);

    g_io.tick_count++;

    taskEXIT_CRITICAL(&g_io_lock);

    // Scan-side cached-index tag write path for the 128 UDP_* runtime tags.
    // This is intentionally outside g_io_lock so tag mutex contention shows up
    // in udp_tag_write_last_us without extending the process-image critical section.
    plc_io_consume_udp_tag_image_scan(now_us);
}

void plc_io_apply_outputs(void)
{
    uint8_t do_snapshot[PLC_DO_COUNT];

    taskENTER_CRITICAL(&g_io_lock);
    memcpy(do_snapshot, g_io.do_cmd, sizeof(do_snapshot));
    g_io.output_write_count++;
    taskEXIT_CRITICAL(&g_io_lock);

    for (int i = 0; i < PLC_DO_COUNT; ++i) {
        if (valid_gpio(k_do_pins[i])) {
            gpio_set_level(k_do_pins[i], do_snapshot[i] ? 1 : 0);
        }
    }
}

void plc_io_copy_inputs_to_script(bool* di, size_t di_count, float* ai, size_t ai_count)
{
    taskENTER_CRITICAL(&g_io_lock);
    for (size_t i = 0; di && i < di_count && i < PLC_DI_COUNT; ++i) {
        di[i] = g_io.debounced_di[i] != 0;
    }
    for (size_t i = 0; ai && i < ai_count && i < PLC_AI_COUNT; ++i) {
        ai[i] = g_io.ai[i];
    }
    taskEXIT_CRITICAL(&g_io_lock);
}

void plc_io_copy_outputs_to_script(bool* do_cmd, size_t do_count, float* ao, size_t ao_count)
{
    taskENTER_CRITICAL(&g_io_lock);
    for (size_t i = 0; do_cmd && i < do_count && i < PLC_DO_COUNT; ++i) {
        do_cmd[i] = g_io.do_cmd[i] != 0;
    }
    for (size_t i = 0; ao && i < ao_count && i < PLC_AO_COUNT; ++i) {
        ao[i] = g_io.ao[i];
    }
    taskEXIT_CRITICAL(&g_io_lock);
}

void plc_io_copy_outputs_from_script(const bool* do_cmd, size_t do_count, const float* ao, size_t ao_count)
{
    taskENTER_CRITICAL(&g_io_lock);
    for (size_t i = 0; do_cmd && i < do_count && i < PLC_DO_COUNT; ++i) {
        g_io.do_cmd[i] = do_cmd[i] ? 1u : 0u;
    }
    for (size_t i = 0; ao && i < ao_count && i < PLC_AO_COUNT; ++i) {
        g_io.ao[i] = ao[i];
    }
    taskEXIT_CRITICAL(&g_io_lock);
}

uint32_t plc_io_get_di(uint32_t index)
{
    if (index >= PLC_DI_COUNT) return 0;
    uint32_t value;
    taskENTER_CRITICAL(&g_io_lock);
    value = g_io.debounced_di[index] ? 1u : 0u;
    taskEXIT_CRITICAL(&g_io_lock);
    return value;
}

void plc_io_set_do(uint32_t index, uint32_t value)
{
    if (index >= PLC_DO_COUNT) return;
    taskENTER_CRITICAL(&g_io_lock);
    g_io.do_cmd[index] = value ? 1u : 0u;
    taskEXIT_CRITICAL(&g_io_lock);
}

float plc_io_get_ai(uint32_t index)
{
    if (index >= PLC_AI_COUNT) return 0.0f;
    float value;
    taskENTER_CRITICAL(&g_io_lock);
    value = g_io.ai[index];
    taskEXIT_CRITICAL(&g_io_lock);
    return value;
}

void plc_io_set_ao(uint32_t index, float value)
{
    if (index >= PLC_AO_COUNT) return;
    taskENTER_CRITICAL(&g_io_lock);
    g_io.ao[index] = value;
    taskEXIT_CRITICAL(&g_io_lock);
}

uint32_t plc_io_get_raw_di_mask(void)
{
    uint32_t mask;
    taskENTER_CRITICAL(&g_io_lock);
    mask = make_mask_u8(g_io.raw_di, PLC_DI_COUNT);
    taskEXIT_CRITICAL(&g_io_lock);
    return mask;
}

uint32_t plc_io_get_debounced_di_mask(void)
{
    uint32_t mask;
    taskENTER_CRITICAL(&g_io_lock);
    mask = make_mask_u8(g_io.debounced_di, PLC_DI_COUNT);
    taskEXIT_CRITICAL(&g_io_lock);
    return mask;
}

uint32_t plc_io_get_do_mask(void)
{
    uint32_t mask;
    taskENTER_CRITICAL(&g_io_lock);
    mask = make_mask_u8(g_io.do_cmd, PLC_DO_COUNT);
    taskEXIT_CRITICAL(&g_io_lock);
    return mask;
}

uint32_t plc_io_get_tick_count(void)
{
    uint32_t v;
    taskENTER_CRITICAL(&g_io_lock);
    v = g_io.tick_count;
    taskEXIT_CRITICAL(&g_io_lock);
    return v;
}

uint32_t plc_io_get_script_scan_count(void)
{
    uint32_t v;
    taskENTER_CRITICAL(&g_io_lock);
    v = g_io.script_scan_count;
    taskEXIT_CRITICAL(&g_io_lock);
    return v;
}

uint32_t plc_io_get_output_write_count(void)
{
    uint32_t v;
    taskENTER_CRITICAL(&g_io_lock);
    v = g_io.output_write_count;
    taskEXIT_CRITICAL(&g_io_lock);
    return v;
}

void plc_io_note_script_scan(void)
{
    taskENTER_CRITICAL(&g_io_lock);
    g_io.script_scan_count++;
    taskEXIT_CRITICAL(&g_io_lock);
}

void plc_io_publish_udp_input_image(uint32_t seq, uint32_t di_mask, const float* ai, size_t ai_count, uint64_t rx_time_us)
{
    PlcUdpInputImage img = {};
    img.seq = seq;
    img.di_mask = di_mask;
    img.rx_time_us = rx_time_us;
    for (size_t i = 0; ai && i < ai_count && i < PLC_AI_COUNT; ++i) {
        img.ai[i] = ai[i];
    }

    taskENTER_CRITICAL(&g_udp_lock);
    uint32_t next_index = (g_udp_publish_index ^ 1u) & 1u;
    g_udp_img[next_index] = img;
    g_udp_publish_index = next_index;
    g_udp_publish_generation++;
    g_udp_stats.rx_publish_count++;
    g_udp_stats.published_seq = seq;
    taskEXIT_CRITICAL(&g_udp_lock);
}

void plc_io_publish_udp_tag_image(uint32_t seq, const uint32_t* values, size_t value_count, uint64_t rx_time_us)
{
    if (!values || value_count < PLC_UDP_TAG_VALUE_COUNT) return;

    PlcUdpTagImage img = {};
    img.seq = seq;
    img.rx_time_us = rx_time_us;
    memcpy(img.values, values, sizeof(img.values));

    taskENTER_CRITICAL(&g_udp_lock);
    uint32_t next_index = (g_udp_tag_publish_index ^ 1u) & 1u;
    g_udp_tag_img[next_index] = img;
    g_udp_tag_publish_index = next_index;
    g_udp_tag_publish_generation++;
    g_udp_stats.tag_publish_count++;
    g_udp_stats.tag_published_seq = seq;
    taskEXIT_CRITICAL(&g_udp_lock);
}

void plc_io_get_udp_io_stats(PlcUdpIoStats* stats)
{
    if (!stats) return;
    taskENTER_CRITICAL(&g_udp_lock);
    *stats = g_udp_stats;
    taskEXIT_CRITICAL(&g_udp_lock);
}

void plc_io_clear_udp_io_stats(void)
{
    taskENTER_CRITICAL(&g_udp_lock);
    memset(g_udp_img, 0, sizeof(g_udp_img));
    memset(&g_udp_active_img, 0, sizeof(g_udp_active_img));
    memset(g_udp_tag_img, 0, sizeof(g_udp_tag_img));
    memset(&g_udp_tag_active_img, 0, sizeof(g_udp_tag_active_img));
    memset(&g_udp_stats, 0, sizeof(g_udp_stats));
    g_udp_publish_index = 0;
    g_udp_publish_generation = 0;
    g_udp_consumed_generation = 0;
    g_udp_tag_publish_index = 0;
    g_udp_tag_publish_generation = 0;
    g_udp_tag_consumed_generation = 0;
    taskEXIT_CRITICAL(&g_udp_lock);
    plc_tags_clear_udp_tag_write_stats();
}


void plc_io_get_snapshot(PlcIoSnapshot* snapshot)
{
    if (!snapshot) return;

    taskENTER_CRITICAL(&g_io_lock);

    memcpy(snapshot->raw_di, g_io.raw_di, sizeof(snapshot->raw_di));
    memcpy(snapshot->debounced_di, g_io.debounced_di, sizeof(snapshot->debounced_di));
    memcpy(snapshot->do_cmd, g_io.do_cmd, sizeof(snapshot->do_cmd));
    memcpy(snapshot->ai, g_io.ai, sizeof(snapshot->ai));
    memcpy(snapshot->ao, g_io.ao, sizeof(snapshot->ao));

    snapshot->raw_di_mask = make_mask_u8(g_io.raw_di, PLC_DI_COUNT);
    snapshot->di_mask = make_mask_u8(g_io.debounced_di, PLC_DI_COUNT);
    snapshot->do_mask = make_mask_u8(g_io.do_cmd, PLC_DO_COUNT);
    snapshot->tick_count = g_io.tick_count;
    snapshot->script_scan_count = g_io.script_scan_count;
    snapshot->output_write_count = g_io.output_write_count;

    taskEXIT_CRITICAL(&g_io_lock);
}

void plc_io_get_status_json(char* out, size_t out_len)
{
    if (!out || out_len == 0) return;

    uint32_t raw_mask;
    uint32_t di_mask;
    uint32_t do_mask;
    uint32_t tick_count;
    uint32_t script_scan_count;
    uint32_t output_write_count;
    float ai0, ai1, ao0, ao1;
    PlcUdpIoStats udp_stats;

    taskENTER_CRITICAL(&g_io_lock);
    raw_mask = make_mask_u8(g_io.raw_di, PLC_DI_COUNT);
    di_mask = make_mask_u8(g_io.debounced_di, PLC_DI_COUNT);
    do_mask = make_mask_u8(g_io.do_cmd, PLC_DO_COUNT);
    tick_count = g_io.tick_count;
    script_scan_count = g_io.script_scan_count;
    output_write_count = g_io.output_write_count;
    ai0 = g_io.ai[0];
    ai1 = g_io.ai[1];
    ao0 = g_io.ao[0];
    ao1 = g_io.ao[1];
    taskEXIT_CRITICAL(&g_io_lock);

    plc_io_get_udp_io_stats(&udp_stats);

    snprintf(out, out_len,
             "{\"di_count\":%u,\"do_count\":%u,\"raw_di_mask\":%lu,\"di_mask\":%lu,\"do_mask\":%lu,"
             "\"tick_count\":%lu,\"script_scan_count\":%lu,\"output_write_count\":%lu,"
             "\"ai0\":%.3f,\"ai1\":%.3f,\"ao0\":%.3f,\"ao1\":%.3f,"
             "\"udp_io_active\":%lu,\"udp_io_rx_publish_count\":%lu,\"udp_io_consume_count\":%lu,"
             "\"udp_io_missed_update_count\":%lu,\"udp_io_published_seq\":%lu,\"udp_io_consumed_seq\":%lu,"
             "\"udp_io_last_di_mask\":%lu,\"udp_io_last_age_us\":%lu,\"udp_io_max_age_us\":%lu,"
             "\"udp_tag_publish_count\":%lu,\"udp_tag_consume_count\":%lu,\"udp_tag_missed_update_count\":%lu,"
             "\"udp_tag_published_seq\":%lu,\"udp_tag_consumed_seq\":%lu,"
             "\"udp_tag_values_written\":%lu,\"udp_tag_write_last_us\":%lu,\"udp_tag_write_max_us\":%lu,"
             "\"udp_tag_cache_ready\":%lu,\"udp_tag_last_age_us\":%lu,\"udp_tag_max_age_us\":%lu}",
             (unsigned)PLC_DI_COUNT,
             (unsigned)PLC_DO_COUNT,
             (unsigned long)raw_mask,
             (unsigned long)di_mask,
             (unsigned long)do_mask,
             (unsigned long)tick_count,
             (unsigned long)script_scan_count,
             (unsigned long)output_write_count,
             (double)ai0,
             (double)ai1,
             (double)ao0,
             (double)ao1,
             (unsigned long)udp_stats.active,
             (unsigned long)udp_stats.rx_publish_count,
             (unsigned long)udp_stats.consume_count,
             (unsigned long)udp_stats.missed_update_count,
             (unsigned long)udp_stats.published_seq,
             (unsigned long)udp_stats.consumed_seq,
             (unsigned long)udp_stats.last_di_mask,
             (unsigned long)udp_stats.last_age_us,
             (unsigned long)udp_stats.max_age_us,
             (unsigned long)udp_stats.tag_publish_count,
             (unsigned long)udp_stats.tag_consume_count,
             (unsigned long)udp_stats.tag_missed_update_count,
             (unsigned long)udp_stats.tag_published_seq,
             (unsigned long)udp_stats.tag_consumed_seq,
             (unsigned long)udp_stats.tag_values_written,
             (unsigned long)udp_stats.tag_write_last_us,
             (unsigned long)udp_stats.tag_write_max_us,
             (unsigned long)udp_stats.tag_cache_ready,
             (unsigned long)udp_stats.tag_last_age_us,
             (unsigned long)udp_stats.tag_max_age_us);
}
