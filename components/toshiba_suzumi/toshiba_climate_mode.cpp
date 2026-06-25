#include "toshiba_climate_mode.h"
#include "esphome/components/climate/climate.h"
#include "esphome/core/log.h"
#include "toshiba_climate.h"

namespace esphome {
namespace toshiba_suzumi {

const MODE ClimateModeToInt(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      return MODE::HEAT_COOL;
    case climate::CLIMATE_MODE_COOL:
      return MODE::COOL;
    case climate::CLIMATE_MODE_HEAT:
      return MODE::HEAT;
    case climate::CLIMATE_MODE_DRY:
      return MODE::DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return MODE::FAN_ONLY;
    default:
      ESP_LOGE(TAG, "Invalid climate mode.");
      return MODE::HEAT_COOL;
  }
}

const climate::ClimateMode IntToClimateMode(MODE mode) {
  switch (mode) {
    case MODE::HEAT_COOL:
      return climate::CLIMATE_MODE_HEAT_COOL;
    case MODE::COOL:
      return climate::CLIMATE_MODE_COOL;
    case MODE::HEAT:
      return climate::CLIMATE_MODE_HEAT;
    case MODE::DRY:
      return climate::CLIMATE_MODE_DRY;
    case MODE::FAN_ONLY:
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
const optional<FAN> StringToFanLevel(const char* mode) {
  if (mode == CUSTOM_FAN_LEVEL_2) {
    return FAN::FANMODE_2;
  } else if (mode == CUSTOM_FAN_LEVEL_4) {
    return FAN::FANMODE_4;
  } else {
    return nullopt;
  }
}

/**
 * Convert a Toshiba fan mode code to a custom fan mode string
 * @param mode The Toshiba fan mode code to convert
 * @return The custom fan mode string
 */
const char* IntToCustomFanMode(FAN mode) {
  switch (mode) {
    case FAN::FANMODE_2:
      return CUSTOM_FAN_LEVEL_2;
    case FAN::FANMODE_4:
      return CUSTOM_FAN_LEVEL_4;
    default:
      return "Unknown";
  }
}

const optional<PWR_LEVEL> StringToPwrLevel(const std::string &mode) {
  if (str_equals_case_insensitive(mode, CUSTOM_PWR_LEVEL_100)) {
    return PWR_LEVEL::PCT_100;
  } else if (str_equals_case_insensitive(mode, CUSTOM_PWR_LEVEL_75)) {
    return PWR_LEVEL::PCT_75;
  } else if (str_equals_case_insensitive(mode, CUSTOM_PWR_LEVEL_50)) {
    return PWR_LEVEL::PCT_50;
  } else {
    return nullopt;
  }
}

const std::string IntToPowerLevel(PWR_LEVEL mode) {
  switch (mode) {
    case PWR_LEVEL::PCT_100:
      return CUSTOM_PWR_LEVEL_100;
    case PWR_LEVEL::PCT_75:
      return CUSTOM_PWR_LEVEL_75;
    case PWR_LEVEL::PCT_50:
      return CUSTOM_PWR_LEVEL_50;
    default:
      return "Unknown";
  }
}

struct VerticalAirDirection {
  SWING swing;
  const char *name;
};

// Toshiba service manuals call the physical up/down flap a horizontal louver;
// expose the user-facing effect as vertical air direction.
static const VerticalAirDirection VERTICAL_AIR_DIRECTIONS[] = {
    {SWING::OFF, "Off"},
    {SWING::VERTICAL, "Swing"},
    {SWING::VERTICAL_FIX_POSITION_1, "Top"},
    {SWING::VERTICAL_FIX_POSITION_2, "Middle Top"},
    {SWING::VERTICAL_FIX_POSITION_3, "Middle"},
    {SWING::VERTICAL_FIX_POSITION_4, "Middle Bottom"},
    {SWING::VERTICAL_FIX_POSITION_5, "Bottom"},
};

const optional<SWING> StringToVerticalAirDirection(const std::string &position) {
  for (auto const &direction : VERTICAL_AIR_DIRECTIONS) {
    if (str_equals_case_insensitive(position, direction.name)) {
      return direction.swing;
    }
  }
  return nullopt;
}

const char* SwingToVerticalAirDirection(SWING mode) {
  if (mode == SWING::HORIZONTAL) {
    mode = SWING::OFF;
  }
  if (mode == SWING::BOTH) {
    mode = SWING::VERTICAL;
  }
  for (auto const &direction : VERTICAL_AIR_DIRECTIONS) {
    if (mode == direction.swing) {
      return direction.name;
    }
  }
  return nullptr;
}

bool IsFixedVerticalAirDirection(SWING mode) {
  auto value = static_cast<uint8_t>(mode);
  return value >= static_cast<uint8_t>(SWING::VERTICAL_FIX_POSITION_1) &&
         value <= static_cast<uint8_t>(SWING::VERTICAL_FIX_POSITION_5);
}

const SWING ClimateSwingModeToInt(climate::ClimateSwingMode mode) {
  switch (mode) {
    case climate::CLIMATE_SWING_OFF:
      return SWING::OFF;
    case climate::CLIMATE_SWING_BOTH:
      return SWING::BOTH;
    case climate::CLIMATE_SWING_VERTICAL:
      return SWING::VERTICAL;
    case climate::CLIMATE_SWING_HORIZONTAL:
      return SWING::HORIZONTAL;
    default:
      ESP_LOGE(TAG, "Invalid swing mode %d.", mode);
      return SWING::OFF;
  }
}

const climate::ClimateSwingMode IntToClimateSwingMode(SWING mode) {
  switch (mode) {
    case SWING::OFF:
      return climate::CLIMATE_SWING_OFF;
    case SWING::VERTICAL:
      return climate::CLIMATE_SWING_VERTICAL;
    case SWING::HORIZONTAL:
      return climate::CLIMATE_SWING_HORIZONTAL;
    case SWING::BOTH:
      return climate::CLIMATE_SWING_BOTH;
    default:
      ESP_LOGE(TAG, "Invalid swing mode %d.", mode);
      return climate::CLIMATE_SWING_OFF;
  }
}

/**
 * Convert a standard climate fan mode to a Toshiba fan mode code
 * @param mode The climate fan mode to convert
 * @return The Toshiba fan mode
 */
const optional<FAN> ClimateFanModeToInt(climate::ClimateFanMode mode) {
  switch (mode) {
    case climate::CLIMATE_FAN_AUTO:
      return FAN::FAN_AUTO;
    case climate::CLIMATE_FAN_QUIET:
      return FAN::FAN_QUIET;
    case climate::CLIMATE_FAN_LOW:
      return FAN::FAN_LOW;
    case climate::CLIMATE_FAN_MEDIUM:
      return FAN::FAN_MEDIUM;
    case climate::CLIMATE_FAN_HIGH:
      return FAN::FAN_HIGH;
    default:
      return nullopt;
  }
}

const LogString *climate_state_to_string(STATE mode) {
  switch (mode) {
    case STATE::ON:
      return LOG_STR("ON");
    case STATE::OFF:
      return LOG_STR("OFF");
    default:
      return LOG_STR("UNKNOWN");
  }
}

const optional<SPECIAL_MODE> PresetToSpecialMode(const char* preset) {
  if (str_equals_case_insensitive(preset, SPECIAL_MODE_STANDARD)) {
    return SPECIAL_MODE::STANDARD;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_HI_POWER)) {
    return SPECIAL_MODE::HI_POWER;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_ECO)) {
    return SPECIAL_MODE::ECO;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_FIREPLACE_1)) {
    return SPECIAL_MODE::FIREPLACE_1;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_FIREPLACE_2)) {
    return SPECIAL_MODE::FIREPLACE_2;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_EIGHT_DEG)) {
    return SPECIAL_MODE::EIGHT_DEG;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SILENT_1)) {
    return SPECIAL_MODE::SILENT_1;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SILENT_2)) {
    return SPECIAL_MODE::SILENT_2;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_SLEEP)) {
    return SPECIAL_MODE::SLEEP;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_FLOOR)) {
    return SPECIAL_MODE::FLOOR;
  } else if (str_equals_case_insensitive(preset, SPECIAL_MODE_COMFORT)) {
    return SPECIAL_MODE::COMFORT;
  } else {
    return nullopt;
  }
}

