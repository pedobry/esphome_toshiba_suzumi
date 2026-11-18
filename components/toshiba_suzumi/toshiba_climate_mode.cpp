#include <cstring>
#include "toshiba_climate_mode.h"
#include "esphome/components/climate/climate.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace toshiba_suzumi {

ESPHOME_LOG_TAG(TAG, "toshiba_suzumi");

ToshibaMode to_toshiba_mode(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      return ToshibaMode::HEAT_COOL;
    case climate::CLIMATE_MODE_COOL:
      return ToshibaMode::COOL;
    case climate::CLIMATE_MODE_HEAT:
      return ToshibaMode::HEAT;
    case climate::CLIMATE_MODE_DRY:
      return ToshibaMode::DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return ToshibaMode::FAN_ONLY;
    default:
      ESP_LOGE(TAG, "Invalid climate mode.");
      return ToshibaMode::HEAT_COOL;
  }
}

climate::ClimateMode from_toshiba_mode(ToshibaMode mode) {
  switch (mode) {
    case ToshibaMode::HEAT_COOL:
      return climate::CLIMATE_MODE_HEAT_COOL;
    case ToshibaMode::COOL:
      return climate::CLIMATE_MODE_COOL;
    case ToshibaMode::HEAT:
      return climate::CLIMATE_MODE_HEAT;
    case ToshibaMode::DRY:
      return climate::CLIMATE_MODE_DRY;
    case ToshibaMode::FAN_ONLY:
      return climate::CLIMATE_MODE_FAN_ONLY;
    default:
      ESP_LOGE(TAG, "Invalid climate mode.");
      return climate::CLIMATE_MODE_OFF;
  }
}

/**
 * Convert a custom fan mode string to a Toshiba fan mode code
 * @param mode The custom fan mode string to convert
 * @return The Toshiba fan mode code
 */
const optional<ToshibaFan> custom_fan_mode_to_toshiba(const char *mode) {
  if (mode == nullptr) {
    return nullopt;
  }
  if (strcmp(mode, CUSTOM_FAN_LEVEL_2) == 0) {
    return ToshibaFan::FANMODE_2;
  } else if (strcmp(mode, CUSTOM_FAN_LEVEL_4) == 0) {
    return ToshibaFan::FANMODE_4;
  } else {
    return nullopt;
  }
}

/**
 * Convert a Toshiba fan mode code to a custom fan mode string
 * @param mode The Toshiba fan mode code to convert
 * @return The custom fan mode string
 */
const char *toshiba_fan_to_custom_fan_mode(ToshibaFan mode) {
  switch (mode) {
    case ToshibaFan::FANMODE_2:
      return CUSTOM_FAN_LEVEL_2;
    case ToshibaFan::FANMODE_4:
      return CUSTOM_FAN_LEVEL_4;
    default:
      return "Unknown";
  }
}

// Same order as the power_select options in climate.py.
static const PowerLevel POWER_LEVELS[] = {PowerLevel::PCT_50, PowerLevel::PCT_75, PowerLevel::PCT_100};

optional<PowerLevel> power_level_from_index(size_t index) {
  if (index >= sizeof(POWER_LEVELS) / sizeof(POWER_LEVELS[0]))
    return nullopt;
  return POWER_LEVELS[index];
}

const char *power_level_to_string(PowerLevel mode) {
  switch (mode) {
    case PowerLevel::PCT_100:
      return CUSTOM_PWR_LEVEL_100;
    case PowerLevel::PCT_75:
      return CUSTOM_PWR_LEVEL_75;
    case PowerLevel::PCT_50:
      return CUSTOM_PWR_LEVEL_50;
    default:
      return "Unknown";
  }
}

