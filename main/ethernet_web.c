#include "ethernet_web.hpp"
#include "script_engine.hpp"
#include "plc_io.hpp"
#include "plc_tags.hpp"
#include "plc_filesystem.hpp"
#include "perf_test.hpp"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_http_server.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_eth_phy_ip101.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "ETH_WEB";

#define LARGE_HTML_SIZE_BYTES   (75 * 1024)
#define LARGE_JSON_ITEMS        2048
#define CHUNK_SIZE              512

// Web transfer shaping for reduced PLC jitter.
#define WEB_SEND_CHUNK_SIZE      1024
#define WEB_SEND_YIELD_EVERY     2
#define WEB_RECV_CHUNK_SIZE      4096

// /api/plc_data HMI-like JSON transfer shaping.
#define PLC_DATA_CHUNK_SIZE       512
#define PLC_DATA_YIELD_EVERY      4
#define PLC_DATA_SIM_COUNT        0
#define PLC_DATA_JSON_CAP          (24 * 1024)

// /api/plc_data cache settings.
// The HTTP GET handler serves this prebuilt JSON buffer instead of rebuilding
// process/tag JSON on every browser/HMI request. This keeps refresh storms from
// repeatedly touching live PLC state, allocating heap, or formatting JSON.
#define PLC_DATA_CACHE_PERIOD_MS    100
#define PLC_DATA_CACHE_STACK_WORDS  8192
#define PLC_DATA_CACHE_PRIORITY     1


static httpd_handle_t g_http_server = NULL;

static SemaphoreHandle_t g_plc_data_cache_mutex = NULL;
static TaskHandle_t g_plc_data_cache_task = NULL;
static char *g_plc_data_cache_json = NULL;
static size_t g_plc_data_cache_len = 0;
static uint32_t g_plc_data_cache_version = 0;
static uint32_t g_plc_data_cache_point_count = 0;
static uint32_t g_plc_data_cache_build_us = 0;
static int64_t g_plc_data_cache_snapshot_us = 0;
static bool g_plc_data_cache_ready = false;

// Static IP settings
static const char *STATIC_IP_ADDR = "192.168.5.210";
static const char *STATIC_NETMASK = "255.255.255.0";
static const char *STATIC_GATEWAY = "192.168.5.1";

// ESP32-P4 / Waveshare-style RMII Ethernet pinout
static const int ETH_PHY_ADDR = 1;
static const int ETH_PHY_RST_GPIO = 51;

static const int ETH_MDC_GPIO = 31;
static const int ETH_MDIO_GPIO = 52;

static const int ETH_RMII_CLK_GPIO = 50;
static const int ETH_RMII_TX_EN_GPIO = 49;
static const int ETH_RMII_TXD0_GPIO = 34;
static const int ETH_RMII_TXD1_GPIO = 35;
static const int ETH_RMII_CRS_DV_GPIO = 28;
static const int ETH_RMII_RXD0_GPIO = 29;
static const int ETH_RMII_RXD1_GPIO = 30;


// Embedded Vue SPA files.
// ESP-IDF EMBED_FILES uses the embedded file basename for linker symbols.
// For example, main/web/index.html becomes _binary_index_html_start.
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t index_css_start[] asm("_binary_index_css_start");
extern const uint8_t index_css_end[]   asm("_binary_index_css_end");

extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[]   asm("_binary_app_js_end");

static esp_err_t send_embedded_chunked(httpd_req_t *req,
                                       const uint8_t *data,
                                       size_t len,
                                       const char *content_type,
                                       const char *cache_control,
                                       const char *log_name)
{
    httpd_resp_set_type(req, content_type);

    // Firmware-hosted SPA assets must not be browser-cached during alpha/dev
    // releases. The ESP32-P4 serves fixed filenames like /assets/app.js and
    // /assets/index.css, so long-lived browser caching can resurrect stale Vue
    // bundles after a firmware update. These headers force Chrome/Edge/etc.
    // to re-fetch the embedded assets after every reload.
    if (cache_control && cache_control[0]) {
        httpd_resp_set_hdr(req, "Cache-Control", cache_control);
    }
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "X-PiLab-Asset-Build", __DATE__ " " __TIME__);

    const int64_t start_us = esp_timer_get_time();
    size_t sent = 0;
    uint32_t chunks = 0;

    while (sent < len) {
        size_t n = len - sent;
        if (n > WEB_SEND_CHUNK_SIZE) {
            n = WEB_SEND_CHUNK_SIZE;
        }

        esp_err_t ret = httpd_resp_send_chunk(req, (const char *)(data + sent), n);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Chunked send failed for %s after %u/%u bytes: %s",
                     log_name ? log_name : "asset",
                     (unsigned)sent, (unsigned)len, esp_err_to_name(ret));
            return ret;
        }

        sent += n;
        chunks++;

        if ((chunks % WEB_SEND_YIELD_EVERY) == 0) {
            vTaskDelay(1);
        }
    }

    esp_err_t ret = httpd_resp_send_chunk(req, NULL, 0);
    const int64_t elapsed_us = esp_timer_get_time() - start_us;
    ESP_LOGI(TAG, "Served %s: %u bytes in %lld us, chunks=%u",
             log_name ? log_name : "asset",
             (unsigned)len, (long long)elapsed_us, (unsigned)chunks);
    return ret;
}

static esp_err_t spa_index_get_handler(httpd_req_t *req)
{
    return send_embedded_chunked(
        req,
        index_html_start,
        (size_t)(index_html_end - index_html_start),
        "text/html; charset=utf-8",
        "no-store, no-cache, must-revalidate, max-age=0",
        "Vue SPA index.html"
    );
}

static esp_err_t spa_css_get_handler(httpd_req_t *req)
{
    return send_embedded_chunked(
        req,
        index_css_start,
        (size_t)(index_css_end - index_css_start),
        "text/css; charset=utf-8",
        "no-store, no-cache, must-revalidate, max-age=0",
        "Vue SPA index.css"
    );
}

static esp_err_t spa_js_get_handler(httpd_req_t *req)
{
    return send_embedded_chunked(
        req,
        app_js_start,
        (size_t)(app_js_end - app_js_start),
        "application/javascript; charset=utf-8",
        "no-store, no-cache, must-revalidate, max-age=0",
        "Vue SPA app.js"
    );
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    const char *json =
        "{"
        "\"device\":\"ESP32-P4\","
        "\"status\":\"running\","
        "\"ethernet\":\"static-ip\","
        "\"ip\":\"192.168.5.210\""
        "}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



static bool flash_write_allowed(httpd_req_t *req)
{
    if (!perf_test_is_plc_running()) {
        return true;
    }

    httpd_resp_set_status(req, "423 Locked");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"ok\":false,\"error\":\"plc_running\","
        "\"message\":\"Flash writes are disabled while PLC runtime is in RUN. Set PLC mode to STOP first.\"}");
    return false;
}

static bool request_wants_run(httpd_req_t *req, bool *out_run)
{
    if (!out_run) return false;

    char query[96] = {0};
    char value[24] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "run", value, sizeof(value)) == ESP_OK) {
            *out_run = (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "run") == 0);
            return true;
        }
        if (httpd_query_key_value(query, "mode", value, sizeof(value)) == ESP_OK) {
            *out_run = (strcasecmp(value, "run") == 0 || strcasecmp(value, "running") == 0);
            return true;
        }
    }

    if (req->content_len == 0 || req->content_len > 128) {
        return false;
    }

    char body[129] = {0};
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return false;
        received += (size_t)r;
    }
    body[received] = '\0';

    if (strstr(body, "\"run\":true") || strstr(body, "\"run\":1") || strstr(body, "RUN")) {
        *out_run = true;
        return true;
    }
    if (strstr(body, "\"run\":false") || strstr(body, "\"run\":0") || strstr(body, "STOP")) {
        *out_run = false;
        return true;
    }

    return false;
}

static esp_err_t plc_mode_get_handler(httpd_req_t *req)
{
    const bool running = perf_test_is_plc_running();
    char json[160];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"running\":%s,\"mode\":\"%s\",\"flash_writes_allowed\":%s}",
             running ? "true" : "false",
             running ? "RUN" : "STOP",
             running ? "false" : "true");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t plc_mode_post_handler(httpd_req_t *req)
{
    bool run = true;
    if (!request_wants_run(req, &run)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"expected run/mode value\"}");
        return ESP_OK;
    }

    perf_test_set_plc_running(run);
    return plc_mode_get_handler(req);
}



