"""
RuView CSI — text_sensor platform.

Exposes a human-readable state string to Home Assistant:
  - state: idle | waiting_for_wifi | starting | running | error
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import ruview_csi_ns, RuviewCsiComponent

DEPENDENCIES = ["ruview_csi"]

CONF_RUVIEW_CSI_ID = "ruview_csi_id"
CONF_STATE = "state"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_RUVIEW_CSI_ID): cv.use_id(RuviewCsiComponent),
        cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:state-machine",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_RUVIEW_CSI_ID])

    if CONF_STATE in config:
        ts = await text_sensor.new_text_sensor(config[CONF_STATE])
        cg.add(parent.set_state_text_sensor(ts))
