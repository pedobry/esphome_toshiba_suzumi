#pragma once

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/preferences.h"
#include "toshiba_climate_mode.h"

namespace esphome {
namespace time {
class RealTimeClock;
}  // namespace time
namespace toshiba_suzumi {

static const char *const TAG = "ToshibaClimateUart";
// default max temp for units
static const uint8_t MAX_TEMP = 30;
// default min temp for units without 8° heating mode
static const uint8_t MIN_TEMP_STANDARD = 17;
// "8°C operation" (merit A = 4): heating at 5..16 °C. Seiya-type units limit it
// to 5..13 °C, Haori-type units use 5..16 °C and enter it on their own below
// 17 °C. On the wire the setpoint (0xB3) is the temperature + 16 (21..32).
static const uint8_t SPECIAL_TEMP_OFFSET = 16;
static const uint8_t SPECIAL_MODE_EIGHT_DEG_MIN_TEMP = 5;
static const uint8_t SPECIAL_MODE_EIGHT_DEG_MAX_TEMP = MIN_TEMP_STANDARD - 1;
static const uint8_t SPECIAL_MODE_EIGHT_DEG_DEF_TEMP = 8;
static const uint8_t NORMAL_MODE_DEF_TEMP = 20;

// Init sequence (FT/CN handshake, then identity queries), captured on the wire.
// HANDSHAKE_5 is "request model/unit info" (FT=0x0002); the AC replies with a
// long frame (DL > 0xA7) that carries the indoor unit (FCU) model and serial.
// The outdoor unit (CDU) model is not part of it; the unit sends it in an
// unsolicited EPC 0xE0 frame (see MODEL_INFO_* below).
// The frames themselves are in toshiba_climate.cpp (HANDSHAKE_0..5).

// Unit-info tail inside handshake identity reply (FT=0x0002, CN=0x80, DL>0xA7).
// Confirmed by live capture (e.g. RAS-13PKVSG):
//   abs[171..174] = FD[0xA4..0xA7] unit identification bytes
//   abs[180]      = model string length
//   abs[181..]    = model ASCII (length-prefixed), then serial ASCII
static constexpr size_t UNIT_INFO_MIN_LEN = 182;
static constexpr size_t UNIT_INFO_FD_VER0 = 7 + 0xA4;  // absolute 171
static constexpr size_t UNIT_INFO_MODEL_LEN_IDX = 180;
static constexpr size_t UNIT_INFO_MODEL_DATA = 181;
static constexpr size_t UNIT_INFO_MODEL_MAX = 32;
static constexpr size_t UNIT_INFO_SERIAL_MAX = 24;

// Model-info notify (EPC 0xE0): two 50-byte records, FCU at EDT[0], CDU at
// EDT[50]. Each holds four ASCII fields: model, serial, firmware, EEPROM info.
static constexpr size_t MODEL_INFO_EDT_LEN = 100;
static constexpr size_t MODEL_INFO_RECORD_LEN = 50;
static constexpr size_t MODEL_INFO_MODEL_OFF = 0x00, MODEL_INFO_MODEL_LEN = 20;
static constexpr size_t MODEL_INFO_SERIAL_OFF = 0x15, MODEL_INFO_SERIAL_LEN = 12;
static constexpr size_t MODEL_INFO_FIRMWARE_OFF = 0x22, MODEL_INFO_FIRMWARE_LEN = 8;
static constexpr size_t MODEL_INFO_EEPROM_OFF = 0x2B, MODEL_INFO_EEPROM_LEN = 6;

// Indoor (FCU) or outdoor (CDU) unit info from the 0xE0 notify.
struct UnitModelInfo {
  std::string model;
  std::string serial;
  std::string firmware;
};

// Unit capabilities for automatic entity setup. Bits are persisted in flash,
// so keep the values stable. Sources: replies to Gets / notifies seen on the
// wire, the identity profile and the unit's feature mask (EPC 0xEE).
enum UnitFeature : uint32_t {
  FEATURE_OUTDOOR_TEMP = 1u << 0,   // 0xBE answered
  FEATURE_POWER_SEL = 1u << 1,      // 0x87 answered
  FEATURE_SELF_CLEAN = 1u << 2,     // 0xCB answered
  FEATURE_OFF_TIMER = 1u << 3,      // 0x94 answered
  FEATURE_ERROR_CODE = 1u << 4,     // 0x88 answered
  FEATURE_FCU_IDENTITY = 1u << 5,   // FCU model in the handshake identity
  FEATURE_MODEL_INFO = 1u << 6,     // 0xE0 notify seen
  FEATURE_IDU_DIAG = 1u << 7,       // 0xE4 seen
  FEATURE_ODU_DIAG = 1u << 8,       // 0xE5 seen
  FEATURE_ENERGY = 1u << 9,         // profile 3, 0xEE byte 2 bit 0
  FEATURE_LOUVER_FIXED = 1u << 10,  // profile 3, 0xEE byte 2 bit 1
  FEATURE_PURE = 1u << 11,          // profile 2/3, 0xEE byte 1 bit 4
  FEATURE_LR_LOUVER = 1u << 12,     // profile 3, 0xEE byte 1 bit 6
  FEATURE_ECO_HIPOWER = 1u << 13,   // profile 2/3
  FEATURE_EIGHT_DEG = 1u << 14,     // profile 2/3, 0xEE byte 1 bit 2
  FEATURE_FIREPLACE = 1u << 15,     // profile 2/3, 0xEE byte 1 bit 3
  FEATURE_SILENT = 1u << 16,        // profile 2/3, 0xEE byte 1 bit 5
  FEATURE_FLOOR = 1u << 17,         // profile 2/3, 0xEE byte 1 bit 7
  FEATURE_COOL_ONLY = 1u << 18,     // 0xEE byte 1 bits 1..0 = 10
  FEATURE_HEAT_ONLY = 1u << 19,     // 0xEE byte 1 bits 1..0 = 01
};

// Persisted detection result (flash / RTC preferences).
struct UnitCapabilities {
  uint32_t magic;
  uint32_t features;
  uint32_t unit_hash;  // FCU model + serial, 0 when unknown
  uint8_t profile;
  uint8_t merit_mask[2];
  uint8_t reserved;
} __attribute__((packed));

struct ToshibaCommand {
  ToshibaCommandType cmd;
  std::vector<uint8_t> payload;
  int delay;
  // Set on the periodic link check. The reply (or its absence) updates the fail counter.
  bool keepalive = false;
};

class ToshibaClimateUart : public PollingComponent, public climate::Climate, public uart::UARTDevice {
 public:

  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;
  void scan();
  void set_wifi_led(bool enabled);
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_indoor_temp_sensor(sensor::Sensor *indoor_temp_sensor) { indoor_temp_sensor_ = indoor_temp_sensor; }
  void set_outdoor_temp_sensor(sensor::Sensor *outdoor_temp_sensor) { outdoor_temp_sensor_ = outdoor_temp_sensor; }
  void set_cdu_td_temp_sensor(sensor::Sensor *sensor) { cdu_td_temp_sensor_ = sensor; }
  void set_cdu_ts_temp_sensor(sensor::Sensor *sensor) { cdu_ts_temp_sensor_ = sensor; }
  void set_cdu_te_temp_sensor(sensor::Sensor *sensor) { cdu_te_temp_sensor_ = sensor; }
  void set_cdu_load_sensor(sensor::Sensor *sensor) { cdu_load_sensor_ = sensor; }
  void set_cdu_iac_sensor(sensor::Sensor *sensor) { cdu_iac_sensor_ = sensor; }
  void set_fcu_tc_temp_sensor(sensor::Sensor *sensor) { fcu_tc_temp_sensor_ = sensor; }
  void set_fcu_tcj_temp_sensor(sensor::Sensor *sensor) { fcu_tcj_temp_sensor_ = sensor; }
  void set_fcu_fan_rpm_sensor(sensor::Sensor *sensor) { fcu_fan_rpm_sensor_ = sensor; }
  void set_time(time::RealTimeClock *time) { time_ = time; }
  void set_energy_sensor(sensor::Sensor *sensor) { energy_sensor_ = sensor; }
  void set_power_sensor(sensor::Sensor *sensor) { power_sensor_ = sensor; }
  void set_pwr_select(select::Select *pws_select) { pwr_select_ = pws_select; }
  void set_vertical_air_direction_select(select::Select *vertical_air_direction_select) {
    vertical_air_direction_select_ = vertical_air_direction_select;
  }
  void set_self_clean_sensor(binary_sensor::BinarySensor *self_clean_sensor) { self_clean_sensor_ = self_clean_sensor; }
  void set_off_timer_sensor(binary_sensor::BinarySensor *sensor) { off_timer_sensor_ = sensor; }
  void set_off_timer_remaining_sensor(sensor::Sensor *sensor) { off_timer_remaining_sensor_ = sensor; }
  void set_fcu_model_text_sensor(text_sensor::TextSensor *sensor) { fcu_model_text_sensor_ = sensor; }
  void set_fcu_serial_text_sensor(text_sensor::TextSensor *sensor) { fcu_serial_text_sensor_ = sensor; }
  void set_error_code_text_sensor(text_sensor::TextSensor *sensor) { error_code_text_sensor_ = sensor; }
  void set_fcu_firmware_text_sensor(text_sensor::TextSensor *sensor) { fcu_firmware_text_sensor_ = sensor; }
  void set_cdu_model_text_sensor(text_sensor::TextSensor *sensor) { cdu_model_text_sensor_ = sensor; }
  void set_cdu_serial_text_sensor(text_sensor::TextSensor *sensor) { cdu_serial_text_sensor_ = sensor; }
  void set_cdu_firmware_text_sensor(text_sensor::TextSensor *sensor) { cdu_firmware_text_sensor_ = sensor; }
  void set_horizontal_swing(bool enabled) {
    horizontal_swing_ = enabled;
    horizontal_swing_explicit_ = true;
  }
  void disable_heat_mode(bool disabled) {
    heat_mode_disabled_ = disabled;
    heat_mode_explicit_ = true;
  }
  void set_ionizer_switch(switch_::Switch *sw) { ionizer_switch_ = sw; }
  void set_auto_detect(bool enabled) { auto_detect_ = enabled; }
  // Entity created by the component (not listed in YAML); hidden from Home
  // Assistant unless the unit has `feature`.
  void add_auto_entity(EntityBase *entity, uint32_t feature) { auto_entities_.push_back({entity, feature}); }
  // Forget the detected capabilities and detect again (restarts the device).
  void reset_capabilities();
  void set_ionizer(bool on);
  void disable_wifi_led(bool disabled) { wifi_led_disabled_ = disabled; }
  void set_supported_presets(const std::vector<const char *> &presets) { supported_presets_ = presets; }
  void set_min_temp(uint8_t min_temp) {
    min_temp_ = min_temp;
    min_temp_explicit_ = true;
  }
  void set_time_sync_interval(uint32_t interval) { time_sync_interval_ = interval; }