static esp_err_t system_overview_get_handler(httpd_req_t *req)
{
    const int64_t now_us = esp_timer_get_time();
    PlcIoSnapshot snap;
    plc_io_get_snapshot(&snap);

    uint32_t cache_version = 0;
    uint32_t cache_points = 0;
    uint32_t cache_build_us = 0;
    size_t cache_bytes = 0;
    int64_t cache_age_ms = -1;

    if (g_plc_data_cache_mutex && xSemaphoreTake(g_plc_data_cache_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        cache_version = g_plc_data_cache_version;
        cache_points = g_plc_data_cache_point_count;
        cache_build_us = g_plc_data_cache_build_us;
        cache_bytes = g_plc_data_cache_len;
        if (g_plc_data_cache_snapshot_us > 0) {
            cache_age_ms = (now_us - g_plc_data_cache_snapshot_us) / 1000;
        }
        xSemaphoreGive(g_plc_data_cache_mutex);
    }

    char active_script_name[96] = {0};
    script_engine_get_active_script_name(active_script_name, sizeof(active_script_name));

    char json[1664];
    snprintf(json, sizeof(json),
        "{"
        "\"device\":\"ESP32-P4\","
        "\"project\":\"PiLab PLC\","
        "\"status\":\"running\","
        "\"plc_mode\":\"%s\","
        "\"plc_running\":%s,"
        "\"flash_writes_allowed\":%s,"
        "\"ip\":\"%s\","
        "\"uptime_ms\":%llu,"
        "\"script_generation\":%lu,"
        "\"active_script_name\":\"%s\","
        "\"active_script_path\":\"%s%s\","
        "\"script_state\":%d,"
        "\"tag_count\":%lu,"
        "\"tick_count\":%lu,"
        "\"script_scan_count\":%lu,"
        "\"output_write_count\":%lu,"
        "\"raw_di_mask\":%lu,"
        "\"di_mask\":%lu,"
        "\"do_mask\":%lu,"
        "\"cache_period_ms\":%u,"
        "\"cache_version\":%lu,"
        "\"cache_points\":%lu,"
        "\"cache_bytes\":%lu,"
        "\"cache_build_us\":%lu,"
        "\"cache_age_ms\":%lld,"
        "\"heap_free\":%lu,"
        "\"heap_internal_free\":%lu,"
        "\"heap_psram_free\":%lu,"
        "\"plc_work_avg_us\":%lu"
        "}",
        perf_test_get_plc_mode_string(),
        perf_test_is_plc_running() ? "true" : "false",
        perf_test_is_plc_running() ? "false" : "true",
        STATIC_IP_ADDR,
        (unsigned long long)(now_us / 1000),
        (unsigned long)script_engine_get_generation(),
        active_script_name,
        active_script_name[0] ? "/scripts/" : "",
        active_script_name,
        (int)script_engine_get_state(),
        (unsigned long)plc_tags_get_count(),
        (unsigned long)snap.tick_count,
        (unsigned long)snap.script_scan_count,
        (unsigned long)snap.output_write_count,
        (unsigned long)snap.raw_di_mask,
        (unsigned long)snap.di_mask,
        (unsigned long)snap.do_mask,
        (unsigned)PLC_DATA_CACHE_PERIOD_MS,
        (unsigned long)cache_version,
        (unsigned long)cache_points,
        (unsigned long)cache_bytes,
        (unsigned long)cache_build_us,
        (long long)cache_age_ms,
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        13ul);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



static esp_err_t command_center_get_handler(httpd_req_t *req)
{
    const int64_t now_us = esp_timer_get_time();
    PlcIoSnapshot snap;
    plc_io_get_snapshot(&snap);

    uint32_t cache_version = 0;
    uint32_t cache_points = 0;
    uint32_t cache_build_us = 0;
    size_t cache_bytes = 0;
    int64_t cache_age_ms = -1;

    if (g_plc_data_cache_mutex && xSemaphoreTake(g_plc_data_cache_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        cache_version = g_plc_data_cache_version;
        cache_points = g_plc_data_cache_point_count;
        cache_build_us = g_plc_data_cache_build_us;
        cache_bytes = g_plc_data_cache_len;
        if (g_plc_data_cache_snapshot_us > 0) {
            cache_age_ms = (now_us - g_plc_data_cache_snapshot_us) / 1000;
        }
        xSemaphoreGive(g_plc_data_cache_mutex);
    }

    char active_script_name[96] = {0};
    script_engine_get_active_script_name(active_script_name, sizeof(active_script_name));

    // Do not place these large JSON buffers on the httpd task stack.
    // The ESP-IDF httpd task stack is limited; stack buffers here caused
    // Stack protection faults when /api/command_center was requested.
    char *script_json = (char*)heap_caps_malloc(1800, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!script_json) {
        script_json = (char*)heap_caps_malloc(1800, MALLOC_CAP_8BIT);
    }

    char *json = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) {
        json = (char*)heap_caps_malloc(4096, MALLOC_CAP_8BIT);
    }

    if (!script_json || !json) {
        if (script_json) free(script_json);
        if (json) free(json);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"out_of_memory\"}");
        return ESP_OK;
    }

    script_engine_get_status_json(script_json, 1800);

    snprintf(json, 4096,
        "{"
        "\"overview\":{" 
        "\"device\":\"ESP32-P4\","
        "\"project\":\"PiLab PLC\","
        "\"status\":\"running\","
        "\"plc_mode\":\"%s\","
        "\"plc_running\":%s,"
        "\"flash_writes_allowed\":%s,"
        "\"ip\":\"%s\","
        "\"uptime_ms\":%llu,"
        "\"script_generation\":%lu,"
        "\"active_script_name\":\"%s\","
        "\"active_script_path\":\"%s%s\","
        "\"script_state\":%d,"
        "\"tag_count\":%lu,"
        "\"tick_count\":%lu,"
        "\"script_scan_count\":%lu,"
        "\"output_write_count\":%lu,"
        "\"raw_di_mask\":%lu,"
        "\"di_mask\":%lu,"
        "\"do_mask\":%lu,"
        "\"cache_period_ms\":%u,"
        "\"dashboard_period_ms\":1000,"
        "\"cache_version\":%lu,"
        "\"cache_points\":%lu,"
        "\"cache_bytes\":%lu,"
        "\"cache_build_us\":%lu,"
        "\"cache_age_ms\":%lld,"
        "\"heap_free\":%lu,"
        "\"heap_internal_free\":%lu,"
        "\"heap_psram_free\":%lu,"
        "\"plc_work_avg_us\":%lu"
        "},"
        "\"plc\":{" 
        "\"tick_count\":%lu,"
        "\"script_scan_count\":%lu,"
        "\"output_write_count\":%lu,"
        "\"raw_di_mask\":%lu,"
        "\"di_mask\":%lu,"
        "\"do_mask\":%lu"
        "},"
        "\"script\":%s"
        "}",
        perf_test_get_plc_mode_string(),
        perf_test_is_plc_running() ? "true" : "false",
        perf_test_is_plc_running() ? "false" : "true",
        STATIC_IP_ADDR,
        (unsigned long long)(now_us / 1000),
        (unsigned long)script_engine_get_generation(),
        active_script_name,
        active_script_name[0] ? "/scripts/" : "",
        active_script_name,
        (int)script_engine_get_state(),
        (unsigned long)plc_tags_get_count(),
        (unsigned long)snap.tick_count,
        (unsigned long)snap.script_scan_count,
        (unsigned long)snap.output_write_count,
        (unsigned long)snap.raw_di_mask,
        (unsigned long)snap.di_mask,
        (unsigned long)snap.do_mask,
        (unsigned)PLC_DATA_CACHE_PERIOD_MS,
        (unsigned long)cache_version,
        (unsigned long)cache_points,
        (unsigned long)cache_bytes,
        (unsigned long)cache_build_us,
        (long long)cache_age_ms,
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        13ul,
        (unsigned long)snap.tick_count,
        (unsigned long)snap.script_scan_count,
        (unsigned long)snap.output_write_count,
        (unsigned long)snap.raw_di_mask,
        (unsigned long)snap.di_mask,
        (unsigned long)snap.do_mask,
        script_json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(script_json);
    free(json);
    return ESP_OK;
}

static esp_err_t large_page_get_handler(httpd_req_t *req)
{
    char chunk[CHUNK_SIZE];

    httpd_resp_set_type(req, "text/html");

    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head><title>ESP32-P4 Large Test</title></head><body>"
        "<h1>ESP32-P4 Large Page Test</h1>"
        "<p>This page is dynamically generated to test larger HTTP payload transfer.</p>"
        "<pre>"
    );

    size_t sent = 0;
    uint32_t line = 0;

    while (sent < LARGE_HTML_SIZE_BYTES) {
        int len = snprintf(
            chunk,
            sizeof(chunk),
            "Line %06lu | PLC_VAR_A=%lu | PLC_VAR_B=%lu | STATUS=OK | "
            "This simulates a larger HMI/configuration/editor page payload.\n",
            (unsigned long)line,
            (unsigned long)(line * 3),
            (unsigned long)(line * 7)
        );

        if (len <= 0) {
            break;
        }

        esp_err_t ret = httpd_resp_send_chunk(req, chunk, len);
        if (ret != ESP_OK) {
            return ret;
        }

        sent += len;
        line++;
    }

    httpd_resp_sendstr_chunk(req, "</pre></body></html>");
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t large_status_get_handler(httpd_req_t *req)
{
    char chunk[CHUNK_SIZE];

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr_chunk(req,
        "{"
        "\"device\":\"ESP32-P4\","
        "\"test\":\"large_status\","
        "\"description\":\"Large dynamically generated PLC/HMI status payload\","
        "\"variables\":["
    );

    for (int i = 0; i < LARGE_JSON_ITEMS; i++) {
        int len = snprintf(
            chunk,
            sizeof(chunk),
            "%s{\"id\":%d,\"name\":\"PLC_VAR_%04d\",\"value\":%d,\"quality\":\"GOOD\",\"timestamp_us\":%llu}",
            (i == 0) ? "" : ",",
            i,
            i,
            i * 10,
            (unsigned long long)esp_timer_get_time()
        );

        if (len <= 0) {
            break;
        }

        esp_err_t ret = httpd_resp_send_chunk(req, chunk, len);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}






static esp_err_t script_status_get_handler(httpd_req_t *req)
{
    char *json = (char*)heap_caps_malloc(1800, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) {
        json = (char*)heap_caps_malloc(1800, MALLOC_CAP_8BIT);
    }
    if (!json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"out_of_memory\"}");
        return ESP_OK;
    }

    script_engine_get_status_json(json, 1800);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}


static esp_err_t plc_io_status_get_handler(httpd_req_t *req)
{
    char json[768];
    plc_io_get_status_json(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


static esp_err_t __attribute__((unused)) send_json_chunk(httpd_req_t *req,
                                 const char *chunk,
                                 int len,
                                 uint32_t *chunk_count)
{
    if (len <= 0) return ESP_OK;

    esp_err_t ret = httpd_resp_send_chunk(req, chunk, len);
    if (ret != ESP_OK) return ret;

    if (chunk_count) {
        (*chunk_count)++;
        if (((*chunk_count) % PLC_DATA_YIELD_EVERY) == 0) {
            vTaskDelay(1);
        }
    }

    return ESP_OK;
}

static bool append_jsonf(char **p, size_t *rem, const char *fmt, ...)
{
    if (!p || !*p || !rem || *rem == 0 || !fmt) return false;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(*p, *rem, fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= *rem) {
        if (*rem > 0) {
            **p = '\0';
        }
        return false;
    }

    *p += n;
    *rem -= (size_t)n;
    return true;
}

static bool plc_data_build_json_into(char *json,
                                     size_t json_cap,
                                     PlcTagInfo *user_tags,
                                     size_t user_tags_cap,
                                     size_t *out_len,
                                     uint32_t *out_point_count,
                                     uint32_t *out_build_us,
                                     int64_t *out_snapshot_us)
{
    if (!json || json_cap == 0 || !out_len || !out_point_count) {
        return false;
    }

    const int64_t start_us = esp_timer_get_time();

    PlcIoSnapshot snap;
    plc_io_get_snapshot(&snap);

    char active_script_name[96] = {0};
    script_engine_get_active_script_name(active_script_name, sizeof(active_script_name));

    size_t user_tag_count = 0;
    size_t visible_user_tag_count = 0;
    if (user_tags && user_tags_cap > 0) {
        user_tag_count = plc_tags_copy_all(user_tags, user_tags_cap);
        for (size_t i = 0; i < user_tag_count; ++i) {
            if (user_tags[i].hmi_visible) {
                visible_user_tag_count++;
            }
        }
    }

    char *p = json;
    size_t rem = json_cap;
    bool ok = true;
    uint32_t point_count = 0;

    ok = ok && append_jsonf(&p, &rem,
        "{\"endpoint\":\"/api/plc_data\"," 
        "\"cached\":true,"
        "\"cache_period_ms\":%u,"
        "\"snapshot_us\":%llu,"
        "\"tick_count\":%lu,"
        "\"script_scan_count\":%lu,"
        "\"output_write_count\":%lu,"
        "\"raw_di_mask\":%lu,"
        "\"di_mask\":%lu,"
        "\"do_mask\":%lu,"
        "\"active_script_name\":\"%s\","
        "\"active_script_path\":\"%s%s\","
        "\"points\":[",
        (unsigned)PLC_DATA_CACHE_PERIOD_MS,
        (unsigned long long)start_us,
        (unsigned long)snap.tick_count,
        (unsigned long)snap.script_scan_count,
        (unsigned long)snap.output_write_count,
        (unsigned long)snap.raw_di_mask,
        (unsigned long)snap.di_mask,
        (unsigned long)snap.do_mask,
        active_script_name,
        active_script_name[0] ? "/scripts/" : "",
        active_script_name);

    bool first = true;

    for (int i = 0; ok && i < PLC_DI_COUNT; ++i) {
        ok = append_jsonf(&p, &rem,
            "%s{\"name\":\"I%d\",\"kind\":\"bool\",\"source\":\"physical\",\"value\":%s,\"raw\":%s}",
            first ? "" : ",",
            i,
            snap.debounced_di[i] ? "true" : "false",
            snap.raw_di[i] ? "true" : "false");
        first = false;
        point_count++;
    }

    for (int i = 0; ok && i < PLC_DO_COUNT; ++i) {
        ok = append_jsonf(&p, &rem,
            ",{\"name\":\"Q%d\",\"kind\":\"bool\",\"source\":\"physical\",\"value\":%s}",
            i,
            snap.do_cmd[i] ? "true" : "false");
        point_count++;
    }

    for (int i = 0; ok && i < PLC_AI_COUNT; ++i) {
        ok = append_jsonf(&p, &rem,
            ",{\"name\":\"AI%d\",\"kind\":\"float\",\"source\":\"physical\",\"value\":%.3f}",
            i,
            (double)snap.ai[i]);
        point_count++;
    }

    for (int i = 0; ok && i < PLC_AO_COUNT; ++i) {
        ok = append_jsonf(&p, &rem,
            ",{\"name\":\"AO%d\",\"kind\":\"float\",\"source\":\"physical\",\"value\":%.3f}",
            i,
            (double)snap.ao[i]);
        point_count++;
    }

    for (size_t i = 0; ok && i < user_tag_count; ++i) {
        const PlcTagInfo *t = &user_tags[i];
        if (!t->hmi_visible) continue;

        if (t->type == PLC_TAG_BOOL) {
            ok = append_jsonf(&p, &rem,
                ",{\"name\":\"%s\",\"kind\":\"bool\",\"source\":\"user\",\"writable\":%s,\"value\":%s}",
                t->name,
                t->writable ? "true" : "false",
                t->value.b ? "true" : "false");
        } else if (t->type == PLC_TAG_INT) {
            ok = append_jsonf(&p, &rem,
                ",{\"name\":\"%s\",\"kind\":\"int\",\"source\":\"user\",\"writable\":%s,\"value\":%ld}",
                t->name,
                t->writable ? "true" : "false",
                (long)t->value.i);
        } else {
            ok = append_jsonf(&p, &rem,
                ",{\"name\":\"%s\",\"kind\":\"float\",\"source\":\"user\",\"writable\":%s,\"value\":%.6g}",
                t->name,
                t->writable ? "true" : "false",
                (double)t->value.f);
        }
        point_count++;
    }

#if PLC_DATA_SIM_COUNT > 0
    for (int i = 0; ok && i < PLC_DATA_SIM_COUNT; ++i) {
        const int selector = i % 3;
        const uint32_t t = snap.tick_count;

        if (selector == 0) {
            bool v = (((t / 100u) + (uint32_t)i) & 1u) != 0u;
            ok = append_jsonf(&p, &rem,
                ",{\"name\":\"SIM_BOOL_%03d\",\"kind\":\"bool\",\"source\":\"sim\",\"value\":%s}",
                i,
                v ? "true" : "false");
        } else if (selector == 1) {
            int v = (int)((t + (uint32_t)(i * 37)) % 10000u);
            ok = append_jsonf(&p, &rem,
                ",{\"name\":\"SIM_INT_%03d\",\"kind\":\"int\",\"source\":\"sim\",\"value\":%d}",
                i,
                v);
        } else {
            float v = (float)(((t % 5000u) * 0.01f) + (float)i * 0.125f);
            ok = append_jsonf(&p, &rem,
                ",{\"name\":\"SIM_FLOAT_%03d\",\"kind\":\"float\",\"source\":\"sim\",\"value\":%.3f}",
                i,
                (double)v);
        }
        point_count++;
    }
#endif

    const uint32_t build_us = (uint32_t)(esp_timer_get_time() - start_us);
    ok = ok && append_jsonf(&p, &rem,
        "],\"point_count\":%lu,\"sim_count\":%u,\"user_tag_count\":%lu,\"build_us\":%lu}",
        (unsigned long)point_count,
        (unsigned)PLC_DATA_SIM_COUNT,
        (unsigned long)visible_user_tag_count,
        (unsigned long)build_us);

    if (!ok) {
        if (json_cap > 0) json[0] = '\0';
        return false;
    }

    *out_len = (size_t)(p - json);
    *out_point_count = point_count;
    if (out_build_us) *out_build_us = build_us;
    if (out_snapshot_us) *out_snapshot_us = start_us;
    return true;
}

static void plc_data_cache_task(void *arg)
{
    (void)arg;

    // Keep the cached PLC JSON buffers in PSRAM. The cache task is not hard real-time,
    // and preserving internal RAM matters more than shaving a few microseconds here.
    char *build_json = (char*)heap_caps_malloc(PLC_DATA_JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!build_json) {
        build_json = (char*)heap_caps_malloc(PLC_DATA_JSON_CAP, MALLOC_CAP_8BIT);
    }

    PlcTagInfo *user_tags = (PlcTagInfo*)heap_caps_calloc(
        PLC_TAG_MAX_COUNT,
        sizeof(PlcTagInfo),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (!user_tags) {
        user_tags = (PlcTagInfo*)heap_caps_calloc(
            PLC_TAG_MAX_COUNT,
            sizeof(PlcTagInfo),
            MALLOC_CAP_8BIT
        );
    }

    if (!build_json || !user_tags) {
        ESP_LOGE(TAG, "PLC data cache task allocation failed");
        if (build_json) heap_caps_free(build_json);
        if (user_tags) heap_caps_free(user_tags);
        g_plc_data_cache_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "PLC data cache task started: period=%u ms", (unsigned)PLC_DATA_CACHE_PERIOD_MS);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t update_count = 0;

    for (;;) {
        size_t build_len = 0;
        uint32_t point_count = 0;
        uint32_t build_us = 0;
        int64_t snapshot_us = 0;

        bool ok = plc_data_build_json_into(
            build_json,
            PLC_DATA_JSON_CAP,
            user_tags,
            PLC_TAG_MAX_COUNT,
            &build_len,
            &point_count,
            &build_us,
            &snapshot_us
        );

        if (ok && build_len > 0 && build_len < PLC_DATA_JSON_CAP) {
            if (g_plc_data_cache_mutex && xSemaphoreTake(g_plc_data_cache_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(g_plc_data_cache_json, build_json, build_len);
                g_plc_data_cache_json[build_len] = '\0';
                g_plc_data_cache_len = build_len;
                g_plc_data_cache_point_count = point_count;
                g_plc_data_cache_build_us = build_us;
                g_plc_data_cache_snapshot_us = snapshot_us;
                g_plc_data_cache_version++;
                g_plc_data_cache_ready = true;
                xSemaphoreGive(g_plc_data_cache_mutex);
            }
        } else {
            ESP_LOGW(TAG, "PLC data cache build failed; cap=%u", (unsigned)PLC_DATA_JSON_CAP);
        }

        update_count++;
        if (build_us > 2000u) {
            ESP_LOGW(TAG, "PLC data cache slow build: version=%lu points=%lu bytes=%lu build_us=%lu",
                     (unsigned long)g_plc_data_cache_version,
                     (unsigned long)g_plc_data_cache_point_count,
                     (unsigned long)g_plc_data_cache_len,
                     (unsigned long)g_plc_data_cache_build_us);
        } else if ((update_count % 1000u) == 0u) {
            ESP_LOGI(TAG, "PLC data cache: version=%lu points=%lu bytes=%lu build_us=%lu",
                     (unsigned long)g_plc_data_cache_version,
                     (unsigned long)g_plc_data_cache_point_count,
                     (unsigned long)g_plc_data_cache_len,
                     (unsigned long)g_plc_data_cache_build_us);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PLC_DATA_CACHE_PERIOD_MS));
    }
}

static esp_err_t plc_data_get_handler(httpd_req_t *req)
{
    if (!g_plc_data_cache_mutex || !g_plc_data_cache_json) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "PLC data cache not initialized");
        return ESP_OK;
    }

    if (xSemaphoreTake(g_plc_data_cache_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "PLC data cache busy");
        return ESP_OK;
    }

    const bool ready = g_plc_data_cache_ready;
    const size_t len = g_plc_data_cache_len;

    if (!ready || len == 0) {
        xSemaphoreGive(g_plc_data_cache_mutex);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "PLC data cache warming up");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-PLC-Data-Source", "cache");

    // Keep the mutex while sending so the cache task cannot overwrite the
    // buffer mid-transfer. This only blocks the low-priority cache task, not
    // the PLC scan or script task.
    esp_err_t ret = httpd_resp_send(req, g_plc_data_cache_json, len);
    xSemaphoreGive(g_plc_data_cache_mutex);
    return ret;
}


#define LITTLEFS_MOUNT_POINT       "/littlefs"
#define FILE_API_PATH_MAX          192
#define FILE_API_FS_PATH_MAX       (sizeof(LITTLEFS_MOUNT_POINT) + FILE_API_PATH_MAX)
#define FILE_API_UPLOAD_MAX_BYTES  (512 * 1024)
#define FILE_API_VIEW_MAX_BYTES    (128 * 1024)
#define FILE_API_IO_CHUNK          1024

static int hex_digit_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static void url_decode_inplace(char *s)
{
    if (!s) return;
    char *d = s;
    for (char *p = s; *p; ++p) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = hex_digit_to_int(p[1]);
            int lo = hex_digit_to_int(p[2]);
            if (hi >= 0 && lo >= 0) {
                *d++ = (char)((hi << 4) | lo);
                p += 2;
                continue;
            }
        }
        *d++ = (*p == '+') ? ' ' : *p;
    }
    *d = 0;
}

static bool file_api_get_query_path(httpd_req_t *req, char *out_path, size_t out_len)
{
    if (!out_path || out_len == 0) return false;
    out_path[0] = 0;

    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        strlcpy(out_path, "/", out_len);
        return true;
    }

    char value[FILE_API_PATH_MAX] = {0};
    if (httpd_query_key_value(query, "path", value, sizeof(value)) != ESP_OK) {
        strlcpy(out_path, "/", out_len);
        return true;
    }

    url_decode_inplace(value);
    if (value[0] == 0) {
        strlcpy(out_path, "/", out_len);
    } else {
        strlcpy(out_path, value, out_len);
    }
    return true;
}

