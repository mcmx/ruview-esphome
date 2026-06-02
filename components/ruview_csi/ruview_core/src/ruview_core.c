/**
 * @file ruview_core.c
 * @brief Top-level orchestrator for ruview_core.
 *
 * Wires together csi_collector, adr018_serializer, udp_sender, edge_dsp,
 * and runtime_config behind the ruview_core_* public API.
 */

#include "ruview_core/include/ruview_core.h"
#include "ruview_core/include/ruview_types.h"
#include "ruview_core/src/csi_collector.h"
#include "ruview_core/src/adr018_serializer.h"
#include "ruview_core/src/udp_sender.h"
#include "ruview_core/src/edge_dsp.h"
#include "ruview_core/src/runtime_config.h"
#include "ruview_core/src/compat_logging.h"

#include <string.h>

static const char *TAG = "ruview_core";

/* ---- State ---- */

static ruview_core_config_t s_config;
static ruview_ring_buf_t    s_ring;
static bool s_initialized = false;
static bool s_started     = false;
static bool s_wifi_ready  = false;

/* ---- Lifecycle ---- */

esp_err_t ruview_core_init(const ruview_core_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;

    /* If re-initialising, stop first. */
    if (s_started) {
        ruview_core_stop();
    }

    /* Copy config. */
    s_config = *config;

    /* Reset ring buffer. */
    memset(&s_ring, 0, sizeof(s_ring));

    s_initialized = true;
    s_started     = false;

    ESP_LOGI(TAG, "Initialized (node=%u, target=%s:%u, edge=%s tier=%u)",
             (unsigned)s_config.node_id,
             s_config.target_ip, (unsigned)s_config.target_port,
             s_config.edge_enabled ? "yes" : "no",
             (unsigned)s_config.edge_tier);

    return ESP_OK;
}

esp_err_t ruview_core_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized — call ruview_core_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started) {
        ESP_LOGW(TAG, "Already started");
        return ESP_OK;
    }
    if (!s_wifi_ready) {
        ESP_LOGW(TAG, "Wi-Fi not ready — deferring start");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    /* ---- UDP sender ---- */
    bool need_udp = s_config.udp_raw_enabled || s_config.udp_vitals_enabled;
    if (need_udp && s_config.target_ip[0] != '\0') {
        err = udp_sender_init(s_config.target_ip, s_config.target_port);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UDP sender init failed");
            return err;
        }
    }

    /* ---- Edge DSP ---- */
    if (s_config.edge_enabled) {
        ruview_edge_config_t ecfg = {
            .tier              = s_config.edge_tier,
            .presence_thresh   = s_config.presence_threshold,
            .fall_thresh       = s_config.fall_threshold,
            .vital_window      = RUVIEW_PHASE_HISTORY_LEN,
            .vital_interval_ms = s_config.vital_interval_ms,
            .top_k_count       = s_config.top_k_count,
        };

        edge_dsp_set_node_id(s_config.node_id);
        edge_dsp_set_udp_vitals_enabled(s_config.udp_vitals_enabled);

        err = edge_dsp_init(&ecfg, &s_ring);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Edge DSP init failed");
            return err;
        }
    }

    /* ---- CSI collector ---- */
    err = csi_collector_init(s_config.node_id, s_config.wifi_channel,
                             &s_ring,
                             s_config.udp_raw_enabled,
                             s_config.target_ip, s_config.target_port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI collector init failed");
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

esp_err_t ruview_core_stop(void)
{
    if (!s_started) return ESP_OK;

    csi_collector_stop();
    udp_sender_deinit();
    /* Note: edge DSP task is not cleanly stoppable (runs forever).
     * For a clean stop, the task would need a shutdown flag.
     * This is acceptable for ESPHome — stop is only called on teardown. */

    s_started = false;
    ESP_LOGI(TAG, "Stopped");
    return ESP_OK;
}

esp_err_t ruview_core_set_wifi_ready(bool ready)
{
    s_wifi_ready = ready;
    return ESP_OK;
}

esp_err_t ruview_core_get_status(ruview_core_status_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    out->running    = s_started;
    out->wifi_ready = s_wifi_ready;

    /* CSI counters */
    out->csi_frames_received = csi_collector_get_frame_count();
    out->adr018_frames_sent  = csi_collector_get_send_count();

    /* UDP counters */
    uint32_t raw_sent = 0, vitals_sent = 0, errors = 0;
    udp_sender_get_counters(&raw_sent, &vitals_sent, &errors);
    out->vitals_packets_sent = vitals_sent;

    /* DSP state */
    bool presence = false, fall = false;
    float motion = 0.0f, br = 0.0f, hr = 0.0f;
    edge_dsp_get_quick_status(&presence, &fall, &motion, &br, &hr);

    out->presence       = presence;
    out->fall_detected  = fall;
    out->motion_energy  = motion;
    out->breathing_bpm  = br;
    out->heart_rate_bpm = hr;

    /* RSSI from the latest vitals packet */
    ruview_vitals_pkt_t pkt;
    if (edge_dsp_get_vitals(&pkt)) {
        out->last_rssi = (int32_t)pkt.rssi;
    }

    return ESP_OK;
}

/* ---- Runtime tuning ---- */

esp_err_t ruview_core_set_presence_threshold(float value)
{
    s_config.presence_threshold = value;
    edge_dsp_set_presence_threshold(value);
    return ESP_OK;
}

esp_err_t ruview_core_set_fall_threshold(float value)
{
    s_config.fall_threshold = value;
    edge_dsp_set_fall_threshold(value);
    return ESP_OK;
}

esp_err_t ruview_core_set_target(const char *ip, uint16_t port)
{
    if (ip == NULL) return ESP_ERR_INVALID_ARG;
    strncpy(s_config.target_ip, ip, sizeof(s_config.target_ip) - 1);
    s_config.target_ip[sizeof(s_config.target_ip) - 1] = '\0';
    s_config.target_port = port;

    if (s_started) {
        return udp_sender_update_target(ip, port);
    }
    return ESP_OK;
}

/* ---- NVS helpers ---- */

esp_err_t ruview_core_save_runtime_config(void)
{
    return runtime_config_save(&s_config, s_config.nvs_namespace);
}

esp_err_t ruview_core_load_runtime_config(void)
{
    return runtime_config_load(&s_config, s_config.nvs_namespace);
}
