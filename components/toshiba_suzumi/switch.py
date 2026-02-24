import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, switch, uart
from esphome.const import CONF_ID, CONF_TYPE, ENTITY_CATEGORY_DIAGNOSTIC

CONF_CLIMATE_ID = "climate_id"
CONF_POLL_INTERVAL = "poll_interval"
CONF_BATCH_SIZE = "batch_size"
CONF_INITIAL_FROM_ID = "initial_from_id"
CONF_INITIAL_TO_ID = "initial_to_id"
SWITCH_TYPE_WIFI_LED = "wifi_led"
SWITCH_TYPE_DEBUG = "debug"

toshiba_ns = cg.esphome_ns.namespace("toshiba_suzumi")
ToshibaClimateUart = toshiba_ns.class_("ToshibaClimateUart", cg.PollingComponent, climate.Climate, uart.UARTDevice)
ToshibaWifiLedSwitch = toshiba_ns.class_("ToshibaWifiLedSwitch", switch.Switch, cg.Component)
ToshibaDebugSwitch = toshiba_ns.class_("ToshibaDebugSwitch", switch.Switch, cg.Component)

WIFI_LED_SCHEMA = switch.switch_schema(
    ToshibaWifiLedSwitch,
    default_restore_mode="RESTORE_DEFAULT_OFF",
).extend(
    {
        cv.Required(CONF_CLIMATE_ID): cv.use_id(ToshibaClimateUart),
    }
).extend(cv.COMPONENT_SCHEMA)

DEBUG_SCHEMA = switch.switch_schema(
    ToshibaDebugSwitch,
    default_restore_mode="ALWAYS_OFF",
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.Required(CONF_CLIMATE_ID): cv.use_id(ToshibaClimateUart),
        cv.Optional(CONF_POLL_INTERVAL, default="30s"): cv.positive_time_period_seconds,
        cv.Optional(CONF_BATCH_SIZE, default=1): cv.int_range(min=1, max=255),
        cv.Optional(CONF_INITIAL_FROM_ID, default=128): cv.int_range(min=0, max=255),
        cv.Optional(CONF_INITIAL_TO_ID, default=254): cv.int_range(min=0, max=255),
    }
).extend(cv.COMPONENT_SCHEMA)

def _validate_debug_schema(config):
    if config[CONF_INITIAL_FROM_ID] > config[CONF_INITIAL_TO_ID]:
        raise cv.Invalid(f"{CONF_INITIAL_FROM_ID} must be <= {CONF_INITIAL_TO_ID}")
    return config

CONFIG_SCHEMA = cv.typed_schema(
    {
        SWITCH_TYPE_WIFI_LED: WIFI_LED_SCHEMA,
        SWITCH_TYPE_DEBUG: cv.All(DEBUG_SCHEMA, _validate_debug_schema),
    },
    default_type=SWITCH_TYPE_WIFI_LED,
)


async def to_code(config):
    switch_type = config[CONF_TYPE]
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
    parent = await cg.get_variable(config[CONF_CLIMATE_ID])
    await cg.register_parented(var, config[CONF_CLIMATE_ID])
    if switch_type == SWITCH_TYPE_DEBUG:
        cg.add(parent.set_debug_switch(var))
        cg.add(parent.set_debug_poll_interval_ms(config[CONF_POLL_INTERVAL].total_milliseconds))
        cg.add(parent.set_debug_batch_size(config[CONF_BATCH_SIZE]))
        cg.add(parent.set_debug_initial_range(config[CONF_INITIAL_FROM_ID], config[CONF_INITIAL_TO_ID]))
    else:
        cg.add(parent.set_wifi_led_switch(var))
