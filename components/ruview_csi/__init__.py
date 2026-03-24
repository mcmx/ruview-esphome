"""
RuView CSI — ESPHome external component for WiFi CSI presence sensing.

Hub component: owns the main ``ruview_csi:`` YAML block, validates config,
and emits C++ codegen that constructs ``RuviewCsiComponent`` and wires
all setters before ``setup()`` runs.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)

CODEOWNERS = ["@anomalyco"]
DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "number"]
MULTI_CONF = False

# C++ namespace / class references
ruview_csi_ns = cg.esphome_ns.namespace("ruview_csi")
RuviewCsiComponent = ruview_csi_ns.class_("RuviewCsiComponent", cg.Component)

# ---- Config keys ----
CONF_NODE_ID = "node_id"
CONF_TARGET_IP = "target_ip"
CONF_TARGET_PORT = "target_port"
CONF_WIFI_CHANNEL = "wifi_channel"
CONF_UDP_RAW_ENABLED = "udp_raw_enabled"
CONF_UDP_VITALS_ENABLED = "udp_vitals_enabled"
CONF_EDGE_ENABLED = "edge_enabled"
CONF_EDGE_TIER = "edge_tier"
CONF_VITAL_INTERVAL_MS = "vital_interval_ms"
CONF_TOP_K_COUNT = "top_k_count"
CONF_PRESENCE_THRESHOLD = "presence_threshold"
CONF_FALL_THRESHOLD = "fall_threshold"
CONF_LOAD_RUNTIME_FROM_NVS = "load_runtime_from_nvs"
CONF_PERSIST_RUNTIME_TO_NVS = "persist_runtime_to_nvs"
CONF_NVS_NAMESPACE = "nvs_namespace"


def _validate_udp_target(config):
    """If either UDP mode is on, target_ip and target_port are required."""
    if config.get(CONF_UDP_RAW_ENABLED, True) or config.get(
        CONF_UDP_VITALS_ENABLED, True
    ):
        if CONF_TARGET_IP not in config:
            raise cv.Invalid(
                "target_ip is required when udp_raw_enabled or udp_vitals_enabled is true"
            )
        if CONF_TARGET_PORT not in config:
            raise cv.Invalid(
                "target_port is required when udp_raw_enabled or udp_vitals_enabled is true"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RuviewCsiComponent),
            cv.Optional(CONF_NODE_ID, default=1): cv.int_range(min=0, max=255),
            cv.Optional(CONF_TARGET_IP): cv.string,
            cv.Optional(CONF_TARGET_PORT, default=5005): cv.port,
            cv.Optional(CONF_WIFI_CHANNEL, default=0): cv.int_range(min=0, max=14),
            cv.Optional(CONF_UDP_RAW_ENABLED, default=True): cv.boolean,
            cv.Optional(CONF_UDP_VITALS_ENABLED, default=True): cv.boolean,
            cv.Optional(CONF_EDGE_ENABLED, default=True): cv.boolean,
            cv.Optional(CONF_EDGE_TIER, default=1): cv.int_range(min=0, max=2),
            cv.Optional(CONF_VITAL_INTERVAL_MS, default=1000): cv.int_range(
                min=100, max=60000
            ),
            cv.Optional(CONF_TOP_K_COUNT, default=3): cv.int_range(min=1, max=8),
            cv.Optional(CONF_PRESENCE_THRESHOLD, default=0.0): cv.float_range(
                min=0.0, max=10.0
            ),
            cv.Optional(CONF_FALL_THRESHOLD, default=15.0): cv.float_range(
                min=0.0, max=100.0
            ),
            cv.Optional(CONF_LOAD_RUNTIME_FROM_NVS, default=True): cv.boolean,
            cv.Optional(CONF_PERSIST_RUNTIME_TO_NVS, default=False): cv.boolean,
            cv.Optional(CONF_NVS_NAMESPACE, default="csi_cfg"): cv.All(
                cv.string, cv.Length(min=1, max=15)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_udp_target,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_node_id(config[CONF_NODE_ID]))
    if CONF_TARGET_IP in config:
        cg.add(var.set_target_ip(config[CONF_TARGET_IP]))
    cg.add(var.set_target_port(config[CONF_TARGET_PORT]))
    cg.add(var.set_wifi_channel(config[CONF_WIFI_CHANNEL]))
    cg.add(var.set_udp_raw_enabled(config[CONF_UDP_RAW_ENABLED]))
    cg.add(var.set_udp_vitals_enabled(config[CONF_UDP_VITALS_ENABLED]))
    cg.add(var.set_edge_enabled(config[CONF_EDGE_ENABLED]))
    cg.add(var.set_edge_tier(config[CONF_EDGE_TIER]))
    cg.add(var.set_vital_interval_ms(config[CONF_VITAL_INTERVAL_MS]))
    cg.add(var.set_top_k_count(config[CONF_TOP_K_COUNT]))
    cg.add(var.set_presence_threshold(config[CONF_PRESENCE_THRESHOLD]))
    cg.add(var.set_fall_threshold(config[CONF_FALL_THRESHOLD]))
    cg.add(var.set_load_runtime_from_nvs(config[CONF_LOAD_RUNTIME_FROM_NVS]))
    cg.add(var.set_persist_runtime_to_nvs(config[CONF_PERSIST_RUNTIME_TO_NVS]))
    cg.add(var.set_nvs_namespace(config[CONF_NVS_NAMESPACE]))
