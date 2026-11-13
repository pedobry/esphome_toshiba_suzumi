import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, climate, uart, select, switch, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_AMPERE,
    UNIT_WATT_HOURS,
    UNIT_WATT,
    UNIT_MINUTE,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    CONF_TIME_ID,
    STATE_CLASS_TOTAL_INCREASING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
import logging

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "select", "switch", "text_sensor"]

CONF_ROOM_TEMP = "room_temp"
CONF_INDOOR_TEMP = "indoor_temp"
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
CONF_VERTICAL_AIR_DIRECTION = "vertical_air_direction"
CONF_SPECIAL_MODE = "special_mode" # deprecated - replaced by CONF_SUPPORTED_PRESETS
CONF_SPECIAL_MODE_MODES = "modes" # deprecated - replaced by CONF_SUPPORTED_PRESETS
CONF_SUPPORTED_PRESETS = "supported_presets"
CONF_SELF_CLEAN = "self_clean"
CONF_OFF_TIMER = "off_timer"
CONF_OFF_TIMER_REMAINING = "off_timer_remaining"
CONF_TIME_SYNC_INTERVAL = "time_sync_interval"
CONF_ENERGY = "energy"
CONF_POWER = "power"
CONF_FCU_MODEL = "fcu_model"
CONF_FCU_SERIAL = "fcu_serial"
CONF_FCU_FIRMWARE = "fcu_firmware"
CONF_CDU_MODEL = "cdu_model"
CONF_CDU_SERIAL = "cdu_serial"
CONF_CDU_FIRMWARE = "cdu_firmware"
CONF_ERROR_CODE = "error_code"
CONF_IONIZER = "ionizer"
CONF_AUTO_DETECT = "auto_detect"

PRESETS = ("Standard", "Hi POWER", "ECO", "Fireplace 1", "Fireplace 2", "8 degrees", "Silent#1", "Silent#2", "Sleep",
           "Floor", "Comfort")

FEATURE_HORIZONTAL_SWING = "horizontal_swing"
MIN_TEMP = "min_temp"
DISABLE_HEAT_MODE = "disable_heat_mode"
DISABLE_WIFI_LED = "disable_wifi_led"

toshiba_ns = cg.esphome_ns.namespace("toshiba_suzumi")
ToshibaClimateUart = toshiba_ns.class_("ToshibaClimateUart", cg.PollingComponent, climate.Climate, uart.UARTDevice)
ToshibaPwrModeSelect = toshiba_ns.class_('ToshibaPwrModeSelect', select.Select)
ToshibaSpecialModeSelect = toshiba_ns.class_('ToshibaSpecialModeSelect', select.Select)
ToshibaVerticalAirDirectionSelect = toshiba_ns.class_('ToshibaVerticalAirDirectionSelect', select.Select)
ToshibaIonizerSwitch = toshiba_ns.class_('ToshibaIonizerSwitch', switch.Switch)

# Unit features (must match UnitFeature in toshiba_climate.h).
FEATURE_OUTDOOR_TEMP = 1 << 0
FEATURE_POWER_SEL = 1 << 1
FEATURE_SELF_CLEAN = 1 << 2
FEATURE_OFF_TIMER = 1 << 3
FEATURE_ERROR_CODE = 1 << 4
FEATURE_FCU_IDENTITY = 1 << 5
FEATURE_MODEL_INFO = 1 << 6
FEATURE_IDU_DIAG = 1 << 7
FEATURE_ODU_DIAG = 1 << 8
FEATURE_ENERGY = 1 << 9
FEATURE_LOUVER_FIXED = 1 << 10
FEATURE_PURE = 1 << 11

