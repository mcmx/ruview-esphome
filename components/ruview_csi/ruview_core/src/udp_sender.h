/**
 * @file udp_sender.h
 * @brief UDP sender for ADR-018 frames and vitals packets.
 *
 * Maintains a single UDP socket with configurable destination.
 * Includes ENOMEM backoff from the original firmware.
 */

#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "ruview_core/include/ruview_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create UDP socket and set initial destination.
 */
esp_err_t udp_sender_init(const char *target_ip, uint16_t target_port);

/**
 * Change destination at runtime (does not re-create socket).
 */
esp_err_t udp_sender_update_target(const char *target_ip, uint16_t target_port);

/**
 * Send a raw byte buffer (ADR-018 frame or any packet).
 * Returns number of bytes sent, or negative on error.
 */
int udp_sender_send_raw(const uint8_t *buf, size_t len);

/**
 * Send a 32-byte vitals packet.
 */
esp_err_t udp_sender_send_vitals(const ruview_vitals_pkt_t *vitals);

/**
 * Close the socket and release resources.
 */
esp_err_t udp_sender_deinit(void);

/**
 * Get cumulative send counters.
 */
void udp_sender_get_counters(uint32_t *raw_sent, uint32_t *vitals_sent,
                             uint32_t *errors);

#ifdef __cplusplus
}
#endif

#endif /* UDP_SENDER_H */
