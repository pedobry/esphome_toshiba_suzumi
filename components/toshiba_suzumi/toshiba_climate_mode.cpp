#include "toshiba_climate_mode.h"
#include "esphome/components/climate/climate.h"
#include "esphome/core/log.h"
#include "toshiba_climate.h"

namespace esphome {
namespace toshiba_suzumi {

const MODE ClimateModeToInt(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_AUTO:
      return MODE::AUTO;
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
      return MODE::AUTO;
  }
}

const climate::ClimateMode IntToClimateMode(MODE mode) {
  switch (mode) {
    case MODE::AUTO:
      return climate::CLIMATE_MODE_AUTO;
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

const optional<FAN> StringToFanLevel(std::string mode) {
  if (mode == CUSTOM_FAN_LEVEL_1) {
    return FAN::FANMODE_1;
  } else if (mode == CUSTOM_FAN_LEVEL_2) {
    return FAN::FANMODE_2;
  } else if (mode == CUSTOM_FAN_LEVEL_3) {
    return FAN::FANMODE_3;
  } else if (mode == CUSTOM_FAN_LEVEL_4) {
    return FAN::FANMODE_4;
  } else if (mode == CUSTOM_FAN_LEVEL_5) {
    return FAN::FANMODE_5;
  } else {
    return nullopt;
  }
}

const std::string IntToCustomFanMode(FAN mode) {
  switch (mode) {
    case FAN::FANMODE_1:
      return CUSTOM_FAN_LEVEL_1;
    case FAN::FANMODE_2:
      return CUSTOM_FAN_LEVEL_2;
    case FAN::FANMODE_3:
      return CUSTOM_FAN_LEVEL_3;
    case FAN::FANMODE_4:
      return CUSTOM_FAN_LEVEL_4;
    case FAN::FANMODE_5:
      return CUSTOM_FAN_LEVEL_5;
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

const SWING ClimateSwingModeToInt(climate::ClimateSwingMode mode) {
  switch (mode) {
    case climate::CLIMATE_SWING_OFF:
      return SWING::OFF;
    case climate::CLIMATE_SWING_VERTICAL:
      return SWING::ON;
    default:
      ESP_LOGE(TAG, "Invalid swing mode.");
      return SWING::OFF;
  }
}

const climate::ClimateSwingMode IntToClimateSwingMode(SWING mode) {
  switch (mode) {
    case SWING::OFF:
      return climate::CLIMATE_SWING_OFF;
    case SWING::ON:
      return climate::CLIMATE_SWING_VERTICAL;
    default:
      ESP_LOGE(TAG, "Invalid swing mode.");
      return climate::CLIMATE_SWING_OFF;
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

}  // namespace toshiba_suzumi
}  // namespace esphome