TEMPERATURE_SENSOR = dict(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

ENTITY_SCHEMAS = {
    CONF_INDOOR_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR),
    CONF_OUTDOOR_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR),
    CONF_CDU_TD_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_CDU_TS_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_CDU_TE_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_CDU_LOAD: sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_CDU_IAC: sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_FCU_TC_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_FCU_TCJ_TEMP: sensor.sensor_schema(**TEMPERATURE_SENSOR, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_FCU_FAN_RPM: sensor.sensor_schema(
        unit_of_measurement="RPM",
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_PWR_SELECT: select.select_schema(ToshibaPwrModeSelect).extend({
        cv.GenerateID(): cv.declare_id(ToshibaPwrModeSelect),
    }),
    CONF_VERTICAL_AIR_DIRECTION: select.select_schema(ToshibaVerticalAirDirectionSelect).extend({
        cv.GenerateID(): cv.declare_id(ToshibaVerticalAirDirectionSelect),
    }),
    CONF_SELF_CLEAN: binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    CONF_OFF_TIMER: binary_sensor.binary_sensor_schema(icon="mdi:timer-off-outline"),
    CONF_OFF_TIMER_REMAINING: sensor.sensor_schema(
        unit_of_measurement=UNIT_MINUTE,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_DURATION,
        icon="mdi:timer-sand",
    ),
    # Indoor unit model / serial from the handshake identity reply.
    CONF_FCU_MODEL: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_FCU_SERIAL: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    # Firmware of the indoor unit and model / serial / firmware of the
    # outdoor unit, from the unsolicited model-info frame (EPC 0xE0).
    CONF_FCU_FIRMWARE: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_CDU_MODEL: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_CDU_SERIAL: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    CONF_CDU_FIRMWARE: text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    # Fault code: EPC 0x88 (fault present) + 0x86 (detail), "none" when OK.
    CONF_ERROR_CODE: text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon="mdi:alert-circle-outline",
    ),
    CONF_ENERGY: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT_HOURS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    CONF_POWER: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    # Ionizer / air purifier, EPC 0xC7.
    CONF_IONIZER: switch.switch_schema(ToshibaIonizerSwitch, icon="mdi:air-purifier"),
}

# Entities created automatically when `auto_detect` is on and the key is not in
# YAML: key -> (default name, feature the unit must have to show it, needs time).
# indoor_temp is left out: the climate entity already shows the room temperature.
AUTO_ENTITIES = {
    CONF_OUTDOOR_TEMP: ("Outdoor Temperature", FEATURE_OUTDOOR_TEMP, False),
    CONF_PWR_SELECT: ("Power Level", FEATURE_POWER_SEL, False),
    CONF_VERTICAL_AIR_DIRECTION: ("Vertical Air Direction", FEATURE_LOUVER_FIXED, False),
    CONF_IONIZER: ("Ionizer", FEATURE_PURE, False),
    CONF_SELF_CLEAN: ("Self Clean", FEATURE_SELF_CLEAN, False),
    CONF_OFF_TIMER: ("Off Timer", FEATURE_OFF_TIMER, False),
    CONF_OFF_TIMER_REMAINING: ("Off Timer Remaining", FEATURE_OFF_TIMER, False),
    CONF_ERROR_CODE: ("Error Code", FEATURE_ERROR_CODE, False),
    CONF_FCU_MODEL: ("Indoor Unit Model", FEATURE_FCU_IDENTITY, False),
    CONF_FCU_SERIAL: ("Indoor Unit Serial", FEATURE_FCU_IDENTITY, False),
    CONF_FCU_FIRMWARE: ("Indoor Unit Firmware", FEATURE_MODEL_INFO, False),
    CONF_CDU_MODEL: ("Outdoor Unit Model", FEATURE_MODEL_INFO, False),
    CONF_CDU_SERIAL: ("Outdoor Unit Serial", FEATURE_MODEL_INFO, False),
    CONF_CDU_FIRMWARE: ("Outdoor Unit Firmware", FEATURE_MODEL_INFO, False),
    CONF_FCU_TC_TEMP: ("Indoor Coil Temperature", FEATURE_IDU_DIAG, False),
    CONF_FCU_TCJ_TEMP: ("Indoor Coil Junction Temperature", FEATURE_IDU_DIAG, False),
    CONF_FCU_FAN_RPM: ("Indoor Fan Speed", FEATURE_IDU_DIAG, False),
    CONF_CDU_TD_TEMP: ("Compressor Discharge Temperature", FEATURE_ODU_DIAG, False),
    CONF_CDU_TS_TEMP: ("Compressor Suction Temperature", FEATURE_ODU_DIAG, False),
    CONF_CDU_TE_TEMP: ("Outdoor Coil Temperature", FEATURE_ODU_DIAG, False),
    CONF_CDU_LOAD: ("Compressor Load", FEATURE_ODU_DIAG, False),
    CONF_CDU_IAC: ("Compressor Current", FEATURE_ODU_DIAG, False),
    # Energy needs the unit clock, which is set only with a time source.
    CONF_ENERGY: ("Energy", FEATURE_ENERGY, True),
    CONF_POWER: ("Power", FEATURE_ENERGY, True),
}

# Keys added by _add_auto_entities (not set by the user).
CONF_AUTO_KEYS = "_toshiba_auto_entities"

