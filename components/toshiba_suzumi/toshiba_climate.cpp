#include "toshiba_climate.h"
#include "toshiba_climate_mode.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>

namespace esphome {
namespace toshiba_suzumi {

using namespace esphome::climate;

static const int RECEIVE_TIMEOUT = 300;
static const int COMMAND_DELAY = 100;
static const uint32_t PUBLISH_SOFT_DEBOUNCE_MS = 120;
static const uint32_t PUBLISH_HARD_TIMEOUT_MS = 500;
static const uint8_t WIFI_LED_DISABLED_VALUE = 128;
static const uint8_t WIFI_LED_ENABLED_VALUE = 129;

static bool is_known_sensor_id(uint8_t id) {
  switch (static_cast<ToshibaCommandType>(id)) {
    case ToshibaCommandType::TARGET_TEMP:
    case ToshibaCommandType::FAN:
    case ToshibaCommandType::SWING:
    case ToshibaCommandType::MODE:
    case ToshibaCommandType::ROOM_TEMP:
    case ToshibaCommandType::OUTDOOR_TEMP:
    case ToshibaCommandType::POWER_SEL:
    case ToshibaCommandType::POWER_STATE:
    case ToshibaCommandType::SPECIAL_MODE:
    case ToshibaCommandType::WIFI_LED:
      return true;
    default:
      return false;
  }
}

/**
 * Checksum is calculated from all bytes excluding start byte.
 * It's (256 - (sum % 256)).
 */
uint8_t checksum(std::vector<uint8_t> data, uint8_t length) {
  uint8_t sum = 0;
  for (size_t i = 1; i < length; i++) {
    sum += data[i];
  }
  return 256 - sum;
}

/**
 * Send the command to UART interface.
 */
void ToshibaClimateUart::send_to_uart(ToshibaCommand command) {
  this->last_command_timestamp_ = millis();
  ESP_LOGV(TAG, "Sending: [%s]", format_hex_pretty(command.payload).c_str());
  this->write_array(command.payload);
}

/**
 * Send starting handshake to initialize communication with the unit.
 */
void ToshibaClimateUart::start_handshake() {
  ESP_LOGCONFIG(TAG, "Sending handshake...");
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = HANDSHAKE[0]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = HANDSHAKE[1]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = HANDSHAKE[2]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = HANDSHAKE[3]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = HANDSHAKE[4]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = HANDSHAKE[5]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::DELAY, .delay = 2000});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = AFTER_HANDSHAKE[0]});
  enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = AFTER_HANDSHAKE[1]});
}

/**
 * Handle data in RX buffer, validate message for content and checksum.
 * Since we know the format only of some messages (expected length), unknown messages
 * are ended via RECIEVE timeout.
 */
bool ToshibaClimateUart::validate_message_() {
  uint8_t at = this->rx_message_.size() - 1;
  auto *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  // Byte 0: HEADER (always 0x02)
  if (at == 0)
    return new_byte == 0x02;

  // always get first three bytes
  if (at < 2) {
    return true;
  }

  // Byte 3
  if (data[2] != 0x03) {
    // Normal commands starts with 0x02 0x00 0x03 and have length between 15-17 bytes.
    // however there are some special unknown handshake commands which has non-standard replies.
    // Since we don't know their format, we can't validate them.
    return true;
  }

  if (at <= 5) {
    // no validation for these fields
    return true;
  }

  // Byte 7: LENGTH
  uint8_t length = 6 + data[6] + 1;  // prefix + data + checksum

  // wait until all data is read
  if (at < length)
    return true;

  // last byte: CHECKSUM
  uint8_t rx_checksum = new_byte;
  uint8_t calc_checksum = checksum(this->rx_message_, at);

  if (rx_checksum != calc_checksum) {
    ESP_LOGW(TAG, "Received invalid message checksum %02X!=%02X DATA=[%s]", rx_checksum, calc_checksum,
             format_hex_pretty(data, length).c_str());
    return false;
  }

  // valid message
  ESP_LOGV(TAG, "Received: DATA=[%s]", format_hex_pretty(data, length).c_str());
  this->parseResponse(this->rx_message_);

  // return false to reset rx buffer
  return false;
}

void ToshibaClimateUart::enqueue_command_(const ToshibaCommand &command) {
  this->command_queue_.push_back(command);
  this->process_command_queue_();
}

void ToshibaClimateUart::sendCmd(ToshibaCommandType cmd, uint8_t value) {
  std::vector<uint8_t> payload = {2, 0, 3, 16, 0, 0, 7, 1, 48, 1, 0, 2};
  payload.push_back(static_cast<uint8_t>(cmd));
  payload.push_back(value);
  payload.push_back(checksum(payload, payload.size()));
  ESP_LOGD(TAG, "Sending ToshibaCommand: %d, value: %d, checksum: %d", cmd, value, payload[14]);
  this->enqueue_command_(ToshibaCommand{.cmd = cmd, .payload = std::vector<uint8_t>{payload}, .delay = 0});
}

