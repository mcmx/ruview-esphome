/**
 * @file edge_dsp.c
 * @brief Edge DSP pipeline — ported from firmware/esp32-csi-node/main/edge_processing.c.
 *
 * This is a faithful port of the original DSP pipeline.  Algorithms are
 * preserved without redesign.  Key changes from the original:
 *   - No dependency on g_nvs_config / wasm_runtime / mmwave_sensor.
 *   - Ring buffer pointer is injected, not file-scope.
 *   - Vitals packet node_id is set via setter, not global.
 *   - UDP vitals sending gated by explicit flag.
 */

#include "ruview_core/src/edge_dsp.h"
#include "ruview_core/src/udp_sender.h"
#include "ruview_core/src/compat_logging.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "edge_dsp";

/* ====================================================================
 * State
 * ==================================================================== */

static ruview_edge_config_t s_cfg;
static ruview_ring_buf_t   *s_ring = NULL;
static uint8_t              s_node_id = 0;
static bool                 s_udp_vitals_enabled = false;

/* Per-subcarrier running variance */
static ruview_welford_t s_subcarrier_var[RUVIEW_MAX_SUBCARRIERS];

/* Previous phase per subcarrier (for unwrapping) */
static float s_prev_phase[RUVIEW_MAX_SUBCARRIERS];
static bool  s_phase_initialized;

/* Top-K subcarrier indices */
static uint8_t  s_top_k[RUVIEW_TOP_K];
static uint8_t  s_top_k_count;

/* Phase history ring buffer */
static float    s_phase_history[RUVIEW_PHASE_HISTORY_LEN];
static uint16_t s_history_len;
static uint16_t s_history_idx;

/* Biquad filters */
static ruview_biquad_t s_bq_breathing;
static ruview_biquad_t s_bq_heartrate;

/* Filtered signal histories */
static float s_breathing_filtered[RUVIEW_PHASE_HISTORY_LEN];
static float s_heartrate_filtered[RUVIEW_PHASE_HISTORY_LEN];

/* Vitals state */
static float    s_breathing_bpm;
static float    s_heartrate_bpm;
static float    s_motion_energy;
static float    s_presence_score;
static bool     s_presence_detected;
static bool     s_fall_detected;
static int8_t   s_latest_rssi;
static uint32_t s_frame_count;

/* Fall detection */
static float    s_prev_phase_velocity;
static uint8_t  s_fall_consec_count;
static int64_t  s_fall_last_alert_us;

/* Adaptive calibration */
static bool     s_calibrated;
static float    s_calib_sum;
static float    s_calib_sum_sq;
static uint32_t s_calib_count;
static float    s_adaptive_threshold;

/* Vitals send timing */
static int64_t  s_last_vitals_send_us;

/* Multi-person */
static ruview_person_vitals_t s_persons[RUVIEW_MAX_PERSONS];
static ruview_biquad_t s_person_bq_br[RUVIEW_MAX_PERSONS];
static ruview_biquad_t s_person_bq_hr[RUVIEW_MAX_PERSONS];
static float s_person_br_filt[RUVIEW_MAX_PERSONS][RUVIEW_PHASE_HISTORY_LEN];
static float s_person_hr_filt[RUVIEW_MAX_PERSONS][RUVIEW_PHASE_HISTORY_LEN];

/* Thread-safe latest packet */
static volatile ruview_vitals_pkt_t s_latest_pkt;
static volatile bool s_pkt_valid;

/* DSP task handle */
static TaskHandle_t s_task_handle = NULL;

/* ====================================================================
 * Biquad IIR Filter (from original)
 * ==================================================================== */

static void biquad_bandpass_design(ruview_biquad_t *bq, float fs,
                                   float f_lo, float f_hi)
{
    float w0    = 2.0f * (float)M_PI * (f_lo + f_hi) / 2.0f / fs;
    float bw    = 2.0f * (float)M_PI * (f_hi - f_lo) / fs;
    float alpha = sinf(w0) * sinhf(logf(2.0f) / 2.0f * bw / sinf(w0));

    float a0_inv = 1.0f / (1.0f + alpha);
    bq->b0 =  alpha * a0_inv;
    bq->b1 =  0.0f;
    bq->b2 = -alpha * a0_inv;
    bq->a1 = -2.0f * cosf(w0) * a0_inv;
    bq->a2 =  (1.0f - alpha) * a0_inv;

    bq->x1 = bq->x2 = 0.0f;
    bq->y1 = bq->y2 = 0.0f;
}