static bool file_api_sanitize_path(const char *input, char *out_rel, size_t out_rel_len, char *err, size_t err_len)
{
    if (!input || !out_rel || out_rel_len < 2) return false;

    char tmp[FILE_API_PATH_MAX] = {0};
    strlcpy(tmp, input[0] ? input : "/", sizeof(tmp));

    for (char *p = tmp; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

    if (strstr(tmp, "..")) {
        snprintf(err, err_len, "Parent directory segments are not allowed");
        return false;
    }

    char cleaned[FILE_API_PATH_MAX] = "/";
    size_t wr = 1;
    const char *p = tmp;
    while (*p == '/') p++;
    while (*p && wr + 1 < sizeof(cleaned)) {
        if (*p == '/') {
            while (*p == '/') p++;
            if (!*p) break;
            cleaned[wr++] = '/';
        } else {
            unsigned char c = (unsigned char)*p++;
            if (c < 32) continue;
            cleaned[wr++] = (char)c;
        }
    }
    cleaned[wr] = 0;

    if (wr > 1 && cleaned[wr - 1] == '/') {
        cleaned[wr - 1] = 0;
    }

    strlcpy(out_rel, cleaned, out_rel_len);
    return true;
}

static bool file_api_to_fs_path(const char *input, char *out_fs, size_t out_fs_len, char *out_rel, size_t out_rel_len, char *err, size_t err_len)
{
    char rel[FILE_API_PATH_MAX] = {0};
    if (!file_api_sanitize_path(input, rel, sizeof(rel), err, err_len)) return false;

    int n = snprintf(out_fs, out_fs_len, "%s%s", LITTLEFS_MOUNT_POINT, rel);
    if (n < 0 || (size_t)n >= out_fs_len) {
        snprintf(err, err_len, "Path is too long");
        return false;
    }
    if (out_rel && out_rel_len) strlcpy(out_rel, rel, out_rel_len);
    return true;
}

static void json_escape_chunk(httpd_req_t *req, const char *s)
{
    char tmp[8];
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            tmp[0] = '\\'; tmp[1] = (char)c; tmp[2] = 0;
            httpd_resp_send_chunk(req, tmp, HTTPD_RESP_USE_STRLEN);
        } else if (c == '\n') {
            httpd_resp_send_chunk(req, "\\n", 2);
        } else if (c == '\r') {
            httpd_resp_send_chunk(req, "\\r", 2);
        } else if (c == '\t') {
            httpd_resp_send_chunk(req, "\\t", 2);
        } else if (c < 32) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            httpd_resp_send_chunk(req, tmp, HTTPD_RESP_USE_STRLEN);
        } else {
            httpd_resp_send_chunk(req, (const char*)s, 1);
        }
    }
}

