#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace toshiba_suzumi {

static const char *const TAG = "ToshibaClimateUart";

enum class MODE { AUTO = 65, COOL = 66, HEAT = 67, DRY = 68, FAN_ONLY = 69 };
enum class FAN {
  QUIET = 49,
  FANMODE_1 = 50,
  FANMODE_2 = 51,
  FANMODE_3 = 52,
  FANMODE_4 = 53,
  FANMODE_5 = 54,
  AUTO = 65
};
enum class SWING { OFF = 49, VERTICAL = 65 };
enum class STATE { ON = 49, OFF = 48 };
enum class Command {
  SET_STATE = 128,
  POWER_SEL = 135,
  FAN_MODE = 160,
  SWING_CONTROL = 163,
  MODE = 176,
  SET_TEMP = 179,
  ROOM_TEMP = 187,
  OUTDOOR_TEMP = 190,
};

class ToshibaClimateUart : public PollingComponent, public climate::Climate, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;

  void set_outdoor_temp_sensor(sensor::Sensor *outdoor_temp_sensor) { outdoor_temp_sensor_ = outdoor_temp_sensor; }

 protected:
  sensor::Sensor *outdoor_temp_sensor_{nullptr};

  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall &call) override;

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

 private:
  std::vector<uint8_t> m_rvc_data;

  void send_to_uart(const uint8_t *data, size_t len);
  void start_handshake();
  void parseResponse(uint8_t *rawData, uint8_t length);
  void requestData(Command fn_code);
  void sendCmd(Command fn_code, uint8_t fn_value);
  void getInitData();
};

}  // namespace toshiba_suzumi
}  // namespace esphome