static inline float biquad_process(ruview_biquad_t *bq, float x)
{
    float y = bq->b0 * x + bq->b1 * bq->x1 + bq->b2 * bq->x2
            - bq->a1 * bq->y1 - bq->a2 * bq->y2;
    bq->x2 = bq->x1;
    bq->x1 = x;
    bq->y2 = bq->y1;
    bq->y1 = y;
    return y;
}

/* ====================================================================
 * Phase extraction / unwrapping (from original)
 * ==================================================================== */

static inline float extract_phase(const uint8_t *iq, uint16_t idx)
{
    int8_t i_val = (int8_t)iq[idx * 2];
    int8_t q_val = (int8_t)iq[idx * 2 + 1];
    return atan2f((float)q_val, (float)i_val);
}

static inline float unwrap_phase(float prev, float curr)
{
    float diff = curr - prev;
    if (diff > (float)M_PI)        diff -= 2.0f * (float)M_PI;
    else if (diff < -(float)M_PI)  diff += 2.0f * (float)M_PI;
    return prev + diff;
}

/* ====================================================================
 * Welford running statistics (from original)
 * ==================================================================== */

static inline void welford_update(ruview_welford_t *w, double x)
{
    w->count++;
    double delta  = x - w->mean;
    w->mean      += delta / (double)w->count;
    double delta2 = x - w->mean;
    w->m2        += delta * delta2;
}

static inline double welford_variance(const ruview_welford_t *w)
{
    return (w->count > 1) ? (w->m2 / (double)(w->count - 1)) : 0.0;
}

/* ====================================================================
 * Zero-crossing BPM estimation (from original)
 * ==================================================================== */

static float estimate_bpm_zero_crossing(const float *history, uint16_t len,
                                        float sample_rate)
{
    if (len < 4) return 0.0f;

    uint16_t crossings[128];
    uint16_t n_cross = 0;

    for (uint16_t i = 1; i < len && n_cross < 128; i++) {
        if (history[i - 1] <= 0.0f && history[i] > 0.0f) {
            crossings[n_cross++] = i;
        }
    }

    if (n_cross < 2) return 0.0f;

    float total_period = 0.0f;
    for (uint16_t i = 1; i < n_cross; i++) {
        total_period += (float)(crossings[i] - crossings[i - 1]);
    }
    float avg_period = total_period / (float)(n_cross - 1);

    if (avg_period < 1.0f) return 0.0f;
    return (sample_rate / avg_period) * 60.0f;
}

/* ====================================================================
 * Top-K subcarrier selection (from original)
 * ==================================================================== */

static void update_top_k(uint16_t n_subcarriers)
{
    uint8_t k = s_cfg.top_k_count;
    if (k > RUVIEW_TOP_K)       k = RUVIEW_TOP_K;
    if (k > n_subcarriers)      k = (uint8_t)n_subcarriers;

    bool used[RUVIEW_MAX_SUBCARRIERS];
    memset(used, 0, n_subcarriers * sizeof(bool));

    for (uint8_t ki = 0; ki < k; ki++) {
        double best_var = -1.0;
        uint8_t best_idx = 0;

        for (uint16_t sc = 0; sc < n_subcarriers; sc++) {
            if (!used[sc]) {
                double v = welford_variance(&s_subcarrier_var[sc]);
                if (v > best_var) {
                    best_var = v;
                    best_idx = (uint8_t)sc;
                }
            }
        }
        s_top_k[ki]  = best_idx;
        used[best_idx] = true;
    }
    s_top_k_count = k;
}

/* ====================================================================
 * Adaptive calibration (from original)
 * ==================================================================== */

static void calibration_update(float motion)
{
    if (s_calibrated) return;

    s_calib_sum    += motion;
    s_calib_sum_sq += motion * motion;
    s_calib_count++;

    if (s_calib_count >= RUVIEW_CALIB_FRAMES) {
        float mean  = s_calib_sum / (float)s_calib_count;
        float var   = (s_calib_sum_sq / (float)s_calib_count) - (mean * mean);
        float sigma = (var > 0.0f) ? sqrtf(var) : 0.001f;

        s_adaptive_threshold = mean + RUVIEW_CALIB_SIGMA_MULT * sigma;
        if (s_adaptive_threshold < 0.01f) s_adaptive_threshold = 0.01f;

        s_calibrated = true;
        ESP_LOGI(TAG, "Calibration done: mean=%.4f sigma=%.4f thresh=%.4f (%lu frames)",
                 mean, sigma, s_adaptive_threshold,
                 (unsigned long)s_calib_count);
    }
}

