/**
 * @file csi_collector.c
 * @brief CSI callback + ADR-018 raw-frame UDP sender.
 *
 * Ported from firmware/esp32-csi-node/main/csi_collector.c.
 * Key differences from original:
 *   - No global g_nvs_config dependency — node_id and channel are parameters.
 *   - Does not call Wi-Fi connect or NVS init.
 *   - Delegates serialization to adr018_serializer module.
 *   - Ring-buffer pointer is injected (not a file-scope global).
 */

#include "ruview_core/src/csi_collector.h"
#include "ruview_core/src/adr018_serializer.h"
#include "ruview_core/src/udp_sender.h"
#include "ruview_core/src/compat_logging.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_timer.h"

static const char *TAG = "csi_collect";

/* ---- State ---- */

static ruview_ring_buf_t *s_ring   = NULL;
static uint8_t            s_node_id = 0;
static bool               s_udp_raw_enabled = false;
static bool               s_active = false;

static uint32_t s_cb_count  = 0;
static uint32_t s_send_ok   = 0;
static uint32_t s_send_fail = 0;
static uint32_t s_rate_skip = 0;

/** Rate limiter: 20 ms = 50 Hz max send rate. */
#define CSI_MIN_SEND_INTERVAL_US  (20 * 1000)
static int64_t s_last_send_us = 0;

/* ---- Ring buffer push (inline, lock-free SPSC) ---- */

static inline bool ring_push(const uint8_t *iq, uint16_t len,
                             int8_t rssi, uint8_t channel)
{
    if (s_ring == NULL) return false;

    uint32_t next = (s_ring->head + 1) % RUVIEW_RING_SLOTS;
    if (next == s_ring->tail) {
        return false;  /* Full — drop frame. */
    }

    ruview_ring_slot_t *slot = &s_ring->slots[s_ring->head];
    uint16_t copy_len = (len > RUVIEW_MAX_IQ_BYTES) ? RUVIEW_MAX_IQ_BYTES : len;
    memcpy(slot->iq_data, iq, copy_len);
    slot->iq_len      = copy_len;
    slot->rssi        = rssi;
    slot->channel     = channel;
    slot->timestamp_us = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);

    __sync_synchronize();
    s_ring->head = next;
    return true;
}

/* ---- CSI callback ---- */

static void wifi_csi_callback(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    if (info == NULL || info->buf == NULL || info->len <= 0) return;

    s_cb_count++;

    if (s_cb_count <= 3 || (s_cb_count % 500) == 0) {
        ESP_LOGI(TAG, "CSI #%lu: len=%d rssi=%d ch=%d",
                 (unsigned long)s_cb_count, info->len,
                 info->rx_ctrl.rssi, info->rx_ctrl.channel);
    }

    /* ---- Serialize + rate-limited UDP send ---- */
    if (s_udp_raw_enabled) {
        int64_t now = esp_timer_get_time();
        if ((now - s_last_send_us) >= CSI_MIN_SEND_INTERVAL_US) {
            ruview_csi_frame_t frame = {
                .iq_data     = (const uint8_t *)info->buf,
                .iq_len      = (uint16_t)info->len,
                .channel     = info->rx_ctrl.channel,
                .rssi        = (int8_t)info->rx_ctrl.rssi,
                .noise_floor = (int8_t)info->rx_ctrl.noise_floor,
                .n_antennas  = 1,
            };

            uint8_t buf[RUVIEW_CSI_MAX_FRAME_SIZE];
            size_t  serialized_len = 0;

            esp_err_t err = adr018_serialize_frame(&frame, s_node_id,
                                                   buf, sizeof(buf),
                                                   &serialized_len);
            if (err == ESP_OK && serialized_len > 0) {
                int ret = udp_sender_send_raw(buf, serialized_len);
                if (ret > 0) {
                    s_send_ok++;
                    s_last_send_us = now;
                } else {
                    s_send_fail++;
                }
            }
        } else {
            s_rate_skip++;
        }
    }

    /* ---- Enqueue into ring buffer for edge DSP ---- */
    ring_push((const uint8_t *)info->buf, (uint16_t)info->len,
              (int8_t)info->rx_ctrl.rssi, info->rx_ctrl.channel);
}

/** Promiscuous RX callback — no-op, just needed for CSI to fire on all frames. */
static void wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)buf;
    (void)type;
}

/* ---- Public API ---- */

esp_err_t csi_collector_init(uint8_t node_id, uint8_t channel,
                             ruview_ring_buf_t *ring,
                             bool udp_raw_en,
                             const char *target_ip, uint16_t target_port)
{
    s_node_id         = node_id;
    s_ring            = ring;
    s_udp_raw_enabled = udp_raw_en;
    s_cb_count        = 0;
    s_send_ok         = 0;
    s_send_fail       = 0;
    s_rate_skip       = 0;
    s_last_send_us    = 0;

    adr018_serializer_reset();

    /* ---- Determine channel ---- */
    uint8_t csi_channel = channel;
    if (csi_channel == 0) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.primary > 0) {
            csi_channel = ap_info.primary;
            ESP_LOGI(TAG, "Auto-detected AP channel: %u", (unsigned)csi_channel);
        } else {
            csi_channel = 6;  /* Safe default. */
            ESP_LOGW(TAG, "Could not detect AP channel, using default: %u",
                     (unsigned)csi_channel);
        }
    }

    /* ---- Promiscuous mode ---- */
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb));

    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));

    /* ---- CSI config ---- */
    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = true,
        .ltf_merge_en      = true,
        .channel_filter_en = false,
        .manu_scale        = false,
        .shift             = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    s_active = true;

    ESP_LOGI(TAG, "CSI collector started (node=%u, ch=%u, udp_raw=%s)",
             (unsigned)node_id, (unsigned)csi_channel,
             udp_raw_en ? "yes" : "no");
    return ESP_OK;
}

esp_err_t csi_collector_stop(void)
{
    if (!s_active) return ESP_OK;

    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(NULL, NULL);
    esp_wifi_set_promiscuous(false);

    s_active = false;
    ESP_LOGI(TAG, "CSI collector stopped (frames=%lu, sent=%lu, dropped=%lu)",
             (unsigned long)s_cb_count, (unsigned long)s_send_ok,
             (unsigned long)s_send_fail);
    return ESP_OK;
}

uint32_t csi_collector_get_frame_count(void)
{
    return s_cb_count;
}

uint32_t csi_collector_get_send_count(void)
{
    return s_send_ok;
}