void ToshibaClimateUart::requestData(ToshibaCommandType cmd, bool is_debug_request) {
  std::vector<uint8_t> payload = {2, 0, 3, 16, 0, 0, 6, 1, 48, 1, 0, 1};
  payload.push_back(static_cast<uint8_t>(cmd));
  payload.push_back(checksum(payload, payload.size()));
  if (is_debug_request) {
    ESP_LOGD(TAG, "Debug request sensor %d, checksum: %d", payload[12], payload[13]);
    this->debug_pending_requests_[static_cast<uint8_t>(cmd)]++;
  } else {
    ESP_LOGI(TAG, "Requesting data from sensor %d, checksum: %d", payload[12], payload[13]);
  }
  this->enqueue_command_(ToshibaCommand{
      .cmd = cmd,
      .payload = std::vector<uint8_t>{payload},
      .delay = 0,
      .is_data_request = true,
      .is_debug_request = is_debug_request,
      .request_id = static_cast<uint8_t>(cmd),
  });
}

void ToshibaClimateUart::getInitData() {
  ESP_LOGD(TAG, "Requesting initial data from AC unit");
  this->requestData(ToshibaCommandType::POWER_STATE);
  this->requestData(ToshibaCommandType::MODE);
  this->requestData(ToshibaCommandType::TARGET_TEMP);
  this->requestData(ToshibaCommandType::FAN);
  this->requestData(ToshibaCommandType::POWER_SEL);
  this->requestData(ToshibaCommandType::SWING);
  this->requestData(ToshibaCommandType::ROOM_TEMP);
  this->requestData(ToshibaCommandType::OUTDOOR_TEMP);
  this->requestData(ToshibaCommandType::SPECIAL_MODE);
  if (wifi_led_switch_ != nullptr) {
    this->requestData(ToshibaCommandType::WIFI_LED);
  }
}

void ToshibaClimateUart::setup() {
  // establish communication
  this->start_handshake();
  // load initial sensor data from the unit
  this->getInitData();
}

/**
 * Detect RX timeout and send next command in the queue to the unit.
 */
void ToshibaClimateUart::process_command_queue_() {
  uint32_t now = millis();
  uint32_t cmdDelay = now - this->last_command_timestamp_;

  // when we have not processed message and timeout since last received byte has expired,
  // we likely won't receive any more data and there is nothing we can do with the message as it's
  // format is was not recognized by validate_message_ function.
  // Nothing to do - drop the message to free up communication and allow to send next command.
  if (now - this->last_rx_char_timestamp_ > RECEIVE_TIMEOUT) {
    this->rx_message_.clear();
    this->active_request_is_data_ = false;
    this->active_request_is_debug_ = false;
  }

  // when there is no RX message and there is a command to send
  if (cmdDelay > COMMAND_DELAY && !this->command_queue_.empty() && this->rx_message_.empty()) {
    auto cmd_it = this->command_queue_.begin();
    if (cmd_it->is_debug_request) {
      auto normal_it = std::find_if(this->command_queue_.begin(), this->command_queue_.end(),
                                    [](const ToshibaCommand &cmd) { return !cmd.is_debug_request; });
      if (normal_it != this->command_queue_.end()) {
        cmd_it = normal_it;
      }
    }
    auto newCommand = *cmd_it;
    if (newCommand.cmd == ToshibaCommandType::DELAY && cmdDelay < newCommand.delay) {
      // delay command did not finished yet
      return;
    }
    // DELAY commands don't send data over UART, just remove them from queue
    if (newCommand.cmd == ToshibaCommandType::DELAY) {
      this->command_queue_.erase(cmd_it);
      return;
    }
    this->active_request_is_data_ = newCommand.is_data_request;
    this->active_request_is_debug_ = newCommand.is_debug_request;
    this->active_request_id_ = newCommand.request_id;
    this->send_to_uart(newCommand);
    this->command_queue_.erase(cmd_it);
  }
}

/**
 * Handle received byte from UART
 */
void ToshibaClimateUart::handle_rx_byte_(uint8_t c) {
  this->rx_message_.push_back(c);
  if (!validate_message_()) {
    this->rx_message_.clear();
  } else {
    this->last_rx_char_timestamp_ = millis();
  }
}

