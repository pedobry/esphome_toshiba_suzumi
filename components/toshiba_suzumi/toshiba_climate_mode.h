#pragma once

#include <cstddef>
#include <cstdint>
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace toshiba_suzumi {

constexpr const char *CUSTOM_FAN_LEVEL_2 = "Low-Medium";
constexpr const char *CUSTOM_FAN_LEVEL_4 = "Medium-High";

constexpr const char *CUSTOM_PWR_LEVEL_50 = "50 %";
constexpr const char *CUSTOM_PWR_LEVEL_75 = "75 %";
constexpr const char *CUSTOM_PWR_LEVEL_100 = "100 %";

constexpr const char *SPECIAL_MODE_STANDARD = "Standard";
constexpr const char *SPECIAL_MODE_HI_POWER = "Hi POWER";
constexpr const char *SPECIAL_MODE_ECO = "ECO";
// Special modes are described in the owner's manuals of the indoor units
// (e.g. Seiya RAS-xxE2KVG, Haori RAS-Bxx N4KVRG, Shorai Edge RAS-xxJ2KVSG).
constexpr const char *SPECIAL_MODE_FIREPLACE_1 = "Fireplace 1";
constexpr const char *SPECIAL_MODE_FIREPLACE_2 = "Fireplace 2";
constexpr const char *SPECIAL_MODE_EIGHT_DEG = "8 degrees";
constexpr const char *SPECIAL_MODE_SILENT_1 = "Silent#1";
constexpr const char *SPECIAL_MODE_SILENT_2 = "Silent#2";
constexpr const char *SPECIAL_MODE_SLEEP = "Sleep";
constexpr const char *SPECIAL_MODE_FLOOR = "Floor";
constexpr const char *SPECIAL_MODE_COMFORT = "Comfort";

// Value encodings (ECHONET Lite home air conditioner class plus Toshiba
// specifics). See PROTOCOL.md for the EPC map.

// EPC 0xB0 (ECHONET operation mode).
enum class ToshibaMode { HEAT_COOL = 65, COOL = 66, HEAT = 67, DRY = 68, FAN_ONLY = 69 };
// EPC 0xA0 (ECHONET air flow rate): Toshiba levels 0x31..0x36, 0x41 auto.
enum class ToshibaFan {
  FAN_QUIET = 49,
  FAN_LOW = 50,
  FANMODE_2 = 51,
  FAN_MEDIUM = 52,
  FANMODE_4 = 53,
  FAN_HIGH = 54,
  FAN_AUTO = 65
};
// EPC 0xA3 (ECHONET automatic swing) plus Toshiba fixed louver positions 0x50..0x54.
enum class ToshibaSwing {
  OFF = 49,
  BOTH = 67,
  VERTICAL = 65,
  HORIZONTAL = 66,
  VERTICAL_FIX_POSITION_1 = 80,
  VERTICAL_FIX_POSITION_2 = 81,
  VERTICAL_FIX_POSITION_3 = 82,
  VERTICAL_FIX_POSITION_4 = 83,
  VERTICAL_FIX_POSITION_5 = 84,
  // HADA Care Flow: both louvers move to their middle positions and keep the
  // air flow away from people (0xEE byte 2 bit 2).
  HADA_CARE = 96,
  // Fixed left/right louver positions, left to right (0xEE byte 2 bit 5).
  HORIZONTAL_FIX_POSITION_1 = 97,
  HORIZONTAL_FIX_POSITION_2 = 98,
  HORIZONTAL_FIX_POSITION_3 = 99,
  HORIZONTAL_FIX_POSITION_4 = 100,
  HORIZONTAL_FIX_POSITION_5 = 101
};

// Options of the air direction select. Each needs a unit feature, except Off
// and Swing, which every unit has.
enum class AirDirectionGroup : uint8_t { ALWAYS, VERTICAL_FIXED, HORIZONTAL_FIXED, HADA };
struct AirDirection {
  ToshibaSwing swing;
  const char *name;
  AirDirectionGroup group;
};
static constexpr size_t AIR_DIRECTION_COUNT = 13;
extern const AirDirection AIR_DIRECTIONS[AIR_DIRECTION_COUNT];
// EPC 0x80.
enum class PowerState { ON = 48, OFF = 49 };
// EPC 0x87 (ECHONET current limit setting), Toshiba power level 50 / 75 / 100 %.
enum class PowerLevel { PCT_50 = 50, PCT_75 = 75, PCT_100 = 100 };
// EPC 0xCB status bits 3..4. Values documented by maxmacstn/ToshibaCarrierController.
enum class SelfCleanState : uint8_t { RUNNING = 0x18, OFF = 0x10 };
// The status is in bits 3..4; the low bits are settings (bit 0 = self-clean enabled).
static constexpr uint8_t SELF_CLEAN_STATUS_MASK = 0x18;

// Wi-Fi LED on the indoor unit front panel. Units use either 0xDE or 0xDF;
// this component writes both, which is harmless on units that ignore one.
enum class WifiLed : uint8_t {
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
enum SpecialMode {
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
  HANDSHAKE = 0,               // dummy command to handle all handshake requests
  DELAY = 1,                   // dummy command to issue a delay in communication
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
  WIFI_LED_1 = 0xDE,     // Wi-Fi LED register (one of two, depends on the unit)
  WIFI_LED_2 = 0xDF,     // Wi-Fi LED register (one of two, depends on the unit)
  MODEL_INFO = 0xE0,     // notify only: FCU and CDU model, serial, firmware, EEPROM (100 bytes)
  IDU_STATUS = 0xE4,     // indoor unit diagnostics: Tc, Tcj, fan
  ODU_STATUS = 0xE5,     // outdoor unit diagnostics: Td, Ts, Te, load, Iac
  MERIT_SUPPORT = 0xEE,  // 2 bytes: unit feature mask
  SPECIAL_MODE = 0xF7,   // merit / special mode
  BUNDLE = 0xF8,         // mode, temp, fan, merit in one frame
};

ToshibaMode to_toshiba_mode(climate::ClimateMode mode);
climate::ClimateMode from_toshiba_mode(ToshibaMode mode);

ToshibaSwing to_toshiba_swing(climate::ClimateSwingMode mode);
climate::ClimateSwingMode from_toshiba_swing(ToshibaSwing mode);

const optional<ToshibaFan> to_toshiba_fan(climate::ClimateFanMode mode);

const LogString *power_state_to_string(PowerState mode);

const optional<ToshibaFan> custom_fan_mode_to_toshiba(const char *mode);
const char *toshiba_fan_to_custom_fan_mode(ToshibaFan mode);

// Select option index -> value. The option lists in climate.py use the same order.
optional<PowerLevel> power_level_from_index(size_t index);
const char *power_level_to_string(PowerLevel mode);

// The air direction option that shows a swing value: horizontal swing shows as
// Off, swing in both directions as Swing.
ToshibaSwing air_direction_of(ToshibaSwing mode);
const char *air_direction_name(ToshibaSwing mode);
// A fixed louver position or HADA Care Flow: the louvers do not swing.
bool is_fixed_air_direction(ToshibaSwing mode);

const optional<SpecialMode> preset_to_special_mode(const char *preset);
const char *special_mode_to_preset(SpecialMode mode);

const optional<climate::ClimatePreset> standard_preset_from_name(const char *preset);
const char *standard_preset_name(climate::ClimatePreset preset);
const optional<climate::ClimatePreset> special_mode_to_standard_preset(SpecialMode mode);

}  // namespace toshiba_suzumi
}  // namespace esphome
