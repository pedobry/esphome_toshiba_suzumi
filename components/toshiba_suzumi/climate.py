import logging

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import binary_sensor, climate, select, sensor, switch, text_sensor, uart
from esphome.const import (
    CONF_ENERGY,
    CONF_ID,
    CONF_NAME,
    CONF_POWER,
    CONF_SUPPORTED_PRESETS,
    CONF_TIME_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_MINUTE,
    UNIT_PERCENT,
    UNIT_REVOLUTIONS_PER_MINUTE,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)
from esphome.core import CORE

from . import DOMAIN

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "select", "switch", "text_sensor"]

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
CONF_SPECIAL_MODE = "special_mode"  # deprecated - replaced by CONF_SUPPORTED_PRESETS
CONF_SPECIAL_MODE_MODES = "modes"  # deprecated - replaced by CONF_SUPPORTED_PRESETS
CONF_SELF_CLEAN = "self_clean"
CONF_OFF_TIMER = "off_timer"
CONF_OFF_TIMER_REMAINING = "off_timer_remaining"
CONF_TIME_SYNC_INTERVAL = "time_sync_interval"
CONF_FCU_MODEL = "fcu_model"
CONF_FCU_SERIAL = "fcu_serial"
CONF_FCU_FIRMWARE = "fcu_firmware"
CONF_CDU_MODEL = "cdu_model"
CONF_CDU_SERIAL = "cdu_serial"
CONF_CDU_FIRMWARE = "cdu_firmware"
CONF_ERROR_CODE = "error_code"
CONF_IONIZER = "ionizer"
CONF_AUTO_DETECT = "auto_detect"

# Preset names, the same strings as SPECIAL_MODE_* in toshiba_climate_mode.h.
PRESET_EIGHT_DEG = "8 degrees"
PRESETS = (
    "Standard",
    "Hi POWER",
    "ECO",
    "Fireplace 1",
    "Fireplace 2",
    PRESET_EIGHT_DEG,
    "Silent#1",
    "Silent#2",
    "Sleep",
    "Floor",
    "Comfort",
)

CONF_HORIZONTAL_SWING = "horizontal_swing"
CONF_MIN_TEMP = "min_temp"
CONF_DISABLE_HEAT_MODE = "disable_heat_mode"
CONF_DISABLE_WIFI_LED = "disable_wifi_led"

# Select options. The C++ side maps the option index, so keep the order of
# POWER_LEVELS and VERTICAL_AIR_DIRECTIONS in toshiba_climate_mode.cpp.
PWR_SELECT_OPTIONS = ["50 %", "75 %", "100 %"]
VERTICAL_AIR_DIRECTION_OPTIONS = ["Off", "Swing", "Top", "Middle Top", "Middle", "Middle Bottom", "Bottom"]

toshiba_ns = cg.esphome_ns.namespace("toshiba_suzumi")
ToshibaClimateUart = toshiba_ns.class_("ToshibaClimateUart", cg.PollingComponent, climate.Climate, uart.UARTDevice)
ToshibaPwrModeSelect = toshiba_ns.class_("ToshibaPwrModeSelect", select.Select)
ToshibaVerticalAirDirectionSelect = toshiba_ns.class_("ToshibaVerticalAirDirectionSelect", select.Select)
ToshibaIonizerSwitch = toshiba_ns.class_("ToshibaIonizerSwitch", switch.Switch)