void ToshibaClimateUart::loop() {
  while (available()) {
    uint8_t c;
    this->read_byte(&c);
    this->handle_rx_byte_(c);
  }
  if (this->debug_enabled_ && this->command_queue_.empty() && this->rx_message_.empty() &&
      this->debug_initial_scan_in_progress_) {
    this->run_debug_initial_scan_step_();
  }
  if (this->debug_enabled_ && !this->debug_initial_scan_in_progress_ && !this->debug_poll_run_in_progress_ &&
      millis() - this->last_debug_poll_timestamp_ >= this->debug_poll_interval_ms_) {
    this->start_debug_poll_run_();
  }
  if (this->debug_enabled_ && this->debug_poll_run_in_progress_ && this->command_queue_.empty() &&
      this->rx_message_.empty()) {
    this->run_debug_poll_step_();
  }
  this->process_command_queue_();
  this->flush_pending_publish_if_ready_();
}

void ToshibaClimateUart::parseResponse(std::vector<uint8_t> rawData) {
  const bool is_debug_response = this->active_request_is_debug_;
  uint8_t response_id = 0;
  const bool has_response_id = this->extract_response_id_(rawData, response_id);
  const bool is_pending_debug_response = has_response_id && this->debug_pending_requests_[response_id] > 0;
  auto clear_active_request = [this]() {
    this->active_request_is_data_ = false;
    this->active_request_is_debug_ = false;
  };
  if (is_debug_response || is_pending_debug_response) {
    if (has_response_id && this->debug_pending_requests_[response_id] > 0) {
      this->debug_pending_requests_[response_id]--;
    }
    this->handle_debug_response_(rawData, response_id, has_response_id);
    clear_active_request();
    this->rx_message_.clear();  // message processed, clear buffer
    return;
  }

  uint8_t length = rawData.size();
  ToshibaCommandType sensor;
  uint8_t value;

  switch (length) {
    case 15:  // response to requestData with the actual value of sensor/setting
      sensor = static_cast<ToshibaCommandType>(rawData[12]);
      value = rawData[13];
      break;
    case 16:  // probably ACK for issued command
      ESP_LOGD(TAG, "Received message with length: %d and value %s", length, format_hex_pretty(rawData).c_str());
      clear_active_request();
      return;
    case 17:  // response to requestData with the actual value of sensor/setting
      sensor = static_cast<ToshibaCommandType>(rawData[14]);
      value = rawData[15];
      break;
    default:
      ESP_LOGW(TAG, "Received unknown message with length: %d and value %s", length,
               format_hex_pretty(rawData).c_str());
      clear_active_request();
      return;
  }
  switch (sensor) {
    case ToshibaCommandType::TARGET_TEMP:
      ESP_LOGI(TAG, "Received target temp: %d", value);
      if (this->special_mode_ == SPECIAL_MODE::EIGHT_DEG) {
        // if special mode is EIGHT_DEG, shift the target temperature by SPECIAL_TEMP_OFFSET
        value -= SPECIAL_TEMP_OFFSET;

        ESP_LOGI(TAG, "Note: Special Mode \"%s\" is active, shifting target temp to %d", SPECIAL_MODE_EIGHT_DEG, value);
      }
      this->target_temperature = value;
      break;
    case ToshibaCommandType::FAN: {
      if (static_cast<FAN>(value) == FAN::FAN_AUTO) {
        ESP_LOGI(TAG, "Received fan mode: AUTO");
        this->set_fan_mode_(CLIMATE_FAN_AUTO);
      } else if (static_cast<FAN>(value) == FAN::FAN_QUIET) {
        ESP_LOGI(TAG, "Received fan mode: QUIET");
        this->set_fan_mode_(CLIMATE_FAN_QUIET);
      } else if (static_cast<FAN>(value) == FAN::FAN_LOW) {
        ESP_LOGI(TAG, "Received fan mode: LOW");
        this->set_fan_mode_(CLIMATE_FAN_LOW);
      } else if (static_cast<FAN>(value) == FAN::FAN_MEDIUM) {
        ESP_LOGI(TAG, "Received fan mode: MEDIUM");
        this->set_fan_mode_(CLIMATE_FAN_MEDIUM);
      } else if (static_cast<FAN>(value) == FAN::FAN_HIGH) {
        ESP_LOGI(TAG, "Received fan mode: HIGH");
        this->set_fan_mode_(CLIMATE_FAN_HIGH);
      } else {
        auto fanMode = IntToCustomFanMode(static_cast<FAN>(value));
        ESP_LOGI(TAG, "Received fan mode: %s", fanMode);
        this->set_custom_fan_mode_(fanMode);
      }
      break;
    }
    case ToshibaCommandType::SWING: {
      auto swingMode = IntToClimateSwingMode(static_cast<SWING>(value));
      ESP_LOGI(TAG, "Received swing mode: %s", climate_swing_mode_to_string(swingMode));
      this->swing_mode = swingMode;
      break;
    }
    case ToshibaCommandType::MODE: {
      auto mode = IntToClimateMode(static_cast<MODE>(value));
      ESP_LOGI(TAG, "Received AC mode: %s", climate_mode_to_string(mode));
      if (this->power_state_ == STATE::ON) {
        this->mode = mode;
      }
      break;
    }
    case ToshibaCommandType::ROOM_TEMP:
      ESP_LOGI(TAG, "Received room temp: %d °C", value);
      this->current_temperature = value;
      break;
    case ToshibaCommandType::OUTDOOR_TEMP:
      if (outdoor_temp_sensor_ != nullptr) {
        ESP_LOGI(TAG, "Received outdoor temp: %d °C", (int8_t) value);
        outdoor_temp_sensor_->publish_state((int8_t) value);
      }
      break;
    case ToshibaCommandType::POWER_SEL: {
      auto pwr_level = IntToPowerLevel(static_cast<PWR_LEVEL>(value));
      ESP_LOGI(TAG, "Received power select: %d", value);
      if (pwr_select_ != nullptr) {
        pwr_select_->publish_state(pwr_level);
      }
      break;
    }
    case ToshibaCommandType::POWER_STATE: {
      auto climateState = static_cast<STATE>(value);
      ESP_LOGI(TAG, "Received AC unit power state: %s", climate_state_to_string(climateState));
      if (climateState == STATE::OFF) {
        // AC unit was just powered off, set mode to OFF
        this->mode = climate::CLIMATE_MODE_OFF;
      } else if (this->mode == climate::CLIMATE_MODE_OFF && climateState == STATE::ON) {
        // AC unit was just powered on, query unit for it's MODE
        this->requestData(ToshibaCommandType::MODE);
      }
      this->power_state_ = climateState;
      break;
    }
    case ToshibaCommandType::SPECIAL_MODE: {
      this->special_mode_ = static_cast<SPECIAL_MODE>(value);
      auto preset_string = SpecialModeToPreset(this->special_mode_.value());
      ESP_LOGI(TAG, "Received special mode: %s", preset_string);
      // Only update preset if it's supported
      if (std::find(supported_presets_.begin(), supported_presets_.end(), preset_string) != supported_presets_.end()) {
        auto climate_preset = SpecialModeToClimatePreset(this->special_mode_.value());
        if (climate_preset.has_value()) {
          // Use standard preset
          this->set_preset_(climate_preset.value());
        } else {
          // Use custom preset
          this->set_custom_preset_(preset_string);
          this->set_preset_(climate::CLIMATE_PRESET_NONE);
        }
      }
      break;
    }
    case ToshibaCommandType::WIFI_LED:
      if (wifi_led_switch_ != nullptr) {
        const bool wifi_led_enabled = value != WIFI_LED_DISABLED_VALUE;
        ESP_LOGI(TAG, "Received wifi led state: %s (raw: %d)", wifi_led_enabled ? "ON" : "OFF", value);
        wifi_led_switch_->publish_state(wifi_led_enabled);
      }
      break;
    default:
      ESP_LOGW(TAG, "Unknown sensor: %d with value %d", sensor, value);
      break;
  }
  clear_active_request();
  this->rx_message_.clear();  // message processed, clear buffer
  this->schedule_publish_();
}