/* ====================================================================
 * Multi-person vitals (from original)
 * ==================================================================== */

static void update_multi_person_vitals(const uint8_t *iq_data, uint16_t n_sc,
                                       float sample_rate)
{
    if (s_top_k_count < 2) return;

    uint8_t n_persons = s_top_k_count / 2;
    if (n_persons > RUVIEW_MAX_PERSONS) n_persons = RUVIEW_MAX_PERSONS;
    if (n_persons < 1) n_persons = 1;

    uint8_t subs_per_person = s_top_k_count / n_persons;

    for (uint8_t p = 0; p < n_persons; p++) {
        ruview_person_vitals_t *pv = &s_persons[p];
        pv->active = true;
        pv->subcarrier_idx = s_top_k[p * subs_per_person];

        float avg_phase = 0.0f;
        uint8_t count = 0;
        for (uint8_t s = 0; s < subs_per_person; s++) {
            uint8_t sc_idx = s_top_k[p * subs_per_person + s];
            if (sc_idx < n_sc) {
                avg_phase += extract_phase(iq_data, sc_idx);
                count++;
            }
        }
        if (count > 0) avg_phase /= (float)count;

        if (pv->history_len > 0) {
            uint16_t prev_idx = (pv->history_idx + RUVIEW_PHASE_HISTORY_LEN - 1)
                                % RUVIEW_PHASE_HISTORY_LEN;
            avg_phase = unwrap_phase(pv->phase_history[prev_idx], avg_phase);
        }

        pv->phase_history[pv->history_idx] = avg_phase;
        pv->history_idx = (pv->history_idx + 1) % RUVIEW_PHASE_HISTORY_LEN;
        if (pv->history_len < RUVIEW_PHASE_HISTORY_LEN) pv->history_len++;

        float br_val = biquad_process(&s_person_bq_br[p], avg_phase);
        float hr_val = biquad_process(&s_person_bq_hr[p], avg_phase);

        uint16_t idx = (pv->history_idx + RUVIEW_PHASE_HISTORY_LEN - 1)
                       % RUVIEW_PHASE_HISTORY_LEN;
        s_person_br_filt[p][idx] = br_val;
        s_person_hr_filt[p][idx] = hr_val;

        if (pv->history_len >= 64) {
            float br_buf[RUVIEW_PHASE_HISTORY_LEN];
            float hr_buf[RUVIEW_PHASE_HISTORY_LEN];
            uint16_t buf_len = pv->history_len;

            for (uint16_t i = 0; i < buf_len; i++) {
                uint16_t ri = (pv->history_idx + RUVIEW_PHASE_HISTORY_LEN
                               - buf_len + i) % RUVIEW_PHASE_HISTORY_LEN;
                br_buf[i] = s_person_br_filt[p][ri];
                hr_buf[i] = s_person_hr_filt[p][ri];
            }

            float br = estimate_bpm_zero_crossing(br_buf, buf_len, sample_rate);
            float hr = estimate_bpm_zero_crossing(hr_buf, buf_len, sample_rate);

            if (br >= 6.0f && br <= 40.0f)   pv->breathing_bpm = br;
            if (hr >= 40.0f && hr <= 180.0f)  pv->heartrate_bpm = hr;
        }
    }

    for (uint8_t p = n_persons; p < RUVIEW_MAX_PERSONS; p++) {
        s_persons[p].active = false;
    }
}

/* ====================================================================
 * Vitals packet sending (from original, minus mmWave fusion)
 * ==================================================================== */