// All values of EPC 0xA3 a user can pick, as in the Toshiba app. Toshiba
// service manuals call the up/down flap the horizontal louver; the names say
// where the air goes. The order is the option order of the select, and the
// option list in climate.py has the same names in the same order.
const AirDirection AIR_DIRECTIONS[AIR_DIRECTION_COUNT] = {
    {ToshibaSwing::OFF, "Off", AirDirectionGroup::ALWAYS},
    {ToshibaSwing::VERTICAL, "Swing", AirDirectionGroup::ALWAYS},
    {ToshibaSwing::VERTICAL_FIX_POSITION_1, "Top", AirDirectionGroup::VERTICAL_FIXED},
    {ToshibaSwing::VERTICAL_FIX_POSITION_2, "Middle Top", AirDirectionGroup::VERTICAL_FIXED},
    {ToshibaSwing::VERTICAL_FIX_POSITION_3, "Middle", AirDirectionGroup::VERTICAL_FIXED},
    {ToshibaSwing::VERTICAL_FIX_POSITION_4, "Middle Bottom", AirDirectionGroup::VERTICAL_FIXED},
    {ToshibaSwing::VERTICAL_FIX_POSITION_5, "Bottom", AirDirectionGroup::VERTICAL_FIXED},
    {ToshibaSwing::HORIZONTAL_FIX_POSITION_1, "Left", AirDirectionGroup::HORIZONTAL_FIXED},
    {ToshibaSwing::HORIZONTAL_FIX_POSITION_2, "Middle Left", AirDirectionGroup::HORIZONTAL_FIXED},
    {ToshibaSwing::HORIZONTAL_FIX_POSITION_3, "Center", AirDirectionGroup::HORIZONTAL_FIXED},
    {ToshibaSwing::HORIZONTAL_FIX_POSITION_4, "Middle Right", AirDirectionGroup::HORIZONTAL_FIXED},
    {ToshibaSwing::HORIZONTAL_FIX_POSITION_5, "Right", AirDirectionGroup::HORIZONTAL_FIXED},
    {ToshibaSwing::HADA_CARE, "HADA Care Flow", AirDirectionGroup::HADA},
};

ToshibaSwing air_direction_of(ToshibaSwing mode) {
  if (mode == ToshibaSwing::HORIZONTAL)
    return ToshibaSwing::OFF;
  if (mode == ToshibaSwing::BOTH)
    return ToshibaSwing::VERTICAL;
  return mode;
}

const char *air_direction_name(ToshibaSwing mode) {
  mode = air_direction_of(mode);
  for (const auto &direction : AIR_DIRECTIONS) {
    if (mode == direction.swing)
      return direction.name;
  }
  return nullptr;
}

bool is_fixed_air_direction(ToshibaSwing mode) {
  auto value = static_cast<uint8_t>(mode);
  return (value >= static_cast<uint8_t>(ToshibaSwing::VERTICAL_FIX_POSITION_1) &&
          value <= static_cast<uint8_t>(ToshibaSwing::VERTICAL_FIX_POSITION_5)) ||
         (value >= static_cast<uint8_t>(ToshibaSwing::HADA_CARE) &&
          value <= static_cast<uint8_t>(ToshibaSwing::HORIZONTAL_FIX_POSITION_5));
}

ToshibaSwing to_toshiba_swing(climate::ClimateSwingMode mode) {
  switch (mode) {
    case climate::CLIMATE_SWING_OFF:
      return ToshibaSwing::OFF;
    case climate::CLIMATE_SWING_BOTH:
      return ToshibaSwing::BOTH;
    case climate::CLIMATE_SWING_VERTICAL:
      return ToshibaSwing::VERTICAL;
    case climate::CLIMATE_SWING_HORIZONTAL:
      return ToshibaSwing::HORIZONTAL;
    default:
      ESP_LOGE(TAG, "Invalid swing mode %d.", static_cast<int>(mode));
      return ToshibaSwing::OFF;
  }
}

climate::ClimateSwingMode from_toshiba_swing(ToshibaSwing mode) {
  switch (mode) {
    case ToshibaSwing::OFF:
      return climate::CLIMATE_SWING_OFF;
    case ToshibaSwing::VERTICAL:
      return climate::CLIMATE_SWING_VERTICAL;
    case ToshibaSwing::HORIZONTAL:
      return climate::CLIMATE_SWING_HORIZONTAL;
    case ToshibaSwing::BOTH:
      return climate::CLIMATE_SWING_BOTH;
    default:
      ESP_LOGE(TAG, "Invalid swing mode %d.", static_cast<int>(mode));
      return climate::CLIMATE_SWING_OFF;
  }
}

/**
 * Convert a standard climate fan mode to a Toshiba fan mode code
 * @param mode The climate fan mode to convert
 * @return The Toshiba fan mode
 */
const optional<ToshibaFan> to_toshiba_fan(climate::ClimateFanMode mode) {
  switch (mode) {
    case climate::CLIMATE_FAN_AUTO:
      return ToshibaFan::FAN_AUTO;
    case climate::CLIMATE_FAN_QUIET:
      return ToshibaFan::FAN_QUIET;
    case climate::CLIMATE_FAN_LOW:
      return ToshibaFan::FAN_LOW;
    case climate::CLIMATE_FAN_MEDIUM:
      return ToshibaFan::FAN_MEDIUM;
    case climate::CLIMATE_FAN_HIGH:
      return ToshibaFan::FAN_HIGH;
    default:
      return nullopt;
  }
}