void ToshibaClimateUart::dump_config() {
  ESP_LOGCONFIG(TAG, "ToshibaClimate:");
  LOG_CLIMATE("", "Thermostat", this);
  if (outdoor_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "Outdoor Temp", this->outdoor_temp_sensor_);
  }
  if (pwr_select_ != nullptr) {
    LOG_SELECT("", "Power selector", this->pwr_select_);
  }
  if (wifi_led_switch_ != nullptr) {
    LOG_SWITCH("", "Wifi LED", this->wifi_led_switch_);
  }
  if (!supported_presets_.empty()) {
    ESP_LOGCONFIG(TAG, "Supported presets:");
    for (const char* &preset : supported_presets_) {
      ESP_LOGCONFIG(TAG, "  - %s", preset);
    }
  }
  ESP_LOGI(TAG, "Min Temp: %d", this->min_temp_);
}

/**
 * Periodically request room and outdoor temperature.
 * It servers two purposes - updates data and is like "watchdog" because
 * some people reported that without communication, the unit might stop responding.
 */
void ToshibaClimateUart::update() {
  if (this->debug_enabled_ && this->debug_initial_scan_in_progress_) {
    return;
  }
  this->requestData(ToshibaCommandType::ROOM_TEMP);
  if (outdoor_temp_sensor_ != nullptr) {
    this->requestData(ToshibaCommandType::OUTDOOR_TEMP);
  }
}

