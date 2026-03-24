/**
 * @file runtime_config.c
 * @brief NVS runtime configuration — load, save, defaults.
 *
 * Ported from firmware/esp32-csi-node/main/nvs_config.c.
 * Key differences:
 *   - No WiFi SSID/password handling (ESPHome owns that).
 *   - No WASM, display, swarm bridge config.
 *   - Uses ruview_core_config_t instead of nvs_config_t.
 *   - Thresholds stored as u16 (value * 1000) for NVS compatibility.
 */

#include "ruview_core/src/runtime_config.h"
#include "ruview_core/src/compat_logging.h"

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "rv_config";

/* ---- Defaults ---- */

void runtime_config_defaults(ruview_core_config_t *cfg)
{
    if (cfg == NULL) return;

    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->target_ip, "192.168.1.50", sizeof(cfg->target_ip) - 1);
    cfg->target_port         = 5005;
    cfg->node_id             = 1;
    cfg->wifi_channel        = 0;  /* 0 = auto-detect */

    cfg->udp_raw_enabled     = true;
    cfg->udp_vitals_enabled  = true;

    cfg->edge_enabled        = true;
    cfg->edge_tier           = 1;
    cfg->vital_interval_ms   = 1000;
    cfg->top_k_count         = 8;
    cfg->presence_threshold  = 0.0f;   /* 0 = auto-calibrate */
    cfg->fall_threshold      = 15.0f;

    cfg->persist_runtime_to_nvs = false;
    cfg->load_runtime_from_nvs  = true;
    strncpy(cfg->nvs_namespace, "csi_cfg", sizeof(cfg->nvs_namespace) - 1);
}

/* ---- Load from NVS ---- */

esp_err_t runtime_config_load(ruview_core_config_t *cfg, const char *ns)
{
    if (cfg == NULL || ns == NULL) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS namespace '%s' found, keeping defaults", ns);
        return err;
    }

    char buf[64];
    size_t len;

    /* target_ip */
    len = sizeof(buf);
    if (nvs_get_str(handle, "target_ip", buf, &len) == ESP_OK && len > 1) {
        strncpy(cfg->target_ip, buf, sizeof(cfg->target_ip) - 1);
        cfg->target_ip[sizeof(cfg->target_ip) - 1] = '\0';
        ESP_LOGI(TAG, "NVS: target_ip=%s", cfg->target_ip);
    }

    /* target_port */
    uint16_t port_val;
    if (nvs_get_u16(handle, "target_port", &port_val) == ESP_OK) {
        cfg->target_port = port_val;
        ESP_LOGI(TAG, "NVS: target_port=%u", cfg->target_port);
    }

    /* node_id */
    uint8_t u8_val;
    if (nvs_get_u8(handle, "node_id", &u8_val) == ESP_OK) {
        cfg->node_id = u8_val;
        ESP_LOGI(TAG, "NVS: node_id=%u", (unsigned)cfg->node_id);
    }

    /* wifi_channel (csi_channel in original) */
    if (nvs_get_u8(handle, "csi_channel", &u8_val) == ESP_OK) {
        if ((u8_val >= 1 && u8_val <= 14) || (u8_val >= 36 && u8_val <= 177)) {
            cfg->wifi_channel = u8_val;
            ESP_LOGI(TAG, "NVS: wifi_channel=%u", (unsigned)cfg->wifi_channel);
        }
    }

    /* edge_tier */
    if (nvs_get_u8(handle, "edge_tier", &u8_val) == ESP_OK) {
        if (u8_val <= 2) {
            cfg->edge_tier = u8_val;
            ESP_LOGI(TAG, "NVS: edge_tier=%u", (unsigned)cfg->edge_tier);
        }
    }

    /* vital_interval_ms */
    uint16_t u16_val;
    if (nvs_get_u16(handle, "vital_int", &u16_val) == ESP_OK) {
        if (u16_val >= 100) {
            cfg->vital_interval_ms = u16_val;
            ESP_LOGI(TAG, "NVS: vital_interval_ms=%u", cfg->vital_interval_ms);
        }
    }

    /* top_k_count */
    if (nvs_get_u8(handle, "subk_count", &u8_val) == ESP_OK) {
        if (u8_val >= 1 && u8_val <= 32) {
            cfg->top_k_count = u8_val;
            ESP_LOGI(TAG, "NVS: top_k_count=%u", (unsigned)cfg->top_k_count);
        }
    }

    /* presence_threshold (stored as u16 * 1000) */
    uint16_t pres_val;
    if (nvs_get_u16(handle, "pres_thresh", &pres_val) == ESP_OK) {
        cfg->presence_threshold = (float)pres_val / 1000.0f;
        ESP_LOGI(TAG, "NVS: presence_threshold=%.3f", cfg->presence_threshold);
    }

    /* fall_threshold (stored as u16 * 1000) */
    uint16_t fall_val;
    if (nvs_get_u16(handle, "fall_thresh", &fall_val) == ESP_OK) {
        cfg->fall_threshold = (float)fall_val / 1000.0f;
        ESP_LOGI(TAG, "NVS: fall_threshold=%.3f", cfg->fall_threshold);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "NVS config loaded from '%s'", ns);
    return ESP_OK;
}

/* ---- Save to NVS ---- */

esp_err_t runtime_config_save(const ruview_core_config_t *cfg, const char *ns)
{
    if (cfg == NULL || ns == NULL) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s",
                 ns, esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, "target_ip", cfg->target_ip);
    nvs_set_u16(handle, "target_port", cfg->target_port);
    nvs_set_u8(handle,  "node_id",     cfg->node_id);
    nvs_set_u8(handle,  "csi_channel", cfg->wifi_channel);
    nvs_set_u8(handle,  "edge_tier",   cfg->edge_tier);
    nvs_set_u16(handle, "vital_int",   cfg->vital_interval_ms);
    nvs_set_u8(handle,  "subk_count",  cfg->top_k_count);

    uint16_t pres = (uint16_t)(cfg->presence_threshold * 1000.0f);
    nvs_set_u16(handle, "pres_thresh", pres);

    uint16_t fall = (uint16_t)(cfg->fall_threshold * 1000.0f);
    nvs_set_u16(handle, "fall_thresh", fall);

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Runtime config saved to NVS '%s'", ns);
    } else {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }
    return err;
}