static const char *file_api_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}


static bool file_api_join_rel_path(const char *base, const char *name, char *out, size_t out_len)
{
    if (!base || !name || !out || out_len == 0) return false;

    int n = 0;
    if (strcmp(base, "/") == 0) {
        n = snprintf(out, out_len, "/%s", name);
    } else {
        n = snprintf(out, out_len, "%s/%s", base, name);
    }

    return (n > 0 && (size_t)n < out_len);
}

static esp_err_t files_list_get_handler(httpd_req_t *req)
{
    char req_path[FILE_API_PATH_MAX] = {0};
    char fs_path[FILE_API_FS_PATH_MAX] = {0};
    char rel_path[FILE_API_PATH_MAX] = {0};
    char err[128] = {0};

    file_api_get_query_path(req, req_path, sizeof(req_path));
    if (!file_api_to_fs_path(req_path, fs_path, sizeof(fs_path), rel_path, sizeof(rel_path), err, sizeof(err))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, err);
        return ESP_OK;
    }

    DIR *dir = opendir(fs_path);
    if (!dir) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "Directory not found");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send_chunk(req, "{\"path\":\"", HTTPD_RESP_USE_STRLEN);
    json_escape_chunk(req, rel_path);
    httpd_resp_send_chunk(req, "\",\"entries\":[", HTTPD_RESP_USE_STRLEN);

    bool first = true;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char child_rel[FILE_API_PATH_MAX] = {0};
        if (!file_api_join_rel_path(rel_path, ent->d_name, child_rel, sizeof(child_rel))) {
            ESP_LOGW(TAG, "Skipping over-length file path under %s: %s", rel_path, ent->d_name);
            continue;
        }

        char child_fs[FILE_API_FS_PATH_MAX] = {0};
        snprintf(child_fs, sizeof(child_fs), "%s%s", LITTLEFS_MOUNT_POINT, child_rel);

        struct stat st = {0};
        bool is_dir = false;
        size_t size = 0;
        if (stat(child_fs, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = (size_t)st.st_size;
        }

        if (!first) httpd_resp_send_chunk(req, ",", 1);
        first = false;
        char prefix[96];
        snprintf(prefix, sizeof(prefix), "{\"name\":\"");
        httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN);
        json_escape_chunk(req, ent->d_name);
        httpd_resp_send_chunk(req, "\",\"path\":\"", HTTPD_RESP_USE_STRLEN);
        json_escape_chunk(req, child_rel);
        char suffix[96];
        snprintf(suffix, sizeof(suffix), "\",\"dir\":%s,\"size\":%u}", is_dir ? "true" : "false", (unsigned)size);
        httpd_resp_send_chunk(req, suffix, HTTPD_RESP_USE_STRLEN);
    }

    closedir(dir);
    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t files_mkdir_post_handler(httpd_req_t *req)
{
    if (!flash_write_allowed(req)) return ESP_OK;

    char req_path[FILE_API_PATH_MAX] = {0}, fs_path[FILE_API_FS_PATH_MAX] = {0}, rel_path[FILE_API_PATH_MAX] = {0}, err[128] = {0};
    file_api_get_query_path(req, req_path, sizeof(req_path));
    if (!file_api_to_fs_path(req_path, fs_path, sizeof(fs_path), rel_path, sizeof(rel_path), err, sizeof(err))) {
        httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, err); return ESP_OK;
    }
    if (strcmp(rel_path, "/") == 0) { httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, "Cannot create root"); return ESP_OK; }
    if (mkdir(fs_path, 0775) != 0 && errno != EEXIST) {
        httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, "mkdir failed"); return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, "{\"ok\":true}"); return ESP_OK;
}

