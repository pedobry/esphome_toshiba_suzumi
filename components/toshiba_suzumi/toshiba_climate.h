#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "toshiba_climate_mode.h"
#include <array>
#include <string>

namespace esphome {
namespace toshiba_suzumi {

static const char *const TAG = "ToshibaClimateUart";
// default max temp for units
static const uint8_t MAX_TEMP = 30;
// default min temp for units without 8° heating mode
static const uint8_t MIN_TEMP_STANDARD = 17;
static const uint8_t SPECIAL_TEMP_OFFSET = 16;
static const uint8_t SPECIAL_MODE_EIGHT_DEG_MIN_TEMP = 5;
static const uint8_t SPECIAL_MODE_EIGHT_DEG_MAX_TEMP = 13;
static const uint8_t SPECIAL_MODE_EIGHT_DEG_DEF_TEMP = 8;
static const uint8_t NORMAL_MODE_DEF_TEMP = 20;

static const std::vector<uint8_t> HANDSHAKE[6] = {
    {2, 255, 255, 0, 0, 0, 0, 2},       {2, 255, 255, 1, 0, 0, 1, 2, 254}, {2, 0, 0, 0, 0, 0, 2, 2, 2, 250},
    {2, 0, 1, 129, 1, 0, 2, 0, 0, 123}, {2, 0, 1, 2, 0, 0, 2, 0, 0, 254},  {2, 0, 2, 0, 0, 0, 0, 254},
};

static const std::vector<uint8_t> AFTER_HANDSHAKE[2] = {
    {2, 0, 2, 1, 0, 0, 2, 0, 0, 251},
    {2, 0, 2, 2, 0, 0, 2, 0, 0, 250},
};

struct ToshibaCommand {
  ToshibaCommandType cmd;
  std::vector<uint8_t> payload;
  int delay = 0;
  bool is_data_request = false;
  bool is_debug_request = false;
  uint8_t request_id = 0;
};

class ToshibaClimateUart : public PollingComponent, public climate::Climate, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;
  void scan();
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_outdoor_temp_sensor(sensor::Sensor *outdoor_temp_sensor) { outdoor_temp_sensor_ = outdoor_temp_sensor; }
  void set_pwr_select(select::Select *pws_select) { pwr_select_ = pws_select; }
  void set_horizontal_swing(bool enabled) { horizontal_swing_ = enabled; }
  void set_wifi_led_switch(switch_::Switch *wifi_led_switch) { wifi_led_switch_ = wifi_led_switch; }
  void set_debug_switch(switch_::Switch *debug_switch) { debug_switch_ = debug_switch; }
  void set_debug_change_sensor(text_sensor::TextSensor *sensor) { debug_change_sensor_ = sensor; }
  void set_debug_poll_interval_ms(uint32_t interval_ms) { debug_poll_interval_ms_ = interval_ms; }
  void set_debug_batch_size(uint16_t batch_size) { debug_batch_size_ = batch_size; }
  void set_debug_initial_range(uint8_t from_id, uint8_t to_id) {
    debug_initial_from_ = from_id;
    debug_initial_to_ = to_id;
  }
  void set_supported_presets(const std::vector<const char*> &presets) { supported_presets_ = presets; }
  void set_min_temp(uint8_t min_temp) { min_temp_ = min_temp; }

 protected:
  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall &call) override;

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

 private:
  std::vector<uint8_t> rx_message_;
  std::vector<ToshibaCommand> command_queue_;
  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  STATE power_state_ = STATE::OFF;
  optional<SPECIAL_MODE> special_mode_ = SPECIAL_MODE::STANDARD;
  select::Select *pwr_select_ = nullptr;
  sensor::Sensor *outdoor_temp_sensor_ = nullptr;
  bool horizontal_swing_ = false;
  uint8_t min_temp_ = 17; // default min temp for units without 8° heating mode
  switch_::Switch *wifi_led_switch_ = nullptr;
  switch_::Switch *debug_switch_ = nullptr;
  std::vector<const char*> supported_presets_;
  bool debug_enabled_ = false;
  uint32_t last_debug_poll_timestamp_ = 0;
  uint32_t debug_poll_interval_ms_ = 30000;
  uint16_t debug_batch_size_ = 1;
  uint8_t debug_initial_from_ = 128;
  uint8_t debug_initial_to_ = 254;
  bool debug_initial_scan_in_progress_ = false;
  uint16_t debug_initial_next_id_ = 128;
  bool debug_poll_run_in_progress_ = false;
  size_t debug_poll_cursor_ = 0;
  bool active_request_is_data_ = false;
  bool active_request_is_debug_ = false;
  uint8_t active_request_id_ = 0;
  std::vector<uint8_t> debug_discovered_ids_;
  std::array<bool, 256> debug_id_discovered_{};
  std::array<std::string, 256> debug_last_payloads_{};
  text_sensor::TextSensor *debug_change_sensor_ = nullptr;

  void enqueue_command_(const ToshibaCommand &command);
  void send_to_uart(const ToshibaCommand command);
  void start_handshake();
  void parseResponse(std::vector<uint8_t> rawData);
  void requestData(ToshibaCommandType cmd, bool is_debug_request = false);
  void process_command_queue_();
  void sendCmd(ToshibaCommandType cmd, uint8_t value);
  void getInitData();
  void handle_rx_byte_(uint8_t c);
  bool validate_message_();
  void on_set_pwr_level(const std::string &value);
  void on_set_wifi_led(bool enabled);
  void on_set_debug(bool enabled);
  void start_debug_scan_();
  void run_debug_initial_scan_step_();
  void start_debug_poll_run_();
  void run_debug_poll_step_();
  void clear_queued_debug_requests_();
  void handle_debug_response_(const std::vector<uint8_t> &raw_data);
  bool extract_response_id_(const std::vector<uint8_t> &raw_data, uint8_t &response_id) const;
  std::string payload_to_hex_(const std::vector<uint8_t> &raw_data) const;

  friend class ToshibaPwrModeSelect;
  friend class ToshibaWifiLedSwitch;
  friend class ToshibaDebugSwitch;
};

class ToshibaPwrModeSelect : public select::Select, public esphome::Parented<ToshibaClimateUart> {
 protected:
  virtual void control(const std::string &value) override;
};

class ToshibaWifiLedSwitch : public switch_::Switch, public Component, public esphome::Parented<ToshibaClimateUart> {
 protected:
  float get_setup_priority() const override { return setup_priority::LATE - 1.0f; }
  void setup() override;
  void write_state(bool state) override;
};

class ToshibaDebugSwitch : public switch_::Switch, public Component, public esphome::Parented<ToshibaClimateUart> {
 protected:
  float get_setup_priority() const override { return setup_priority::LATE - 1.0f; }
  void setup() override;
  void write_state(bool state) override;
};

}  // namespace toshiba_suzumi
}  // namespace esphome