 protected:
  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall &call) override;

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

 private:
  std::vector<uint8_t> rx_message_;
  std::vector<uint8_t> rx_frame_;  // complete frame being parsed (swapped with rx_message_)
  bool climate_changed_ = false;    // set by set_custom_*_() while parsing a frame
  std::vector<ToshibaCommand> command_queue_;
  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  STATE power_state_ = STATE::OFF;
  // True while the unit is running its post-shutdown self-cleaning cycle.
  bool self_clean_running_ = false;
  // Last raw values of 0xB0 / 0xA0 / 0xB3 / 0xF7 seen on the wire (Get replies,
  // notifies, and our own Sets). They fill the fields of the 0xF8 bundle that
  // a change does not touch. 0xB3 is kept raw because its
  // meaning depends on 0xF7 (+16 in 8 °C mode) and the two can arrive in any order.
  optional<uint8_t> raw_mode_;
  optional<uint8_t> raw_fan_;
  optional<uint8_t> raw_target_temp_;
  optional<uint8_t> raw_merit_;
  // Set when the unit rejects 0xF8; later changes then use single Sets.
  bool bundle_rejected_ = false;
  select::Select *pwr_select_ = nullptr;
  select::Select *vertical_air_direction_select_ = nullptr;
  binary_sensor::BinarySensor *self_clean_sensor_ = nullptr;
  binary_sensor::BinarySensor *off_timer_sensor_ = nullptr;
  sensor::Sensor *off_timer_remaining_sensor_ = nullptr;
  bool off_timer_active_ = false;
  sensor::Sensor *indoor_temp_sensor_ = nullptr;
  sensor::Sensor *outdoor_temp_sensor_ = nullptr;
  sensor::Sensor *cdu_td_temp_sensor_ = nullptr;
  sensor::Sensor *cdu_ts_temp_sensor_ = nullptr;
  sensor::Sensor *cdu_te_temp_sensor_ = nullptr;
  sensor::Sensor *cdu_load_sensor_ = nullptr;
  sensor::Sensor *cdu_iac_sensor_ = nullptr;
  sensor::Sensor *fcu_tc_temp_sensor_ = nullptr;
  sensor::Sensor *fcu_tcj_temp_sensor_ = nullptr;
  sensor::Sensor *fcu_fan_rpm_sensor_ = nullptr;
  time::RealTimeClock *time_ = nullptr;
  sensor::Sensor *energy_sensor_ = nullptr;
  sensor::Sensor *power_sensor_ = nullptr;
  text_sensor::TextSensor *fcu_model_text_sensor_ = nullptr;
  text_sensor::TextSensor *fcu_serial_text_sensor_ = nullptr;
  text_sensor::TextSensor *error_code_text_sensor_ = nullptr;
  text_sensor::TextSensor *fcu_firmware_text_sensor_ = nullptr;
  text_sensor::TextSensor *cdu_model_text_sensor_ = nullptr;
  text_sensor::TextSensor *cdu_serial_text_sensor_ = nullptr;
  text_sensor::TextSensor *cdu_firmware_text_sensor_ = nullptr;
  bool horizontal_swing_ = false;
  uint8_t min_temp_ = 17; // default min temp for units without 8° heating mode
  bool heat_mode_disabled_ = false;
  bool heat_only_ = false;
  bool horizontal_swing_explicit_ = false;
  bool heat_mode_explicit_ = false;
  bool min_temp_explicit_ = false;
  bool presets_explicit_ = false;
  switch_::Switch *ionizer_switch_ = nullptr;