void ToshibaClimateUart::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    ClimateMode mode = *call.get_mode();
    ESP_LOGD(TAG, "Setting mode to %s", climate_mode_to_string(mode));
    if (this->mode == CLIMATE_MODE_OFF && mode != CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Setting AC unit power state to ON.");
      this->sendCmd(ToshibaCommandType::POWER_STATE, static_cast<uint8_t>(STATE::ON));
    }
    if (mode == CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Setting AC unit power state to OFF.");
      this->sendCmd(ToshibaCommandType::POWER_STATE, static_cast<uint8_t>(STATE::OFF));
    } else {
      auto requestedMode = ClimateModeToInt(mode);
      this->sendCmd(ToshibaCommandType::MODE, static_cast<uint8_t>(requestedMode));
    }
    this->mode = mode;
  }

  if (call.get_target_temperature().has_value()) {
    auto target_temp = *call.get_target_temperature();
    uint8_t newTargetTemp = (uint8_t) target_temp;
    bool special_mode_changed = false;
    if (newTargetTemp >= MIN_TEMP_STANDARD && this->special_mode_ == SPECIAL_MODE::EIGHT_DEG) {
      // if target temp is above MIN_TEMP_STANDARD and special mode is EIGHT_DEG, change to Standard mode
      this->special_mode_ = SPECIAL_MODE::STANDARD;
      special_mode_changed = true;
      ESP_LOGD(TAG, "Changing to Standard Mode");
    } else if (newTargetTemp < MIN_TEMP_STANDARD && this->special_mode_ != SPECIAL_MODE::EIGHT_DEG) {
      // if target temp is below MIN_TEMP_STANDARD and special mode is not EIGHT_DEG, change to FrostGuard mode
      this->special_mode_ = SPECIAL_MODE::EIGHT_DEG;
      special_mode_changed = true;
      ESP_LOGD(TAG, "Changing to FrostGuard Mode");
    }
    if (special_mode_changed) {
      // send command to change special mode
      this->sendCmd(ToshibaCommandType::SPECIAL_MODE, static_cast<uint8_t>(this->special_mode_.value()));
    }

    ESP_LOGD(TAG, "Setting target temp to %d", newTargetTemp);
    if (this->special_mode_ == SPECIAL_MODE::EIGHT_DEG) {
      newTargetTemp += SPECIAL_TEMP_OFFSET;
      ESP_LOGD(TAG, "Note: Special Mode \"%s\" active, shifting setpoint temp to %d", SPECIAL_MODE_EIGHT_DEG,
               newTargetTemp);
    }
    // set the target temperature from HA to Climate component
    this->target_temperature = target_temp;
    // send command to set the target temperature to the unit
    // (which will be shifted by SPECIAL_TEMP_OFFSET if special mode is active)
    this->sendCmd(ToshibaCommandType::TARGET_TEMP, newTargetTemp);
  }

  if (call.get_fan_mode().has_value()) {
    auto fan_mode = *call.get_fan_mode();
    ESP_LOGD(TAG, "Setting fan mode to %s", climate_fan_mode_to_string(fan_mode));
    this->set_fan_mode_(fan_mode);
    auto fan_value = ClimateFanModeToInt(fan_mode);
    if (fan_value.has_value()) {
      this->sendCmd(ToshibaCommandType::FAN, static_cast<uint8_t>(fan_value.value()));
    }
  }

  if (call.has_custom_fan_mode()) {
    auto fan_mode = call.get_custom_fan_mode();
    auto payload = StringToFanLevel(fan_mode.c_str());
    if (payload.has_value()) {
      ESP_LOGD(TAG, "Setting fan mode to custom: %s", fan_mode.c_str());
      this->set_custom_fan_mode_(fan_mode);
      this->sendCmd(ToshibaCommandType::FAN, static_cast<uint8_t>(payload.value()));
    }
  }

  if (call.get_swing_mode().has_value()) {
    auto swing_mode = *call.get_swing_mode();
    auto function_value = ClimateSwingModeToInt(swing_mode);
    ESP_LOGD(TAG, "Setting swing mode to %s", climate_swing_mode_to_string(swing_mode));
    this->swing_mode = swing_mode;
    this->sendCmd(ToshibaCommandType::SWING, static_cast<uint8_t>(function_value));
  }

  if (call.get_preset().has_value()) {
    auto preset = *call.get_preset();
    auto preset_string = ClimatePresetToString(preset);
    ESP_LOGD(TAG, "Setting preset to %s", preset_string);
    auto special_mode = PresetToSpecialMode(preset_string);
    if (special_mode.has_value()) {
      this->sendCmd(ToshibaCommandType::SPECIAL_MODE, static_cast<uint8_t>(special_mode.value()));
      // Set standard preset
      this->set_preset_(preset);

      // Handle special temperature logic for "8 degrees" mode
      if (special_mode.value() != this->special_mode_) {
        if (this->special_mode_ == SPECIAL_MODE::EIGHT_DEG && this->target_temperature < this->min_temp_) {
          // when switching from FrostGuard to Standard mode, set target temperature to default for Standard mode
          this->target_temperature = NORMAL_MODE_DEF_TEMP;
        }
        this->special_mode_ = special_mode.value();
        if (special_mode.value() == SPECIAL_MODE::EIGHT_DEG && this->target_temperature >= this->min_temp_) {
          // when switching from Standard to FrostGuard mode, set target temperature to default for FrostGuard mode
          this->target_temperature = SPECIAL_MODE_EIGHT_DEG_DEF_TEMP;
        }
      } else {
        this->special_mode_ = special_mode.value();
      }
    } else {
      ESP_LOGW(TAG, "Unknown preset: %s", preset_string);
    }
  }

  if (call.has_custom_preset()) {
    auto custom_preset = call.get_custom_preset();
    ESP_LOGD(TAG, "Setting custom preset to %s", custom_preset.c_str());
    auto special_mode = PresetToSpecialMode(custom_preset.c_str());
    if (special_mode.has_value()) {
      this->sendCmd(ToshibaCommandType::SPECIAL_MODE, static_cast<uint8_t>(special_mode.value()));
      // Set custom preset
      this->set_custom_preset_(custom_preset);

      // Handle special temperature logic for "8 degrees" mode
      if (special_mode.value() != this->special_mode_) {
        if (this->special_mode_ == SPECIAL_MODE::EIGHT_DEG && this->target_temperature < this->min_temp_) {
          // when switching from FrostGuard to Standard mode, set target temperature to default for Standard mode
          this->target_temperature = NORMAL_MODE_DEF_TEMP;
        }
        this->special_mode_ = special_mode.value();
        if (special_mode.value() == SPECIAL_MODE::EIGHT_DEG && this->target_temperature >= this->min_temp_) {
          // when switching from Standard to FrostGuard mode, set target temperature to default for FrostGuard mode
          this->target_temperature = SPECIAL_MODE_EIGHT_DEG_DEF_TEMP;
        }
      } else {
        this->special_mode_ = special_mode.value();
      }
    } else {
      ESP_LOGW(TAG, "Unknown custom preset: %s", custom_preset.c_str());
    }
  }

  this->publish_state();
}

