/**
 * @file adr018_serializer.c
 * @brief ADR-018 binary frame serializer implementation.
 *
 * Ported from firmware/esp32-csi-node/main/csi_collector.c
 * (csi_serialize_frame).  The wire format is preserved exactly.
 */

#include "ruview_core/src/adr018_serializer.h"
#include "ruview_core/src/compat_logging.h"
#include <string.h>

static const char *TAG = "adr018";

/** Auto-incrementing sequence number. */
static uint32_t s_sequence = 0;

/* ---- Channel-to-frequency mapping (same as original) ---- */

static uint32_t channel_to_freq_mhz(uint8_t channel)
{
    if (channel >= 1 && channel <= 13) {
        return 2412 + (channel - 1) * 5;
    } else if (channel == 14) {
        return 2484;
    } else if (channel >= 36 && channel <= 177) {
        return 5000 + channel * 5;
    }
    return 0;
}

/* ---- Public API ---- */

esp_err_t adr018_serialize_frame(const ruview_csi_frame_t *frame,
                                 uint8_t node_id,
                                 uint8_t *out_buf,
                                 size_t out_buf_size,
                                 size_t *out_len)
{
    if (frame == NULL || out_buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frame->iq_data == NULL || frame->iq_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t  n_ant = (frame->n_antennas > 0) ? frame->n_antennas : 1;
    uint16_t iq_len = frame->iq_len;
    uint16_t n_sub  = iq_len / (2 * n_ant);

    size_t total = RUVIEW_CSI_HEADER_SIZE + iq_len;
    if (total > out_buf_size) {
        ESP_LOGW(TAG, "Buffer too small: need %u, have %u",
                 (unsigned)total, (unsigned)out_buf_size);
        *out_len = 0;
        return ESP_ERR_NO_MEM;
    }

    uint32_t freq = channel_to_freq_mhz(frame->channel);
    uint32_t magic = RUVIEW_CSI_MAGIC;
    uint32_t seq   = s_sequence++;

    /* Magic (LE) */
    memcpy(&out_buf[0], &magic, 4);

    /* Node ID */
    out_buf[4] = node_id;

    /* Number of antennas */
    out_buf[5] = n_ant;

    /* Number of subcarriers (LE u16) */
    memcpy(&out_buf[6], &n_sub, 2);

    /* Frequency MHz (LE u32) */
    memcpy(&out_buf[8], &freq, 4);

    /* Sequence (LE u32) */
    memcpy(&out_buf[12], &seq, 4);

    /* RSSI (i8) */
    out_buf[16] = (uint8_t)(int8_t)frame->rssi;

    /* Noise floor (i8) */
    out_buf[17] = (uint8_t)(int8_t)frame->noise_floor;

    /* Reserved */
    out_buf[18] = 0;
    out_buf[19] = 0;

    /* I/Q data */
    memcpy(&out_buf[RUVIEW_CSI_HEADER_SIZE], frame->iq_data, iq_len);

    *out_len = total;
    return ESP_OK;
}

void adr018_serializer_reset(void)
{
    s_sequence = 0;
}
