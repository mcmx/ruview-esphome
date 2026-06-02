/**
 * @file ruview_csi.h
 * @brief ESPHome component class for RuView CSI sensing.
 *
 * Bridges ESPHome lifecycle (setup/loop/dump_config) to ruview_core.
 * Publishes presence, vitals, and status to Home Assistant entities.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"

extern "C" {
#include "ruview_core.h"
#include "ruview_types.h"
}

#include <string>

namespace esphome {
namespace ruview_csi {

/* ====================================================================
 * Number entities for live threshold tuning
 * ==================================================================== */

class RuviewCsiComponent;  // forward decl

class RuviewPresenceThresholdNumber : public number::Number, public Component {
 public:
  void set_parent(RuviewCsiComponent *parent) { this->parent_ = parent; }
  void setup() override {}
  void dump_config() override {}

 protected:
  void control(float value) override;
  RuviewCsiComponent *parent_{nullptr};
};

class RuviewFallThresholdNumber : public number::Number, public Component {
 public:
  void set_parent(RuviewCsiComponent *parent) { this->parent_ = parent; }
  void setup() override {}
  void dump_config() override {}

 protected:
  void control(float value) override;
  RuviewCsiComponent *parent_{nullptr};
};

/* ====================================================================
 * Main component
 * ==================================================================== */

class RuviewCsiComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  /* ---- Config setters (called by codegen before setup) ---- */
  void set_node_id(uint8_t value)              { this->node_id_ = value; }
  void set_target_ip(const std::string &value)  { this->target_ip_ = value; }
  void set_target_port(uint16_t value)          { this->target_port_ = value; }
  void set_wifi_channel(uint8_t value)          { this->wifi_channel_ = value; }
  void set_udp_raw_enabled(bool value)          { this->udp_raw_enabled_ = value; }
  void set_udp_vitals_enabled(bool value)       { this->udp_vitals_enabled_ = value; }
  void set_edge_enabled(bool value)             { this->edge_enabled_ = value; }
  void set_edge_tier(uint8_t value)             { this->edge_tier_ = value; }
  void set_vital_interval_ms(uint16_t value)    { this->vital_interval_ms_ = value; }
  void set_top_k_count(uint8_t value)           { this->top_k_count_ = value; }
  void set_presence_threshold(float value)      { this->presence_threshold_ = value; }
  void set_fall_threshold(float value)          { this->fall_threshold_ = value; }
  void set_load_runtime_from_nvs(bool value)    { this->load_nvs_ = value; }
  void set_persist_runtime_to_nvs(bool value)   { this->persist_nvs_ = value; }
  void set_nvs_namespace(const std::string &value) { this->nvs_namespace_ = value; }

  /* ---- Entity setters ---- */
  void set_presence_binary_sensor(binary_sensor::BinarySensor *s) { this->presence_bs_ = s; }
  void set_fall_binary_sensor(binary_sensor::BinarySensor *s)     { this->fall_bs_ = s; }
  void set_motion_sensor(sensor::Sensor *s)           { this->motion_sensor_ = s; }
  void set_breathing_sensor(sensor::Sensor *s)        { this->breathing_sensor_ = s; }
  void set_heart_rate_sensor(sensor::Sensor *s)       { this->heart_rate_sensor_ = s; }
  void set_rssi_sensor(sensor::Sensor *s)             { this->rssi_sensor_ = s; }
  void set_csi_frames_sensor(sensor::Sensor *s)       { this->csi_frames_sensor_ = s; }
  void set_raw_frames_sent_sensor(sensor::Sensor *s)  { this->raw_frames_sensor_ = s; }
  void set_vitals_sent_sensor(sensor::Sensor *s)      { this->vitals_sent_sensor_ = s; }
  void set_state_text_sensor(text_sensor::TextSensor *s) { this->state_ts_ = s; }

  void set_presence_threshold_number(RuviewPresenceThresholdNumber *n) {
    this->presence_threshold_number_ = n;
  }
  void set_fall_threshold_number(RuviewFallThresholdNumber *n) {
    this->fall_threshold_number_ = n;
  }

  /* ---- Runtime tuning (called from Number entities) ---- */
  void update_presence_threshold(float value);
  void update_fall_threshold(float value);

 protected:
  /* ---- Configuration ---- */
  uint8_t     node_id_{1};
  std::string target_ip_{"192.168.1.50"};
  uint16_t    target_port_{5005};
  uint8_t     wifi_channel_{0};
  bool        udp_raw_enabled_{true};
  bool        udp_vitals_enabled_{true};
  bool        edge_enabled_{true};
  uint8_t     edge_tier_{1};
  uint16_t    vital_interval_ms_{1000};
  uint8_t     top_k_count_{8};
  float       presence_threshold_{0.0f};
  float       fall_threshold_{15.0f};
  bool        load_nvs_{true};
  bool        persist_nvs_{false};
  std::string nvs_namespace_{"csi_cfg"};

  /* ---- Runtime state ---- */
  bool     started_{false};
  bool     wifi_was_connected_{false};
  uint32_t last_publish_ms_{0};
  uint32_t last_counter_publish_ms_{0};
  std::string current_state_{"idle"};

  /* ---- Cached last-published values (to avoid noisy churn) ---- */
  bool  last_presence_{false};
  bool  last_fall_{false};
  float last_motion_{-1.0f};
  float last_breathing_{-1.0f};
  float last_heart_rate_{-1.0f};
  int32_t last_rssi_{0};

  /* ---- Entity pointers ---- */
  binary_sensor::BinarySensor *presence_bs_{nullptr};
  binary_sensor::BinarySensor *fall_bs_{nullptr};
  sensor::Sensor *motion_sensor_{nullptr};
  sensor::Sensor *breathing_sensor_{nullptr};
  sensor::Sensor *heart_rate_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  sensor::Sensor *csi_frames_sensor_{nullptr};
  sensor::Sensor *raw_frames_sensor_{nullptr};
  sensor::Sensor *vitals_sent_sensor_{nullptr};
  text_sensor::TextSensor *state_ts_{nullptr};
  RuviewPresenceThresholdNumber *presence_threshold_number_{nullptr};
  RuviewFallThresholdNumber *fall_threshold_number_{nullptr};

  /* ---- Internal helpers ---- */
  void set_state_(const std::string &state);
  void publish_sensors_(const ruview_core_status_t &status);
  void publish_counters_(const ruview_core_status_t &status);
  bool is_wifi_connected_();
};

}  // namespace ruview_csi
}  // namespace esphome
