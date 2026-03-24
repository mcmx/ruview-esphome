"""
RuView CSI — number platform.

Exposes runtime-tunable thresholds to Home Assistant:
  - presence_threshold  — motion energy threshold for presence detection
  - fall_threshold      — phase acceleration threshold for fall detection
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_CONFIG,
)

from . import ruview_csi_ns, RuviewCsiComponent

DEPENDENCIES = ["ruview_csi"]

CONF_RUVIEW_CSI_ID = "ruview_csi_id"
CONF_PRESENCE_THRESHOLD = "presence_threshold"
CONF_FALL_THRESHOLD = "fall_threshold"

RuviewPresenceThresholdNumber = ruview_csi_ns.class_(
    "RuviewPresenceThresholdNumber", number.Number, cg.Component
)
RuviewFallThresholdNumber = ruview_csi_ns.class_(
    "RuviewFallThresholdNumber", number.Number, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_RUVIEW_CSI_ID): cv.use_id(RuviewCsiComponent),
        cv.Optional(CONF_PRESENCE_THRESHOLD): number.number_schema(
            RuviewPresenceThresholdNumber,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:motion-sensor",
        ),
        cv.Optional(CONF_FALL_THRESHOLD): number.number_schema(
            RuviewFallThresholdNumber,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:alert-octagon",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_RUVIEW_CSI_ID])

    if CONF_PRESENCE_THRESHOLD in config:
        n = await number.new_number(
            config[CONF_PRESENCE_THRESHOLD],
            min_value=0.0,
            max_value=10.0,
            step=0.01,
        )
        cg.add(n.set_parent(parent))
        cg.add(parent.set_presence_threshold_number(n))

    if CONF_FALL_THRESHOLD in config:
        n = await number.new_number(
            config[CONF_FALL_THRESHOLD],
            min_value=0.0,
            max_value=100.0,
            step=0.1,
        )
        cg.add(n.set_parent(parent))
        cg.add(parent.set_fall_threshold_number(n))
