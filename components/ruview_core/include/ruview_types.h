/**
 * @file ruview_types.h
 * @brief Shared types for ruview_core — CSI frame, vitals, ring buffer, DSP state.
 *
 * Ported from firmware/esp32-csi-node edge_processing.h and csi_collector.h.
 * Wire formats (ADR-018, vitals 0xC5110002, compressed 0xC5110003) are
 * preserved exactly.
 */

#ifndef RUVIEW_TYPES_H
#define RUVIEW_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 * ADR-018 frame constants
 * ==================================================================== */

/** ADR-018 raw CSI frame magic. */
#define RUVIEW_CSI_MAGIC          0xC5110001

/** ADR-018 header size in bytes. */
#define RUVIEW_CSI_HEADER_SIZE    20

/** Maximum frame buffer size (header + 4 ant * 256 sub * 2 bytes). */
#define RUVIEW_CSI_MAX_FRAME_SIZE (RUVIEW_CSI_HEADER_SIZE + 4 * 256 * 2)

/** Maximum channels in hop table (ADR-029). */
#define RUVIEW_HOP_CHANNELS_MAX   6

/* ====================================================================
 * Edge / vitals constants
 * ==================================================================== */

/** Vitals packet magic (32-byte wire format). */
#define RUVIEW_VITALS_MAGIC       0xC5110002

/** Delta-compressed frame magic. */
#define RUVIEW_COMPRESSED_MAGIC   0xC5110003

/** SPSC ring buffer slot count (power of 2). */
#define RUVIEW_RING_SLOTS         16

/** Max I/Q payload per ring slot. */
#define RUVIEW_MAX_IQ_BYTES       1024

/** Phase history depth. */
#define RUVIEW_PHASE_HISTORY_LEN  256

/** Max top-K subcarriers. */
#define RUVIEW_TOP_K              8

/** Max subcarriers per frame. */
#define RUVIEW_MAX_SUBCARRIERS    128

/** Max simultaneous persons. */
#define RUVIEW_MAX_PERSONS        4

/** Calibration frame count (~60 s at 20 Hz). */
#define RUVIEW_CALIB_FRAMES       1200

/** Calibration sigma multiplier (mean + 3*sigma). */
#define RUVIEW_CALIB_SIGMA_MULT   3.0f

/** Fall detection cooldown in ms. */
#define RUVIEW_FALL_COOLDOWN_MS   5000

/** Consecutive frames above threshold to trigger fall. */
#define RUVIEW_FALL_CONSEC_MIN    3

/* ====================================================================
 * CSI frame (normalised input to serializer)
 * ==================================================================== */

/**
 * Normalised CSI frame — input to the ADR-018 serializer.
 * Populated from the ESP-IDF wifi_csi_info_t callback data.
 */
typedef struct {
    const uint8_t *iq_data;   /**< Raw I/Q bytes from callback. */
    uint16_t       iq_len;    /**< Length of iq_data in bytes. */
    uint8_t        channel;   /**< WiFi channel number. */
    int8_t         rssi;      /**< Received signal strength. */
    int8_t         noise_floor; /**< Noise floor. */
    uint8_t        n_antennas; /**< Number of RX antennas (usually 1). */
} ruview_csi_frame_t;

/* ====================================================================
 * SPSC ring buffer
 * ==================================================================== */

typedef struct {
    uint8_t  iq_data[RUVIEW_MAX_IQ_BYTES];
    uint16_t iq_len;
    int8_t   rssi;
    uint8_t  channel;
    uint32_t timestamp_us;
} ruview_ring_slot_t;

typedef struct {
    ruview_ring_slot_t slots[RUVIEW_RING_SLOTS];
    volatile uint32_t  head;  /**< Written by producer (Core 0 / WiFi). */
    volatile uint32_t  tail;  /**< Written by consumer (Core 1 / DSP). */
} ruview_ring_buf_t;

/* ====================================================================
 * Biquad IIR filter state
 * ==================================================================== */

typedef struct {
    float b0, b1, b2;
    float a1, a2;
    float x1, x2;
    float y1, y2;
} ruview_biquad_t;

/* ====================================================================
 * Welford running statistics
 * ==================================================================== */

typedef struct {
    double   mean;
    double   m2;
    uint32_t count;
} ruview_welford_t;

/* ====================================================================
 * Per-person vitals (multi-person mode)
 * ==================================================================== */

typedef struct {
    float    phase_history[RUVIEW_PHASE_HISTORY_LEN];
    uint16_t history_len;
    uint16_t history_idx;
    float    breathing_bpm;
    float    heartrate_bpm;
    uint8_t  subcarrier_idx;
    bool     active;
} ruview_person_vitals_t;

/* ====================================================================
 * Vitals packet (32 bytes, wire format — must stay packed)
 * ==================================================================== */

typedef struct __attribute__((packed)) {
    uint32_t magic;           /**< RUVIEW_VITALS_MAGIC = 0xC5110002. */
    uint8_t  node_id;
    uint8_t  flags;           /**< Bit0=presence, Bit1=fall, Bit2=motion. */
    uint16_t breathing_rate;  /**< BPM * 100. */
    uint32_t heartrate;       /**< BPM * 10000. */
    int8_t   rssi;
    uint8_t  n_persons;
    uint8_t  reserved[2];
    float    motion_energy;
    float    presence_score;
    uint32_t timestamp_ms;
    uint32_t reserved2;
} ruview_vitals_pkt_t;

#if defined(__GNUC__) || defined(__clang__)
_Static_assert(sizeof(ruview_vitals_pkt_t) == 32, "vitals packet must be 32 bytes");
#endif

/* ====================================================================
 * Edge configuration
 * ==================================================================== */

typedef struct {
    uint8_t  tier;              /**< 0 = raw, 1 = basic, 2 = full. */
    float    presence_thresh;   /**< 0 = auto-calibrate. */
    float    fall_thresh;       /**< Phase acceleration threshold (rad/s^2). */
    uint16_t vital_window;      /**< Phase history window for BPM. */
    uint16_t vital_interval_ms; /**< Vitals packet send interval (ms). */
    uint8_t  top_k_count;       /**< Number of top subcarriers to track. */
} ruview_edge_config_t;

#ifdef __cplusplus
}
#endif

#endif /* RUVIEW_TYPES_H */
