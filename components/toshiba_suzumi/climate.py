import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, climate, uart, select
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    DEVICE_CLASS_TEMPERATURE,
    __version__ as ESPHOME_VERSION
)
from packaging import version
import logging

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "select"]

CONF_ROOM_TEMP = "room_temp"
CONF_OUTDOOR_TEMP = "outdoor_temp"
CONF_PWR_SELECT = "power_select"
CONF_SPECIAL_MODE = "special_mode"
CONF_SPECIAL_MODE_MODES = "modes"

FEATURE_HORIZONTAL_SWING = "horizontal_swing"
MIN_TEMP = "min_temp"
DISABLE_WIFI_LED = "disable_wifi_led"

toshiba_ns = cg.esphome_ns.namespace("toshiba_suzumi")
ToshibaClimateUart = toshiba_ns.class_("ToshibaClimateUart", cg.PollingComponent, climate.Climate, uart.UARTDevice)
ToshibaPwrModeSelect = toshiba_ns.class_('ToshibaPwrModeSelect', select.Select)
ToshibaSpecialModeSelect = toshiba_ns.class_('ToshibaSpecialModeSelect', select.Select)

if version.parse(ESPHOME_VERSION) >= version.parse("2025.5.0"):
    _LOGGER.info("[TOSHIBA SUZUMI] Using new climate schema (ESPHome >= 2025.5.0)")
    CONFIG_SCHEMA = climate.climate_schema(ToshibaClimateUart).extend(
        {
            cv.GenerateID(): cv.declare_id(ToshibaClimateUart),
            cv.Optional(CONF_OUTDOOR_TEMP): sensor.sensor_schema(
                    unit_of_measurement=UNIT_CELSIUS,
                    accuracy_decimals=0,
                    device_class=DEVICE_CLASS_TEMPERATURE,
                    state_class=STATE_CLASS_MEASUREMENT,
                ),
            cv.Optional(CONF_PWR_SELECT): select.select_schema(ToshibaPwrModeSelect).extend({
                cv.GenerateID(): cv.declare_id(ToshibaPwrModeSelect),
            }),
            cv.Optional(FEATURE_HORIZONTAL_SWING): cv.boolean,
            cv.Optional(DISABLE_WIFI_LED): cv.boolean,
            cv.Optional(CONF_SPECIAL_MODE): select.select_schema(ToshibaSpecialModeSelect).extend({
                cv.GenerateID(): cv.declare_id(ToshibaSpecialModeSelect),
                cv.Required(CONF_SPECIAL_MODE_MODES): cv.ensure_list(cv.one_of("Standard","Hi POWER","ECO","Fireplace 1","Fireplace 2","8 degrees","Silent#1","Silent#2","Sleep","Floor","Comfort"))
            }),
            cv.Optional(MIN_TEMP): cv.int_,
        }
    ).extend(uart.UART_DEVICE_SCHEMA).extend(cv.polling_component_schema("120s"))    
else:
    _LOGGER.info("[TOSHIBA SUZUMI] Using legacy climate schema (ESPHome < 2025.5.0)")
    CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(ToshibaClimateUart),
            cv.Optional(CONF_OUTDOOR_TEMP): sensor.sensor_schema(
                    unit_of_measurement=UNIT_CELSIUS,
                    accuracy_decimals=0,
                    device_class=DEVICE_CLASS_TEMPERATURE,
                    state_class=STATE_CLASS_MEASUREMENT,
                ),
            cv.Optional(CONF_PWR_SELECT): select.SELECT_SCHEMA.extend({
                cv.GenerateID(): cv.declare_id(ToshibaPwrModeSelect),
            }),
            cv.Optional(FEATURE_HORIZONTAL_SWING): cv.boolean,
            cv.Optional(DISABLE_WIFI_LED): cv.boolean,
            cv.Optional(CONF_SPECIAL_MODE): select.SELECT_SCHEMA.extend({
                cv.GenerateID(): cv.declare_id(ToshibaSpecialModeSelect),
                cv.Required(CONF_SPECIAL_MODE_MODES): cv.ensure_list(cv.one_of("Standard","Hi POWER","ECO","Fireplace 1","Fireplace 2","8 degrees","Silent#1","Silent#2","Sleep","Floor","Comfort"))
            }),
            cv.Optional(MIN_TEMP): cv.int_,
        }
    ).extend(uart.UART_DEVICE_SCHEMA).extend(cv.polling_component_schema("120s"))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)

    if CONF_OUTDOOR_TEMP in config:
        conf = config[CONF_OUTDOOR_TEMP]
        sens = await sensor.new_sensor(conf)
        cg.add(var.set_outdoor_temp_sensor(sens))

    if CONF_PWR_SELECT in config:
        sel = await select.new_select(config[CONF_PWR_SELECT], options=['50 %', '75 %', '100 %'])
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(var.set_pwr_select(sel))

    if FEATURE_HORIZONTAL_SWING in config:
        cg.add(var.set_horizontal_swing(True))

    if MIN_TEMP in config:
        cg.add(var.set_min_temp(config[MIN_TEMP]))

    if DISABLE_WIFI_LED in config:
        cg.add(var.disable_wifi_led(True))

    if CONF_SPECIAL_MODE in config:
        sel = await select.new_select(config[CONF_SPECIAL_MODE], options=config[CONF_SPECIAL_MODE][CONF_SPECIAL_MODE_MODES])
        if "8 degrees" in config[CONF_SPECIAL_MODE][CONF_SPECIAL_MODE_MODES]:
            # if "8 degrees" feature is in the list, set the min visual temperature to 5
            cg.add(var.set_min_temp(5))
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(var.set_special_mode_select(sel))