static esp_err_t files_upload_post_handler(httpd_req_t *req)
{
    if (!flash_write_allowed(req)) return ESP_OK;

    if (req->content_len == 0 || req->content_len > FILE_API_UPLOAD_MAX_BYTES) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "File must be 1..524288 bytes");
        return ESP_OK;
    }

    char req_path[FILE_API_PATH_MAX] = {0}, fs_path[FILE_API_FS_PATH_MAX] = {0}, rel_path[FILE_API_PATH_MAX] = {0}, err[128] = {0};
    file_api_get_query_path(req, req_path, sizeof(req_path));
    if (!file_api_to_fs_path(req_path, fs_path, sizeof(fs_path), rel_path, sizeof(rel_path), err, sizeof(err))) {
        httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, err); return ESP_OK;
    }
    if (strcmp(rel_path, "/") == 0 || rel_path[strlen(rel_path)-1] == '/') {
        httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, "Upload path must include a filename"); return ESP_OK;
    }

    FILE *f = fopen(fs_path, "wb");
    if (!f) { httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, "open for write failed"); return ESP_OK; }

    char *buf = (char*)heap_caps_malloc(WEB_RECV_CHUNK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char*)heap_caps_malloc(WEB_RECV_CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); unlink(fs_path); httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, "Out of memory"); return ESP_OK; }

    size_t received = 0;
    while (received < req->content_len) {
        size_t want = req->content_len - received;
        if (want > WEB_RECV_CHUNK_SIZE) want = WEB_RECV_CHUNK_SIZE;
        int r = httpd_req_recv(req, buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { heap_caps_free(buf); fclose(f); unlink(fs_path); httpd_resp_set_status(req, "408 Request Timeout"); httpd_resp_sendstr(req, "Upload timeout"); return ESP_OK; }
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) { heap_caps_free(buf); fclose(f); unlink(fs_path); httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, "File write failed"); return ESP_OK; }
        received += (size_t)r;
    }
    heap_caps_free(buf);
    fclose(f);

    ESP_LOGI(TAG, "LittleFS upload: %s, %u bytes", rel_path, (unsigned)received);
    httpd_resp_set_type(req, "application/json");
    char json[128]; snprintf(json, sizeof(json), "{\"ok\":true,\"bytes\":%u}", (unsigned)received);
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t files_send_file(httpd_req_t *req, bool attachment)
{
    char req_path[FILE_API_PATH_MAX] = {0}, fs_path[FILE_API_FS_PATH_MAX] = {0}, rel_path[FILE_API_PATH_MAX] = {0}, err[128] = {0};
    file_api_get_query_path(req, req_path, sizeof(req_path));
    if (!file_api_to_fs_path(req_path, fs_path, sizeof(fs_path), rel_path, sizeof(rel_path), err, sizeof(err))) {
        httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, err); return ESP_OK;
    }

    struct stat st = {0};
    if (stat(fs_path, &st) != 0 || S_ISDIR(st.st_mode)) {
        httpd_resp_set_status(req, "404 Not Found"); httpd_resp_sendstr(req, "File not found"); return ESP_OK;
    }
    if (!attachment && st.st_size > FILE_API_VIEW_MAX_BYTES) {
        httpd_resp_set_status(req, "413 Payload Too Large"); httpd_resp_sendstr(req, "Text view is limited to 128KB. Use download instead."); return ESP_OK;
    }

    FILE *f = fopen(fs_path, "rb");
    if (!f) { httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, "open for read failed"); return ESP_OK; }

    httpd_resp_set_type(req, attachment ? "application/octet-stream" : "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (attachment) {
        char disp[256]; snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", file_api_basename(rel_path));
        httpd_resp_set_hdr(req, "Content-Disposition", disp);
    }

    char *buf = (char*)heap_caps_malloc(FILE_API_IO_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char*)heap_caps_malloc(FILE_API_IO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, "Out of memory"); return ESP_OK; }

    size_t n = 0;
    esp_err_t ret = ESP_OK;
    while ((n = fread(buf, 1, FILE_API_IO_CHUNK, f)) > 0) {
        ret = httpd_resp_send_chunk(req, buf, n);
        if (ret != ESP_OK) break;
        vTaskDelay(1);
    }
    heap_caps_free(buf);
    fclose(f);
    if (ret != ESP_OK) return ret;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t files_download_get_handler(httpd_req_t *req) { return files_send_file(req, true); }
static esp_err_t files_view_get_handler(httpd_req_t *req) { return files_send_file(req, false); }

static esp_err_t files_delete_post_handler(httpd_req_t *req)
{
    if (!flash_write_allowed(req)) return ESP_OK;

    char req_path[FILE_API_PATH_MAX] = {0}, fs_path[FILE_API_FS_PATH_MAX] = {0}, rel_path[FILE_API_PATH_MAX] = {0}, err[128] = {0};
    file_api_get_query_path(req, req_path, sizeof(req_path));
    if (!file_api_to_fs_path(req_path, fs_path, sizeof(fs_path), rel_path, sizeof(rel_path), err, sizeof(err))) {
        httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, err); return ESP_OK;
    }
    if (strcmp(rel_path, "/") == 0) { httpd_resp_set_status(req, "400 Bad Request"); httpd_resp_sendstr(req, "Cannot delete root"); return ESP_OK; }

    struct stat st = {0};
    if (stat(fs_path, &st) != 0) { httpd_resp_set_status(req, "404 Not Found"); httpd_resp_sendstr(req, "Path not found"); return ESP_OK; }

    int rc = S_ISDIR(st.st_mode) ? rmdir(fs_path) : unlink(fs_path);
    if (rc != 0) { httpd_resp_set_status(req, "500 Internal Server Error"); httpd_resp_sendstr(req, S_ISDIR(st.st_mode) ? "rmdir failed; directory must be empty" : "unlink failed"); return ESP_OK; }

    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, "{\"ok\":true}"); return ESP_OK;
}


static esp_err_t api_tags_get_handler(httpd_req_t *req)
{
    char *json = (char*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) json = (char*)heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_FAIL;
    }
    plc_tags_get_json(json, 8192);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    heap_caps_free(json);
    return ret;
}

static esp_err_t read_small_json_body(httpd_req_t *req, char **out_body, size_t max_len)
{
    if (!out_body) return ESP_FAIL;
    *out_body = NULL;
    if (req->content_len == 0 || req->content_len > max_len) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "JSON body size is invalid");
        return ESP_FAIL;
    }
    char *body = (char*)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) body = (char*)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!body) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory receiving JSON");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            heap_caps_free(body);
            httpd_resp_set_status(req, "408 Request Timeout");
            httpd_resp_sendstr(req, "JSON receive timeout");
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    *out_body = body;
    return ESP_OK;
}

static esp_err_t api_tags_post_handler(httpd_req_t *req)
{
    if (!flash_write_allowed(req)) return ESP_OK;

    char *body = NULL;
    if (read_small_json_body(req, &body, 8192) != ESP_OK) return ESP_FAIL;

    char err[160] = {};
    bool ok = plc_tags_load_json(body, err, sizeof(err));
    heap_caps_free(body);

    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, err, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char json[256];
    snprintf(json, sizeof(json), "{\"ok\":true,\"message\":\"%s\",\"count\":%u}", err, (unsigned)plc_tags_get_count());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t plc_write_post_handler(httpd_req_t *req)
{
    char *body = NULL;
    if (read_small_json_body(req, &body, 1024) != ESP_OK) return ESP_FAIL;

    char err[160] = {};
    bool ok = plc_tags_write_value_json(body, err, sizeof(err));
    heap_caps_free(body);

    if (!ok) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, err, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char json[224];
    snprintf(json, sizeof(json), "{\"ok\":true,\"message\":\"%s\"}", err);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


static int script_hex_digit_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static void script_url_decode_inplace(char *s)
{
    if (!s) return;
    char *d = s;
    for (char *p = s; *p; ++p) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = script_hex_digit_to_int(p[1]);
            int lo = script_hex_digit_to_int(p[2]);
            if (hi >= 0 && lo >= 0) {
                *d++ = (char)((hi << 4) | lo);
                p += 2;
                continue;
            }
        }
        *d++ = (*p == '+') ? ' ' : *p;
    }
    *d = 0;
}

static bool upload_script_get_query_name(httpd_req_t *req, char *out_name, size_t out_len)
{
    if (!out_name || out_len == 0) return false;
    strlcpy(out_name, "main.as", out_len);

    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return true;
    }

    char value[96] = {0};
    if (httpd_query_key_value(query, "name", value, sizeof(value)) != ESP_OK) {
        return true;
    }

    script_url_decode_inplace(value);
    if (value[0] != 0) {
        strlcpy(out_name, value, out_len);
    }
    return true;
}

static bool upload_script_sanitize_filename(const char *input, char *out_name, size_t out_len, char *err, size_t err_len)
{
    if (!input || !out_name || out_len < 2) {
        snprintf(err, err_len, "Invalid script filename");
        return false;
    }

    while (*input == ' ' || *input == '\t') input++;

    char tmp[96] = {0};
    strlcpy(tmp, *input ? input : "main.as", sizeof(tmp));

    size_t len = strlen(tmp);
    while (len > 0 && (tmp[len - 1] == ' ' || tmp[len - 1] == '\t' || tmp[len - 1] == '\r' || tmp[len - 1] == '\n')) {
        tmp[--len] = 0;
    }

    if (len == 0) {
        strlcpy(tmp, "main.as", sizeof(tmp));
        len = strlen(tmp);
    }

    if (strstr(tmp, "..") || strchr(tmp, '/') || strchr(tmp, '\\')) {
        snprintf(err, err_len, "Script name must be a filename only, not a path");
        return false;
    }

    for (size_t i = 0; tmp[i]; ++i) {
        unsigned char c = (unsigned char)tmp[i];
        const bool ok = (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) {
            snprintf(err, err_len, "Script name may only contain letters, numbers, dot, dash, and underscore");
            return false;
        }
    }

    if (strcmp(tmp, ".") == 0 || tmp[0] == '.') {
        snprintf(err, err_len, "Script name must not start with a dot");
        return false;
    }

    if (strlen(tmp) >= out_len) {
        snprintf(err, err_len, "Script name is too long");
        return false;
    }

    strlcpy(out_name, tmp, out_len);
    return true;
}