# Unit features, the UnitFeature enum in toshiba_climate.h.
UnitFeature = toshiba_ns.enum("UnitFeature")

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
        unit_of_measurement=UNIT_REVOLUTIONS_PER_MINUTE,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_PWR_SELECT: select.select_schema(ToshibaPwrModeSelect),
    CONF_VERTICAL_AIR_DIRECTION: select.select_schema(ToshibaVerticalAirDirectionSelect),
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
    CONF_OUTDOOR_TEMP: ("Outdoor Temperature", UnitFeature.FEATURE_OUTDOOR_TEMP, False),
    CONF_PWR_SELECT: ("Power Level", UnitFeature.FEATURE_POWER_SEL, False),
    CONF_VERTICAL_AIR_DIRECTION: ("Vertical Air Direction", UnitFeature.FEATURE_LOUVER_FIXED, False),
    CONF_IONIZER: ("Ionizer", UnitFeature.FEATURE_PURE, False),
    CONF_SELF_CLEAN: ("Self Clean", UnitFeature.FEATURE_SELF_CLEAN, False),
    CONF_OFF_TIMER: ("Off Timer", UnitFeature.FEATURE_OFF_TIMER, False),
    CONF_OFF_TIMER_REMAINING: ("Off Timer Remaining", UnitFeature.FEATURE_OFF_TIMER, False),
    CONF_ERROR_CODE: ("Error Code", UnitFeature.FEATURE_ERROR_CODE, False),
    CONF_FCU_MODEL: ("Indoor Unit Model", UnitFeature.FEATURE_FCU_IDENTITY, False),
    CONF_FCU_SERIAL: ("Indoor Unit Serial", UnitFeature.FEATURE_FCU_IDENTITY, False),
    CONF_FCU_FIRMWARE: ("Indoor Unit Firmware", UnitFeature.FEATURE_MODEL_INFO, False),
    CONF_CDU_MODEL: ("Outdoor Unit Model", UnitFeature.FEATURE_MODEL_INFO, False),
    CONF_CDU_SERIAL: ("Outdoor Unit Serial", UnitFeature.FEATURE_MODEL_INFO, False),
    CONF_CDU_FIRMWARE: ("Outdoor Unit Firmware", UnitFeature.FEATURE_MODEL_INFO, False),
    CONF_FCU_TC_TEMP: ("Indoor Coil Temperature", UnitFeature.FEATURE_IDU_DIAG, False),
    CONF_FCU_TCJ_TEMP: ("Indoor Coil Junction Temperature", UnitFeature.FEATURE_IDU_DIAG, False),
    CONF_FCU_FAN_RPM: ("Indoor Fan Speed", UnitFeature.FEATURE_IDU_DIAG, False),
    CONF_CDU_TD_TEMP: ("Compressor Discharge Temperature", UnitFeature.FEATURE_ODU_DIAG, False),
    CONF_CDU_TS_TEMP: ("Compressor Suction Temperature", UnitFeature.FEATURE_ODU_DIAG, False),
    CONF_CDU_TE_TEMP: ("Outdoor Coil Temperature", UnitFeature.FEATURE_ODU_DIAG, False),
    CONF_CDU_LOAD: ("Compressor Load", UnitFeature.FEATURE_ODU_DIAG, False),
    CONF_CDU_IAC: ("Compressor Current", UnitFeature.FEATURE_ODU_DIAG, False),
    # Energy needs the unit clock, which is set only with a time source.
    CONF_ENERGY: ("Energy", UnitFeature.FEATURE_ENERGY, True),
    CONF_POWER: ("Power", UnitFeature.FEATURE_ENERGY, True),
}

# Keys added by _add_auto_entities (not set by the user).
CONF_AUTO_KEYS = "_toshiba_auto_entities"


def _warn_special_mode_deprecated(value):
    _LOGGER.warning(
        "%s: 'special_mode' is deprecated and will be removed. Use 'supported_presets' with the same list "
        "instead of 'special_mode: modes:'.",
        DOMAIN,
    )
    return value


