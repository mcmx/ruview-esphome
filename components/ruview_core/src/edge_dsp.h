/**
 * @file edge_dsp.h
 * @brief Edge DSP pipeline — presence, vitals, fall detection.
 *
 * Consumes CSI frames from the SPSC ring buffer.  Runs the full
 * signal-processing pipeline on Core 1 via a FreeRTOS task.
 */

#ifndef EDGE_DSP_H
#define EDGE_DSP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ruview_core/include/ruview_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the edge DSP pipeline.
 *
 * Designs biquad filters, resets all state, and starts the DSP task
 * on Core 1 (unless tier == 0, which is raw passthrough).
 *
 * @param cfg   Edge configuration (thresholds, window sizes, tier).
 * @param ring  Shared ring buffer (producer = CSI collector).
 * @return ESP_OK on success.
 */
esp_err_t edge_dsp_init(const ruview_edge_config_t *cfg,
                        ruview_ring_buf_t *ring);

/**
 * Get the latest vitals snapshot (thread-safe copy).
 * @return true if valid data is available.
 */
bool edge_dsp_get_vitals(ruview_vitals_pkt_t *pkt);

/**
 * Get current presence/fall/motion state without full packet.
 */
void edge_dsp_get_quick_status(bool *presence, bool *fall,
                               float *motion, float *br_bpm, float *hr_bpm);

/**
 * Update presence threshold at runtime.
 */
void edge_dsp_set_presence_threshold(float value);

/**
 * Update fall threshold at runtime.
 */
void edge_dsp_set_fall_threshold(float value);

/**
 * Set the node_id used in vitals packets.
 */
void edge_dsp_set_node_id(uint8_t node_id);

/**
 * Get the DSP frame counter (total frames processed).
 */
uint32_t edge_dsp_get_frame_count(void);

/**
 * Enable/disable UDP vitals sending from the DSP task.
 */
void edge_dsp_set_udp_vitals_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_DSP_H */