static esp_err_t upload_script_post_handler(httpd_req_t *req)
{
    const size_t max_script_size = 128 * 1024;

    if (req->content_len == 0 || req->content_len > max_script_size) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "Script must be 1..131072 bytes");
        return ESP_FAIL;
    }

    // Single-flight reload protection: reject before reading/copying the body
    // when the compiler is busy, a compiled program is pending activation, or
    // the reload cooldown has not expired. This prevents browser/script spam
    // from piling up compile jobs and repeatedly churning AngelScript engines.
    char early_response[256] = {0};
    if (!script_engine_can_accept_upload(early_response, sizeof(early_response))) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, early_response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char *script = (char*)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!script) {
        // Keep a fallback so boards without PSRAM, or with PSRAM disabled, can still work.
        script = (char*)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!script) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory receiving script");
        return ESP_FAIL;
    }

    const int64_t recv_start_us = esp_timer_get_time();
    size_t received = 0;
    uint32_t recv_chunks = 0;

    while (received < req->content_len) {
        size_t want = req->content_len - received;
        if (want > WEB_RECV_CHUNK_SIZE) {
            want = WEB_RECV_CHUNK_SIZE;
        }

        int r = httpd_req_recv(req, script + received, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            // Do not fail immediately on a transient socket timeout.
            continue;
        }
        if (r <= 0) {
            heap_caps_free(script);
            httpd_resp_set_status(req, "408 Request Timeout");
            httpd_resp_sendstr(req, "Upload timeout");
            return ESP_FAIL;
        }

        received += (size_t)r;
        recv_chunks++;

        // Deliberately no vTaskDelay() here. For incoming uploads, delaying every
        // chunk stretched the TCP receive window and made the jitter last longer.
        // Keep chunks bounded, but finish receiving as quickly as possible.
    }

    const int64_t recv_us = esp_timer_get_time() - recv_start_us;
    ESP_LOGI(TAG, "Script upload receive complete: %u bytes in %lld us, chunks=%u, chunk_size=%u",
             (unsigned)received,
             (long long)recv_us,
             (unsigned)recv_chunks,
             (unsigned)WEB_RECV_CHUNK_SIZE);

    char requested_name[96] = {0};
    char script_filename[96] = {0};
    char save_err[160] = {0};
    char saved_rel_path[128] = {0};

    upload_script_get_query_name(req, requested_name, sizeof(requested_name));
    if (!upload_script_sanitize_filename(requested_name, script_filename, sizeof(script_filename), save_err, sizeof(save_err))) {
        heap_caps_free(script);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, save_err, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    snprintf(saved_rel_path, sizeof(saved_rel_path), "/scripts/%s", script_filename);

    const int64_t submit_start_us = esp_timer_get_time();
    char response[256] = {0};
    bool accepted = script_engine_submit_compile_text(script, received, script_filename, response, sizeof(response));
    const int64_t submit_us = esp_timer_get_time() - submit_start_us;
    ESP_LOGI(TAG, "Script upload submit result: accepted=%d, submit_us=%lld", accepted ? 1 : 0, (long long)submit_us);
    heap_caps_free(script);

    if (!accepted) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char json[384];
    snprintf(json, sizeof(json),
        "{\"accepted\":true,\"message\":\"%s\",\"save_after_compile\":true,\"saved_path\":\"%s\",\"status_url\":\"/api/script_status\"}",
        response,
        saved_rel_path);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



// -----------------------------------------------------------------------------
// PiLab runtime settings API
// -----------------------------------------------------------------------------
// Runtime POST updates replace the in-RAM JSON for a settings page and mark it
// dirty. Persistent save is explicit and writes /littlefs/config/<route>.json;
// the save route reuses flash_write_allowed(), so it is blocked in PLC RUN mode.

#define SETTINGS_MAX_JSON_BYTES 2048
#define SETTINGS_ROUTE_COUNT 9

typedef struct {
    const char *route;
    const char *default_json;
    char json[SETTINGS_MAX_JSON_BYTES];
    bool loaded;
    bool dirty;
} RuntimeSettingsPage;

static RuntimeSettingsPage g_settings_pages[SETTINGS_ROUTE_COUNT] = {
    { "system", "{\"deviceName\":\"PiLab PLC\",\"location\":\"Bench / Lab\",\"bootBehavior\":\"stopped\",\"startupScript\":\"\",\"ntpEnabled\":true,\"timezone\":\"America/Toronto\"}", {0}, false, false },
    { "network", "{\"hostname\":\"pilab-p4\",\"mdnsName\":\"pilab-p4.local\",\"ipMode\":\"static\",\"staticIpv4\":\"192.168.5.210\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.5.1\",\"dns\":\"192.168.5.1\",\"ntpServer\":\"pool.ntp.org\"}", {0}, false, false },
    { "logging", "{\"espConsoleLogLevel\":\"warn\",\"pilabScriptLogLevel\":\"info\",\"retainedEvents\":250,\"serialConsoleLogs\":true,\"includeCompileLog\":true,\"includeTimingSamples\":true,\"includeTagSnapshot\":true,\"exportFormat\":\"jsonBundle\"}", {0}, false, false },
    { "script-engine", "{\"compileBeforeRun\":true,\"scriptWatchdog\":true,\"watchdogBudgetUs\":5000,\"startupCompileAction\":\"stopOnError\",\"registeredAddons\":\"Arrays, Math, Strings\",\"tagBinding\":\"auto\",\"scriptUpdateMode\":\"pauseDuringCompile\"}", {0}, false, false },
    { "performance", "{\"scriptScanBudgetUs\":5000,\"warnAtPercent\":75,\"dashboardPollMs\":1000,\"tagCachePollMs\":100,\"plcDataCacheMs\":100,\"telemetryEnabled\":true,\"overrunPolicy\":\"faultAfterLimit\",\"scriptTimeMode\":\"actualClamped\",\"maxDeltaUs\":50000,\"faultAfterConsecutiveOverruns\":20,\"faultAfterCoalescedScans\":100}", {0}, false, false },
    { "storage", "{\"writesWhileRunning\":\"blocked\",\"backupPath\":\"/backups\",\"userFilesRoot\":\"/user\",\"maxUploadKb\":512,\"configSaveMode\":\"atomic\",\"keepBackups\":5}", {0}, false, false },
    { "security", "{\"authentication\":\"disabled\",\"writeConfirmation\":true,\"sessionTimeoutMin\":30,\"apiTokenConfigured\":false,\"corsPolicy\":\"sameOrigin\",\"readonlyGuestMode\":false}", {0}, false, false },
    { "updates", "{\"firmwareVersion\":\"Development build\",\"webAppVersion\":\"Development build\",\"updateMethod\":\"manual\",\"rollbackSlot\":\"notConfigured\",\"preUpdateAction\":\"stopPlc\",\"backupBeforeUpdate\":true}", {0}, false, false },
    { "diagnostics", "{\"includeRuntimeStatus\":true,\"includeTaskStats\":true,\"includeHeapInfo\":true,\"includeFileManifest\":true,\"includeScriptSource\":\"optional\",\"snapshotFormat\":\"jsonText\"}", {0}, false, false },
};

static RuntimeSettingsPage *settings_find_page(const char *route)
{
    if (!route || !route[0]) {
        return NULL;
    }
    for (size_t i = 0; i < SETTINGS_ROUTE_COUNT; ++i) {
        if (strcmp(g_settings_pages[i].route, route) == 0) {
            return &g_settings_pages[i];
        }
    }
    return NULL;
}

static void settings_path_for(const RuntimeSettingsPage *page, char *out, size_t out_len)
{
    snprintf(out, out_len, PLC_FS_MOUNT_POINT "/config/%s.json", page->route);
}

// Minimal JSON object sanity check used to avoid adding another ESP-IDF
// component dependency. The Settings API stores page settings as JSON text
// and returns that object inside a small response wrapper. Full schema
// validation can be added per page later.
static bool settings_json_looks_like_object(const char *text)
{
    if (!text) {
        return false;
    }

    const char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    if (*start != '{') {
        return false;
    }

    const char *end = text + strlen(text);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    if (end <= start || end[-1] != '}') {
        return false;
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;

    for (const char *p = start; p < end; ++p) {
        char c = *p;
        if (escape) {
            escape = false;
            continue;
        }
        if (in_string) {
            if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth < 0) {
                return false;
            }
        }
    }

    return depth == 0 && !in_string && !escape;
}

static bool settings_copy_json_object(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0 || !settings_json_looks_like_object(src)) {
        return false;
    }
    size_t len = strlen(src);
    if (len >= dst_len) {
        return false;
    }
    memcpy(dst, src, len + 1);
    return true;
}


// -----------------------------------------------------------------------------
// Settings page specific runtime apply hooks
// -----------------------------------------------------------------------------
// The Settings API stores and persists page JSON as text, but runtime behavior
// must still be applied by the subsystem that owns each setting. These small
// helpers intentionally extract only the fields currently used by firmware.

static bool settings_json_get_string_value(const char *json,
                                           const char *key,
                                           char *out,
                                           size_t out_len)
{
    if (!json || !key || !out || out_len == 0) {
        return false;
    }

    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) {
        return false;
    }

    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '"') return false;
    ++p;

    size_t n = 0;
    bool escape = false;
    while (*p) {
        char c = *p++;
        if (escape) {
            if (n + 1 < out_len) out[n++] = c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            out[n] = '\0';
            return true;
        }
        if (n + 1 < out_len) {
            out[n++] = c;
        }
    }

    out[0] = '\0';
    return false;
}

static bool settings_json_get_bool_value(const char *json,
                                         const char *key,
                                         bool default_value)
{
    if (!json || !key) {
        return default_value;
    }

    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) {
        return default_value;
    }

    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return default_value;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;

    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return default_value;
}


static int settings_json_get_int_value(const char *json,
                                       const char *key,
                                       int default_value)
{
    if (!json || !key) {
        return default_value;
    }

    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) {
        return default_value;
    }

    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return default_value;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;

    return (int)strtol(p, NULL, 10);
}

static PlcOverrunPolicy settings_parse_overrun_policy(const char *text)
{
    if (!text || !text[0]) return PLC_OVERRUN_FAULT_AFTER_LIMIT;
    if (strcasecmp(text, "coalesce") == 0 || strcasecmp(text, "coalesceContinue") == 0) return PLC_OVERRUN_COALESCE_CONTINUE;
    if (strcasecmp(text, "warn") == 0 || strcasecmp(text, "warnContinue") == 0) return PLC_OVERRUN_WARN_CONTINUE;
    if (strcasecmp(text, "fault") == 0 || strcasecmp(text, "faultAfterLimit") == 0) return PLC_OVERRUN_FAULT_AFTER_LIMIT;
    if (strcasecmp(text, "stop") == 0 || strcasecmp(text, "stopImmediately") == 0) return PLC_OVERRUN_STOP_IMMEDIATELY;
    if (strcasecmp(text, "edge") == 0 || strcasecmp(text, "edgeTask") == 0) return PLC_OVERRUN_EDGE_TASK;
    return PLC_OVERRUN_FAULT_AFTER_LIMIT;
}

static PlcScriptTimeMode settings_parse_script_time_mode(const char *text)
{
    if (!text || !text[0]) return PLC_TIME_ACTUAL_CLAMPED;
    if (strcasecmp(text, "nominal") == 0) return PLC_TIME_NOMINAL;
    if (strcasecmp(text, "actual") == 0) return PLC_TIME_ACTUAL;
    if (strcasecmp(text, "actualClamped") == 0 || strcasecmp(text, "clamped") == 0) return PLC_TIME_ACTUAL_CLAMPED;
    return PLC_TIME_ACTUAL_CLAMPED;
}


static void settings_apply_script_engine_settings(const char *json)
{
    char mode[48] = "pauseDuringCompile";
    settings_json_get_string_value(json, "scriptUpdateMode", mode, sizeof(mode));
    script_engine_set_update_mode(mode);
}

