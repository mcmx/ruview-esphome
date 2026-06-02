/**
 * @file adr018_serializer.h
 * @brief ADR-018 binary frame serializer — transport-agnostic.
 *
 * Accepts a normalised ruview_csi_frame_t and produces the exact ADR-018
 * wire format.  Does NOT send anything — the caller owns transport.
 */

#ifndef ADR018_SERIALIZER_H
#define ADR018_SERIALIZER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "ruview_core/include/ruview_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Serialize a CSI frame into ADR-018 binary format.
 *
 * Layout (little-endian):
 *   [0..3]   Magic  0xC5110001
 *   [4]      node_id
 *   [5]      n_antennas
 *   [6..7]   n_subcarriers  (u16)
 *   [8..11]  freq_mhz       (u32)
 *   [12..15] sequence        (u32, auto-incremented)
 *   [16]     rssi            (i8)
 *   [17]     noise_floor     (i8)
 *   [18..19] reserved
 *   [20..]   raw I/Q data
 *
 * @param frame        Normalised CSI frame.
 * @param node_id      Node identifier to stamp into the header.
 * @param out_buf      Output buffer (>= RUVIEW_CSI_MAX_FRAME_SIZE).
 * @param out_buf_size Size of out_buf.
 * @param out_len      Receives actual serialized length.
 * @return ESP_OK on success.
 */
esp_err_t adr018_serialize_frame(const ruview_csi_frame_t *frame,
                                 uint8_t node_id,
                                 uint8_t *out_buf,
                                 size_t out_buf_size,
                                 size_t *out_len);

/**
 * Reset the internal sequence counter (useful for tests / reinit).
 */
void adr018_serializer_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ADR018_SERIALIZER_H */