BASE_SCHEMA = climate.climate_schema(ToshibaClimateUart).extend(
    {
        cv.GenerateID(): cv.declare_id(ToshibaClimateUart),
        **{cv.Optional(key): schema for key, schema in ENTITY_SCHEMAS.items()},
        # Detect what the unit supports and create the matching entities.
        # Settings and entities listed in YAML always win.
        cv.Optional(CONF_AUTO_DETECT, default=True): cv.boolean,
        cv.Optional(FEATURE_HORIZONTAL_SWING): cv.boolean,
        cv.Optional(DISABLE_WIFI_LED): cv.boolean,
        cv.Optional(DISABLE_HEAT_MODE): cv.boolean,
        # CONF_SPECIAL_MODE is deprecated - replaced by CONF_SUPPORTED_PRESETS
        # Keep it for backward compatibility
        cv.Optional(CONF_SPECIAL_MODE): select.select_schema(ToshibaSpecialModeSelect).extend({
            cv.GenerateID(): cv.declare_id(ToshibaSpecialModeSelect),
            cv.Required(CONF_SPECIAL_MODE_MODES): cv.ensure_list(cv.one_of(*PRESETS))
        }),
        cv.Optional(CONF_SUPPORTED_PRESETS): cv.ensure_list(cv.one_of(*PRESETS)),
        cv.Optional(MIN_TEMP): cv.int_,
        cv.Optional(CONF_TIME_ID): cv.use_id(cg.esphome_ns.namespace("time").class_("RealTimeClock")),
        cv.Optional(CONF_TIME_SYNC_INTERVAL, default="24h"): cv.positive_time_period_milliseconds,
    }
).extend(uart.UART_DEVICE_SCHEMA).extend(cv.polling_component_schema("120s"))


def _add_auto_entities(config):
    auto_keys = []
    if config[CONF_AUTO_DETECT]:
        for key, (name, _feature, needs_time) in AUTO_ENTITIES.items():
            if key in config or (needs_time and CONF_TIME_ID not in config):
                continue
            config[key] = ENTITY_SCHEMAS[key]({CONF_NAME: name})
            auto_keys.append(key)
    config[CONF_AUTO_KEYS] = auto_keys
    return config


CONFIG_SCHEMA = cv.All(BASE_SCHEMA, _add_auto_entities)