static void settings_apply_performance_settings(const char *json)
{
    PlcRuntimePolicy policy;
    perf_test_get_runtime_policy(&policy);

    char overrun_policy[40] = "faultAfterLimit";
    char time_mode[40] = "actualClamped";
    settings_json_get_string_value(json, "overrunPolicy", overrun_policy, sizeof(overrun_policy));
    settings_json_get_string_value(json, "scriptTimeMode", time_mode, sizeof(time_mode));

    policy.overrun_policy = settings_parse_overrun_policy(overrun_policy);
    policy.time_mode = settings_parse_script_time_mode(time_mode);
    policy.script_budget_us = (uint32_t)settings_json_get_int_value(json, "scriptScanBudgetUs", 5000);
    policy.max_delta_us = (uint32_t)settings_json_get_int_value(json, "maxDeltaUs", 50000);
    policy.fault_after_consecutive_overruns = (uint32_t)settings_json_get_int_value(json, "faultAfterConsecutiveOverruns", 20);
    policy.fault_after_coalesced_scans = (uint32_t)settings_json_get_int_value(json, "faultAfterCoalescedScans", 100);

    perf_test_set_runtime_policy(&policy);
}

static esp_log_level_t settings_parse_esp_log_level(const char *level)
{
    if (!level || !level[0]) return ESP_LOG_WARN;
    if (strcasecmp(level, "off") == 0 || strcasecmp(level, "none") == 0) return ESP_LOG_NONE;
    if (strcasecmp(level, "error") == 0 || strcasecmp(level, "err") == 0) return ESP_LOG_ERROR;
    if (strcasecmp(level, "warn") == 0 || strcasecmp(level, "warning") == 0) return ESP_LOG_WARN;
    if (strcasecmp(level, "info") == 0) return ESP_LOG_INFO;
    if (strcasecmp(level, "debug") == 0) return ESP_LOG_DEBUG;
    if (strcasecmp(level, "verbose") == 0 || strcasecmp(level, "trace") == 0) return ESP_LOG_VERBOSE;
    return ESP_LOG_WARN;
}

static void settings_apply_logging_settings(const char *json, bool allow_console_off)
{
    char esp_level_text[32] = "warn";
    char script_level_text[32] = "info";

    // Current field names.
    settings_json_get_string_value(json, "espConsoleLogLevel", esp_level_text, sizeof(esp_level_text));
    settings_json_get_string_value(json, "pilabScriptLogLevel", script_level_text, sizeof(script_level_text));

    // Backward compatibility with the first Settings UI build.
    settings_json_get_string_value(json, "systemLogLevel", esp_level_text, sizeof(esp_level_text));
    settings_json_get_string_value(json, "scriptLogLevel", script_level_text, sizeof(script_level_text));

    bool serial_console_logs = settings_json_get_bool_value(json, "serialConsoleLogs", true);
    esp_log_level_t esp_level = serial_console_logs ? settings_parse_esp_log_level(esp_level_text) : ESP_LOG_NONE;

    // Do not allow saved config to silence all boot diagnostics before the
    // firmware has completed startup and the web UI is reachable. POST requests
    // and the final post-webserver boot apply pass set allow_console_off=true.
    if (!allow_console_off && esp_level == ESP_LOG_NONE) {
        esp_level = ESP_LOG_WARN;
    }

    esp_log_level_set("*", esp_level);
    script_engine_set_runtime_log_level(script_level_text);

    ESP_LOGI(TAG, "Logging settings applied: espConsole=%s%s, pilabScript=%s",
             esp_level_text,
             serial_console_logs ? "" : " (serial disabled)",
             script_level_text);
}

static void settings_apply_runtime_page(RuntimeSettingsPage *page, bool allow_console_off)
{
    if (!page) {
        return;
    }

    if (strcmp(page->route, "logging") == 0) {
        const char *json = settings_json_looks_like_object(page->json) ? page->json : page->default_json;
        settings_apply_logging_settings(json, allow_console_off);
    } else if (strcmp(page->route, "performance") == 0) {
        const char *json = settings_json_looks_like_object(page->json) ? page->json : page->default_json;
        settings_apply_performance_settings(json);
    } else if (strcmp(page->route, "script-engine") == 0) {
        const char *json = settings_json_looks_like_object(page->json) ? page->json : page->default_json;
        settings_apply_script_engine_settings(json);
    }
}

static void settings_ensure_loaded(RuntimeSettingsPage *page);

static void settings_apply_after_webserver_ready(void)
{
    RuntimeSettingsPage *logging = settings_find_page("logging");
    if (logging) {
        settings_ensure_loaded(logging);
        settings_apply_runtime_page(logging, true);
    }
    RuntimeSettingsPage *performance = settings_find_page("performance");
    if (performance) {
        settings_ensure_loaded(performance);
        settings_apply_runtime_page(performance, true);
    }
    RuntimeSettingsPage *script_engine = settings_find_page("script-engine");
    if (script_engine) {
        settings_ensure_loaded(script_engine);
        settings_apply_runtime_page(script_engine, true);
    }
}

static void settings_ensure_loaded(RuntimeSettingsPage *page)
{
    if (!page || page->loaded) {
        return;
    }

    strncpy(page->json, page->default_json, sizeof(page->json) - 1);
    page->json[sizeof(page->json) - 1] = '\0';

    char path[160] = {0};
    settings_path_for(page, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        page->loaded = true;
        page->dirty = false;
        ESP_LOGI(TAG, "Settings default loaded for %s", page->route);
        return;
    }

    size_t n = fread(page->json, 1, sizeof(page->json) - 1, f);
    fclose(f);
    page->json[n] = '\0';

    if (!settings_json_looks_like_object(page->json)) {
        ESP_LOGW(TAG, "Invalid saved settings for %s; using defaults", page->route);
        strncpy(page->json, page->default_json, sizeof(page->json) - 1);
        page->json[sizeof(page->json) - 1] = '\0';
    }

    page->loaded = true;
    page->dirty = false;
    ESP_LOGI(TAG, "Settings loaded for %s from %s", page->route, path);
}

static esp_err_t settings_read_body(httpd_req_t *req, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return ESP_FAIL;
    }
    if (req->content_len == 0 || req->content_len >= out_len) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"settings_payload_too_large\"}");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, out + received, req->content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    out[received] = '\0';
    return ESP_OK;
}

static bool settings_parse_uri(const char *uri, char *route, size_t route_len, bool *out_save)
{
    static const char *prefix = "/api/settings/";
    if (!uri || strncmp(uri, prefix, strlen(prefix)) != 0) {
        return false;
    }

    const char *p = uri + strlen(prefix);
    if (!p[0]) {
        return false;
    }

    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    if (n == 0 || n >= route_len) {
        return false;
    }
    memcpy(route, p, n);
    route[n] = '\0';

    *out_save = false;
    if (slash && strcmp(slash, "/save") == 0) {
        *out_save = true;
    } else if (slash) {
        return false;
    }
    return true;
}

static esp_err_t settings_send_page(httpd_req_t *req, RuntimeSettingsPage *page)
{
    settings_ensure_loaded(page);

    const char *settings_json = settings_json_looks_like_object(page->json) ? page->json : page->default_json;

    size_t needed = strlen(settings_json) + 256;
    char *text = (char *)heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!text) {
        text = (char *)heap_caps_malloc(needed, MALLOC_CAP_8BIT);
    }
    if (!text) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"out_of_memory\"}");
        return ESP_OK;
    }

    int n = snprintf(text, needed,
        "{\"ok\":true,\"route\":\"%s\",\"dirty\":%s,\"saved\":%s,\"flashWritesAllowed\":%s,\"settings\":%s}",
        page->route,
        page->dirty ? "true" : "false",
        page->dirty ? "false" : "true",
        perf_test_is_plc_running() ? "false" : "true",
        settings_json);

    if (n < 0 || (size_t)n >= needed) {
        free(text);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"settings_response_too_large\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, text);
    free(text);
    return ESP_OK;
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    char route[48] = {0};
    bool save = false;
    if (!settings_parse_uri(req->uri, route, sizeof(route), &save) || save) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"unknown_settings_route\"}");
        return ESP_OK;
    }

    RuntimeSettingsPage *page = settings_find_page(route);
    if (!page) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"unknown_settings_page\"}");
        return ESP_OK;
    }

    return settings_send_page(req, page);
}

static esp_err_t settings_save_page(httpd_req_t *req, RuntimeSettingsPage *page)
{
    if (!flash_write_allowed(req)) {
        return ESP_OK;
    }

    settings_ensure_loaded(page);

    char path[160] = {0};
    char tmp_path[180] = {0};
    settings_path_for(page, path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"open_failed\"}");
        return ESP_OK;
    }

    size_t len = strlen(page->json);
    size_t written = fwrite(page->json, 1, len, f);
    fclose(f);

    if (written != len) {
        unlink(tmp_path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"write_failed\"}");
        return ESP_OK;
    }

    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"rename_failed\"}");
        return ESP_OK;
    }

    page->dirty = false;
    ESP_LOGI(TAG, "Settings saved for %s to %s", page->route, path);
    return settings_send_page(req, page);
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    char route[48] = {0};
    bool save = false;
    if (!settings_parse_uri(req->uri, route, sizeof(route), &save)) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"unknown_settings_route\"}");
        return ESP_OK;
    }

    RuntimeSettingsPage *page = settings_find_page(route);
    if (!page) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"unknown_settings_page\"}");
        return ESP_OK;
    }

    if (save) {
        return settings_save_page(req, page);
    }

    char body[SETTINGS_MAX_JSON_BYTES] = {0};
    if (settings_read_body(req, body, sizeof(body)) != ESP_OK) {
        return ESP_OK;
    }

    if (!settings_copy_json_object(page->json, sizeof(page->json), body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid_json\"}");
        return ESP_OK;
    }

    page->loaded = true;
    page->dirty = true;

    settings_apply_runtime_page(page, true);

    ESP_LOGI(TAG, "Runtime settings applied for %s; persistent save not performed", page->route);
    return settings_send_page(req, page);
}


static void register_uri_checked(httpd_handle_t server, const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(server, uri);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Registered URI %s", uri->uri);
    } else {
        ESP_LOGE(TAG, "Failed to register URI %s: %s", uri->uri, esp_err_to_name(err));
    }
}

