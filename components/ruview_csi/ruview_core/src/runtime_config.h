/**
 * @file runtime_config.h
 * @brief NVS-backed runtime configuration — load, save, defaults.
 *
 * Config precedence:
 *   1. Compile-time defaults (set by ruview_core_config_t)
 *   2. NVS values (if load_runtime_from_nvs is enabled)
 *   3. YAML values from ESPHome (applied after NVS load)
 *   4. Live tuning via number entities
 *   5. Optional NVS persist (if persist_runtime_to_nvs is enabled)
 */

#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ruview_core/include/ruview_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load persisted fields from NVS into the config struct.
 * Only modifies fields that exist in NVS; leaves others untouched.
 *
 * @param cfg       Config struct to update (should already have defaults).
 * @param ns        NVS namespace (max 15 chars).
 * @return ESP_OK, or ESP_ERR_NVS_NOT_FOUND if namespace doesn't exist yet.
 */
esp_err_t runtime_config_load(ruview_core_config_t *cfg, const char *ns);

/**
 * Save current runtime-tunable fields to NVS.
 *
 * @param cfg       Config struct to persist from.
 * @param ns        NVS namespace.
 * @return ESP_OK on success.
 */
esp_err_t runtime_config_save(const ruview_core_config_t *cfg, const char *ns);

/**
 * Populate a config struct with sane defaults.
 */
void runtime_config_defaults(ruview_core_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_CONFIG_H */