ClimateTraits ToshibaClimateUart::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_COOL,
                              climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
  if (this->horizontal_swing_) {
    traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                                      climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH});
  } else {
    traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL});
  }
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);

  // Toshiba AC has more FAN levels that standard climate component, we have to use custom.
  traits.add_supported_fan_mode(CLIMATE_FAN_AUTO);
  traits.add_supported_fan_mode(CLIMATE_FAN_QUIET);
  traits.add_supported_fan_mode(CLIMATE_FAN_LOW);
  traits.add_supported_fan_mode(CLIMATE_FAN_MEDIUM);
  traits.add_supported_fan_mode(CLIMATE_FAN_HIGH);
  traits.set_supported_custom_fan_modes({CUSTOM_FAN_LEVEL_2, CUSTOM_FAN_LEVEL_4});

  traits.set_visual_temperature_step(1);
  traits.set_visual_min_temperature(this->min_temp_);
  traits.set_visual_max_temperature(MAX_TEMP);

  // Add supported presets based on configuration
  if (!supported_presets_.empty()) {
    std::vector<const char*> custom_preset_names;
    // Presets are automatically enabled when adding supported presets
    for (const char* &preset_string : supported_presets_) {
      climate::ClimatePreset climate_preset = StringToClimatePreset(preset_string);
      if (climate_preset != climate::CLIMATE_PRESET_NONE || preset_string == SPECIAL_MODE_STANDARD) {
        // Use standard presets for mapped modes
        traits.add_supported_preset(climate_preset);
      } else {
        custom_preset_names.push_back(preset_string);
      }
    }
    if (!custom_preset_names.empty()) {
      traits.set_supported_custom_presets(custom_preset_names);
    }
  }
  return traits;
}

void ToshibaClimateUart::on_set_pwr_level(const std::string &value) {
  ESP_LOGD(TAG, "Setting power level to %s", value.c_str());
  auto pwr_level = StringToPwrLevel(value);
  this->sendCmd(ToshibaCommandType::POWER_SEL, static_cast<uint8_t>(pwr_level.value()));
  pwr_select_->publish_state(value);
}

void ToshibaPwrModeSelect::control(const std::string &value) { parent_->on_set_pwr_level(value); }

void ToshibaClimateUart::on_set_wifi_led(bool enabled) {
  ESP_LOGD(TAG, "Setting wifi led to %s", enabled ? "ON" : "OFF");
  this->sendCmd(ToshibaCommandType::WIFI_LED, enabled ? WIFI_LED_ENABLED_VALUE : WIFI_LED_DISABLED_VALUE);
  if (wifi_led_switch_ != nullptr) {
    wifi_led_switch_->publish_state(enabled);
  }
}

