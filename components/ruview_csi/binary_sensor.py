"""
RuView CSI — binary_sensor platform.

Exposes boolean state to Home Assistant:
  - presence       — someone detected in the room
  - fall_detected   — fall event triggered
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_OCCUPANCY,
    DEVICE_CLASS_SAFETY,
)

from . import ruview_csi_ns, RuviewCsiComponent

DEPENDENCIES = ["ruview_csi"]

CONF_RUVIEW_CSI_ID = "ruview_csi_id"
CONF_PRESENCE = "presence"
CONF_FALL_DETECTED = "fall_detected"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_RUVIEW_CSI_ID): cv.use_id(RuviewCsiComponent),
        cv.Optional(CONF_PRESENCE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_OCCUPANCY,
        ),
        cv.Optional(CONF_FALL_DETECTED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_SAFETY,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_RUVIEW_CSI_ID])

    if CONF_PRESENCE in config:
        bs = await binary_sensor.new_binary_sensor(config[CONF_PRESENCE])
        cg.add(parent.set_presence_binary_sensor(bs))

    if CONF_FALL_DETECTED in config:
        bs = await binary_sensor.new_binary_sensor(config[CONF_FALL_DETECTED])
        cg.add(parent.set_fall_binary_sensor(bs))