BASE_SCHEMA = (
    climate.climate_schema(ToshibaClimateUart)
    .extend(
        {
            **{cv.Optional(key): schema for key, schema in ENTITY_SCHEMAS.items()},
            # Detect what the unit supports and create the matching entities.
            # Settings and entities listed in YAML always win.
            cv.Optional(CONF_AUTO_DETECT, default=True): cv.boolean,
            cv.Optional(CONF_HORIZONTAL_SWING): cv.boolean,
            cv.Optional(CONF_DISABLE_WIFI_LED): cv.boolean,
            cv.Optional(CONF_DISABLE_HEAT_MODE): cv.boolean,
            # Deprecated since 2025.09, replaced by supported_presets. Only
            # `modes` is used; the old select entity keys are accepted and ignored.
            cv.Optional(CONF_SPECIAL_MODE): cv.All(
                _warn_special_mode_deprecated,
                cv.Schema(
                    {cv.Required(CONF_SPECIAL_MODE_MODES): cv.ensure_list(cv.one_of(*PRESETS))},
                    extra=cv.ALLOW_EXTRA,
                ),
            ),
            cv.Optional(CONF_SUPPORTED_PRESETS): cv.ensure_list(cv.one_of(*PRESETS)),
            cv.Optional(CONF_MIN_TEMP): cv.int_range(min=5, max=30),
            cv.Optional(CONF_TIME_ID): cv.use_id(cg.esphome_ns.namespace("time").class_("RealTimeClock")),
            cv.Optional(CONF_TIME_SYNC_INTERVAL, default="24h"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.polling_component_schema("120s"))
)


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


# Older ESPHome versions: use the 2026.10 tag of this repository (see README).
CONFIG_SCHEMA = cv.All(cv.require_esphome_version(2026, 11, 0), BASE_SCHEMA, _add_auto_entities)


def _caps_lost_on_power_cut(full_config, config):
    """ESP8266 without restore_from_flash keeps preferences in RTC memory only."""
    if not config[CONF_AUTO_DETECT] or "esp8266" not in full_config:
        return False
    return not full_config["esp8266"].get("restore_from_flash", False)


def _final_validate(config):
    if _caps_lost_on_power_cut(fv.full_config.get(), config):
        _LOGGER.warning(
            "%s: the detected unit features are lost on a power cut, and the "
            "device restarts once to detect them again. Add 'restore_from_flash: true' "
            "under 'esp8266:' to keep them.",
            DOMAIN,
        )
    return config


# The unit talks 9600 baud 8E1; catch a wrong uart: block at compile time.
FINAL_VALIDATE_SCHEMA = cv.All(
    uart.final_validate_device_schema(DOMAIN, baud_rate=9600, data_bits=8, parity="EVEN", stop_bits=1),
    _final_validate,
)


def _mark_auto(var, config, key, entity):
    if key in config[CONF_AUTO_KEYS]:
        cg.add(var.add_auto_entity(entity, AUTO_ENTITIES[key][1]))


# Entity setters: key -> (setter on the climate component).
SENSORS = {
    CONF_INDOOR_TEMP: "set_indoor_temp_sensor",
    CONF_OUTDOOR_TEMP: "set_outdoor_temp_sensor",
    CONF_CDU_TD_TEMP: "set_cdu_td_temp_sensor",
    CONF_CDU_TS_TEMP: "set_cdu_ts_temp_sensor",
    CONF_CDU_TE_TEMP: "set_cdu_te_temp_sensor",
    CONF_CDU_LOAD: "set_cdu_load_sensor",
    CONF_CDU_IAC: "set_cdu_iac_sensor",
    CONF_FCU_TC_TEMP: "set_fcu_tc_temp_sensor",
    CONF_FCU_TCJ_TEMP: "set_fcu_tcj_temp_sensor",
    CONF_FCU_FAN_RPM: "set_fcu_fan_rpm_sensor",
    CONF_OFF_TIMER_REMAINING: "set_off_timer_remaining_sensor",
    CONF_ENERGY: "set_energy_sensor",
    CONF_POWER: "set_power_sensor",
}
BINARY_SENSORS = {
    CONF_SELF_CLEAN: "set_self_clean_sensor",
    CONF_OFF_TIMER: "set_off_timer_sensor",
}
TEXT_SENSORS = {
    CONF_FCU_MODEL: "set_fcu_model_text_sensor",
    CONF_FCU_SERIAL: "set_fcu_serial_text_sensor",
    CONF_FCU_FIRMWARE: "set_fcu_firmware_text_sensor",
    CONF_CDU_MODEL: "set_cdu_model_text_sensor",
    CONF_CDU_SERIAL: "set_cdu_serial_text_sensor",
    CONF_CDU_FIRMWARE: "set_cdu_firmware_text_sensor",
    CONF_ERROR_CODE: "set_error_code_text_sensor",
}
SELECTS = {
    CONF_PWR_SELECT: ("set_pwr_select", PWR_SELECT_OPTIONS),
    CONF_VERTICAL_AIR_DIRECTION: ("set_vertical_air_direction_select", VERTICAL_AIR_DIRECTION_OPTIONS),
}


def _set_presets(var, presets):
    cg.add(var.set_supported_presets(presets))
    if PRESET_EIGHT_DEG in presets:
        # 8 °C heating goes down to 5 °C.
        cg.add(var.set_min_temp(5))


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_auto_detect(config[CONF_AUTO_DETECT]))
    if _caps_lost_on_power_cut(CORE.config, config):
        cg.add(var.set_caps_volatile(True))

    if conf := config.get(CONF_IONIZER):
        sw = await switch.new_switch(conf)
        await cg.register_parented(sw, config[CONF_ID])
        cg.add(var.set_ionizer_switch(sw))
        _mark_auto(var, config, CONF_IONIZER, sw)

    for key, setter in SENSORS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(var, setter)(sens))
            _mark_auto(var, config, key, sens)

    for key, setter in BINARY_SENSORS.items():
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(var, setter)(sens))
            _mark_auto(var, config, key, sens)

    for key, setter in TEXT_SENSORS.items():
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(var, setter)(sens))
            _mark_auto(var, config, key, sens)

    for key, (setter, options) in SELECTS.items():
        if conf := config.get(key):
            sel = await select.new_select(conf, options=options)
            await cg.register_parented(sel, config[CONF_ID])
            cg.add(getattr(var, setter)(sel))
            _mark_auto(var, config, key, sel)

    if CONF_HORIZONTAL_SWING in config:
        cg.add(var.set_horizontal_swing(config[CONF_HORIZONTAL_SWING]))

    if (min_temp := config.get(CONF_MIN_TEMP)) is not None:
        cg.add(var.set_min_temp(min_temp))

    if (disable_heat := config.get(CONF_DISABLE_HEAT_MODE)) is not None:
        cg.add(var.disable_heat_mode(disable_heat))

    if (disable_led := config.get(CONF_DISABLE_WIFI_LED)) is not None:
        cg.add(var.disable_wifi_led(disable_led))

    if presets := config.get(CONF_SUPPORTED_PRESETS):
        _set_presets(var, presets)

    # CONF_SPECIAL_MODE is deprecated - replaced by CONF_SUPPORTED_PRESETS
    # Keep it for backward compatibility
    if special_mode := config.get(CONF_SPECIAL_MODE):
        _set_presets(var, special_mode[CONF_SPECIAL_MODE_MODES])

    if time_id := config.get(CONF_TIME_ID):
        time_ = await cg.get_variable(time_id)
        cg.add(var.set_time(time_))

    cg.add(var.set_time_sync_interval(config[CONF_TIME_SYNC_INTERVAL]))