void ToshibaClimateUart::on_set_debug(bool enabled) {
  this->debug_enabled_ = enabled;
  if (debug_switch_ != nullptr) {
    debug_switch_->publish_state(enabled);
  }
  if (enabled) {
    ESP_LOGI(TAG, "Debug enabled: scanning IDs %u..%u", this->debug_initial_from_, this->debug_initial_to_);
    this->start_debug_scan_();
    this->last_debug_poll_timestamp_ = millis();
  } else {
    ESP_LOGI(TAG, "Debug disabled: stopping debug polling");
    this->clear_queued_debug_requests_();
  }
}

void ToshibaClimateUart::start_debug_scan_() {
  this->debug_initial_scan_in_progress_ = true;
  this->debug_initial_next_id_ = this->debug_initial_from_;
  this->debug_poll_cursor_ = 0;
  this->debug_poll_run_in_progress_ = false;
}

void ToshibaClimateUart::run_debug_initial_scan_step_() {
  uint16_t sent = 0;
  while (sent < this->debug_batch_size_ && this->debug_initial_scan_in_progress_) {
    if (this->debug_initial_next_id_ > this->debug_initial_to_) {
      this->debug_initial_scan_in_progress_ = false;
      ESP_LOGI(TAG, "Debug initial scan finished.");
      std::string report = "{\"initial_scan_found\":" + std::to_string(this->debug_discovered_ids_.size()) + "}";
      ESP_LOGD(TAG, "Debug change %s", report.c_str());
      if (this->debug_change_sensor_ != nullptr) {
        this->debug_change_sensor_->publish_state(report);
      }
      break;
    }
    this->requestData(static_cast<ToshibaCommandType>(this->debug_initial_next_id_), true);
    this->debug_initial_next_id_++;
    sent++;
  }
}

void ToshibaClimateUart::start_debug_poll_run_() {
  this->debug_poll_cursor_ = 0;
  this->debug_poll_run_in_progress_ = true;
}

void ToshibaClimateUart::run_debug_poll_step_() {
  if (!this->debug_enabled_) {
    this->debug_poll_run_in_progress_ = false;
    return;
  }
  if (this->debug_discovered_ids_.empty()) {
    this->debug_poll_run_in_progress_ = false;
    this->last_debug_poll_timestamp_ = millis();
    return;
  }

  uint16_t sent = 0;
  while (sent < this->debug_batch_size_ && this->debug_poll_cursor_ < this->debug_discovered_ids_.size()) {
    uint8_t id = this->debug_discovered_ids_[this->debug_poll_cursor_];
    this->requestData(static_cast<ToshibaCommandType>(id), true);
    this->debug_poll_cursor_++;
    sent++;
  }

  if (this->debug_poll_cursor_ >= this->debug_discovered_ids_.size()) {
    this->debug_poll_run_in_progress_ = false;
    this->last_debug_poll_timestamp_ = millis();
  }
}

void ToshibaClimateUart::clear_queued_debug_requests_() {
  this->command_queue_.erase(
      std::remove_if(this->command_queue_.begin(), this->command_queue_.end(),
                     [](const ToshibaCommand &cmd) { return cmd.is_debug_request; }),
      this->command_queue_.end());
  this->active_request_is_debug_ = false;
  this->debug_initial_scan_in_progress_ = false;
  this->debug_poll_run_in_progress_ = false;
  this->debug_pending_requests_.fill(0);
}

void ToshibaClimateUart::handle_debug_response_(const std::vector<uint8_t> &raw_data, uint8_t sensor_id_override,
                                                bool has_sensor_id_override) {
  uint8_t sensor_id = has_sensor_id_override ? sensor_id_override : this->active_request_id_;
  uint8_t response_id = 0;
  if (has_sensor_id_override) {
    // Sensor id already resolved by parser via pending-request correlation.
  } else if (this->extract_response_id_(raw_data, response_id)) {
    if (response_id != this->active_request_id_) {
      ESP_LOGD(TAG, "Debug response ID mismatch: requested %u but got %u. Using response ID.",
               this->active_request_id_, response_id);
    }
    sensor_id = response_id;
  } else {
    ESP_LOGD(TAG, "Debug response ID could not be extracted. Mapping payload to requested ID %u.",
             this->active_request_id_);
  }

  std::string payload_hex = this->payload_to_hex_(raw_data);
  if (payload_hex.empty()) {
    return;
  }
  ESP_LOGD(TAG, "Debug payload id:%u payload:%s", sensor_id, payload_hex.c_str());
  if (!this->debug_id_discovered_[sensor_id]) {
    this->debug_id_discovered_[sensor_id] = true;
    this->debug_discovered_ids_.push_back(sensor_id);
    ESP_LOGI(TAG, "Debug discovered responding ID: %u", sensor_id);
  }

  std::string &last_payload = this->debug_last_payloads_[sensor_id];
  if (last_payload.empty()) {
    last_payload = payload_hex;
    return;
  }
  if (last_payload != payload_hex) {
    std::string report =
        "{\"id\":" + std::to_string(sensor_id) + ",\"old\":\"" + last_payload + "\",\"new\":\"" + payload_hex + "\"}";
    if (report.size() > 250) {
      report.resize(247);
      report += "...";
    }
    ESP_LOGD(TAG, "Debug change %s", report.c_str());
    if (this->debug_change_sensor_ != nullptr) {
      this->debug_change_sensor_->publish_state(report);
    }
    last_payload = payload_hex;
  }
}

