/**
 * @file ruview_core.h
 * @brief Public API for ruview_core — CSI capture, DSP, UDP streaming.
 *
 * This is the single entry point that ESPHome (or any other host) calls.
 * ruview_core does NOT own Wi-Fi, NVS flash init, or app_main().
 *
 * Lifecycle:
 *   1. ruview_core_init(config)   — configure, allocate, design filters
 *   2. ruview_core_set_wifi_ready(true) — signal that Wi-Fi is connected
 *   3. ruview_core_start()        — register CSI callback, start DSP task
 *   4. (loop) ruview_core_get_status()  — poll vitals / counters
 *   5. ruview_core_stop()         — tear down
 */

#ifndef RUVIEW_CORE_H
#define RUVIEW_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * Configuration
 * ==================================================================== */

typedef struct {
    /* Network / aggregator */
    char     target_ip[64];
    uint16_t target_port;
    uint8_t  node_id;
    uint8_t  wifi_channel;

    /* UDP feature flags */
    bool udp_raw_enabled;
    bool udp_vitals_enabled;

    /* Edge DSP */
    bool    edge_enabled;
    uint8_t edge_tier;           /**< 0 = raw, 1 = basic, 2 = full. */
    uint16_t vital_interval_ms;
    uint8_t  top_k_count;
    float    presence_threshold;
    float    fall_threshold;

    /* NVS persistence */
    bool persist_runtime_to_nvs;
    bool load_runtime_from_nvs;
    char nvs_namespace[16];
} ruview_core_config_t;

/* ====================================================================
 * Live status (read-only snapshot)
 * ==================================================================== */

typedef struct {
    bool     running;
    bool     wifi_ready;
    uint32_t csi_frames_received;
    uint32_t adr018_frames_sent;
    uint32_t vitals_packets_sent;
    int32_t  last_rssi;
    bool     presence;
    bool     fall_detected;
    float    motion_energy;
    float    breathing_bpm;
    float    heart_rate_bpm;
} ruview_core_status_t;

/* ====================================================================
 * Lifecycle
 * ==================================================================== */

/**
 * Initialise the core: store config, design filters, init ring buffer.
 * Does NOT start capture — call ruview_core_start() after Wi-Fi is ready.
 * Safe to call multiple times (re-initialises).
 */
esp_err_t ruview_core_init(const ruview_core_config_t *config);

/**
 * Start CSI capture + edge DSP task.
 * Requires Wi-Fi to be ready (call ruview_core_set_wifi_ready first).
 * No-op if already started.
 */
esp_err_t ruview_core_start(void);

/**
 * Stop CSI capture + edge DSP task.
 * Safe to call if not started.
 */
esp_err_t ruview_core_stop(void);

/**
 * Signal whether Wi-Fi station is connected and ready for CSI.
 */
esp_err_t ruview_core_set_wifi_ready(bool ready);

/**
 * Get a snapshot of current status (thread-safe copy).
 */
esp_err_t ruview_core_get_status(ruview_core_status_t *out_status);

/* ====================================================================
 * Runtime tuning
 * ==================================================================== */

esp_err_t ruview_core_set_presence_threshold(float value);
esp_err_t ruview_core_set_fall_threshold(float value);
esp_err_t ruview_core_set_target(const char *ip, uint16_t port);

/* ====================================================================
 * NVS helpers
 * ==================================================================== */

esp_err_t ruview_core_save_runtime_config(void);
esp_err_t ruview_core_load_runtime_config(void);

#ifdef __cplusplus
}
#endif

#endif /* RUVIEW_CORE_H */
