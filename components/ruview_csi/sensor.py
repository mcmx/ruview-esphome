"""
RuView CSI — sensor platform.

Exposes analog measurements to Home Assistant:
  - motion_energy
  - breathing_bpm
  - heart_rate_bpm
  - rssi
  - csi_frames_received
  - adr018_frames_sent
  - vitals_packets_sent
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_DECIBEL_MILLIWATT,
    ICON_SIGNAL,
)

from . import ruview_csi_ns, RuviewCsiComponent

DEPENDENCIES = ["ruview_csi"]

CONF_RUVIEW_CSI_ID = "ruview_csi_id"
CONF_MOTION_ENERGY = "motion_energy"
CONF_BREATHING_BPM = "breathing_bpm"
CONF_HEART_RATE_BPM = "heart_rate_bpm"
CONF_RSSI = "rssi"
CONF_CSI_FRAMES_RECEIVED = "csi_frames_received"
CONF_ADR018_FRAMES_SENT = "adr018_frames_sent"
CONF_VITALS_PACKETS_SENT = "vitals_packets_sent"

ICON_MOTION = "mdi:motion-sensor"
ICON_LUNGS = "mdi:lungs"
ICON_HEART = "mdi:heart-pulse"
ICON_COUNTER = "mdi:counter"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_RUVIEW_CSI_ID): cv.use_id(RuviewCsiComponent),
        cv.Optional(CONF_MOTION_ENERGY): sensor.sensor_schema(
            accuracy_decimals=4,
            icon=ICON_MOTION,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BREATHING_BPM): sensor.sensor_schema(
            accuracy_decimals=1,
            icon=ICON_LUNGS,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement="BPM",
        ),
        cv.Optional(CONF_HEART_RATE_BPM): sensor.sensor_schema(
            accuracy_decimals=1,
            icon=ICON_HEART,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement="BPM",
        ),
        cv.Optional(CONF_RSSI): sensor.sensor_schema(
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_CSI_FRAMES_RECEIVED): sensor.sensor_schema(
            accuracy_decimals=0,
            icon=ICON_COUNTER,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_ADR018_FRAMES_SENT): sensor.sensor_schema(
            accuracy_decimals=0,
            icon=ICON_COUNTER,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_VITALS_PACKETS_SENT): sensor.sensor_schema(
            accuracy_decimals=0,
            icon=ICON_COUNTER,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_RUVIEW_CSI_ID])

    if CONF_MOTION_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_MOTION_ENERGY])
        cg.add(parent.set_motion_sensor(sens))

    if CONF_BREATHING_BPM in config:
        sens = await sensor.new_sensor(config[CONF_BREATHING_BPM])
        cg.add(parent.set_breathing_sensor(sens))

    if CONF_HEART_RATE_BPM in config:
        sens = await sensor.new_sensor(config[CONF_HEART_RATE_BPM])
        cg.add(parent.set_heart_rate_sensor(sens))

    if CONF_RSSI in config:
        sens = await sensor.new_sensor(config[CONF_RSSI])
        cg.add(parent.set_rssi_sensor(sens))

    if CONF_CSI_FRAMES_RECEIVED in config:
        sens = await sensor.new_sensor(config[CONF_CSI_FRAMES_RECEIVED])
        cg.add(parent.set_csi_frames_sensor(sens))

    if CONF_ADR018_FRAMES_SENT in config:
        sens = await sensor.new_sensor(config[CONF_ADR018_FRAMES_SENT])
        cg.add(parent.set_raw_frames_sent_sensor(sens))

    if CONF_VITALS_PACKETS_SENT in config:
        sens = await sensor.new_sensor(config[CONF_VITALS_PACKETS_SENT])
        cg.add(parent.set_vitals_sent_sensor(sens))
