/**
 * @file ruview_csi.cpp
 * @brief ESPHome component implementation for RuView CSI sensing.
 *
 * Lifecycle:
 *   setup()  — build config, init core, wait for Wi-Fi
 *   loop()   — detect Wi-Fi, start core once, publish sensors at ~1 Hz
 */

#include "ruview_csi.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include "esp_wifi.h"
#include "nvs_flash.h"

extern "C" {
#include "ruview_core/src/runtime_config.h"
}

namespace esphome {
namespace ruview_csi {

static const char *TAG = "ruview_csi";

/* Publish intervals */
static const uint32_t SENSOR_PUBLISH_INTERVAL_MS  = 1000;
static const uint32_t COUNTER_PUBLISH_INTERVAL_MS = 5000;

/* ====================================================================
 * Number entity control callbacks
 * ==================================================================== */

void RuviewPresenceThresholdNumber::control(float value) {
  this->publish_state(value);
  if (this->parent_ != nullptr) {
    this->parent_->update_presence_threshold(value);
  }
}

void RuviewFallThresholdNumber::control(float value) {
  this->publish_state(value);
  if (this->parent_ != nullptr) {
    this->parent_->update_fall_threshold(value);
  }
}

/* ====================================================================
 * Component lifecycle
 * ==================================================================== */

float RuviewCsiComponent::get_setup_priority() const {
  /* Run after Wi-Fi is initialised but we'll wait for connection in loop(). */
  return setup_priority::AFTER_WIFI;
}

void RuviewCsiComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up RuView CSI...");

  /* Ensure NVS flash is initialised (ESPHome usually does this). */
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  /* Build config — start with defaults. */
  ruview_core_config_t cfg;
  runtime_config_defaults(&cfg);

  /* Step 2: optionally load NVS values. */
  if (this->load_nvs_) {
    runtime_config_load(&cfg, this->nvs_namespace_.c_str());
  }

  /* Step 3: apply YAML values (always override NVS). */
  strncpy(cfg.target_ip, this->target_ip_.c_str(), sizeof(cfg.target_ip) - 1);
  cfg.target_ip[sizeof(cfg.target_ip) - 1] = '\0';
  cfg.target_port        = this->target_port_;
  cfg.node_id            = this->node_id_;
  cfg.wifi_channel       = this->wifi_channel_;
  cfg.udp_raw_enabled    = this->udp_raw_enabled_;
  cfg.udp_vitals_enabled = this->udp_vitals_enabled_;
  cfg.edge_enabled       = this->edge_enabled_;
  cfg.edge_tier          = this->edge_tier_;
  cfg.vital_interval_ms  = this->vital_interval_ms_;
  cfg.top_k_count        = this->top_k_count_;
  cfg.presence_threshold = this->presence_threshold_;
  cfg.fall_threshold     = this->fall_threshold_;
  cfg.persist_runtime_to_nvs = this->persist_nvs_;
  cfg.load_runtime_from_nvs  = this->load_nvs_;
  strncpy(cfg.nvs_namespace, this->nvs_namespace_.c_str(),
          sizeof(cfg.nvs_namespace) - 1);
  cfg.nvs_namespace[sizeof(cfg.nvs_namespace) - 1] = '\0';

  /* Init core (does not start capture yet). */
  err = ruview_core_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ruview_core_init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    this->set_state_("error");
    return;
  }

  /* Publish initial number values if entities exist. */
  if (this->presence_threshold_number_ != nullptr) {
    this->presence_threshold_number_->publish_state(this->presence_threshold_);
  }
  if (this->fall_threshold_number_ != nullptr) {
    this->fall_threshold_number_->publish_state(this->fall_threshold_);
  }

  this->set_state_("waiting_for_wifi");
  ESP_LOGCONFIG(TAG, "RuView CSI initialised, waiting for Wi-Fi...");
}

void RuviewCsiComponent::loop() {
  uint32_t now = millis();

  /* ---- Wi-Fi detection ---- */
  bool wifi_connected = this->is_wifi_connected_();

  if (wifi_connected && !this->wifi_was_connected_) {
    /* Wi-Fi just became available. */
    ESP_LOGI(TAG, "Wi-Fi connected — starting CSI capture");
    ruview_core_set_wifi_ready(true);

    this->set_state_("starting");
    esp_err_t err = ruview_core_start();
    if (err == ESP_OK) {
      this->started_ = true;
      this->set_state_("running");
    } else {
      ESP_LOGE(TAG, "ruview_core_start failed: %s", esp_err_to_name(err));
      this->set_state_("error");
    }
  } else if (!wifi_connected && this->wifi_was_connected_) {
    /* Wi-Fi lost — stop capture. */
    ESP_LOGW(TAG, "Wi-Fi disconnected — stopping CSI capture");
    ruview_core_set_wifi_ready(false);
    ruview_core_stop();
    this->started_ = false;
    this->set_state_("waiting_for_wifi");
  }

  this->wifi_was_connected_ = wifi_connected;

  /* ---- Sensor publishing (only when running) ---- */
  if (!this->started_) return;

  ruview_core_status_t status;
  ruview_core_get_status(&status);

  /* Vitals / binary sensors at ~1 Hz. */
  if ((now - this->last_publish_ms_) >= SENSOR_PUBLISH_INTERVAL_MS) {
    this->publish_sensors_(status);
    this->last_publish_ms_ = now;
  }

  /* Counters at ~5 s. */
  if ((now - this->last_counter_publish_ms_) >= COUNTER_PUBLISH_INTERVAL_MS) {
    this->publish_counters_(status);
    this->last_counter_publish_ms_ = now;
  }
}

void RuviewCsiComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "RuView CSI:");
  ESP_LOGCONFIG(TAG, "  Node ID: %u", (unsigned)this->node_id_);
  ESP_LOGCONFIG(TAG, "  Target: %s:%u", this->target_ip_.c_str(),
                (unsigned)this->target_port_);
  ESP_LOGCONFIG(TAG, "  Wi-Fi Channel: %u%s", (unsigned)this->wifi_channel_,
                this->wifi_channel_ == 0 ? " (auto)" : "");
  ESP_LOGCONFIG(TAG, "  UDP Raw: %s", this->udp_raw_enabled_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  UDP Vitals: %s", this->udp_vitals_enabled_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Edge DSP: %s (tier=%u)", this->edge_enabled_ ? "yes" : "no",
                (unsigned)this->edge_tier_);
  ESP_LOGCONFIG(TAG, "  Vital Interval: %u ms", (unsigned)this->vital_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Top-K: %u", (unsigned)this->top_k_count_);
  ESP_LOGCONFIG(TAG, "  Presence Threshold: %.3f%s", this->presence_threshold_,
                this->presence_threshold_ == 0.0f ? " (auto-calibrate)" : "");
  ESP_LOGCONFIG(TAG, "  Fall Threshold: %.3f", this->fall_threshold_);
  ESP_LOGCONFIG(TAG, "  NVS Load: %s", this->load_nvs_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  NVS Persist: %s", this->persist_nvs_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  NVS Namespace: %s", this->nvs_namespace_.c_str());
}

/* ====================================================================
 * Runtime tuning
 * ==================================================================== */

void RuviewCsiComponent::update_presence_threshold(float value) {
  this->presence_threshold_ = value;
  ruview_core_set_presence_threshold(value);
  ESP_LOGI(TAG, "Presence threshold set to %.4f", value);

  if (this->persist_nvs_) {
    ruview_core_save_runtime_config();
  }
}

void RuviewCsiComponent::update_fall_threshold(float value) {
  this->fall_threshold_ = value;
  ruview_core_set_fall_threshold(value);
  ESP_LOGI(TAG, "Fall threshold set to %.4f", value);

  if (this->persist_nvs_) {
    ruview_core_save_runtime_config();
  }
}

/* ====================================================================
 * Internal helpers
 * ==================================================================== */

void RuviewCsiComponent::set_state_(const std::string &state) {
  if (state == this->current_state_) return;
  this->current_state_ = state;
  if (this->state_ts_ != nullptr) {
    this->state_ts_->publish_state(state);
  }
}

void RuviewCsiComponent::publish_sensors_(const ruview_core_status_t &status) {
  /* Binary sensors — publish on state change. */
  if (this->presence_bs_ != nullptr && status.presence != this->last_presence_) {
    this->presence_bs_->publish_state(status.presence);
    this->last_presence_ = status.presence;
  }

  if (this->fall_bs_ != nullptr && status.fall_detected != this->last_fall_) {
    this->fall_bs_->publish_state(status.fall_detected);
    this->last_fall_ = status.fall_detected;
  }

  /* Analog sensors — publish at interval, only if changed. */
  if (this->motion_sensor_ != nullptr) {
    if (fabsf(status.motion_energy - this->last_motion_) > 0.001f) {
      this->motion_sensor_->publish_state(status.motion_energy);
      this->last_motion_ = status.motion_energy;
    }
  }

  if (this->breathing_sensor_ != nullptr) {
    if (fabsf(status.breathing_bpm - this->last_breathing_) > 0.1f) {
      this->breathing_sensor_->publish_state(status.breathing_bpm);
      this->last_breathing_ = status.breathing_bpm;
    }
  }

  if (this->heart_rate_sensor_ != nullptr) {
    if (fabsf(status.heart_rate_bpm - this->last_heart_rate_) > 0.1f) {
      this->heart_rate_sensor_->publish_state(status.heart_rate_bpm);
      this->last_heart_rate_ = status.heart_rate_bpm;
    }
  }

  if (this->rssi_sensor_ != nullptr) {
    if (status.last_rssi != this->last_rssi_) {
      this->rssi_sensor_->publish_state((float)status.last_rssi);
      this->last_rssi_ = status.last_rssi;
    }
  }
}

void RuviewCsiComponent::publish_counters_(const ruview_core_status_t &status) {
  if (this->csi_frames_sensor_ != nullptr) {
    this->csi_frames_sensor_->publish_state((float)status.csi_frames_received);
  }
  if (this->raw_frames_sensor_ != nullptr) {
    this->raw_frames_sensor_->publish_state((float)status.adr018_frames_sent);
  }
  if (this->vitals_sent_sensor_ != nullptr) {
    this->vitals_sent_sensor_->publish_state((float)status.vitals_packets_sent);
  }
}

bool RuviewCsiComponent::is_wifi_connected_() {
  wifi_ap_record_t ap_info;
  return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

}  // namespace ruview_csi
}  // namespace esphome