static void send_vitals_packet(void)
{
    ruview_vitals_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic   = RUVIEW_VITALS_MAGIC;
    pkt.node_id = s_node_id;

    pkt.flags = 0;
    if (s_presence_detected)        pkt.flags |= 0x01;
    if (s_fall_detected)            pkt.flags |= 0x02;
    if (s_motion_energy > 0.01f)    pkt.flags |= 0x04;

    pkt.breathing_rate = (uint16_t)(s_breathing_bpm * 100.0f);
    pkt.heartrate      = (uint32_t)(s_heartrate_bpm * 10000.0f);
    pkt.rssi           = s_latest_rssi;

    uint8_t n_active = 0;
    for (uint8_t p = 0; p < RUVIEW_MAX_PERSONS; p++) {
        if (s_persons[p].active) n_active++;
    }
    pkt.n_persons = n_active;

    pkt.motion_energy  = s_motion_energy;
    pkt.presence_score = s_presence_score;
    pkt.timestamp_ms   = (uint32_t)(esp_timer_get_time() / 1000);

    /* Thread-safe copy for external readers. */
    s_latest_pkt = pkt;
    s_pkt_valid  = true;

    /* UDP send if enabled. */
    if (s_udp_vitals_enabled) {
        udp_sender_send_vitals(&pkt);
    }
}

/* ====================================================================
 * Main DSP frame processor (from original, minus WASM dispatch)
 * ==================================================================== */

