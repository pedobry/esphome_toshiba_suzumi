#pragma once

#include <cstdint>
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace toshiba_suzumi {

constexpr const char* CUSTOM_FAN_LEVEL_2 = "Low-Medium";
constexpr const char* CUSTOM_FAN_LEVEL_4 = "Medium-High";

constexpr const char* CUSTOM_PWR_LEVEL_50 = "50 %";
constexpr const char* CUSTOM_PWR_LEVEL_75 = "75 %";
constexpr const char* CUSTOM_PWR_LEVEL_100 = "100 %";

constexpr const char* SPECIAL_MODE_STANDARD = "Standard";
constexpr const char* SPECIAL_MODE_HI_POWER = "Hi POWER";
constexpr const char* SPECIAL_MODE_ECO = "ECO";
// Special modes are described in the owner's manuals of the indoor units
// (e.g. Seiya RAS-xxE2KVG, Haori RAS-Bxx N4KVRG, Shorai Edge RAS-xxJ2KVSG).
constexpr const char* SPECIAL_MODE_FIREPLACE_1 = "Fireplace 1";
constexpr const char* SPECIAL_MODE_FIREPLACE_2 = "Fireplace 2";
constexpr const char* SPECIAL_MODE_EIGHT_DEG = "8 degrees";
constexpr const char* SPECIAL_MODE_SILENT_1 = "Silent#1";
constexpr const char* SPECIAL_MODE_SILENT_2 = "Silent#2";
constexpr const char* SPECIAL_MODE_SLEEP = "Sleep";
constexpr const char* SPECIAL_MODE_FLOOR = "Floor";
constexpr const char* SPECIAL_MODE_COMFORT = "Comfort";

// Value encodings (ECHONET Lite home air conditioner class plus Toshiba
// specifics). See PROTOCOL.md for the EPC map.

// EPC 0xB0 (ECHONET operation mode).
enum class MODE { HEAT_COOL = 65, COOL = 66, HEAT = 67, DRY = 68, FAN_ONLY = 69 };
// EPC 0xA0 (ECHONET air flow rate): Toshiba levels 0x31..0x36, 0x41 auto.
enum class FAN {
  FAN_QUIET = 49,
  FAN_LOW = 50,
  FANMODE_2 = 51,
  FAN_MEDIUM = 52,
  FANMODE_4 = 53,
  FAN_HIGH = 54,
  FAN_AUTO = 65
};
// EPC 0xA3 (ECHONET automatic swing) plus Toshiba fixed louver positions 0x50..0x54.
enum class SWING {
  OFF = 49,
  BOTH = 67,
  VERTICAL = 65,
  HORIZONTAL = 66,
  VERTICAL_FIX_POSITION_1 = 80,
  VERTICAL_FIX_POSITION_2 = 81,
  VERTICAL_FIX_POSITION_3 = 82,
  VERTICAL_FIX_POSITION_4 = 83,
  VERTICAL_FIX_POSITION_5 = 84
};
// EPC 0x80.
enum class STATE { ON = 48, OFF = 49 };
// EPC 0x87 (ECHONET current limit setting), Toshiba power level 50 / 75 / 100 %.
enum class PWR_LEVEL { PCT_50 = 50, PCT_75 = 75, PCT_100 = 100 };
// EPC 0xCB status bits 3..4. Values documented by maxmacstn/ToshibaCarrierController.
enum class SELF_CLEAN_STATE : uint8_t { RUNNING = 0x18, OFF = 0x10 };

// Wi-Fi LED on the indoor unit front panel. Units use either 0xDE or 0xDF;
// this component writes both, which is harmless on units that ignore one.
enum class WIFI_LED : uint8_t {
  // Values for EPC 0xDE
  DE_OFF = 0x00,
  DE_BLINK = 0x01,
  DE_ON = 0x05,
  // Values for EPC 0xDF
  DF_ON = 0x00,
  DF_OFF = 0x80,
};

// EPC 0xF7 special mode ("merit") byte. Low nibble = merit A (0..7, 0x0A),
// high nibble = merit B (0x00, 0x20, 0x30). Units with a feature mask get mode,
// temp, fan and merit together in one 0xF8 frame; others get plain 0xF7 / 0xB3 Sets.
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

// UART framing (captured on the wire):
//   STX(02) | FT(u16 BE) | CN | FN | DL(u16 BE) | FD[DL] | FCC
// FCC = two's complement of the sum of all bytes except STX.
// Property traffic uses FT=0x0003 and EOJ 01 30 01 (home air conditioner).
//   CN=0x10  Get/Set request from the adapter
//   CN=0x90  reply from the unit
//   CN=0x11  unsolicited notify from the unit (IR remote, internal change)
//   CN=0x91  adapter ACK of a notify (FN echoed)
// Requests use FN=0x00; the unit answers with the same FN.
//
// FD of a request or notify:
//   01 30 01 | LEN(u16 BE) | EPC | EDT[LEN-1]
//   LEN counts EPC + EDT, so a Get is 01 30 01 00 01 <EPC> and a one-byte Set
//   is 01 30 01 00 02 <EPC> <VAL>. (Byte 4 is NOT an opcode.)
// FD of a reply:
//   01 30 01 | STATUS(u16, 00 00 = OK) | LEN(u16 BE) | EPC | EDT[LEN-1]
// A Set reply carries the EPC with no EDT.
static constexpr uint8_t FRAME_STX = 0x02;
static constexpr uint16_t FRAME_FT_PROPERTY = 0x0003;
static constexpr uint16_t FRAME_FT_IDENTITY = 0x0002;
static constexpr uint8_t FRAME_CN_PROPERTY = 0x10;
static constexpr uint8_t FRAME_CN_PROPERTY_REPLY = 0x90;
static constexpr uint8_t FRAME_CN_NOTIFY = 0x11;
static constexpr uint8_t FRAME_CN_NOTIFY_ACK = 0x91;

// Property codes (EPC), ECHONET Lite names where the code is a standard one.
enum class ToshibaCommandType : uint8_t {
  HANDSHAKE = 0,  // dummy command to handle all handshake requests
  DELAY = 1,      // dummy command to issue a delay in communication
  POWER_STATE = 0x80,          // operation status ON/OFF; also the keepalive Get
  ERROR_DETAIL = 0x86,         // manufacturer's fault code, 9 bytes, code in byte 8; read when 0x88 == 0x41
  POWER_SEL = 0x87,            // current limit setting: power level 50/75/100 %
  FAULT_STATUS = 0x88,         // 0x41 = fault present, 0x42 = no fault
  COMFORT_SLEEP = 0x94,        // OFF timer reservation: 0x41 set, 0x42 not set
  OFF_TIMER_REMAINING = 0x96,  // OFF timer relative time: hours, minutes
  CLOCK_SET = 0x98,            // unit clock, 7 bytes; clock sync for units that ignore 0x99
  SCHEDULE_PROGRAM = 0x99,     // weekly schedule (date/time + 7x4 programs); default clock sync
  FAN = 0xA0,                  // air flow rate
  SWING = 0xA3,                // air flow direction / swing
  MODE = 0xB0,                 // operation mode
  TARGET_TEMP = 0xB3,          // set temperature
  ROOM_TEMP = 0xBB,            // measured indoor temperature
  OUTDOOR_TEMP = 0xBE,         // measured outdoor temperature
  AIR_PURIFIER = 0xC7,         // air purifier function setting (ionizer): 0x18 on, 0x10 off
  FILTER_STATUS = 0xCA,        // keepalive Get on profile 3 units with level >= 3
  SELF_CLEAN = 0xCB,           // self-cleaning function setting / status
  ENERGY_DAILY = 0xD8,         // unit clock (6) + 24 x u16 LE hourly energy
  ENERGY_WEEKLY = 0xD9,
  ENERGY_MONTHLY = 0xDA,
  ENERGY_YEARLY = 0xDB,
  WIFI_LED_1 = 0xDE,           // Wi-Fi LED register (one of two, depends on the unit)
  WIFI_LED_2 = 0xDF,           // Wi-Fi LED register (one of two, depends on the unit)
  MODEL_INFO = 0xE0,           // notify only: FCU and CDU model, serial, firmware, EEPROM (100 bytes)
  IDU_STATUS = 0xE4,           // indoor unit diagnostics: Tc, Tcj, fan
  ODU_STATUS = 0xE5,           // outdoor unit diagnostics: Td, Ts, Te, load, Iac
  MERIT_SUPPORT = 0xEE,        // 2 bytes: unit feature mask
  SPECIAL_MODE = 0xF7,         // merit / special mode
  BUNDLE = 0xF8,               // mode, temp, fan, merit in one frame
};

MODE ClimateModeToInt(climate::ClimateMode mode);
climate::ClimateMode IntToClimateMode(MODE mode);

SWING ClimateSwingModeToInt(climate::ClimateSwingMode mode);
climate::ClimateSwingMode IntToClimateSwingMode(SWING mode);

const optional<FAN> ClimateFanModeToInt(climate::ClimateFanMode mode);

const LogString *climate_state_to_string(STATE mode);

const optional<FAN> StringToFanLevel(const char* mode);
const char* IntToCustomFanMode(FAN mode);

const optional<PWR_LEVEL> StringToPwrLevel(const std::string &mode);
const char *IntToPowerLevel(PWR_LEVEL mode);

const optional<SWING> StringToVerticalAirDirection(const std::string &position);
const char* SwingToVerticalAirDirection(SWING mode);
bool IsFixedVerticalAirDirection(SWING mode);

const optional<SPECIAL_MODE> PresetToSpecialMode(const char* preset);
const char* SpecialModeToPreset(SPECIAL_MODE mode);

const optional<climate::ClimatePreset> StringToClimatePreset(const char *preset);
const char* ClimatePresetToString(climate::ClimatePreset preset);
const optional<climate::ClimatePreset> SpecialModeToClimatePreset(SPECIAL_MODE mode);

}  // namespace toshiba_suzumi
}  // namespace esphome
