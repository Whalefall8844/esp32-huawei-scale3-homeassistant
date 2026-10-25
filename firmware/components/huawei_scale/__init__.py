# SPDX-License-Identifier: GPL-3.0-or-later
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, sensor, text_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "text_sensor"]
ns = cg.global_ns.namespace("huawei_scale")
HuaweiScaleClient = ns.class_("HuaweiScaleClient", cg.Component, ble_client.BLEClientNode)
def key_hex(value):
    value = cv.string_strict(value)
    if value == "":
        return value
    if len(value) != 32 or any(ch not in "0123456789abcdefABCDEF" for ch in value):
        raise cv.Invalid("Key must contain exactly 32 hexadecimal characters")
    return value

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HuaweiScaleClient),
    cv.Optional("cak", default=""): key_hex,
    cv.Optional("c1", default=""): key_hex,
    cv.Optional("c2", default=""): key_hex,
    cv.Optional("gender", default=1): cv.int_range(min=0,max=1),
    cv.Optional("age", default=30): cv.int_range(min=0,max=255),
    cv.Optional("height_cm", default=170): cv.int_range(min=1,max=65535),
    cv.Required("weight_sensor"): cv.use_id(sensor.Sensor),
    cv.Required("impedance_sensor"): cv.use_id(sensor.Sensor),
    cv.Required("heart_rate_sensor"): cv.use_id(sensor.Sensor),
    cv.Required("status_sensor"): cv.use_id(text_sensor.TextSensor),
    cv.Optional("measurement_sensor"): cv.use_id(text_sensor.TextSensor),
}).extend(cv.COMPONENT_SCHEMA).extend(ble_client.BLE_CLIENT_SCHEMA)

async def to_code(config):
    cg.add_define("USE_ESP32_BLE_DEVICE")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_keys(config["cak"], config["c1"], config["c2"]))
    cg.add(var.set_user_profile(config["gender"], config["age"], config["height_cm"]))
    for field in ("weight_sensor", "impedance_sensor", "heart_rate_sensor", "status_sensor"):
        cg.add(getattr(var, "set_" + field)(await cg.get_variable(config[field])))
    if "measurement_sensor" in config:
        cg.add(var.set_measurement_sensor(await cg.get_variable(config["measurement_sensor"])))