static void process_frame(const ruview_ring_slot_t *slot)
{
    uint16_t n_sc = slot->iq_len / 2;
    if (n_sc == 0 || n_sc > RUVIEW_MAX_SUBCARRIERS) return;

    s_frame_count++;
    s_latest_rssi = slot->rssi;

    const float sample_rate = 20.0f;

    /* Phase extraction + unwrapping */
    float phases[RUVIEW_MAX_SUBCARRIERS];
    for (uint16_t sc = 0; sc < n_sc; sc++) {
        float raw = extract_phase(slot->iq_data, sc);
        if (s_phase_initialized) {
            phases[sc] = unwrap_phase(s_prev_phase[sc], raw);
        } else {
            phases[sc] = raw;
        }
        s_prev_phase[sc] = phases[sc];
    }
    s_phase_initialized = true;

    /* Welford variance per subcarrier */
    for (uint16_t sc = 0; sc < n_sc; sc++) {
        welford_update(&s_subcarrier_var[sc], (double)phases[sc]);
    }

    /* Top-K selection (every 100 frames) */
    if ((s_frame_count % 100) == 1 || s_top_k_count == 0) {
        update_top_k(n_sc);
    }
    if (s_top_k_count == 0) return;

    /* Primary subcarrier phase history */
    float primary_phase = phases[s_top_k[0]];
    s_phase_history[s_history_idx] = primary_phase;
    s_history_idx = (s_history_idx + 1) % RUVIEW_PHASE_HISTORY_LEN;
    if (s_history_len < RUVIEW_PHASE_HISTORY_LEN) s_history_len++;

    /* Biquad filtering */
    float br_val = biquad_process(&s_bq_breathing, primary_phase);
    float hr_val = biquad_process(&s_bq_heartrate, primary_phase);

    uint16_t filt_idx = (s_history_idx + RUVIEW_PHASE_HISTORY_LEN - 1)
                        % RUVIEW_PHASE_HISTORY_LEN;
    s_breathing_filtered[filt_idx] = br_val;
    s_heartrate_filtered[filt_idx] = hr_val;

    /* BPM estimation */
    if (s_history_len >= 64) {
        float br_buf[RUVIEW_PHASE_HISTORY_LEN];
        float hr_buf[RUVIEW_PHASE_HISTORY_LEN];
        uint16_t buf_len = s_history_len;

        for (uint16_t i = 0; i < buf_len; i++) {
            uint16_t ri = (s_history_idx + RUVIEW_PHASE_HISTORY_LEN
                           - buf_len + i) % RUVIEW_PHASE_HISTORY_LEN;
            br_buf[i] = s_breathing_filtered[ri];
            hr_buf[i] = s_heartrate_filtered[ri];
        }

        float br_bpm = estimate_bpm_zero_crossing(br_buf, buf_len, sample_rate);
        float hr_bpm = estimate_bpm_zero_crossing(hr_buf, buf_len, sample_rate);

        if (br_bpm >= 6.0f  && br_bpm <= 40.0f)  s_breathing_bpm = br_bpm;
        if (hr_bpm >= 40.0f && hr_bpm <= 180.0f)  s_heartrate_bpm = hr_bpm;
    }

    /* Motion energy (variance of recent phases) */
    if (s_history_len >= 10) {
        float sum = 0.0f, sum2 = 0.0f;
        uint16_t window = (s_history_len < 20) ? s_history_len : 20;
        for (uint16_t i = 0; i < window; i++) {
            uint16_t ri = (s_history_idx + RUVIEW_PHASE_HISTORY_LEN
                           - window + i) % RUVIEW_PHASE_HISTORY_LEN;
            float v = s_phase_history[ri];
            sum  += v;
            sum2 += v * v;
        }
        float mean = sum / (float)window;
        s_motion_energy = (sum2 / (float)window) - (mean * mean);
        if (s_motion_energy < 0.0f) s_motion_energy = 0.0f;
    }

    /* Presence detection */
    s_presence_score = s_motion_energy;

    if (!s_calibrated && s_cfg.presence_thresh == 0.0f) {
        calibration_update(s_motion_energy);
    }

    float threshold = s_cfg.presence_thresh;
    if (threshold == 0.0f && s_calibrated)  threshold = s_adaptive_threshold;
    else if (threshold == 0.0f)             threshold = 0.05f;
    s_presence_detected = (s_presence_score > threshold);

    /* Fall detection (phase acceleration + debounce) */
    if (s_history_len >= 3) {
        uint16_t i0 = (s_history_idx + RUVIEW_PHASE_HISTORY_LEN - 1) % RUVIEW_PHASE_HISTORY_LEN;
        uint16_t i1 = (s_history_idx + RUVIEW_PHASE_HISTORY_LEN - 2) % RUVIEW_PHASE_HISTORY_LEN;
        float velocity = s_phase_history[i0] - s_phase_history[i1];
        float accel    = fabsf(velocity - s_prev_phase_velocity);
        s_prev_phase_velocity = velocity;

        if (accel > s_cfg.fall_thresh) {
            s_fall_consec_count++;
        } else {
            s_fall_consec_count = 0;
        }

        int64_t now_us     = esp_timer_get_time();
        int64_t cooldown   = (int64_t)RUVIEW_FALL_COOLDOWN_MS * 1000;
        if (s_fall_consec_count >= RUVIEW_FALL_CONSEC_MIN
            && (now_us - s_fall_last_alert_us) >= cooldown) {
            s_fall_detected      = true;
            s_fall_last_alert_us = now_us;
            s_fall_consec_count  = 0;
            ESP_LOGW(TAG, "Fall detected! accel=%.4f thresh=%.4f",
                     accel, s_cfg.fall_thresh);
        } else if (s_fall_consec_count == 0) {
            s_fall_detected = false;
        }
    }

    /* Multi-person vitals */
    update_multi_person_vitals(slot->iq_data, n_sc, sample_rate);

    /* Vitals packet at configured interval */
    int64_t now_us      = esp_timer_get_time();
    int64_t interval_us = (int64_t)s_cfg.vital_interval_ms * 1000;
    if ((now_us - s_last_vitals_send_us) >= interval_us) {
        send_vitals_packet();
        s_last_vitals_send_us = now_us;

        if ((s_frame_count % 200) == 0) {
            ESP_LOGI(TAG, "Vitals: br=%.1f hr=%.1f motion=%.4f pres=%s "
                     "fall=%s persons=%u frames=%lu",
                     s_breathing_bpm, s_heartrate_bpm, s_motion_energy,
                     s_presence_detected ? "YES" : "no",
                     s_fall_detected ? "YES" : "no",
                     (unsigned)s_latest_pkt.n_persons,
                     (unsigned long)s_frame_count);
        }
    }
}

/* ====================================================================
 * Ring buffer pop (SPSC consumer)
 * ==================================================================== */

static inline bool ring_pop(ruview_ring_slot_t *out)
{
    if (s_ring == NULL) return false;
    if (s_ring->tail == s_ring->head) return false;

    memcpy(out, &s_ring->slots[s_ring->tail], sizeof(ruview_ring_slot_t));
    __sync_synchronize();
    s_ring->tail = (s_ring->tail + 1) % RUVIEW_RING_SLOTS;
    return true;
}

/* ====================================================================
 * DSP task (pinned to Core 1)
 * ==================================================================== */