static void start_webserver(void)
{
    if (g_http_server != NULL) {
        return;
    }

   // httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    //config.server_port = 80;

httpd_config_t config = HTTPD_DEFAULT_CONFIG();

config.server_port = 80;

// Important under connection churn
config.lru_purge_enable = true;

// More breathing room
config.max_open_sockets = 4;
config.stack_size = 8192;

// Avoid long blocked sockets
config.recv_wait_timeout = 2;
config.send_wait_timeout = 2;

// keep webserver away from PLC core
config.task_priority = 1;
config.core_id = 1;

// Default ESP-IDF HTTPD URI slots are easy to exhaust once /hmi and
// /api/plc_data are added. Keep enough room for future HMI/API routes.
config.max_uri_handlers = 64;
config.uri_match_fn = httpd_uri_match_wildcard;










    ESP_LOGI(TAG, "Starting HTTP server on port 80, max_uri_handlers=%d", config.max_uri_handlers);

    esp_err_t ret = httpd_start(&g_http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return;
    }

    if (!g_plc_data_cache_mutex) {
        g_plc_data_cache_mutex = xSemaphoreCreateMutex();
    }
    if (!g_plc_data_cache_json) {
        g_plc_data_cache_json = (char*)heap_caps_malloc(PLC_DATA_JSON_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_plc_data_cache_json) {
            g_plc_data_cache_json = (char*)heap_caps_malloc(PLC_DATA_JSON_CAP, MALLOC_CAP_8BIT);
        }
    }
    if (!g_plc_data_cache_mutex || !g_plc_data_cache_json) {
        ESP_LOGE(TAG, "PLC data cache init failed");
    } else if (!g_plc_data_cache_task) {
        BaseType_t task_ok = xTaskCreatePinnedToCore(
            plc_data_cache_task,
            "plc_data_cache",
            PLC_DATA_CACHE_STACK_WORDS,
            NULL,
            PLC_DATA_CACHE_PRIORITY,
            &g_plc_data_cache_task,
            1
        );
        if (task_ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to start PLC data cache task");
            g_plc_data_cache_task = NULL;
        }
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };

    register_uri_checked(g_http_server, &root_uri);

    httpd_uri_t editor_uri = {
        .uri = "/editor",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &editor_uri);

    httpd_uri_t hmi_uri = {
        .uri = "/hmi",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &hmi_uri);

    httpd_uri_t tags_uri = {
        .uri = "/tags",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &tags_uri);

    httpd_uri_t files_uri = {
        .uri = "/files",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_uri);

    httpd_uri_t script_uri = {
        .uri = "/script",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &script_uri);

    httpd_uri_t settings_page_uri = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = spa_index_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &settings_page_uri);

    httpd_uri_t app_js_uri = {
        .uri = "/assets/app.js",
        .method = HTTP_GET,
        .handler = spa_js_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &app_js_uri);

    httpd_uri_t index_css_uri = {
        .uri = "/assets/index.css",
        .method = HTTP_GET,
        .handler = spa_css_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &index_css_uri);

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL
    };

    register_uri_checked(g_http_server, &status_uri);

    httpd_uri_t system_overview_uri = {
        .uri = "/api/system_overview",
        .method = HTTP_GET,
        .handler = system_overview_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &system_overview_uri);

    httpd_uri_t command_center_uri = {
        .uri = "/api/command_center",
        .method = HTTP_GET,
        .handler = command_center_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &command_center_uri);

  httpd_uri_t large_page_uri = {
    .uri = "/large",
    .method = HTTP_GET,
    .handler = large_page_get_handler,
    .user_ctx = NULL
};

register_uri_checked(g_http_server, &large_page_uri);

httpd_uri_t large_status_uri = {
    .uri = "/api/large_status",
    .method = HTTP_GET,
    .handler = large_status_get_handler,
    .user_ctx = NULL
};

   register_uri_checked(g_http_server, &large_status_uri);

    httpd_uri_t upload_script_uri = {
        .uri = "/api/upload_script",
        .method = HTTP_POST,
        .handler = upload_script_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &upload_script_uri);

    httpd_uri_t script_status_uri = {
        .uri = "/api/script_status",
        .method = HTTP_GET,
        .handler = script_status_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &script_status_uri);

    httpd_uri_t plc_io_status_uri = {
        .uri = "/api/plc_io",
        .method = HTTP_GET,
        .handler = plc_io_status_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &plc_io_status_uri);

    httpd_uri_t plc_data_uri = {
        .uri = "/api/plc_data",
        .method = HTTP_GET,
        .handler = plc_data_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &plc_data_uri);

    httpd_uri_t api_tags_get_uri = {
        .uri = "/api/tags",
        .method = HTTP_GET,
        .handler = api_tags_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &api_tags_get_uri);

    httpd_uri_t api_tags_post_uri = {
        .uri = "/api/tags",
        .method = HTTP_POST,
        .handler = api_tags_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &api_tags_post_uri);

    httpd_uri_t plc_write_uri = {
        .uri = "/api/plc_write",
        .method = HTTP_POST,
        .handler = plc_write_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &plc_write_uri);

    httpd_uri_t plc_mode_get_uri = {
        .uri = "/api/plc_mode",
        .method = HTTP_GET,
        .handler = plc_mode_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &plc_mode_get_uri);

    httpd_uri_t plc_mode_post_uri = {
        .uri = "/api/plc_mode",
        .method = HTTP_POST,
        .handler = plc_mode_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &plc_mode_post_uri);

    httpd_uri_t settings_get_uri = {
        .uri = "/api/settings/*",
        .method = HTTP_GET,
        .handler = settings_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &settings_get_uri);

    httpd_uri_t settings_post_uri = {
        .uri = "/api/settings/*",
        .method = HTTP_POST,
        .handler = settings_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &settings_post_uri);

    httpd_uri_t files_list_uri = {
        .uri = "/api/files/list",
        .method = HTTP_GET,
        .handler = files_list_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_list_uri);

    httpd_uri_t files_upload_uri = {
        .uri = "/api/files/upload",
        .method = HTTP_POST,
        .handler = files_upload_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_upload_uri);

    httpd_uri_t files_download_uri = {
        .uri = "/api/files/download",
        .method = HTTP_GET,
        .handler = files_download_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_download_uri);

    httpd_uri_t files_view_uri = {
        .uri = "/api/files/view",
        .method = HTTP_GET,
        .handler = files_view_get_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_view_uri);

    httpd_uri_t files_delete_uri = {
        .uri = "/api/files/delete",
        .method = HTTP_POST,
        .handler = files_delete_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_delete_uri);

    httpd_uri_t files_mkdir_uri = {
        .uri = "/api/files/mkdir",
        .method = HTTP_POST,
        .handler = files_mkdir_post_handler,
        .user_ctx = NULL
    };
    register_uri_checked(g_http_server, &files_mkdir_uri);

    // The web server is now reachable. It is safe to honor a saved/requested
    // ESP_LOG_NONE setting without hiding boot diagnostics.
    settings_apply_after_webserver_ready();
}

static void eth_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_base;

    uint8_t mac_addr[6] = {0};

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED: {
            esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);

            ESP_LOGI(TAG, "Ethernet link up");
            ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     mac_addr[0], mac_addr[1], mac_addr[2],
                     mac_addr[3], mac_addr[4], mac_addr[5]);
            break;
        }

        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet link down");
            break;

        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;

        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet stopped");
            break;

        default:
            break;
    }
}

static void configure_static_ip(esp_netif_t *eth_netif)
{
    ESP_LOGI(TAG, "Configuring static IP");

    esp_err_t ret = esp_netif_dhcpc_stop(eth_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "esp_netif_dhcpc_stop failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_netif_ip_info_t ip_info = {0};

    ip_info.ip.addr = esp_ip4addr_aton(STATIC_IP_ADDR);
    ip_info.netmask.addr = esp_ip4addr_aton(STATIC_NETMASK);
    ip_info.gw.addr = esp_ip4addr_aton(STATIC_GATEWAY);

    //esp_ip4addr_aton(STATIC_IP_ADDR, &ip_info.ip);
    //esp_ip4addr_aton(STATIC_NETMASK, &ip_info.netmask);
    //esp_ip4addr_aton(STATIC_GATEWAY, &ip_info.gw);

    //ip4addr_aton(STATIC_IP_ADDR, &ip_info.ip);
    //ip4addr_aton(STATIC_NETMASK, &ip_info.netmask);
    //ip4addr_aton(STATIC_GATEWAY, &ip_info.gw);

    ret = esp_netif_set_ip_info(eth_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_set_ip_info failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Static IP: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Netmask:   " IPSTR, IP2STR(&ip_info.netmask));
    ESP_LOGI(TAG, "Gateway:   " IPSTR, IP2STR(&ip_info.gw));
}

void ethernet_web_start(void)
{
    ESP_LOGI(TAG, "Initializing Ethernet + web server");

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t event_loop_ret = esp_event_loop_create_default();
    if (event_loop_ret != ESP_OK && event_loop_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_loop_ret);
    }

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_config);
    if (eth_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return;
    }

    configure_static_ip(eth_netif);

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;

    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = ETH_RMII_CLK_GPIO;

    emac_config.emac_dataif_gpio.rmii.tx_en_num = ETH_RMII_TX_EN_GPIO;
    emac_config.emac_dataif_gpio.rmii.txd0_num = ETH_RMII_TXD0_GPIO;
    emac_config.emac_dataif_gpio.rmii.txd1_num = ETH_RMII_TXD1_GPIO;
    emac_config.emac_dataif_gpio.rmii.crs_dv_num = ETH_RMII_CRS_DV_GPIO;
    emac_config.emac_dataif_gpio.rmii.rxd0_num = ETH_RMII_RXD0_GPIO;
    emac_config.emac_dataif_gpio.rmii.rxd1_num = ETH_RMII_RXD1_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (mac == NULL) {
        ESP_LOGE(TAG, "esp_eth_mac_new_esp32 failed");
        return;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(TAG, "esp_eth_phy_new_ip101 failed");
        mac->del(mac);
        return;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);

    esp_eth_handle_t eth_handle = NULL;
    esp_err_t ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(ret));
        mac->del(mac);
        phy->del(phy);
        return;
    }

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    ret = esp_netif_attach(eth_netif, glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT,
        ESP_EVENT_ANY_ID,
        &eth_event_handler,
        NULL
    ));

    // With static IP we do not need to wait for IP_EVENT_ETH_GOT_IP.
    start_webserver();

    ret = esp_eth_start(eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Ethernet driver started with static IP: %s", STATIC_IP_ADDR);
}