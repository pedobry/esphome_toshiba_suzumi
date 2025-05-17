#pragma once

#include <cstdint>
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace toshiba_suzumi {

static const std::string &CUSTOM_FAN_LEVEL_2 = "Low-Medium";
static const std::string &CUSTOM_FAN_LEVEL_4 = "Medium-High";

static const std::string &CUSTOM_PWR_LEVEL_50 = "50 %";
static const std::string &CUSTOM_PWR_LEVEL_75 = "75 %";
static const std::string &CUSTOM_PWR_LEVEL_100 = "100 %";

static const std::string &SPECIAL_MODE_STANDARD = "Standard";
static const std::string &SPECIAL_MODE_HI_POWER = "Hi POWER";
static const std::string &SPECIAL_MODE_ECO = "ECO";
// Special modes described at:
// https://partner.toshiba-klima.at/data/01_RAS/02_Multi/01_R32/01_multi_indoor/02_SHORAI_EDGE_J2KVSG/02_Manuals/OM_RAS_18_B22_B24_J2KVSG_J2AVSG_E_ML.pdf
static const std::string &SPECIAL_MODE_FIREPLACE_1 = "Fireplace 1";
static const std::string &SPECIAL_MODE_FIREPLACE_2 = "Fireplace 2";
static const std::string &SPECIAL_MODE_EIGHT_DEG = "8 degrees";
static const std::string &SPECIAL_MODE_SILENT_1 = "Silent#1";
static const std::string &SPECIAL_MODE_SILENT_2 = "Silent#2";
static const std::string &SPECIAL_MODE_SLEEP = "Sleep";
static const std::string &SPECIAL_MODE_FLOOR = "Floor";
static const std::string &SPECIAL_MODE_COMFORT = "Comfort";

// codes as reverse engineered from Toshiba AC communication with original Wifi module.
enum class MODE { HEAT_COOL = 65, COOL = 66, HEAT = 67, DRY = 68, FAN_ONLY = 69 };
enum class FAN {
  FAN_QUIET = 49,
  FAN_LOW = 50,
  FANMODE_2 = 51,
  FAN_MEDIUM = 52,
  FANMODE_4 = 53,
  FAN_HIGH = 54,
  FAN_AUTO = 65
};
enum class SWING { OFF = 49, BOTH =  67, VERTICAL = 65, HORIZONTAL = 66 };
enum class STATE { ON = 48, OFF = 49 };
enum class PWR_LEVEL { PCT_50 = 50, PCT_75 = 75, PCT_100 = 100 };

enum SPECIAL_MODE {
  STANDARD = 0,
  HI_POWER = 1,
  ECO = 3,
  FIREPLACE_1 = 32,
  FIREPLACE_2 = 48,
  EIGHT_DEG = 4,
  SILENT_1 = 2,
  SILENT_2 = 10,
  SLEEP = 5,
  FLOOR = 6,
  COMFORT = 7
};

enum class ToshibaCommandType : uint8_t {
  HANDSHAKE = 0,  // dummy command to handle all handshake requests
  DELAY = 1, // dummy command to issue a delay in communication
  POWER_STATE = 128,
  POWER_SEL = 135,
  COMFORT_SLEEP = 148, // { ON = 65, OFF = 66 }
  FAN = 160,
  SWING = 163,
  MODE = 176,
  TARGET_TEMP = 179,
  ROOM_TEMP = 187,
  OUTDOOR_TEMP = 190,
  WIFI_LED = 223,
  SPECIAL_MODE = 247,
};

const MODE ClimateModeToInt(climate::ClimateMode mode);
const climate::ClimateMode IntToClimateMode(MODE mode);

const SWING ClimateSwingModeToInt(climate::ClimateSwingMode mode);
const climate::ClimateSwingMode IntToClimateSwingMode(SWING mode);

const LogString *climate_state_to_string(STATE mode);

const optional<FAN> StringToFanLevel(std::string mode);
const ::std::string IntToCustomFanMode(FAN mode);

const optional<PWR_LEVEL> StringToPwrLevel(const std::string &mode);
const std::string IntToPowerLevel(PWR_LEVEL mode);

const optional<SPECIAL_MODE> SpecialModeToInt(const std::string &mode);
const std::string IntToSpecialMode(SPECIAL_MODE mode);

}  // namespace toshiba_suzumi
}  // namespace esphome