def _mark_auto(var, config, key, entity):
    if key in config[CONF_AUTO_KEYS]:
        cg.add(var.add_auto_entity(entity, AUTO_ENTITIES[key][1]))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_auto_detect(config[CONF_AUTO_DETECT]))

    if CONF_IONIZER in config:
        sw = await switch.new_switch(config[CONF_IONIZER])
        await cg.register_parented(sw, config[CONF_ID])
        cg.add(var.set_ionizer_switch(sw))
        _mark_auto(var, config, CONF_IONIZER, sw)

    if CONF_INDOOR_TEMP in config:
        conf = config[CONF_INDOOR_TEMP]
        sens = await sensor.new_sensor(conf)
        cg.add(var.set_indoor_temp_sensor(sens))

    if CONF_OUTDOOR_TEMP in config:
        conf = config[CONF_OUTDOOR_TEMP]
        sens = await sensor.new_sensor(conf)
        cg.add(var.set_outdoor_temp_sensor(sens))
        _mark_auto(var, config, CONF_OUTDOOR_TEMP, sens)

    if CONF_CDU_TD_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_CDU_TD_TEMP])
        cg.add(var.set_cdu_td_temp_sensor(sens))
        _mark_auto(var, config, CONF_CDU_TD_TEMP, sens)

    if CONF_CDU_TS_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_CDU_TS_TEMP])
        cg.add(var.set_cdu_ts_temp_sensor(sens))
        _mark_auto(var, config, CONF_CDU_TS_TEMP, sens)

    if CONF_CDU_TE_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_CDU_TE_TEMP])
        cg.add(var.set_cdu_te_temp_sensor(sens))
        _mark_auto(var, config, CONF_CDU_TE_TEMP, sens)

    if CONF_CDU_LOAD in config:
        sens = await sensor.new_sensor(config[CONF_CDU_LOAD])
        cg.add(var.set_cdu_load_sensor(sens))
        _mark_auto(var, config, CONF_CDU_LOAD, sens)

    if CONF_CDU_IAC in config:
        sens = await sensor.new_sensor(config[CONF_CDU_IAC])
        cg.add(var.set_cdu_iac_sensor(sens))
        _mark_auto(var, config, CONF_CDU_IAC, sens)

    if CONF_FCU_TC_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_FCU_TC_TEMP])
        cg.add(var.set_fcu_tc_temp_sensor(sens))
        _mark_auto(var, config, CONF_FCU_TC_TEMP, sens)

    if CONF_FCU_TCJ_TEMP in config:
        sens = await sensor.new_sensor(config[CONF_FCU_TCJ_TEMP])
        cg.add(var.set_fcu_tcj_temp_sensor(sens))
        _mark_auto(var, config, CONF_FCU_TCJ_TEMP, sens)

    if CONF_FCU_FAN_RPM in config:
        sens = await sensor.new_sensor(config[CONF_FCU_FAN_RPM])
        cg.add(var.set_fcu_fan_rpm_sensor(sens))
        _mark_auto(var, config, CONF_FCU_FAN_RPM, sens)

    if CONF_PWR_SELECT in config:
        sel = await select.new_select(config[CONF_PWR_SELECT], options=['50 %', '75 %', '100 %'])
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(var.set_pwr_select(sel))
        _mark_auto(var, config, CONF_PWR_SELECT, sel)

    if CONF_VERTICAL_AIR_DIRECTION in config:
        sel = await select.new_select(config[CONF_VERTICAL_AIR_DIRECTION], options=['Off', 'Swing', 'Top', 'Middle Top', 'Middle', 'Middle Bottom', 'Bottom'])
        await cg.register_parented(sel, config[CONF_ID])
        cg.add(var.set_vertical_air_direction_select(sel))
        _mark_auto(var, config, CONF_VERTICAL_AIR_DIRECTION, sel)
    if CONF_SELF_CLEAN in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_SELF_CLEAN])
        cg.add(var.set_self_clean_sensor(sens))
        _mark_auto(var, config, CONF_SELF_CLEAN, sens)

    if CONF_OFF_TIMER in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_OFF_TIMER])
        cg.add(var.set_off_timer_sensor(sens))
        _mark_auto(var, config, CONF_OFF_TIMER, sens)

    if CONF_OFF_TIMER_REMAINING in config:
        sens = await sensor.new_sensor(config[CONF_OFF_TIMER_REMAINING])
        cg.add(var.set_off_timer_remaining_sensor(sens))
        _mark_auto(var, config, CONF_OFF_TIMER_REMAINING, sens)

    if CONF_FCU_MODEL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_FCU_MODEL])
        cg.add(var.set_fcu_model_text_sensor(sens))
        _mark_auto(var, config, CONF_FCU_MODEL, sens)

    if CONF_FCU_SERIAL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_FCU_SERIAL])
        cg.add(var.set_fcu_serial_text_sensor(sens))
        _mark_auto(var, config, CONF_FCU_SERIAL, sens)

    if CONF_FCU_FIRMWARE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_FCU_FIRMWARE])
        cg.add(var.set_fcu_firmware_text_sensor(sens))
        _mark_auto(var, config, CONF_FCU_FIRMWARE, sens)

    if CONF_CDU_MODEL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CDU_MODEL])
        cg.add(var.set_cdu_model_text_sensor(sens))
        _mark_auto(var, config, CONF_CDU_MODEL, sens)

    if CONF_CDU_SERIAL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CDU_SERIAL])
        cg.add(var.set_cdu_serial_text_sensor(sens))
        _mark_auto(var, config, CONF_CDU_SERIAL, sens)

    if CONF_CDU_FIRMWARE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CDU_FIRMWARE])
        cg.add(var.set_cdu_firmware_text_sensor(sens))
        _mark_auto(var, config, CONF_CDU_FIRMWARE, sens)

    if CONF_ERROR_CODE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ERROR_CODE])
        cg.add(var.set_error_code_text_sensor(sens))
        _mark_auto(var, config, CONF_ERROR_CODE, sens)

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

    if CONF_TIME_ID in config:
        time_ = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(time_))

    if CONF_TIME_SYNC_INTERVAL in config:
        cg.add(var.set_time_sync_interval(config[CONF_TIME_SYNC_INTERVAL]))

    if CONF_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_ENERGY])
        cg.add(var.set_energy_sensor(sens))
        _mark_auto(var, config, CONF_ENERGY, sens)

    if CONF_POWER in config:
        sens = await sensor.new_sensor(config[CONF_POWER])
        cg.add(var.set_power_sensor(sens))
        _mark_auto(var, config, CONF_POWER, sens)