  // Automatic capability detection.
  struct AutoEntity {
    EntityBase *entity;
    uint32_t feature;
  };
  bool auto_detect_ = false;
  std::vector<AutoEntity> auto_entities_;
  ESPPreferenceObject caps_pref_;
  UnitCapabilities caps_{};       // applied at boot (from flash)
  bool caps_loaded_ = false;
  uint32_t detected_features_ = 0;  // seen during this boot
  bool caps_evaluated_ = false;
  bool caps_reboot_pending_ = false;
  bool wifi_led_disabled_ = false;
  std::vector<const char*> supported_presets_;
  uint32_t last_time_sync_ = 0;  // millis() when the last sync frame was queued
  uint32_t last_energy_sync_ = 0;
  uint32_t last_total_daily_energy_ = 0;
  uint32_t last_energy_update_ms_ = 0;
  bool time_synced_ = false;
  // Clock sync format. Most units accept the weekly-schedule frame 0x99; some
  // (e.g. Carrier 40WHP / Toshiba Haori) ignore it and only accept 0x98. The
  // format that got an ACK is kept for later syncs.
  bool time_sync_use_clock_epc_ = false;
  bool time_sync_pending_ = false;  // frame sent, waiting for the ACK
  uint8_t time_sync_attempts_ = 0;  // unacknowledged attempts since the last ACK
  uint32_t next_time_sync_at_ = 0;  // millis() of the next sync attempt
  uint32_t time_sync_interval_{86400000};
  std::string fcu_model_;
  std::string fcu_serial_;
  // From the 0xE0 notify; empty until the unit sends it.
  UnitModelInfo fcu_info_;
  UnitModelInfo cdu_info_;
  // Unit profile and feature level, derived from the identification bytes
  // FD[0xA4..0xA7] (see apply_unit_capabilities_()). Diagnostics 0xE4/0xE5 are
  // polled on profile 3 only.
  uint8_t unit_profile_ = 0;
  uint8_t cap_level_ = 0;
  uint8_t unit_id_bytes_[4] = {0, 0, 0, 0};  // raw FD[0xA4..0xA7], for dump_config()
  uint8_t feature_mask_[2] = {0, 0};  // EPC 0xEE, for dump_config()
  bool feature_mask_known_ = false;
  bool poll_diagnostics_ = false;
  bool status_init_started_ = false;
  // Keepalive: Get 0x80, or Get 0xCA on profile 3 with feature level >= 3.
  // Five replies missing in a row restart the handshake.
  bool keepalive_pending_ = false;
  uint8_t keepalive_fails_ = 0;
  uint8_t keepalive_fn_ = 0;
  uint8_t keepalive_epc_ = 0;
  uint32_t keepalive_sent_at_ = 0;

