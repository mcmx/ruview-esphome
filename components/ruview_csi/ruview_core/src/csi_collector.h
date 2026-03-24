/**
 * @file csi_collector.h
 * @brief CSI callback registration and frame queuing.
 *
 * Registers the ESP-IDF CSI receive callback and pushes frames into
 * the SPSC ring buffer for the DSP task.  Does NOT own Wi-Fi init.
 */

#ifndef CSI_COLLECTOR_H
#define CSI_COLLECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ruview_core/include/ruview_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the CSI collector.
 *
 * Sets up promiscuous mode, registers CSI callback, enables CSI.
 * Requires Wi-Fi to be started (STA mode connected or at least started).
 *
 * @param node_id     Node identifier for ADR-018 header.
 * @param channel     Wi-Fi channel for CSI (0 = auto-detect from AP).
 * @param ring        Pointer to the shared ring buffer (owned by caller).
 * @param udp_raw_en  If true, serialize and UDP-send raw ADR-018 frames.
 * @param target_ip   UDP target IP (only used if udp_raw_en).
 * @param target_port UDP target port (only used if udp_raw_en).
 * @return ESP_OK on success.
 */
esp_err_t csi_collector_init(uint8_t node_id, uint8_t channel,
                             ruview_ring_buf_t *ring,
                             bool udp_raw_en,
                             const char *target_ip, uint16_t target_port);

/**
 * Stop CSI collection: disable CSI, disable promiscuous mode.
 */
esp_err_t csi_collector_stop(void);

/**
 * Get the total number of CSI callbacks received.
 */
uint32_t csi_collector_get_frame_count(void);

/**
 * Get the number of raw ADR-018 frames sent via UDP.
 */
uint32_t csi_collector_get_send_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CSI_COLLECTOR_H */
