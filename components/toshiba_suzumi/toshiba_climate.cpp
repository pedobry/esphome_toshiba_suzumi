#include "toshiba_climate.h"
#include "esphome/core/log.h"

#define ARRAY_SIZE(A) (sizeof(A) / sizeof(A[0]))

namespace esphome {
namespace toshiba_suzumi {

using namespace esphome::climate;

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

const optional<climate::ClimateMode> IntToClimateMode(MODE mode) {
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

// {"quiet":49, "lvl_1": 50, "lvl_2":51, "lvl_3":52, "lvl_4":53, "lvl_5":54, "auto":65}
const FAN ClimateFanModeToInt(climate::ClimateFanMode mode) {
  switch (mode) {
    case climate::CLIMATE_FAN_AUTO:
      return FAN::AUTO;
    case climate::CLIMATE_FAN_LOW:
      return FAN::FANMODE_1;
    case climate::CLIMATE_FAN_MEDIUM:
      return FAN::FANMODE_3;
    case climate::CLIMATE_FAN_HIGH:
      return FAN::FANMODE_5;
    default:
      ESP_LOGE(TAG, "Invalid fan mode.");
      return FAN::AUTO;
  }
}

const climate::ClimateFanMode IntToClimateFanMode(FAN mode) {
  switch (mode) {
    case FAN::AUTO:
      return climate::CLIMATE_FAN_AUTO;
    case FAN::FANMODE_1:
      return climate::CLIMATE_FAN_LOW;
    case FAN::FANMODE_3:
      return climate::CLIMATE_FAN_MEDIUM;
    case FAN::FANMODE_5:
      return climate::CLIMATE_FAN_HIGH;
    default:
      ESP_LOGE(TAG, "Invalid fan mode status %d", mode);
      return climate::CLIMATE_FAN_AUTO;
  }
}

const SWING ClimateSwingModeToInt(climate::ClimateSwingMode mode) {
  switch (mode) {
    case climate::CLIMATE_SWING_OFF:
      return SWING::OFF;
    case climate::CLIMATE_SWING_VERTICAL:
      return SWING::VERTICAL;
    default:
      ESP_LOGE(TAG, "Invalid swing mode.");
      return SWING::OFF;
  }
}

const climate::ClimateSwingMode IntToClimateSwingMode(SWING mode) {
  switch (mode) {
    case SWING::OFF:
      return climate::CLIMATE_SWING_OFF;
    case SWING::VERTICAL:
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

void log_dec(std::vector<uint8_t> bytes, uint8_t separator) {
  std::string res;
  size_t len = bytes.size();
  char buf[5];
  for (size_t i = 0; i < len; i++) {
    if (i > 0) {
      res += separator;
    }
    sprintf(buf, "%d", bytes[i]);
    res += buf;
  }
  ESP_LOGD(TAG, "%s", res.c_str());
  delay(10);
}

/**
 * checksum is in twos complement form, calculated from all bytes
 * excluding start byte, can be calculated with formula (0x100-(sum(s)%0x100))%0x100
 */
uint8_t checksum(uint8_t *data, uint8_t length) {
  uint16_t sum = 0;
  for (size_t i = 1; i < length - 1; i++) {
    sum = sum + data[i];
  }
  return (256 - (sum % 256)) % 256;
}

void ToshibaClimateUart::send_to_uart(const uint8_t *data, size_t len) {
  delay(150);
  this->write_array(data, len);
}

/**
 * Send starting handshake to initialize communication with the unit.
 */
void ToshibaClimateUart::start_handshake() {
  int a = 8;
  uint8_t bootlist1[a] = {2, 255, 255, 0, 0, 0, 0, 2};
  send_to_uart(bootlist1, a);
  a = 9;
  uint8_t bootlist2[a] = {2, 255, 255, 1, 0, 0, 1, 2, 254};
  send_to_uart(bootlist2, a);
  a = 10;
  uint8_t bootlist3[a] = {2, 0, 0, 0, 0, 0, 2, 2, 2, 250};
  send_to_uart(bootlist3, a);
  a = 10;
  uint8_t bootlist4[a] = {2, 0, 1, 129, 1, 0, 2, 0, 0, 123};
  send_to_uart(bootlist4, a);
  a = 10;
  uint8_t bootlist5[a] = {2, 0, 1, 2, 0, 0, 2, 0, 0, 254};
  send_to_uart(bootlist5, a);
  a = 7;
  uint8_t bootlist6[a] = {2, 0, 2, 0, 0, 0, 0, 254};
  send_to_uart(bootlist6, a);
  this->flush();
  delay(1800);
  // aftershake
  a = 10;
  uint8_t bootlist7[a] = {2, 0, 2, 1, 0, 0, 2, 0, 0, 251};
  send_to_uart(bootlist7, a);
  a = 10;
  uint8_t bootlist8[a] = {2, 0, 2, 2, 0, 0, 2, 0, 0, 250};
  send_to_uart(bootlist8, a);
}

void ToshibaClimateUart::sendCmd(Command fn_code, uint8_t fn_value) {
  uint8_t send_cmd[15] = {2, 0, 3, 16, 0, 0, 7, 1, 48, 1, 0, 2, 0, fn_value, 0};
  send_cmd[12] = static_cast<uint8_t>(fn_code);
  send_cmd[14] = checksum(send_cmd, 15);
  ESP_LOGD(TAG, "Sending command: %d, value: %d, checksum: %d", send_cmd[12], send_cmd[13], send_cmd[14]);
  this->send_to_uart(send_cmd, 15);
}

void ToshibaClimateUart::requestData(Command fn_code) {
  uint8_t getlist[14] = {2, 0, 3, 16, 0, 0, 6, 1, 48, 1, 0, 1, 0, 0};
  getlist[12] = static_cast<uint8_t>(fn_code);
  getlist[13] = checksum(getlist, 14);
  ESP_LOGD(TAG, "Requesting data from sensor %d, checksum: %d", getlist[12], getlist[13]);
  this->send_to_uart(getlist, 14);
}

void ToshibaClimateUart::getInitData() {
  this->requestData(Command::SET_STATE);
  this->requestData(Command::MODE);
  this->requestData(Command::SET_TEMP);
  this->requestData(Command::FAN_MODE);
  this->requestData(Command::POWER_SEL);
  this->requestData(Command::SWING_CONTROL);
  this->requestData(Command::ROOM_TEMP);
  this->requestData(Command::OUTDOOR_TEMP);
}

void ToshibaClimateUart::setup() {
  ESP_LOGCONFIG(TAG, "Sending handshake...");
  if (this->has_overridden_loop()) {
    ESP_LOGCONFIG(TAG, "Has overriden loop");
  }
  this->start_handshake();
  this->getInitData();
}

void ToshibaClimateUart::loop() {
  if (!this->available()) {
    return;
  }
  ESP_LOGD(TAG, "Received some data.");
  this->m_rvc_data.clear();
  while (this->available()) {
    uint8_t c;
    this->read_byte(&c);
    this->m_rvc_data.push_back(c);
  }
  log_dec(this->m_rvc_data, ',');
  // response size 16 seems to be some kind of ACK
  this->parseResponse(this->m_rvc_data.data(), this->m_rvc_data.size());
}

void ToshibaClimateUart::parseResponse(uint8_t *rawData, uint8_t length) {
  ESP_LOGD(TAG, "Response size: %d", length);
  Command sensor;
  uint8_t value;
  if (length == 15) {
    sensor = static_cast<Command>(rawData[12]);
    value = rawData[13];
  } else if (length == 17) {
    sensor = static_cast<Command>(rawData[14]);
    value = rawData[15];
  } else {
    return;
  }
  switch (sensor) {
    case Command::SET_TEMP:
      ESP_LOGD(TAG, "Received target temp: %d", value);
      this->target_temperature = value;
      break;
    case Command::FAN_MODE: {
      auto fanMode = IntToClimateFanMode(static_cast<FAN>(value));
      ESP_LOGD(TAG, "Received fan mode: %s", climate_fan_mode_to_string(fanMode));
      this->fan_mode = fanMode;
      break;
    }
    case Command::SWING_CONTROL: {
      auto swingMode = IntToClimateSwingMode(static_cast<SWING>(value));
      ESP_LOGD(TAG, "Received swing mode: %s", climate_swing_mode_to_string(swingMode));
      this->swing_mode = swingMode;
      break;
    }
    case Command::MODE: {
      auto mode = IntToClimateMode(static_cast<MODE>(value));
      if (mode.has_value()) {
        ESP_LOGD(TAG, "Received AC mode: %s", climate_mode_to_string(mode.value()));
        this->mode = mode.value();
      }
      break;
    }
    case Command::ROOM_TEMP:
      ESP_LOGD(TAG, "Received room temp: %d °C", value);
      this->current_temperature = value;
      break;
    case Command::OUTDOOR_TEMP:
      ESP_LOGD(TAG, "Received outdoor temp: %d °C", (int8_t) value);
      if (outdoor_temp_sensor_ != nullptr) {
        outdoor_temp_sensor_->publish_state((int8_t) value);
      }
      break;
    case Command::SET_STATE: {
      auto climateState = static_cast<STATE>(value);
      ESP_LOGD(TAG, "Received state: %s", climate_state_to_string(climateState));
      if (climateState == STATE::OFF) {
        this->mode = climate::CLIMATE_MODE_OFF;
      }
      break;
    }
    default:
      ESP_LOGW(TAG, "Unknown sensor: %d with value %d", sensor, value);
      break;
  }
  this->publish_state();
}

void ToshibaClimateUart::dump_config() {
  ESP_LOGCONFIG(TAG, "ToshibaClimate:");
  LOG_CLIMATE("", "Thermostat", this);
  LOG_SENSOR("", "Outdoor Temp", this->outdoor_temp_sensor_);
}

void ToshibaClimateUart::control(const climate::ClimateCall &call) {
  ESP_LOGD(TAG, "Received ToshibaClimateUart::control");
  if (call.get_mode().has_value()) {
    ClimateMode mode = *call.get_mode();
    ESP_LOGD(TAG, "Setting mode to %s", climate_mode_to_string(mode));
    if (this->mode == CLIMATE_MODE_OFF && mode != CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Setting AC state to ON.");
      this->sendCmd(Command::SET_STATE, static_cast<uint8_t>(STATE::ON));
    }
    if (mode == CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Setting AC state to OFF.");
      this->sendCmd(Command::SET_STATE, static_cast<uint8_t>(STATE::OFF));
    } else {
      auto requestedMode = ClimateModeToInt(mode);
      this->sendCmd(Command::MODE, static_cast<uint8_t>(requestedMode));
    }
    this->mode = mode;
  }

  if (call.get_target_temperature().has_value()) {
    auto target_temp = *call.get_target_temperature();
    ESP_LOGD(TAG, "Setting target temp to %f", target_temp);
    this->target_temperature = target_temp;
    this->sendCmd(Command::SET_TEMP, floor(target_temp));
  }

  if (call.get_fan_mode().has_value()) {
    auto fan_mode = *call.get_fan_mode();
    auto function_value = ClimateFanModeToInt(fan_mode);
    ESP_LOGD(TAG, "Setting fan mode to %s", climate_fan_mode_to_string(fan_mode));
    this->fan_mode = fan_mode;
    this->sendCmd(Command::FAN_MODE, static_cast<uint8_t>(function_value));
  }

  if (call.get_swing_mode().has_value()) {
    auto swing_mode = *call.get_swing_mode();
    auto function_value = ClimateSwingModeToInt(swing_mode);
    ESP_LOGD(TAG, "Setting swing mode to %s", climate_swing_mode_to_string(swing_mode));
    this->swing_mode = swing_mode;
    this->sendCmd(Command::SWING_CONTROL, static_cast<uint8_t>(function_value));
  }

  this->publish_state();
}

ClimateTraits ToshibaClimateUart::traits() {
  auto traits = climate::ClimateTraits();
  // traits.set_supports_current_temperature(this->sensor_ != nullptr);

  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_HEAT,
                              climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
  traits.set_supported_fan_modes(
      {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_HIGH});
  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL});
  traits.set_supports_current_temperature(true);
  return traits;
}
void ToshibaClimateUart::update() {
  ESP_LOGD(TAG, "Update");
  this->requestData(Command::ROOM_TEMP);
  this->requestData(Command::OUTDOOR_TEMP);
}

}  // namespace toshiba_suzumi
}  // namespace esphome