static void edge_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "DSP task started on core %d (tier=%u)",
             xPortGetCoreID(), s_cfg.tier);

    ruview_ring_slot_t slot;
    while (1) {
        if (ring_pop(&slot)) {
            process_frame(&slot);
            /* Yield after every frame to feed the watchdog. */
            vTaskDelay(1);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ====================================================================
 * Public API
 * ==================================================================== */

esp_err_t edge_dsp_init(const ruview_edge_config_t *cfg,
                        ruview_ring_buf_t *ring)
{
    if (cfg == NULL || ring == NULL) {
        ESP_LOGE(TAG, "edge_dsp_init: invalid args");
        return ESP_ERR_INVALID_ARG;
    }

    s_cfg  = *cfg;
    s_ring = ring;

    ESP_LOGI(TAG, "Init (tier=%u, top_k=%u, interval=%ums, pres_thresh=%.3f)",
             s_cfg.tier, s_cfg.top_k_count,
             s_cfg.vital_interval_ms, s_cfg.presence_thresh);

    /* Reset all state */
    memset(s_subcarrier_var, 0, sizeof(s_subcarrier_var));
    memset(s_prev_phase, 0, sizeof(s_prev_phase));
    s_phase_initialized   = false;
    s_top_k_count         = 0;
    s_history_len         = 0;
    s_history_idx         = 0;
    s_breathing_bpm       = 0.0f;
    s_heartrate_bpm       = 0.0f;
    s_motion_energy       = 0.0f;
    s_presence_score      = 0.0f;
    s_presence_detected   = false;
    s_fall_detected       = false;
    s_latest_rssi         = 0;
    s_frame_count         = 0;
    s_prev_phase_velocity = 0.0f;
    s_fall_consec_count   = 0;
    s_fall_last_alert_us  = 0;
    s_last_vitals_send_us = 0;
    s_pkt_valid           = false;

    /* Calibration */
    s_calibrated          = false;
    s_calib_sum           = 0.0f;
    s_calib_sum_sq        = 0.0f;
    s_calib_count         = 0;
    s_adaptive_threshold  = 0.05f;

    /* Multi-person */
    memset(s_persons, 0, sizeof(s_persons));

    /* Design biquad bandpass filters at ~20 Hz sample rate */
    const float fs = 20.0f;
    biquad_bandpass_design(&s_bq_breathing, fs, 0.1f, 0.5f);
    biquad_bandpass_design(&s_bq_heartrate, fs, 0.8f, 2.0f);

    for (uint8_t p = 0; p < RUVIEW_MAX_PERSONS; p++) {
        biquad_bandpass_design(&s_person_bq_br[p], fs, 0.1f, 0.5f);
        biquad_bandpass_design(&s_person_bq_hr[p], fs, 0.8f, 2.0f);
    }

    if (s_cfg.tier == 0) {
        ESP_LOGI(TAG, "Tier 0: raw passthrough (no DSP task)");
        return ESP_OK;
    }

    /* Start DSP task on Core 1 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        edge_task, "edge_dsp", 8192, NULL, 5, &s_task_handle, 1);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DSP task");
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "DSP task created on Core 1 (stack=8192, priority=5)");
    return ESP_OK;
}

bool edge_dsp_get_vitals(ruview_vitals_pkt_t *pkt)
{
    if (!s_pkt_valid || pkt == NULL) return false;
    memcpy(pkt, (const void *)&s_latest_pkt, sizeof(ruview_vitals_pkt_t));
    return true;
}

void edge_dsp_get_quick_status(bool *presence, bool *fall,
                               float *motion, float *br_bpm, float *hr_bpm)
{
    if (presence) *presence = s_presence_detected;
    if (fall)     *fall     = s_fall_detected;
    if (motion)   *motion   = s_motion_energy;
    if (br_bpm)   *br_bpm   = s_breathing_bpm;
    if (hr_bpm)   *hr_bpm   = s_heartrate_bpm;
}

void edge_dsp_set_presence_threshold(float value)
{
    s_cfg.presence_thresh = value;
    ESP_LOGI(TAG, "Presence threshold updated: %.4f", value);
}

void edge_dsp_set_fall_threshold(float value)
{
    s_cfg.fall_thresh = value;
    ESP_LOGI(TAG, "Fall threshold updated: %.4f", value);
}

void edge_dsp_set_node_id(uint8_t node_id)
{
    s_node_id = node_id;
}

uint32_t edge_dsp_get_frame_count(void)
{
    return s_frame_count;
}

void edge_dsp_set_udp_vitals_enabled(bool enabled)
{
    s_udp_vitals_enabled = enabled;
}