const char* SpecialModeToPreset(SPECIAL_MODE mode) {
  switch (mode) {
    case SPECIAL_MODE::STANDARD:
      return SPECIAL_MODE_STANDARD;
    case SPECIAL_MODE::HI_POWER:
      return SPECIAL_MODE_HI_POWER;
    case SPECIAL_MODE::ECO:
      return SPECIAL_MODE_ECO;
    case SPECIAL_MODE::FIREPLACE_1:
      return SPECIAL_MODE_FIREPLACE_1;
    case SPECIAL_MODE::FIREPLACE_2:
      return SPECIAL_MODE_FIREPLACE_2;
    case SPECIAL_MODE::EIGHT_DEG:
      return SPECIAL_MODE_EIGHT_DEG;
    case SPECIAL_MODE::SILENT_1:
      return SPECIAL_MODE_SILENT_1;
    case SPECIAL_MODE::SILENT_2:
      return SPECIAL_MODE_SILENT_2;
    case SPECIAL_MODE::SLEEP:
      return SPECIAL_MODE_SLEEP;
    case SPECIAL_MODE::FLOOR:
      return SPECIAL_MODE_FLOOR;
    case SPECIAL_MODE::COMFORT:
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
const optional<climate::ClimatePreset> StringToClimatePreset(const char *preset) {
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

const char* ClimatePresetToString(climate::ClimatePreset preset) {
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

const optional<SPECIAL_MODE> ClimatePresetToSpecialMode(climate::ClimatePreset preset) {
  auto preset_string = ClimatePresetToString(preset);
  return PresetToSpecialMode(preset_string);
}

const optional<climate::ClimatePreset> SpecialModeToClimatePreset(SPECIAL_MODE mode) {
  switch (mode) {
    case SPECIAL_MODE::STANDARD:
      return climate::CLIMATE_PRESET_NONE;
    case SPECIAL_MODE::ECO:
      return climate::CLIMATE_PRESET_ECO;
    case SPECIAL_MODE::HI_POWER:
      return climate::CLIMATE_PRESET_BOOST;
    case SPECIAL_MODE::SLEEP:
      return climate::CLIMATE_PRESET_SLEEP;
    case SPECIAL_MODE::COMFORT:
      return climate::CLIMATE_PRESET_COMFORT;
    default:
      // For modes that don't have standard equivalents, return none
      return nullopt;
  }
}

}  // namespace toshiba_suzumi
}  // namespace esphome