const LogString *power_state_to_string(PowerState mode) {
  switch (mode) {
    case PowerState::ON:
      return LOG_STR("ON");
    case PowerState::OFF:
      return LOG_STR("OFF");
    default:
      return LOG_STR("UNKNOWN");
  }
}

const optional<SpecialMode> preset_to_special_mode(const char *preset) {
  if (str_equals_case_insensitive(preset, SPECIAL_MODE_STANDARD)) {
    return SpecialMode::STANDARD;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_HI_POWER)) {
    return SpecialMode::HI_POWER;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_ECO)) {
    return SpecialMode::ECO;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_FIREPLACE_1)) {
    return SpecialMode::FIREPLACE_1;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_FIREPLACE_2)) {
    return SpecialMode::FIREPLACE_2;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_EIGHT_DEG)) {
    return SpecialMode::EIGHT_DEG;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SILENT_1)) {
    return SpecialMode::SILENT_1;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SILENT_2)) {
    return SpecialMode::SILENT_2;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SLEEP)) {
    return SpecialMode::SLEEP;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_FLOOR)) {
    return SpecialMode::FLOOR;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_COMFORT)) {
    return SpecialMode::COMFORT;
  } else {
    return nullopt;
  }
}

const char *special_mode_to_preset(SpecialMode mode) {
  switch (mode) {
    case SpecialMode::STANDARD:
      return SPECIAL_MODE_STANDARD;
    case SpecialMode::HI_POWER:
      return SPECIAL_MODE_HI_POWER;
    case SpecialMode::ECO:
      return SPECIAL_MODE_ECO;
    case SpecialMode::FIREPLACE_1:
      return SPECIAL_MODE_FIREPLACE_1;
    case SpecialMode::FIREPLACE_2:
      return SPECIAL_MODE_FIREPLACE_2;
    case SpecialMode::EIGHT_DEG:
      return SPECIAL_MODE_EIGHT_DEG;
    case SpecialMode::SILENT_1:
      return SPECIAL_MODE_SILENT_1;
    case SpecialMode::SILENT_2:
      return SPECIAL_MODE_SILENT_2;
    case SpecialMode::SLEEP:
      return SPECIAL_MODE_SLEEP;
    case SpecialMode::FLOOR:
      return SPECIAL_MODE_FLOOR;
    case SpecialMode::COMFORT:
      return SPECIAL_MODE_COMFORT;
    default:
      return SPECIAL_MODE_STANDARD;
  }
}

/**
 * Convert a preset string to a climate preset code.
 * Return nullopt if the preset is not supported by the climate component
 * and we need to use a custom preset.
 */
const optional<climate::ClimatePreset> standard_preset_from_name(const char *preset) {
  if (str_equals_case_insensitive(preset, SPECIAL_MODE_STANDARD)) {
    return climate::CLIMATE_PRESET_NONE;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_ECO)) {
    return climate::CLIMATE_PRESET_ECO;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_HI_POWER)) {
    return climate::CLIMATE_PRESET_BOOST;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SLEEP)) {
    return climate::CLIMATE_PRESET_SLEEP;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_COMFORT)) {
    return climate::CLIMATE_PRESET_COMFORT;
  }
  return nullopt;
}

const char *standard_preset_name(climate::ClimatePreset preset) {
  switch (preset) {
    case climate::CLIMATE_PRESET_NONE:
      return SPECIAL_MODE_STANDARD;
    case climate::CLIMATE_PRESET_ECO:
      return SPECIAL_MODE_ECO;
    case climate::CLIMATE_PRESET_BOOST:
      return SPECIAL_MODE_HI_POWER;
    case climate::CLIMATE_PRESET_SLEEP:
      return SPECIAL_MODE_SLEEP;
    case climate::CLIMATE_PRESET_COMFORT:
      return SPECIAL_MODE_COMFORT;
    default:
      return SPECIAL_MODE_STANDARD;
  }
}

const optional<climate::ClimatePreset> special_mode_to_standard_preset(SpecialMode mode) {
  switch (mode) {
    case SpecialMode::STANDARD:
      return climate::CLIMATE_PRESET_NONE;
    case SpecialMode::ECO:
      return climate::CLIMATE_PRESET_ECO;
    case SpecialMode::HI_POWER:
      return climate::CLIMATE_PRESET_BOOST;
    case SpecialMode::SLEEP:
      return climate::CLIMATE_PRESET_SLEEP;
    case SpecialMode::COMFORT:
      return climate::CLIMATE_PRESET_COMFORT;
    default:
      // For modes that don't have standard equivalents, return none
      return nullopt;
  }
}

}  // namespace toshiba_suzumi
}  // namespace esphome