bool ToshibaClimateUart::extract_response_id_(const std::vector<uint8_t> &raw_data, uint8_t &response_id) const {
  if (raw_data.size() == 15) {
    response_id = raw_data[12];
    return true;
  }
  if (raw_data.size() == 17 || raw_data.size() == 18 || raw_data.size() == 20) {
    response_id = raw_data[14];
    return true;
  }
  return false;
}

std::string ToshibaClimateUart::payload_to_hex_(const std::vector<uint8_t> &raw_data) const {
  if (raw_data.size() < 8 || raw_data[2] != 0x03) {
    return "";
  }

  const uint8_t payload_len = raw_data[6];
  const size_t payload_start = 7;
  const size_t payload_end = payload_start + payload_len;
  if (raw_data.size() < payload_end + 1) {
    return "";
  }

  std::string out;
  out.reserve(static_cast<size_t>(payload_len) * 2);
  static const char *hex = "0123456789ABCDEF";
  for (size_t i = payload_start; i < payload_end; i++) {
    uint8_t v = raw_data[i];
    out.push_back(hex[(v >> 4) & 0x0F]);
    out.push_back(hex[v & 0x0F]);
  }
  return out;
}

void ToshibaClimateUart::schedule_publish_() {
  uint32_t now = millis();
  this->publish_pending_ = true;
  this->publish_soft_deadline_ms_ = now + PUBLISH_SOFT_DEBOUNCE_MS;
  if (this->publish_hard_deadline_ms_ == 0 || now > this->publish_hard_deadline_ms_) {
    this->publish_hard_deadline_ms_ = now + PUBLISH_HARD_TIMEOUT_MS;
  }
}

bool ToshibaClimateUart::has_pending_non_debug_data_requests_() const {
  if (this->active_request_is_data_ && !this->active_request_is_debug_) {
    return true;
  }
  return std::any_of(this->command_queue_.begin(), this->command_queue_.end(), [](const ToshibaCommand &cmd) {
    return cmd.is_data_request && !cmd.is_debug_request;
  });
}

void ToshibaClimateUart::flush_pending_publish_if_ready_() {
  if (!this->publish_pending_) {
    return;
  }
  uint32_t now = millis();
  const bool hard_timeout = this->publish_hard_deadline_ms_ != 0 && now >= this->publish_hard_deadline_ms_;
  const bool soft_ready = now >= this->publish_soft_deadline_ms_ &&
                          !this->has_pending_non_debug_data_requests_() && this->rx_message_.empty();
  if (!hard_timeout && !soft_ready) {
    return;
  }

  this->publish_state();
  this->publish_pending_ = false;
  this->publish_soft_deadline_ms_ = 0;
  this->publish_hard_deadline_ms_ = 0;
}

void ToshibaWifiLedSwitch::setup() {
  auto restored_state = this->get_initial_state_with_restore_mode();
  if (restored_state.has_value()) {
    if (restored_state.value()) {
      this->turn_on();
    } else {
      this->turn_off();
    }
  }
}

void ToshibaWifiLedSwitch::write_state(bool state) { parent_->on_set_wifi_led(state); }

void ToshibaDebugSwitch::setup() {
  auto restored_state = this->get_initial_state_with_restore_mode();
  if (restored_state.has_value()) {
    if (restored_state.value()) {
      this->turn_on();
    } else {
      this->turn_off();
    }
  } else {
    this->turn_off();
  }
}

void ToshibaDebugSwitch::write_state(bool state) { parent_->on_set_debug(state); }

/**
 * Scan all statuses from 128 to 255 in order to find unknown features.
 */
void ToshibaClimateUart::scan() {
  ESP_LOGI(TAG, "Scan started.");
  for (uint8_t i = 128; i < 255; i++) {
    this->requestData(static_cast<ToshibaCommandType>(i));
  }
}

}  // namespace toshiba_suzumi
}  // namespace esphome
