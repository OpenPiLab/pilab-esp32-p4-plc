#include "udp_server.hpp"
#include "plc_io.hpp"

#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

static const char *TAG = "UDP_SERVER";

#define UDP_PORT 5005
#define UDP_RX_BUFFER_SIZE 1500
#define UDP_TX_PAYLOAD_SIZE 1400

// The old stack was 4096 bytes while the function had ~2900 bytes of local
// packet buffers. The hot packet buffers are now static DRAM buffers, but keep
// a little more stack margin for lwIP/socket call frames and diagnostics.
#define UDP_TASK_STACK_SIZE 6144
#define UDP_TASK_PRIORITY   14
#define UDP_TASK_CORE       1

#define UDP_MAGIC 0x50345544u      // "UDP4" legacy response marker
#define UDP_VERSION 1

#define PILAB_UDP_IO_MAGIC 0x4F493450u  // "P4IO" little-endian marker
#define PILAB_UDP_IO_VERSION 1
#define PILAB_UDP_TAG_VERSION 2
#define PILAB_UDP_IO_FLAG_CHECKSUM_BAD   (1u << 0)
#define PILAB_UDP_IO_FLAG_BAD_MAGIC      (1u << 1)
#define PILAB_UDP_IO_FLAG_BAD_VERSION    (1u << 2)
#define PILAB_UDP_IO_FLAG_BAD_SIZE       (1u << 3)
#define PILAB_UDP_IO_FLAG_ACCEPTED       (1u << 8)
#define PILAB_UDP_IO_FLAG_LEGACY_ECHO    (1u << 9)

// Request-side test/control flags carried in pilab_udp_io_request_t.flags.
// The test client sets this on seq=1 so each run starts from clean diagnostics
// without requiring a reboot.
#define PILAB_UDP_IO_REQ_FLAG_RESET_STATS (1u << 0)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t packet_size;

    uint32_t request_seq;
    uint32_t response_seq;

    uint32_t rx_len;
    uint32_t flags;

    uint64_t esp_rx_time_us;
    uint64_t esp_tx_time_us;

    uint8_t payload[UDP_TX_PAYLOAD_SIZE - 40];
} udp_response_packet_t;

static_assert(sizeof(udp_response_packet_t) == UDP_TX_PAYLOAD_SIZE,
              "UDP response packet must be exactly UDP_TX_PAYLOAD_SIZE bytes");

// New simulated remote-I/O request packet. This intentionally stays small and
// fixed-size so the UDP task can validate it and publish a complete process
// image without strings, JSON, allocation, or tag-name lookup.
typedef struct {
    uint32_t magic;          // PILAB_UDP_IO_MAGIC, "P4IO"
    uint16_t version;        // PILAB_UDP_IO_VERSION
    uint16_t packet_size;    // sizeof(pilab_udp_io_request_t)

    uint32_t request_seq;
    uint32_t flags;
    uint64_t client_tx_time_us;

    uint32_t di_mask;        // simulated remote digital inputs, bit 0 = I0
    uint32_t do_mask;        // optional remote status/echo field for tests
    float    ai[PLC_AI_COUNT];
    uint32_t payload_counter;

    uint32_t checksum;       // FNV-1a over the whole packet with this field = 0
    uint8_t  reserved[8];
} pilab_udp_io_request_t;

static_assert(sizeof(pilab_udp_io_request_t) == 64,
              "PiLab UDP I/O request packet must stay 64 bytes");

typedef struct {
    uint32_t magic;          // PILAB_UDP_IO_MAGIC, "P4IO"
    uint16_t version;        // PILAB_UDP_TAG_VERSION
    uint16_t packet_size;    // sizeof(pilab_udp_tag_request_t)

    uint32_t request_seq;
    uint32_t flags;
    uint64_t client_tx_time_us;

    uint32_t value_count;    // must be PLC_UDP_TAG_VALUE_COUNT
    uint32_t checksum;       // FNV-1a over the whole packet with this field = 0
    uint32_t values[PLC_UDP_TAG_VALUE_COUNT]; // 128 x 4-byte values = 512 bytes
} pilab_udp_tag_request_t;

static_assert(sizeof(pilab_udp_tag_request_t) == 544,
              "PiLab UDP tag request packet must stay 544 bytes");

static DRAM_ATTR uint8_t g_rx_buffer[UDP_RX_BUFFER_SIZE];
static DRAM_ATTR udp_response_packet_t g_tx_packet;