  void enqueue_command_(const ToshibaCommand &command);
  void send_to_uart(const ToshibaCommand &command);
  void start_handshake();
  void parseResponse(const std::vector<uint8_t> &rawData);
  void parse_unit_identity_(const std::vector<uint8_t> &rawData);
  static std::string extract_unit_field_(const uint8_t *data, size_t size, size_t offset, size_t max_len);
  static UnitModelInfo parse_model_info_record_(const uint8_t *record);
  void handle_model_info_(const uint8_t *edt, size_t len);
  void requestData(ToshibaCommandType cmd);
  void process_command_queue_();
  void sendCmd(ToshibaCommandType cmd, uint8_t value);
  void send_bundle_(uint8_t mode, uint8_t temp, uint8_t fan, uint8_t merit);
  bool use_bundle_() const;
  bool eight_deg_active_() const;
  void update_target_temperature_();
  void publish_special_mode_preset_(uint8_t merit);
  static std::vector<uint8_t> make_request_fd_(uint8_t epc, const std::vector<uint8_t> &edt);
  std::vector<uint8_t> build_frame_(uint8_t cn, uint8_t fn, const std::vector<uint8_t> &fd);
  std::vector<uint8_t> encode_frame_(const std::vector<uint8_t> &fd);
  void send_notify_ack_(uint8_t fn);
  void apply_unit_capabilities_(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t level);
  void start_status_reads_();
  // probe = true: read for every configured sensor (used at connect so that
  // detection can see what the unit answers); false: skip hidden ones.
  void request_diagnostic_reads_(bool probe);
  void request_keepalive_();
  bool keepalive_reply_matches_(uint8_t cn, uint8_t fn, uint8_t epc) const;
  // Returns true when this result restarted the handshake.
  bool note_keepalive_result_(bool ok);
  void recover_link_();
  bool has_idu_diagnostics_(bool probe) const;
  bool has_odu_diagnostics_(bool probe) const;
  // Entity worth polling for: configured, and not hidden by detection stored
  // in flash. Before anything is stored, every configured entity is polled.
  bool poll_enabled_(const EntityBase *entity, bool probe = false) const {
    if (entity == nullptr)
      return false;
    return probe || !this->caps_loaded_ || !this->is_hidden_auto_entity_(entity);
  }
  void getInitData();
  void handle_rx_byte_(uint8_t c);
  bool validate_message_();
  void set_self_clean_running_(bool running);
  void handle_fault_status_(uint8_t fault);
  void publish_error_code_(const uint8_t *edt, size_t len);
  bool has_off_timer_sensors_() const { return off_timer_sensor_ != nullptr || off_timer_remaining_sensor_ != nullptr; }
  void on_set_pwr_level(const std::string &value);
  void on_set_vertical_air_direction(const std::string &value);
  void publish_vertical_air_direction_(SWING swing_mode);
  void configure_supported_custom_modes_();
  void load_capabilities_();
  void apply_capabilities_();
  uint32_t derived_features_() const;
  uint32_t unit_hash_() const;
  void note_feature_(uint32_t features);
  void evaluate_capabilities_();
  void store_capabilities_and_restart_(uint32_t features, const char *reason);
  bool has_feature_(uint32_t feature) const { return (this->caps_.features & feature) != 0; }
  bool is_hidden_auto_entity_(const EntityBase *entity) const;
  bool energy_enabled_() const {
    return (this->energy_sensor_ != nullptr && !this->is_hidden_auto_entity_(this->energy_sensor_)) ||
           (this->power_sensor_ != nullptr && !this->is_hidden_auto_entity_(this->power_sensor_));
  }
#ifdef USE_TIME
  void check_time_sync_(uint32_t now);
  void sync_time_();
  void on_time_sync_result_(uint8_t epc, bool ok);
#endif
  void sync_energy_();
  void estimate_wattage_(uint32_t current_energy);

  friend class ToshibaPwrModeSelect;
  friend class ToshibaVerticalAirDirectionSelect;
  friend class ToshibaIonizerSwitch;
};

class ToshibaPwrModeSelect : public select::Select, public esphome::Parented<ToshibaClimateUart> {
 protected:
  virtual void control(const std::string &value) override;
};

class ToshibaIonizerSwitch : public switch_::Switch, public esphome::Parented<ToshibaClimateUart> {
 protected:
  void write_state(bool state) override { parent_->set_ionizer(state); }
};

class ToshibaVerticalAirDirectionSelect : public select::Select, public esphome::Parented<ToshibaClimateUart> {
 protected:
  virtual void control(const std::string &value) override;
};

}  // namespace toshiba_suzumi
}  // namespace esphome
