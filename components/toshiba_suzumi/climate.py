import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, climate, uart, select
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_AMPERE,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_CURRENT,
    __version__ as ESPHOME_VERSION
)
from packaging import version
import logging

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "select"]

CONF_ROOM_TEMP = "room_temp"
CONF_OUTDOOR_TEMP = "outdoor_temp"
CONF_CDU_TD_TEMP = "cdu_td_temp"
CONF_CDU_TS_TEMP = "cdu_ts_temp"
CONF_CDU_TE_TEMP = "cdu_te_temp"
CONF_CDU_LOAD = "cdu_load"
CONF_CDU_IAC = "cdu_iac"
CONF_FCU_TC_TEMP = "fcu_tc_temp"
CONF_FCU_TCJ_TEMP = "fcu_tcj_temp"
CONF_FCU_FAN_RPM = "fcu_fan_rpm"
CONF_PWR_SELECT = "power_select"
CONF_SPECIAL_MODE = "special_mode" # deprecated - replaced by CONF_SUPPORTED_PRESETS
CONF_SPECIAL_MODE_MODES = "modes" # deprecated - replaced by CONF_SUPPORTED_PRESETS
CONF_SUPPORTED_PRESETS = "supported_presets"

FEATURE_HORIZONTAL_SWING = "horizontal_swing"
MIN_TEMP = "min_temp"
DISABLE_HEAT_MODE = "disable_heat_mode"
DISABLE_WIFI_LED = "disable_wifi_led"

toshiba_ns = cg.esphome_ns.namespace("toshiba_suzumi")
ToshibaClimateUart = toshiba_ns.class_("ToshibaClimateUart", cg.PollingComponent, climate.Climate, uart.UARTDevice)
ToshibaPwrModeSelect = toshiba_ns.class_('ToshibaPwrModeSelect', select.Select)
ToshibaSpecialModeSelect = toshiba_ns.class_('ToshibaSpecialModeSelect', select.Select)

CONFIG_SCHEMA = climate.climate_schema(ToshibaClimateUart).extend(
    {
        cv.GenerateID(): cv.declare_id(ToshibaClimateUart),
        cv.Optional(CONF_OUTDOOR_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_CDU_TD_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_CDU_TS_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_CDU_TE_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_CDU_LOAD): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_CDU_IAC): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_FCU_TC_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_FCU_TCJ_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_FCU_FAN_RPM): sensor.sensor_schema(
                unit_of_measurement="RPM",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        cv.Optional(CONF_PWR_SELECT): select.select_schema(ToshibaPwrModeSelect).extend({
            cv.GenerateID(): cv.declare_id(ToshibaPwrModeSelect),
        }),
        cv.Optional(FEATURE_HORIZONTAL_SWING): cv.boolean,
        cv.Optional(DISABLE_WIFI_LED): cv.boolean,
        cv.Optional(DISABLE_HEAT_MODE): cv.boolean,
        # CONF_SPECIAL_MODE is deprecated - replaced by CONF_SUPPORTED_PRESETS
        # Keep it for backward compatibility
        cv.Optional(CONF_SPECIAL_MODE): select.select_schema(ToshibaSpecialModeSelect).extend({
            cv.GenerateID(): cv.declare_id(ToshibaSpecialModeSelect),
            cv.Required(CONF_SPECIAL_MODE_MODES): cv.ensure_list(cv.one_of("Standard","Hi POWER","ECO","Fireplace 1","Fireplace 2","8 degrees","Silent#1","Silent#2","Sleep","Floor","Comfort"))
        }),
        cv.Optional(CONF_SUPPORTED_PRESETS): cv.ensure_list(cv.one_of("Standard","Hi POWER","ECO","Fireplace 1","Fireplace 2","8 degrees","Silent#1","Silent#2","Sleep","Floor","Comfort")),
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

    if CONF_CDU_TD_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_CDU_TD_TEMP])
        cg.add(var.set_cdu_td_temp_sensor(sens))

    if CONF_CDU_TS_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_CDU_TS_TEMP])
        cg.add(var.set_cdu_ts_temp_sensor(sens))

    if CONF_CDU_TE_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_CDU_TE_TEMP])
        cg.add(var.set_cdu_te_temp_sensor(sens))

    if CONF_CDU_LOAD in config:
        sens = await sensor.new_sensor(config[CONF_CDU_LOAD])
        cg.add(var.set_cdu_load_sensor(sens))

    if CONF_CDU_IAC in config:
        sens = await sensor.new_sensor(config[CONF_CDU_IAC])
        cg.add(var.set_cdu_iac_sensor(sens))

    if CONF_FCU_TC_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_FCU_TC_TEMP])
        cg.add(var.set_fcu_tc_temp_sensor(sens))

    if CONF_FCU_TCJ_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_FCU_TCJ_TEMP])
        cg.add(var.set_fcu_tcj_temp_sensor(sens))

    if CONF_FCU_FAN_RPM in config:
        sens = await sensor.new_sensor(config[CONF_FCU_FAN_RPM])
        cg.add(var.set_fcu_fan_rpm_sensor(sens))

    if CONF_PWR_SELECT in config:
        sel = await select.new_select(config[CONF_PWR_SELECT], options=['50 %', '75 %', '100 %'])
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(var.set_pwr_select(sel))

    if FEATURE_HORIZONTAL_SWING in config:
        cg.add(var.set_horizontal_swing(True))

    if MIN_TEMP in config:
        cg.add(var.set_min_temp(config[MIN_TEMP]))

    if DISABLE_HEAT_MODE in config:
        cg.add(var.disable_heat_mode(True))

    if DISABLE_WIFI_LED in config:
        cg.add(var.disable_wifi_led(True))

    if CONF_SUPPORTED_PRESETS in config:
        presets = config[CONF_SUPPORTED_PRESETS]
        cg.add(var.set_supported_presets(presets))
        if "8 degrees" in presets:
            # if "8 degrees" feature is in the list, set the min visual temperature to 5
            cg.add(var.set_min_temp(5))

    # CONF_SPECIAL_MODE is deprecated - replaced by CONF_SUPPORTED_PRESETS
    # Keep it for backward compatibility
    if CONF_SPECIAL_MODE in config:
        presets = config[CONF_SPECIAL_MODE][CONF_SPECIAL_MODE_MODES]
        cg.add(var.set_supported_presets(presets))
        if "8 degrees" in presets:
            # if "8 degrees" feature is in the list, set the min visual temperature to 5
            cg.add(var.set_min_temp(5))