static uint32_t fnv1a32(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t validate_and_publish_udp_io(const uint8_t* data, int len, uint64_t rx_time_us)
{
    uint32_t flags = 0;

    if (len < 8) {
        return PILAB_UDP_IO_FLAG_BAD_SIZE;
    }

    uint32_t magic = 0;
    uint16_t version = 0;
    memcpy(&magic, data + offsetof(pilab_udp_io_request_t, magic), sizeof(magic));
    memcpy(&version, data + offsetof(pilab_udp_io_request_t, version), sizeof(version));

    if (magic != PILAB_UDP_IO_MAGIC) {
        return PILAB_UDP_IO_FLAG_BAD_MAGIC;
    }

    if (version == PILAB_UDP_IO_VERSION) {
        if (len < (int)sizeof(pilab_udp_io_request_t)) {
            return PILAB_UDP_IO_FLAG_BAD_SIZE;
        }

        pilab_udp_io_request_t pkt;
        memcpy(&pkt, data, sizeof(pkt));

        if (pkt.packet_size != sizeof(pilab_udp_io_request_t) || len < (int)pkt.packet_size) {
            flags |= PILAB_UDP_IO_FLAG_BAD_SIZE;
        }

        pilab_udp_io_request_t check = pkt;
        check.checksum = 0;
        const uint32_t expected = fnv1a32(&check, sizeof(check));
        if (pkt.checksum != expected) {
            flags |= PILAB_UDP_IO_FLAG_CHECKSUM_BAD;
        }

        if (flags == 0) {
            if (pkt.flags & PILAB_UDP_IO_REQ_FLAG_RESET_STATS) {
                plc_io_clear_udp_io_stats();
            }

            float ai_copy[PLC_AI_COUNT] = {0};
            memcpy(ai_copy, pkt.ai, sizeof(ai_copy));
            plc_io_publish_udp_input_image(pkt.request_seq, pkt.di_mask, ai_copy, PLC_AI_COUNT, rx_time_us);
            flags |= PILAB_UDP_IO_FLAG_ACCEPTED;
        }
        return flags;
    }

    if (version == PILAB_UDP_TAG_VERSION) {
        if (len < (int)sizeof(pilab_udp_tag_request_t)) {
            return PILAB_UDP_IO_FLAG_BAD_SIZE;
        }

        pilab_udp_tag_request_t pkt;
        memcpy(&pkt, data, sizeof(pkt));

        if (pkt.packet_size != sizeof(pilab_udp_tag_request_t) || len < (int)pkt.packet_size ||
            pkt.value_count != PLC_UDP_TAG_VALUE_COUNT) {
            flags |= PILAB_UDP_IO_FLAG_BAD_SIZE;
        }

        pilab_udp_tag_request_t check = pkt;
        check.checksum = 0;
        const uint32_t expected = fnv1a32(&check, sizeof(check));
        if (pkt.checksum != expected) {
            flags |= PILAB_UDP_IO_FLAG_CHECKSUM_BAD;
        }

        if (flags == 0) {
            if (pkt.flags & PILAB_UDP_IO_REQ_FLAG_RESET_STATS) {
                plc_io_clear_udp_io_stats();
            }

            // Publish the full 128-tag image for scan-side cached-index writes.
            plc_io_publish_udp_tag_image(pkt.request_seq, pkt.values, PLC_UDP_TAG_VALUE_COUNT, rx_time_us);

            // Also keep the earlier process-image simulation active so I0..I7 and AI0..AI3
            // still change while the UDP tag bank is being exercised.
            uint32_t di_mask = 0;
            for (uint32_t i = 0; i < 8u && i < PLC_UDP_TAG_GROUP_COUNT; ++i) {
                if (pkt.values[i] != 0u) di_mask |= (1u << i);
            }
            float ai_copy[PLC_AI_COUNT] = {0};
            for (uint32_t i = 0; i < PLC_AI_COUNT; ++i) {
                memcpy(&ai_copy[i], &pkt.values[PLC_UDP_TAG_GROUP_COUNT * 2 + i], sizeof(float));
            }
            plc_io_publish_udp_input_image(pkt.request_seq, di_mask, ai_copy, PLC_AI_COUNT, rx_time_us);

            flags |= PILAB_UDP_IO_FLAG_ACCEPTED;
        }
        return flags;
    }

    return PILAB_UDP_IO_FLAG_BAD_VERSION;
}

static void udp_server_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(UDP_PORT);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int err = bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "bind failed: errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP binary server listening on port %d", UDP_PORT);
    ESP_LOGI(TAG, "UDP task priority=%d core=%d stack=%d", UDP_TASK_PRIORITY, UDP_TASK_CORE, UDP_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "Legacy response size=%d bytes", UDP_TX_PAYLOAD_SIZE);
    ESP_LOGI(TAG, "PiLab UDP I/O request size=%u bytes, UDP tag request size=%u bytes, magic=0x%08lx", (unsigned)sizeof(pilab_udp_io_request_t), (unsigned)sizeof(pilab_udp_tag_request_t), (unsigned long)PILAB_UDP_IO_MAGIC);

    uint32_t response_counter = 0;

    while (true) {
        struct sockaddr_in source_addr = {0};
        socklen_t socklen = sizeof(source_addr);

        int len = recvfrom(
            sock,
            g_rx_buffer,
            sizeof(g_rx_buffer),
            0,
            (struct sockaddr *)&source_addr,
            &socklen
        );

        if (len < 0) {
            ESP_LOGW(TAG, "recvfrom failed: errno=%d", errno);
            continue;
        }

        uint64_t rx_time_us = (uint64_t)esp_timer_get_time();

        uint32_t request_seq = 0;
        uint32_t flags = PILAB_UDP_IO_FLAG_LEGACY_ECHO;
        if (len >= 4) {
            memcpy(&request_seq, g_rx_buffer, sizeof(request_seq));
        }

        // If this is the new fixed binary remote-I/O packet, validate and publish
        // it into the PLC process-image double buffer. Otherwise the old echo
        // benchmark behavior is preserved for your existing Python tester.
        if (len >= 4) {
            uint32_t maybe_magic = 0;
            memcpy(&maybe_magic, g_rx_buffer, sizeof(maybe_magic));
            if (maybe_magic == PILAB_UDP_IO_MAGIC) {
                // Avoid casting the RX byte buffer to a packed struct pointer.
                // memcpy from a byte offset is safe even if the UDP payload is unaligned.
                memcpy(&request_seq, g_rx_buffer + offsetof(pilab_udp_io_request_t, request_seq), sizeof(request_seq));
                flags = validate_and_publish_udp_io(g_rx_buffer, len, rx_time_us);
            }
        }

        response_counter++;

        memset(&g_tx_packet, 0, sizeof(g_tx_packet));

        g_tx_packet.magic = UDP_MAGIC;
        g_tx_packet.version = UDP_VERSION;
        g_tx_packet.packet_size = UDP_TX_PAYLOAD_SIZE;

        g_tx_packet.request_seq = request_seq;
        g_tx_packet.response_seq = response_counter;

        g_tx_packet.rx_len = (uint32_t)len;
        g_tx_packet.flags = flags;

        g_tx_packet.esp_rx_time_us = rx_time_us;
        g_tx_packet.esp_tx_time_us = (uint64_t)esp_timer_get_time();

        // Keep the payload deterministic for the old benchmark. Put a compact
        // stats snapshot at the start so the new test script can inspect the
        // simulated remote-I/O path without adding a separate UDP response type.
        PlcUdpIoStats stats = {0};
        plc_io_get_udp_io_stats(&stats);
        memcpy(g_tx_packet.payload, &stats, sizeof(stats) < sizeof(g_tx_packet.payload) ? sizeof(stats) : sizeof(g_tx_packet.payload));
        for (int i = (int)sizeof(stats); i < (int)sizeof(g_tx_packet.payload); i++) {
            g_tx_packet.payload[i] = (uint8_t)(i & 0xFF);
        }

        int sent = sendto(
            sock,
            &g_tx_packet,
            sizeof(g_tx_packet),
            0,
            (struct sockaddr *)&source_addr,
            sizeof(source_addr)
        );

        if (sent < 0) {
            ESP_LOGW(TAG, "sendto failed: errno=%d", errno);
        }
    }
}

void udp_server_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(
        udp_server_task,
        "udp_server",
        UDP_TASK_STACK_SIZE,
        NULL,
        UDP_TASK_PRIORITY,
        NULL,
        UDP_TASK_CORE
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP server task");
    }
}
