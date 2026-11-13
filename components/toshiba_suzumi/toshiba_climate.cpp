#include <algorithm>
#include <cmath>
#include <cstdio>
#include "toshiba_climate.h"
#include "toshiba_climate_mode.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/core/macros.h"
#include "esphome/core/version.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome {
namespace toshiba_suzumi {

using namespace esphome::climate;

static const int RECEIVE_TIMEOUT = 200;
static const int COMMAND_DELAY = 100;
// How long to wait for the keepalive Get reply.
static const uint32_t KEEPALIVE_REPLY_TIMEOUT = 9000;
// Counter is compared with 4; the fifth missing reply restarts the link.
static const uint8_t KEEPALIVE_FAIL_LIMIT = 4;
static const int KEEPALIVE_RECOVERY_DELAY = 1000;

/**
 * FCC checksum: two's complement of the sum of every byte except STX.
 * `length` is the index of the FCC byte (i.e. total frame size - 1).
 */
namespace {

// Handshake frames, sent in this order with a 2 s pause before the last two.
constexpr uint8_t HANDSHAKE_0[] = {2, 255, 255, 0, 0, 0, 0, 2};
constexpr uint8_t HANDSHAKE_1[] = {2, 255, 255, 1, 0, 0, 1, 2, 254};
constexpr uint8_t HANDSHAKE_2[] = {2, 0, 0, 0, 0, 0, 2, 2, 2, 250};
constexpr uint8_t HANDSHAKE_3[] = {2, 0, 1, 129, 1, 0, 2, 0, 0, 123};
constexpr uint8_t HANDSHAKE_4[] = {2, 0, 1, 2, 0, 0, 2, 0, 0, 254};
constexpr uint8_t HANDSHAKE_5[] = {2, 0, 2, 0, 0, 0, 0, 254};  // request unit identity
constexpr uint8_t AFTER_HANDSHAKE_0[] = {2, 0, 2, 1, 0, 0, 2, 0, 0, 251};
constexpr uint8_t AFTER_HANDSHAKE_1[] = {2, 0, 2, 2, 0, 0, 2, 0, 0, 250};

template<size_t N> ToshibaCommand handshake_frame(const uint8_t (&frame)[N]) {
  return ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = std::vector<uint8_t>(frame, frame + N)};
}

uint8_t checksum(const std::vector<uint8_t> &data, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 1; i < length; i++) {
    sum += data[i];
  }
  return static_cast<uint8_t>(0 - sum);
}

}  // namespace

/**
 * Extract a NUL/space-padded ASCII field from the unit-info string table.
 */
std::string ToshibaClimateUart::extract_unit_field_(const uint8_t *data, size_t size, size_t offset,
                                                     size_t max_len) {
  std::string out;
  for (size_t i = 0; i < max_len && offset + i < size; i++) {
    uint8_t c = data[offset + i];
    if (c == 0x00)
      break;
    if (c < 0x20 || c > 0x7E)
      break;
    out.push_back(static_cast<char>(c));
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}


/**
 * Send the command to UART interface.
 */
void ToshibaClimateUart::send_to_uart(const ToshibaCommand &command) {
  this->last_command_timestamp_ = millis();
  ESP_LOGV(TAG, "Sending: [%s]", format_hex_pretty(command.payload).c_str());
  this->write_array(command.payload);
}

/**
 * Send starting handshake to initialize communication with the unit.
 */
void ToshibaClimateUart::start_handshake() {
  ESP_LOGCONFIG(TAG, "Sending handshake...");
  this->enqueue_command_(handshake_frame(HANDSHAKE_0));
  this->enqueue_command_(handshake_frame(HANDSHAKE_1));
  this->enqueue_command_(handshake_frame(HANDSHAKE_2));
  this->enqueue_command_(handshake_frame(HANDSHAKE_3));
  this->enqueue_command_(handshake_frame(HANDSHAKE_4));
  this->enqueue_command_(handshake_frame(HANDSHAKE_5));
  this->enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::DELAY, .delay = 2000});
  this->enqueue_command_(handshake_frame(AFTER_HANDSHAKE_0));
  this->enqueue_command_(handshake_frame(AFTER_HANDSHAKE_1));
}

/**
 * Handle data in RX buffer, validate message for content and checksum.
 * All frames use STX|FT|CN|FN|DL|FD|FCC with 16-bit big-endian DL (payload
 * length). Incomplete garbage is dropped via RECEIVE timeout.
 */
bool ToshibaClimateUart::validate_message_() {
  size_t at = this->rx_message_.size() - 1;
  auto *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  // Byte 0: HEADER (always 0x02)
  if (at == 0)
    return new_byte == 0x02;

  // Need STX..DL_lo before the frame length is known.
  if (at < 6)
    return true;

  // last FCC index = 7 (header bytes) + DL - 1? Header is 7 bytes (0..6), then
  // DL payload bytes, then FCC. Index of FCC = 7 + DL.
  uint16_t dl = (static_cast<uint16_t>(data[5]) << 8) | data[6];
  size_t last_index = 7 + static_cast<size_t>(dl);

  // Guard against corrupt DL that would grow the buffer without bound.
  if (last_index > 512) {
    ESP_LOGW(TAG, "Received frame with oversized DL=%u, dropping", dl);
    return false;
  }

  // wait until all data is read
  if (at < last_index)
    return true;

  // last byte: CHECKSUM
  uint8_t rx_checksum = new_byte;
  uint8_t calc_checksum = checksum(this->rx_message_, at);

  if (rx_checksum != calc_checksum) {
    ESP_LOGW(TAG, "Received invalid message checksum %02X!=%02X len=%u DATA=[%s]", rx_checksum, calc_checksum,
             static_cast<unsigned>(at + 1), format_hex_pretty(data, at + 1).c_str());
    return false;
  }

  // valid message (trim to exact frame length in case of extras)
  this->rx_message_.resize(at + 1);
  ESP_LOGV(TAG, "Received: DATA=[%s]", format_hex_pretty(this->rx_message_).c_str());
  // Hand the frame over to a second buffer (no copy): parsing may queue and
  // send commands, which must not see or clear the frame being parsed.
  this->rx_frame_.swap(this->rx_message_);
  this->rx_message_.clear();
  this->parseResponse(this->rx_frame_);

  // return false to reset rx buffer
  return false;
}

void ToshibaClimateUart::enqueue_command_(const ToshibaCommand &command) {
  this->command_queue_.push_back(command);
  this->process_command_queue_();
}

/**
 * Build a property frame:
 *   STX | FT=0x0003 | CN | FN | DL | FD | FCC
 */
std::vector<uint8_t> ToshibaClimateUart::build_frame_(uint8_t cn, uint8_t fn, const std::vector<uint8_t> &fd) {
  const uint16_t dl = static_cast<uint16_t>(fd.size());
  std::vector<uint8_t> frame;
  frame.reserve(8 + fd.size());
  frame.push_back(FRAME_STX);
  frame.push_back(static_cast<uint8_t>((FRAME_FT_PROPERTY >> 8) & 0xFF));
  frame.push_back(static_cast<uint8_t>(FRAME_FT_PROPERTY & 0xFF));
  frame.push_back(cn);
  frame.push_back(fn);
  frame.push_back(static_cast<uint8_t>((dl >> 8) & 0xFF));
  frame.push_back(static_cast<uint8_t>(dl & 0xFF));
  frame.insert(frame.end(), fd.begin(), fd.end());
  frame.push_back(checksum(frame, frame.size()));
  return frame;
}

/**
 * Request frame (CN=0x10). FN is always 0x00; the unit
 * echoes it in the reply, and replies are matched by EPC.
 */
std::vector<uint8_t> ToshibaClimateUart::encode_frame_(const std::vector<uint8_t> &fd) {
  return this->build_frame_(FRAME_CN_PROPERTY, 0x00, fd);
}

/**
 * ACK an unsolicited status frame (CN=0x11). The unit's FN is echoed unchanged.
 * Payload is the fixed reply 00 00 01 30 01 (status OK + EOJ), captured on the
 * wire.
 * Queued at the front so it is not stuck behind a pending Get/Set.
 */
void ToshibaClimateUart::send_notify_ack_(uint8_t fn) {
  const std::vector<uint8_t> fd = {0x00, 0x00, 0x01, 0x30, 0x01};
  auto payload = this->build_frame_(FRAME_CN_NOTIFY_ACK, fn, fd);
  ESP_LOGD(TAG, "Sending notify ACK FN=0x%02X", fn);
  this->command_queue_.insert(this->command_queue_.begin(),
                              ToshibaCommand{.cmd = ToshibaCommandType::HANDSHAKE, .payload = std::move(payload)});
}

namespace {

/**
 * One property record inside an FT=0x0003 payload.
 */
struct PropertyRecord {
  uint8_t epc;
  const uint8_t *edt;
  size_t edt_len;
};

/**
 * Locate the EPC/EDT inside a property FD. Two layouts exist:
 *
 *   request / notify (CN 0x10 / 0x11):
 *     01 30 01 | LEN(u16) | EPC | EDT...                 (LEN = 1 + EDT size)
 *   reply (CN 0x90, i.e. request CN | 0x80):
 *     01 30 01 | STATUS(u16) | LEN(u16) | EPC | EDT...   (STATUS 00 00 = OK)
 *
 * A reply with a non-zero STATUS means the unit rejected the request; it is
 * reported as `rejected` (with out->epc filled when present) so the caller can
 * treat it as a failed Get/Set.
 * LEN is checked against DL; a LEN larger than the frame only limits the EDT.
 */
bool find_property_(uint8_t cn, const uint8_t *fd, size_t dl, PropertyRecord *out, bool *rejected) {
  *rejected = false;
  if (dl < 6 || fd[0] != 0x01 || fd[1] != 0x30 || fd[2] != 0x01)
    return false;

  size_t len_pos = 3;
  if (cn & 0x80) {
    if (fd[3] != 0x00 || fd[4] != 0x00) {
      *rejected = true;
      out->epc = dl >= 8 ? fd[7] : 0x00;
      return false;
    }
    if (dl < 8)
      return false;
    len_pos = 5;
  }
  const size_t len = (static_cast<size_t>(fd[len_pos]) << 8) | fd[len_pos + 1];
  const size_t epc_pos = len_pos + 2;
  if (len == 0 || epc_pos >= dl)
    return false;

  out->epc = fd[epc_pos];
  out->edt = fd + epc_pos + 1;
  const size_t available = dl - epc_pos - 1;
  out->edt_len = std::min(len - 1, available);
  if (len - 1 > available) {
    ESP_LOGW(TAG, "EPC 0x%02X claims %u EDT bytes, frame has %u", out->epc, static_cast<unsigned>(len - 1),
             static_cast<unsigned>(available));
  }
  return true;
}

}  // namespace

/**
 * Request FD: EOJ 01 30 01 | LEN(u16 BE) = 1 + EDT size | EPC | EDT.
 */
std::vector<uint8_t> ToshibaClimateUart::make_request_fd_(uint8_t epc, const std::vector<uint8_t> &edt) {
  const uint16_t len = static_cast<uint16_t>(1 + edt.size());
  std::vector<uint8_t> fd = {0x01, 0x30, 0x01, static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len & 0xFF), epc};
  fd.insert(fd.end(), edt.begin(), edt.end());
  return fd;
}

void ToshibaClimateUart::sendCmd(ToshibaCommandType cmd, uint8_t value) {
  // One-byte Set: 01 30 01 00 02 <EPC> <VALUE>
  auto payload = this->encode_frame_(make_request_fd_(static_cast<uint8_t>(cmd), {value}));
  ESP_LOGD(TAG, "Sending Set EPC=0x%02X value=0x%02X", static_cast<uint8_t>(cmd), value);
  this->enqueue_command_(ToshibaCommand{.cmd = cmd, .payload = std::move(payload)});
}

void ToshibaClimateUart::requestData(ToshibaCommandType cmd) {
  // Get: 01 30 01 00 01 <EPC> (length 1 = EPC only)
  auto payload = this->encode_frame_(make_request_fd_(static_cast<uint8_t>(cmd), {}));
  for (const auto &queued : this->command_queue_) {
    if (queued.cmd == cmd && !queued.keepalive && queued.payload == payload) {
      ESP_LOGV(TAG, "Get EPC=0x%02X already queued", static_cast<uint8_t>(cmd));
      return;
    }
  }
  ESP_LOGD(TAG, "Requesting Get EPC=0x%02X", static_cast<uint8_t>(cmd));
  this->enqueue_command_(ToshibaCommand{.cmd = cmd, .payload = std::move(payload)});
}

/**
 * 0xF8 bundle Set: 01 30 01 00 05 F8 <mode> <temp> <fan> <merit>.
 * Used for any of these four on units with a feature mask (profile 2/3), so the
 * setpoint and the special mode always reach the unit together.
 */
void ToshibaClimateUart::send_bundle_(uint8_t mode, uint8_t temp, uint8_t fan, uint8_t merit) {
  auto payload = this->encode_frame_(
      make_request_fd_(static_cast<uint8_t>(ToshibaCommandType::BUNDLE), {mode, temp, fan, merit}));
  ESP_LOGD(TAG, "Sending bundle mode=0x%02X temp=%d fan=0x%02X merit=0x%02X", mode, temp, fan, merit);
  this->enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::BUNDLE, .payload = std::move(payload)});
}

/**
 * 0xF8 is used on profile 2/3 units, single Sets on profile 1.
 * The bundle carries all four values, so it is only used once each of them has
 * been read from the unit; until then (or with an unknown profile) single Sets
 * are sent as before.
 */
bool ToshibaClimateUart::use_bundle_() const {
  return (this->unit_profile_ == 2 || this->unit_profile_ == 3) && !this->bundle_rejected_ &&
         this->raw_mode_.has_value() && this->raw_fan_.has_value() && this->raw_target_temp_.has_value() &&
         this->raw_merit_.has_value();
}

bool ToshibaClimateUart::eight_deg_active_() const {
  return this->raw_merit_.has_value() && (this->raw_merit_.value() & 0x0F) == SPECIAL_MODE::EIGHT_DEG;
}

/**
 * Target temperature for Home Assistant from the raw 0xB3 value and the current
 * merit. Called whenever either of them changes, so the order in which the unit
 * reports them (Get replies at boot, notifies after an IR remote change) does
 * not matter. A raw value of 16 or less is taken as-is even in 8 °C mode.
 */
void ToshibaClimateUart::update_target_temperature_() {
  if (!this->raw_target_temp_.has_value())
    return;
  uint8_t value = this->raw_target_temp_.value();
  if (this->eight_deg_active_() && value > SPECIAL_TEMP_OFFSET) {
    value -= SPECIAL_TEMP_OFFSET;
    ESP_LOGD(TAG, "Special mode \"%s\" active, setpoint %d is %d °C", SPECIAL_MODE_EIGHT_DEG,
             this->raw_target_temp_.value(), value);
  }
  this->target_temperature = value;
}

void ToshibaClimateUart::publish_special_mode_preset_(uint8_t merit) {
  auto preset_string = SpecialModeToPreset(static_cast<SPECIAL_MODE>(merit));
  if (std::find(supported_presets_.begin(), supported_presets_.end(), preset_string) == supported_presets_.end()) {
    // Not offered in Home Assistant (e.g. "Standard" left out of the list):
    // show no preset rather than keeping a stale one such as "8 degrees".
    if (!this->supported_presets_.empty())
      this->climate_changed_ |= this->set_preset_(climate::CLIMATE_PRESET_NONE);
    return;
  }
  auto climate_preset = SpecialModeToClimatePreset(static_cast<SPECIAL_MODE>(merit));
  if (climate_preset.has_value()) {
    this->climate_changed_ |= this->set_preset_(climate_preset.value());
  } else {
    // set_custom_preset_() also clears the standard preset.
    this->climate_changed_ |= this->set_custom_preset_(preset_string);
  }
}

/*
 * Automatic capability detection
 * -------------------------------
 * Home Assistant reads the entity list and the climate traits only when it
 * connects, and ESPHome allows hiding entities (set_internal) only during
 * setup(). So the capabilities found on the wire are stored in flash and
 * applied in the next setup(): when they change, the device restarts once.
 *
 * For the same indoor unit (model + serial) features are only ever added, so
 * a reply that got lost does not hide an entity again or cause restart loops.
 */
static const uint32_t CAPS_MAGIC = 0x54534301;  // "TSC" v1
static const uint32_t CAPS_EVALUATE_DELAY = 30000;

uint32_t ToshibaClimateUart::unit_hash_() const {
  if (this->fcu_model_.empty() && this->fcu_serial_.empty())
    return 0;
  return fnv1_hash(this->fcu_model_ + "|" + this->fcu_serial_);
}

void ToshibaClimateUart::load_capabilities_() {
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 7, 0)
  this->caps_pref_ = this->make_entity_preference<UnitCapabilities>(CAPS_MAGIC);
#else
  this->caps_pref_ = global_preferences->make_preference<UnitCapabilities>(this->get_preference_hash() ^ CAPS_MAGIC);
#endif
  UnitCapabilities stored{};
  if (this->caps_pref_.load(&stored) && stored.magic == CAPS_MAGIC) {
    this->caps_ = stored;
    this->caps_loaded_ = true;
  }
}

bool ToshibaClimateUart::is_hidden_auto_entity_(const EntityBase *entity) const {
  if (!this->auto_detect_ || entity == nullptr)
    return false;
  for (const auto &item : this->auto_entities_) {
    if (item.entity == entity)
      return !this->has_feature_(item.feature);
  }
  return false;
}

/**
 * Called from setup(), before Home Assistant can connect. Settings given in
 * YAML always win over the detected ones.
 */
void ToshibaClimateUart::apply_capabilities_() {
  for (const auto &item : this->auto_entities_) {
    item.entity->set_internal(!this->has_feature_(item.feature));
  }

  if (!this->presets_explicit_ && (this->caps_.profile == 2 || this->caps_.profile == 3)) {
    // ECO and Hi POWER on all profile 2/3 units, the other special modes per
    // 0xEE bit. Sleep and Comfort have no bit, so they are offered only when
    // listed in YAML.
    this->supported_presets_ = {SPECIAL_MODE_STANDARD};
    if (this->has_feature_(FEATURE_ECO_HIPOWER)) {
      this->supported_presets_.push_back(SPECIAL_MODE_ECO);
      this->supported_presets_.push_back(SPECIAL_MODE_HI_POWER);
    }
    if (this->has_feature_(FEATURE_EIGHT_DEG))
      this->supported_presets_.push_back(SPECIAL_MODE_EIGHT_DEG);
    if (this->has_feature_(FEATURE_FIREPLACE)) {
      this->supported_presets_.push_back(SPECIAL_MODE_FIREPLACE_1);
      this->supported_presets_.push_back(SPECIAL_MODE_FIREPLACE_2);
    }
    if (this->has_feature_(FEATURE_SILENT)) {
      this->supported_presets_.push_back(SPECIAL_MODE_SILENT_1);
      this->supported_presets_.push_back(SPECIAL_MODE_SILENT_2);
    }
    if (this->has_feature_(FEATURE_FLOOR))
      this->supported_presets_.push_back(SPECIAL_MODE_FLOOR);
  }
  if (!this->min_temp_explicit_ && this->has_feature_(FEATURE_EIGHT_DEG))
    this->min_temp_ = SPECIAL_MODE_EIGHT_DEG_MIN_TEMP;
  if (!this->heat_mode_explicit_) {
    this->heat_mode_disabled_ = this->has_feature_(FEATURE_COOL_ONLY);
    this->heat_only_ = this->has_feature_(FEATURE_HEAT_ONLY);
  }
  if (!this->horizontal_swing_explicit_)
    this->horizontal_swing_ = this->has_feature_(FEATURE_LR_LOUVER);
}

/**
 * Features that follow from the identity profile and the 0xEE feature mask.
 */
uint32_t ToshibaClimateUart::derived_features_() const {
  uint32_t f = 0;
  if (!this->fcu_model_.empty())
    f |= FEATURE_FCU_IDENTITY;
  const uint8_t profile = this->unit_profile_;
  if (!this->feature_mask_known_ || (profile != 2 && profile != 3))
    return f;
  const uint8_t b1 = this->feature_mask_[0];
  const uint8_t b2 = this->feature_mask_[1];
  switch (b1 & 0x03) {
    case 0x02:
      f |= FEATURE_COOL_ONLY;
      break;
    case 0x01:
      f |= FEATURE_HEAT_ONLY;
      break;
    default:
      break;
  }
  f |= FEATURE_ECO_HIPOWER;
  if (b1 & 0x04)
    f |= FEATURE_EIGHT_DEG;
  if (b1 & 0x08)
    f |= FEATURE_FIREPLACE;
  if (b1 & 0x10)
    f |= FEATURE_PURE;
  if (b1 & 0x20)
    f |= FEATURE_SILENT;
  if (b1 & 0x80)
    f |= FEATURE_FLOOR;
  if (profile == 3) {
    if (b1 & 0x40)
      f |= FEATURE_LR_LOUVER;
    if (b2 & 0x01)
      f |= FEATURE_ENERGY;
    if (b2 & 0x02)
      f |= FEATURE_LOUVER_FIXED;
  }
  return f;
}

void ToshibaClimateUart::note_feature_(uint32_t features) {
  if (!this->auto_detect_ || features == 0)
    return;
  this->detected_features_ |= features;
  // Something new after the first evaluation (e.g. a 0xE4/0xE5/0xE0 notify
  // that profile 1/2 units send only now and then): store it right away.
  if (this->caps_evaluated_ && (features & ~this->caps_.features) != 0)
    this->evaluate_capabilities_();
}

void ToshibaClimateUart::evaluate_capabilities_() {
  if (!this->auto_detect_ || this->caps_reboot_pending_)
    return;
  this->caps_evaluated_ = true;
  uint32_t features = this->detected_features_ | this->derived_features_();
  const uint32_t hash = this->unit_hash_();
  const bool same_unit = this->caps_loaded_ && (hash == 0 || this->caps_.unit_hash == 0 || hash == this->caps_.unit_hash);
  if (same_unit)
    features |= this->caps_.features;

  ESP_LOGI(TAG, "Unit capabilities 0x%05X (applied 0x%05X)%s", static_cast<unsigned>(features),
           static_cast<unsigned>(this->caps_.features), same_unit ? "" : ", new unit");
  // Without an identity reply this boot, keep the stored profile.
  const uint8_t profile = this->unit_profile_ != 0 ? this->unit_profile_ : this->caps_.profile;
  if (this->caps_loaded_ && features == this->caps_.features && this->caps_.profile == profile) {
    return;
  }
  this->store_capabilities_and_restart_(features, this->caps_loaded_ ? (same_unit ? "new features found" : "different unit")
                                                                      : "first detection");
}

void ToshibaClimateUart::store_capabilities_and_restart_(uint32_t features, const char *reason) {
  UnitCapabilities caps{};
  caps.magic = CAPS_MAGIC;
  caps.features = features;
  caps.unit_hash = this->unit_hash_();
  if (caps.unit_hash == 0)
    caps.unit_hash = this->caps_.unit_hash;
  caps.profile = this->unit_profile_ != 0 ? this->unit_profile_ : this->caps_.profile;
  caps.merit_mask[0] = this->feature_mask_known_ ? this->feature_mask_[0] : this->caps_.merit_mask[0];
  caps.merit_mask[1] = this->feature_mask_known_ ? this->feature_mask_[1] : this->caps_.merit_mask[1];
  this->caps_pref_.save(&caps);
  global_preferences->sync();
  this->caps_reboot_pending_ = true;
  ESP_LOGW(TAG, "Unit capabilities changed (%s): 0x%05X -> 0x%05X. Restarting to update Home Assistant entities", reason,
           static_cast<unsigned>(this->caps_.features), static_cast<unsigned>(features));
  this->set_timeout("toshiba_caps_reboot", 2000, []() { App.safe_reboot(); });
}

void ToshibaClimateUart::reset_capabilities() {
  if (!this->auto_detect_)
    return;
  UnitCapabilities caps{};
  this->caps_pref_.save(&caps);
  global_preferences->sync();
  ESP_LOGW(TAG, "Unit capabilities cleared, restarting to detect them again");
  this->caps_reboot_pending_ = true;
  this->set_timeout("toshiba_caps_reboot", 1000, []() { App.safe_reboot(); });
}

/**
 * Ionizer / air purifier (EPC 0xC7): 0x18 on, 0x10 off.
 */
void ToshibaClimateUart::set_ionizer(bool on) {
  ESP_LOGD(TAG, "Setting ionizer %s", on ? "ON" : "OFF");
  this->sendCmd(ToshibaCommandType::AIR_PURIFIER, on ? 0x18 : 0x10);
  if (this->ionizer_switch_ != nullptr)
    this->ionizer_switch_->publish_state(on);
}

bool ToshibaClimateUart::has_idu_diagnostics_(bool probe) const {
  return this->poll_enabled_(this->fcu_tc_temp_sensor_, probe) || this->poll_enabled_(this->fcu_tcj_temp_sensor_, probe) ||
         this->poll_enabled_(this->fcu_fan_rpm_sensor_, probe);
}

bool ToshibaClimateUart::has_odu_diagnostics_(bool probe) const {
  return this->poll_enabled_(this->cdu_td_temp_sensor_, probe) || this->poll_enabled_(this->cdu_ts_temp_sensor_, probe) ||
         this->poll_enabled_(this->cdu_te_temp_sensor_, probe) || this->poll_enabled_(this->cdu_load_sensor_, probe) ||
         this->poll_enabled_(this->cdu_iac_sensor_, probe);
}

/**
 * Profile from the identity bytes:
 *   FD[0xA4] != 0                        -> 1
 *   FD[0xA4] == 0, A5 == 1, A6 == 1      -> 3
 *   FD[0xA4] == 0 otherwise              -> 2
 * 0xE4/0xE5 are polled only for profile 3. Profiles 1 and 2 leave those to
 * unsolicited status from the unit. FD[0xA7] is the profile-3 feature level.
 */
void ToshibaClimateUart::apply_unit_capabilities_(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t level) {
  this->cap_level_ = level;
  this->unit_id_bytes_[0] = b0;
  this->unit_id_bytes_[1] = b1;
  this->unit_id_bytes_[2] = b2;
  this->unit_id_bytes_[3] = level;
  if (b0 != 0) {
    this->unit_profile_ = 1;
  } else if (b1 == 0x01 && b2 == 0x01) {
    this->unit_profile_ = 3;
  } else {
    this->unit_profile_ = 2;
  }
  this->poll_diagnostics_ = this->unit_profile_ == 3;

  ESP_LOGI(TAG, "Unit profile %u level %u (%02X %02X %02X %02X)", this->unit_profile_, this->cap_level_, b0, b1, b2,
           level);
  if (this->poll_diagnostics_) {
    ESP_LOGI(TAG, "Profile 3: polling indoor/outdoor diagnostics");
  } else {
    ESP_LOGI(TAG, "Profile %u: diagnostics only when the unit pushes them", this->unit_profile_);
  }

  if (!this->status_init_started_) {
    this->start_status_reads_();
  } else if (this->poll_diagnostics_) {
    this->request_diagnostic_reads_(true);
  }
}

void ToshibaClimateUart::request_diagnostic_reads_(bool probe) {
  if (!this->poll_diagnostics_)
    return;
  if (this->has_idu_diagnostics_(probe))
    this->requestData(ToshibaCommandType::IDU_STATUS);
  if (this->has_odu_diagnostics_(probe))
    this->requestData(ToshibaCommandType::ODU_STATUS);
}

/**
 * Periodic link check.
 * Profiles 1 and 2, and profile 3 below feature level 3, Get power status 0x80.
 * Profile 3 with feature level >= 3 Gets the 5-byte filter property 0xCA instead.
 * One check at a time; the next update interval is skipped while a reply is outstanding.
 */
void ToshibaClimateUart::request_keepalive_() {
  if (this->keepalive_pending_)
    return;
  for (const auto &queued : this->command_queue_) {
    if (queued.keepalive)
      return;
  }
  const bool extended = this->unit_profile_ == 3 && static_cast<int8_t>(this->cap_level_) >= 3;
  const auto cmd = extended ? ToshibaCommandType::FILTER_STATUS : ToshibaCommandType::POWER_STATE;
  auto payload = this->encode_frame_(make_request_fd_(static_cast<uint8_t>(cmd), {}));
  ESP_LOGD(TAG, "Keepalive Get EPC=0x%02X", static_cast<uint8_t>(cmd));
  this->enqueue_command_(
      ToshibaCommand{.cmd = cmd, .payload = std::move(payload), .keepalive = true});
}

bool ToshibaClimateUart::keepalive_reply_matches_(uint8_t cn, uint8_t fn, uint8_t epc) const {
  return this->keepalive_pending_ && cn == FRAME_CN_PROPERTY_REPLY && epc == this->keepalive_epc_ &&
         fn == this->keepalive_fn_;
}

bool ToshibaClimateUart::note_keepalive_result_(bool ok) {
  this->keepalive_pending_ = false;
  if (ok) {
    this->keepalive_fails_ = 0;
    return false;
  }
  const uint8_t fails = static_cast<uint8_t>(this->keepalive_fails_ + 1);
  this->keepalive_fails_ = fails;
  ESP_LOGW(TAG, "Keepalive Get EPC=0x%02X failed (%u/5)", this->keepalive_epc_, fails);
  if (fails <= KEEPALIVE_FAIL_LIMIT)
    return false;
  this->keepalive_fails_ = 0;
  this->recover_link_();
  return true;
}

/**
 * After five missed keepalive replies: wait 1 s and start a fresh handshake;
 * status is read again once the identity reply is classified.
 */
void ToshibaClimateUart::recover_link_() {
  ESP_LOGW(TAG, "Keepalive failed 5 times; restarting handshake");
  this->command_queue_.clear();
  this->rx_message_.clear();
  this->keepalive_pending_ = false;
  this->status_init_started_ = false;
  // The unit may have lost power (and its clock); sync again once the link is up.
  this->time_synced_ = false;
  this->time_sync_pending_ = false;
  this->time_sync_attempts_ = 0;
  this->next_time_sync_at_ = millis();
  this->last_command_timestamp_ = millis();
  this->enqueue_command_(
      ToshibaCommand{.cmd = ToshibaCommandType::DELAY, .delay = KEEPALIVE_RECOVERY_DELAY});
  this->start_handshake();
  this->set_wifi_led(!this->wifi_led_disabled_);
}

void ToshibaClimateUart::start_status_reads_() {
  if (this->status_init_started_)
    return;
  this->status_init_started_ = true;
  this->getInitData();
  if (this->auto_detect_) {
    // Gets above double as probes: the unit does not answer EPCs it lacks.
    this->set_timeout("toshiba_caps", CAPS_EVALUATE_DELAY, [this]() { this->evaluate_capabilities_(); });
  }
  if (this->unit_profile_ == 2 || this->unit_profile_ == 3) {
    // The feature mask 0xEE is read once at init on profile 2/3 units.
    this->requestData(ToshibaCommandType::MERIT_SUPPORT);
  }
  this->request_diagnostic_reads_(true);
}

void ToshibaClimateUart::getInitData() {
  ESP_LOGD(TAG, "Requesting initial data from AC unit");
  this->requestData(ToshibaCommandType::POWER_STATE);
  if (this->self_clean_sensor_ != nullptr) {
    this->requestData(ToshibaCommandType::SELF_CLEAN);
  }
  this->requestData(ToshibaCommandType::MODE);
  // Merit before setpoint: 0xB3 is +16 while 8 °C mode is active.
  this->requestData(ToshibaCommandType::SPECIAL_MODE);
  this->requestData(ToshibaCommandType::TARGET_TEMP);
  this->requestData(ToshibaCommandType::FAN);
  this->requestData(ToshibaCommandType::POWER_SEL);
  if (this->ionizer_switch_ != nullptr && this->unit_profile_ != 1) {
    // 0xC7 is read on profile 2/3 units only.
    this->requestData(ToshibaCommandType::AIR_PURIFIER);
  }
  this->requestData(ToshibaCommandType::SWING);
  this->requestData(ToshibaCommandType::ROOM_TEMP);
  if (this->outdoor_temp_sensor_ != nullptr) {
    this->requestData(ToshibaCommandType::OUTDOOR_TEMP);
  }
  if (this->energy_enabled_()) {
    this->requestData(ToshibaCommandType::ENERGY_DAILY);
  }
  if (this->has_off_timer_sensors_()) {
    this->requestData(ToshibaCommandType::COMFORT_SLEEP);
  }
  if (this->error_code_text_sensor_ != nullptr) {
    // 0x88 first, 0x86 only on a fault.
    this->requestData(ToshibaCommandType::FAULT_STATUS);
  }
}

void ToshibaClimateUart::set_self_clean_running_(bool running) {
  this->self_clean_running_ = running;
  if (this->self_clean_sensor_ != nullptr) {
    this->self_clean_sensor_->publish_state(running);
  }
}

/**
 * Configure the supported custom modes and presets for the climate component.
 * This is needed because the Toshiba AC has more FAN levels and presets than the standard climate component supports.
 * see https://developers.esphome.io/blog/2026/04/09/climate-and-fan-custom-mode-vectors-moved-to-entity/
 */
void ToshibaClimateUart::configure_supported_custom_modes_() {
  // Toshiba AC has more FAN levels than the standard climate component supports.
  this->set_supported_custom_fan_modes({CUSTOM_FAN_LEVEL_2, CUSTOM_FAN_LEVEL_4});

  // go over all defined presets and filter out the ones that are supported by the climate component
  if (!this->supported_presets_.empty()) {
    std::vector<const char *> custom_preset_names;
    for (const char *preset_string : this->supported_presets_) {
      if (!StringToClimatePreset(preset_string).has_value()) {
        // preset is not supported by the climate component, add it to the custom presets
        custom_preset_names.push_back(preset_string);
      }
    }
    // if there are any custom presets, set them for the climate component
    if (!custom_preset_names.empty()) {
      this->set_supported_custom_presets(custom_preset_names);
    }
  }
}

void ToshibaClimateUart::setup() {
  // Most frames are short; longer ones (identity, energy) grow the buffers
  // once and the capacity is kept, as the two buffers are only swapped.
  this->rx_message_.reserve(64);
  this->rx_frame_.reserve(64);
  this->presets_explicit_ = !this->supported_presets_.empty();
  if (this->auto_detect_) {
    this->load_capabilities_();
    this->apply_capabilities_();
    if (this->caps_loaded_) {
      ESP_LOGI(TAG, "Applying stored unit capabilities 0x%05X (profile %u)", static_cast<unsigned>(this->caps_.features),
               this->caps_.profile);
    } else {
      ESP_LOGI(TAG, "No stored unit capabilities yet; detecting them (the device restarts once afterwards)");
    }
  }
  this->configure_supported_custom_modes_();
  // Handshake first. Status Gets are queued once the identity reply is
  // classified (or from update() if that reply never arrives).
  this->start_handshake();
  // Set Wi-Fi LED initial state
  this->set_wifi_led(!this->wifi_led_disabled_);
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
  }

  if (this->keepalive_pending_ && now - this->keepalive_sent_at_ > KEEPALIVE_REPLY_TIMEOUT) {
    if (this->note_keepalive_result_(false))
      return;
  }

  // when there is no RX message and there is a command to send
  if (cmdDelay > COMMAND_DELAY && !this->command_queue_.empty() && this->rx_message_.empty()) {
    const auto &newCommand = this->command_queue_.front();
    if (newCommand.cmd == ToshibaCommandType::DELAY && cmdDelay < newCommand.delay) {
      // delay command did not finished yet
      return;
    }
    // DELAY commands don't send data over UART, just remove them from queue
    if (newCommand.cmd == ToshibaCommandType::DELAY) {
      this->command_queue_.erase(this->command_queue_.begin());
      return;
    }
    if (newCommand.keepalive && newCommand.payload.size() > 4) {
      this->keepalive_pending_ = true;
      this->keepalive_epc_ = static_cast<uint8_t>(newCommand.cmd);
      this->keepalive_fn_ = newCommand.payload[4];
      this->keepalive_sent_at_ = millis();
    }
    this->send_to_uart(this->command_queue_.front());
    this->command_queue_.erase(this->command_queue_.begin());
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
  this->process_command_queue_();
#ifdef USE_TIME
  // Checked here rather than in update(): the fallback retries need a shorter
  // period than the default 120 s update interval. The check itself only
  // compares timestamps.
  if (this->time_ != nullptr && this->status_init_started_) {
    this->check_time_sync_(millis());
  }
#endif
}

UnitModelInfo ToshibaClimateUart::parse_model_info_record_(const uint8_t *record) {
  UnitModelInfo info;
  const size_t n = MODEL_INFO_RECORD_LEN;
  info.model = extract_unit_field_(record, n, MODEL_INFO_MODEL_OFF, MODEL_INFO_MODEL_LEN);
  info.serial = extract_unit_field_(record, n, MODEL_INFO_SERIAL_OFF, MODEL_INFO_SERIAL_LEN);
  info.firmware = extract_unit_field_(record, n, MODEL_INFO_FIRMWARE_OFF, MODEL_INFO_FIRMWARE_LEN);
  return info;
}

/**
 * Model-info notify (EPC 0xE0). Not requested; the unit pushes it on its own.
 * This is the only source of the outdoor unit (CDU) model and serial.
 * The EEPROM info fields are only logged.
 */
void ToshibaClimateUart::handle_model_info_(const uint8_t *edt, size_t len) {
  if (len < MODEL_INFO_EDT_LEN) {
    ESP_LOGW(TAG, "Model info EDT too short (%u)", static_cast<unsigned>(len));
    return;
  }
  const uint8_t *fcu = edt;
  const uint8_t *cdu = edt + MODEL_INFO_RECORD_LEN;
  this->fcu_info_ = parse_model_info_record_(fcu);
  this->cdu_info_ = parse_model_info_record_(cdu);
  const size_t n = MODEL_INFO_RECORD_LEN;
  ESP_LOGI(TAG, "FCU model='%s' serial='%s' firmware='%s' eeprom='%s'", this->fcu_info_.model.c_str(),
           this->fcu_info_.serial.c_str(), this->fcu_info_.firmware.c_str(),
           extract_unit_field_(fcu, n, MODEL_INFO_EEPROM_OFF, MODEL_INFO_EEPROM_LEN).c_str());
  ESP_LOGI(TAG, "CDU model='%s' serial='%s' firmware='%s' eeprom='%s'", this->cdu_info_.model.c_str(),
           this->cdu_info_.serial.c_str(), this->cdu_info_.firmware.c_str(),
           extract_unit_field_(cdu, n, MODEL_INFO_EEPROM_OFF, MODEL_INFO_EEPROM_LEN).c_str());

  // The handshake identity is the primary source of the FCU model and serial;
  // use 0xE0 only when the identity reply did not carry them.
  if (this->fcu_model_.empty() && !this->fcu_info_.model.empty()) {
    this->fcu_model_ = this->fcu_info_.model;
    if (this->fcu_model_text_sensor_ != nullptr)
      this->fcu_model_text_sensor_->publish_state(this->fcu_model_);
  }
  if (this->fcu_serial_.empty() && !this->fcu_info_.serial.empty()) {
    this->fcu_serial_ = this->fcu_info_.serial;
    if (this->fcu_serial_text_sensor_ != nullptr)
      this->fcu_serial_text_sensor_->publish_state(this->fcu_serial_);
  }

  if (this->fcu_firmware_text_sensor_ != nullptr && !this->fcu_info_.firmware.empty())
    this->fcu_firmware_text_sensor_->publish_state(this->fcu_info_.firmware);
  if (this->cdu_model_text_sensor_ != nullptr && !this->cdu_info_.model.empty())
    this->cdu_model_text_sensor_->publish_state(this->cdu_info_.model);
  if (this->cdu_serial_text_sensor_ != nullptr && !this->cdu_info_.serial.empty())
    this->cdu_serial_text_sensor_->publish_state(this->cdu_info_.serial);
  if (this->cdu_firmware_text_sensor_ != nullptr && !this->cdu_info_.firmware.empty())
    this->cdu_firmware_text_sensor_->publish_state(this->cdu_info_.firmware);
}

void ToshibaClimateUart::parse_unit_identity_(const std::vector<uint8_t> &rawData) {
  // FD[0xA4..0xA7] select the unit profile used for later Gets.
  if (rawData.size() > UNIT_INFO_FD_VER0 + 3) {
    this->apply_unit_capabilities_(rawData[UNIT_INFO_FD_VER0], rawData[UNIT_INFO_FD_VER0 + 1],
                                   rawData[UNIT_INFO_FD_VER0 + 2], rawData[UNIT_INFO_FD_VER0 + 3]);
  }

  // Length-prefixed FCU model at abs[180]/abs[181..], serial is the following ASCII run.
  // Live example: 0F |"RAS-13PKVSG E2 "| "NO.01243D"
  this->fcu_model_.clear();
  this->fcu_serial_.clear();

  if (rawData.size() > UNIT_INFO_MODEL_DATA) {
    size_t model_len = rawData[UNIT_INFO_MODEL_LEN_IDX];
    if (model_len > UNIT_INFO_MODEL_MAX)
      model_len = UNIT_INFO_MODEL_MAX;
    this->fcu_model_ = extract_unit_field_(rawData.data(), rawData.size(), UNIT_INFO_MODEL_DATA, model_len);

    size_t serial_off = UNIT_INFO_MODEL_DATA + model_len;
    this->fcu_serial_ = extract_unit_field_(rawData.data(), rawData.size(), serial_off, UNIT_INFO_SERIAL_MAX);
  }

  ESP_LOGI(TAG, "FCU model='%s' serial='%s'", this->fcu_model_.c_str(), this->fcu_serial_.c_str());

  if (this->fcu_model_text_sensor_ != nullptr && !this->fcu_model_.empty()) {
    this->fcu_model_text_sensor_->publish_state(this->fcu_model_);
  }
  if (this->fcu_serial_text_sensor_ != nullptr && !this->fcu_serial_.empty()) {
    this->fcu_serial_text_sensor_->publish_state(this->fcu_serial_);
  }
}

void ToshibaClimateUart::parseResponse(const std::vector<uint8_t> &rawData) {
  // Header: [0]=STX [1..2]=FT [3]=CN [4]=FN [5..6]=DL, payload at [7..], FCC last.
  if (rawData.size() < 8)
    return;

  const uint16_t ft = (static_cast<uint16_t>(rawData[1]) << 8) | rawData[2];
  const uint8_t cn = rawData[3];
  const uint8_t fn = rawData[4];
  const uint16_t dl = (static_cast<uint16_t>(rawData[5]) << 8) | rawData[6];
  if (rawData.size() < static_cast<size_t>(8 + dl))
    return;
  const uint8_t *fd = rawData.data() + 7;

  // Handshake identity reply: FT=0x0002, long payload with FCU model/serial.
  if (ft == FRAME_FT_IDENTITY && rawData.size() >= UNIT_INFO_MIN_LEN) {
    ESP_LOGD(TAG, "Received unit identity frame (%u bytes) FN=0x%02X", static_cast<unsigned>(rawData.size()), fn);
    this->parse_unit_identity_(rawData);
    return;
  }

  // Short handshake ACKs (FT != 0x0003) are expected during init.
  if (ft != FRAME_FT_PROPERTY) {
    ESP_LOGD(TAG, "Handshake reply FT=0x%04X CN=0x%02X FN=0x%02X [%s]", ft, cn, fn,
             format_hex_pretty(rawData).c_str());
    return;
  }

  // Indoor unit pushed a status change (IR remote, etc.). ACK before parsing so the
  // unit does not retry, including EPCs this component does not apply.
  if (cn == FRAME_CN_NOTIFY) {
    ESP_LOGD(TAG, "Received notify FN=0x%02X DL=%u", fn, dl);
    this->send_notify_ack_(fn);
  }

  PropertyRecord prop;
  bool rejected = false;
  if (!find_property_(cn, fd, dl, &prop, &rejected)) {
    if (rejected) {
      ESP_LOGD(TAG, "Unit rejected request EPC=0x%02X CN=0x%02X FN=0x%02X [%s]", prop.epc, cn, fn,
               format_hex_pretty(rawData).c_str());
      if (this->keepalive_reply_matches_(cn, fn, prop.epc))
        this->note_keepalive_result_(false);
      if (prop.epc == static_cast<uint8_t>(ToshibaCommandType::BUNDLE) && !this->bundle_rejected_) {
        // Not expected on profile 2/3, but if a unit refuses the bundle, use
        // single Sets from now on and re-read the state it did not apply.
        ESP_LOGW(TAG, "Unit rejected the 0xF8 bundle; using single Sets from now on");
        this->bundle_rejected_ = true;
        this->requestData(ToshibaCommandType::MODE);
        this->requestData(ToshibaCommandType::SPECIAL_MODE);
        this->requestData(ToshibaCommandType::TARGET_TEMP);
        this->requestData(ToshibaCommandType::FAN);
      }
#ifdef USE_TIME
      if (prop.epc == static_cast<uint8_t>(ToshibaCommandType::SCHEDULE_PROGRAM) ||
          prop.epc == static_cast<uint8_t>(ToshibaCommandType::CLOCK_SET))
        this->on_time_sync_result_(prop.epc, false);
#endif
    } else {
      ESP_LOGD(TAG, "Property frame without EPC CN=0x%02X FN=0x%02X DL=%u [%s]", cn, fn, dl,
               format_hex_pretty(rawData).c_str());
    }
    return;
  }
  if (prop.edt_len == 0) {
    // Set reply: EPC echoed without data.
    ESP_LOGD(TAG, "Property ACK EPC=0x%02X CN=0x%02X FN=0x%02X", prop.epc, cn, fn);
#ifdef USE_TIME
    if (prop.epc == static_cast<uint8_t>(ToshibaCommandType::SCHEDULE_PROGRAM) ||
        prop.epc == static_cast<uint8_t>(ToshibaCommandType::CLOCK_SET))
      this->on_time_sync_result_(prop.epc, true);
#endif
    if (this->keepalive_reply_matches_(cn, fn, prop.epc))
      this->note_keepalive_result_(false);
    return;
  }
  if (this->keepalive_reply_matches_(cn, fn, prop.epc)) {
    // A returned 0xFF on 0x80 means "no status".
    const bool failed = prop.epc == static_cast<uint8_t>(ToshibaCommandType::POWER_STATE) && prop.edt[0] == 0xFF;
    if (this->note_keepalive_result_(!failed))
      return;
    if (failed)
      return;
  }

  switch (static_cast<ToshibaCommandType>(prop.epc)) {
    case ToshibaCommandType::OUTDOOR_TEMP:
      this->note_feature_(FEATURE_OUTDOOR_TEMP);
      break;
    case ToshibaCommandType::POWER_SEL:
      this->note_feature_(FEATURE_POWER_SEL);
      break;
    case ToshibaCommandType::SELF_CLEAN:
      this->note_feature_(FEATURE_SELF_CLEAN);
      break;
    case ToshibaCommandType::COMFORT_SLEEP:
      this->note_feature_(FEATURE_OFF_TIMER);
      break;
    case ToshibaCommandType::FAULT_STATUS:
      this->note_feature_(FEATURE_ERROR_CODE);
      break;
    case ToshibaCommandType::MODEL_INFO:
      this->note_feature_(FEATURE_MODEL_INFO);
      break;
    case ToshibaCommandType::IDU_STATUS:
      this->note_feature_(FEATURE_IDU_DIAG);
      break;
    case ToshibaCommandType::ODU_STATUS:
      this->note_feature_(FEATURE_ODU_DIAG);
      break;
    default:
      break;
  }

  // Publish the climate entity only when this frame changed it: most frames
  // (sensors, keepalive, diagnostics) do not, and every publish is an API
  // message, a log line and a preference write.
  const ClimateMode old_mode = this->mode;
  const float old_target = this->target_temperature;
  const float old_current = this->current_temperature;
  const optional<ClimateFanMode> old_fan = this->fan_mode;
  const ClimateSwingMode old_swing = this->swing_mode;
  const optional<ClimatePreset> old_preset = this->preset;
  this->climate_changed_ = false;

  const auto sensor = static_cast<ToshibaCommandType>(prop.epc);
  uint8_t value = prop.edt[0];
  const uint8_t *edt = prop.edt;
  const size_t edt_len = prop.edt_len;

  switch (sensor) {
    case ToshibaCommandType::ENERGY_DAILY: {
      // EDT: unit clock (6 bytes), then 24 x u16 little-endian hourly energy (Wh).
      // Extended units also offer 0xCC (24 x 20-byte records with heat/cool
      // quantity, compressor run time, COP, EER); see PROTOCOL.md.
      if (edt_len < 6 + 48) {
        ESP_LOGW(TAG, "Daily energy EDT too short (%u)", static_cast<unsigned>(edt_len));
        break;
      }
      ESP_LOGD(TAG, "Received daily energy update");
      uint32_t total_energy = 0;
#ifdef USE_TIME
      uint8_t current_hour = (this->time_ != nullptr) ? this->time_->now().hour : 25;
#else
      uint8_t current_hour = 25;
#endif
      const uint8_t *hours = edt + 6;
      for (uint8_t i = 0; i < 24; i++) {
        uint16_t hour_val = (hours[i * 2 + 1] << 8) | hours[i * 2];
        total_energy += hour_val;
        if (i == current_hour) {
          ESP_LOGD(TAG, "  Current hour (%d) consumption: %u Wh", i, hour_val);
        }
      }
      if (this->energy_sensor_ != nullptr) {
        this->energy_sensor_->publish_state(total_energy);
      }
      this->estimate_wattage_(total_energy);
      break;
    }
    case ToshibaCommandType::TARGET_TEMP:
      ESP_LOGD(TAG, "Received target temp: %d", value);
      this->raw_target_temp_ = value;
      this->update_target_temperature_();
      break;
    case ToshibaCommandType::FAN: {
      this->raw_fan_ = value;
      if (static_cast<FAN>(value) == FAN::FAN_AUTO) {
        ESP_LOGD(TAG, "Received fan mode: AUTO");
        this->set_fan_mode_(CLIMATE_FAN_AUTO);
      } else if (static_cast<FAN>(value) == FAN::FAN_QUIET) {
        ESP_LOGD(TAG, "Received fan mode: QUIET");
        this->set_fan_mode_(CLIMATE_FAN_QUIET);
      } else if (static_cast<FAN>(value) == FAN::FAN_LOW) {
        ESP_LOGD(TAG, "Received fan mode: LOW");
        this->set_fan_mode_(CLIMATE_FAN_LOW);
      } else if (static_cast<FAN>(value) == FAN::FAN_MEDIUM) {
        ESP_LOGD(TAG, "Received fan mode: MEDIUM");
        this->set_fan_mode_(CLIMATE_FAN_MEDIUM);
      } else if (static_cast<FAN>(value) == FAN::FAN_HIGH) {
        ESP_LOGD(TAG, "Received fan mode: HIGH");
        this->set_fan_mode_(CLIMATE_FAN_HIGH);
      } else {
        auto fanMode = IntToCustomFanMode(static_cast<FAN>(value));
        ESP_LOGD(TAG, "Received fan mode: %s", fanMode);
        this->climate_changed_ |= this->set_custom_fan_mode_(fanMode);
      }
      break;
    }
    case ToshibaCommandType::SWING: {
      auto swing = static_cast<SWING>(value);
      auto air_direction = SwingToVerticalAirDirection(swing);
      if (air_direction != nullptr) {
        ESP_LOGD(TAG, "Received vertical air direction: %s", air_direction);
        this->publish_vertical_air_direction_(swing);
      }

      if (IsFixedVerticalAirDirection(swing)) {
        this->swing_mode = climate::CLIMATE_SWING_OFF;
      } else {
        auto swingMode = IntToClimateSwingMode(swing);
        ESP_LOGD(TAG, "Received swing mode: %s", LOG_STR_ARG(climate_swing_mode_to_string(swingMode)));
        this->swing_mode = swingMode;
      }
      break;
    }
    case ToshibaCommandType::MODE: {
      this->raw_mode_ = value;
      auto mode = IntToClimateMode(static_cast<MODE>(value));
      ESP_LOGD(TAG, "Received AC mode: %s", LOG_STR_ARG(climate_mode_to_string(mode)));
      if (this->power_state_ == STATE::ON && !this->self_clean_running_) {
        this->mode = mode;
      }
      break;
    }
    case ToshibaCommandType::ROOM_TEMP:
      if (value != 127) {
        ESP_LOGD(TAG, "Received room temp: %d °C", value);
        this->current_temperature = value;
        if (indoor_temp_sensor_ != nullptr) {
          indoor_temp_sensor_->publish_state((int8_t) value);
        }
      }
      break;
    case ToshibaCommandType::OFF_TIMER_REMAINING: {
      // Two bytes: hours, minutes (00 1E = 30 min, 02 00 = 2 h).
      if (edt_len < 2)
        break;
      uint16_t minutes = edt[0] * 60 + edt[1];
      ESP_LOGD(TAG, "Received OFF timer remaining: %uh %02umin", edt[0], edt[1]);
      if (this->off_timer_remaining_sensor_ != nullptr) {
        this->off_timer_remaining_sensor_->publish_state(this->off_timer_active_ ? minutes : 0);
      }
      break;
    }
    case ToshibaCommandType::COMFORT_SLEEP: {
      // 0x94 is the OFF timer reservation (0x41 set / 0x42 not set); the
      // remote's sleep timer sets it too.
      bool active = value == 65;
      ESP_LOGD(TAG, "Received OFF timer state: %s", active ? "ON" : "OFF");
      bool changed = active != this->off_timer_active_;
      this->off_timer_active_ = active;
      if (this->off_timer_sensor_ != nullptr) {
        this->off_timer_sensor_->publish_state(active);
      }
      if (this->off_timer_remaining_sensor_ != nullptr) {
        if (active) {
          if (changed) {
            this->requestData(ToshibaCommandType::OFF_TIMER_REMAINING);
          }
        } else {
          this->off_timer_remaining_sensor_->publish_state(0);
        }
      }
      break;
    }
    case ToshibaCommandType::OUTDOOR_TEMP:
      if (value != 127) {
        if (outdoor_temp_sensor_ != nullptr) {
          ESP_LOGD(TAG, "Received outdoor temp: %d °C", (int8_t) value);
          outdoor_temp_sensor_->publish_state((int8_t) value);
        }
      }
      break;
    case ToshibaCommandType::POWER_SEL: {
      auto pwr_level = IntToPowerLevel(static_cast<PWR_LEVEL>(value));
      ESP_LOGD(TAG, "Received power select: %d", value);
      if (pwr_select_ != nullptr) {
        pwr_select_->publish_state(pwr_level);
      }
      break;
    }
    case ToshibaCommandType::FILTER_STATUS:
      // config, target (u16), counter (u16). Units and byte order are not
      // confirmed, so this is only logged. Used as the profile-3 keepalive.
      if (edt_len >= 5) {
        ESP_LOGD(TAG, "Filter status: config=0x%02X target=%02X%02X counter=%02X%02X", edt[0], edt[2], edt[1],
                 edt[4], edt[3]);
      }
      break;
    case ToshibaCommandType::MERIT_SUPPORT:
      // Read once after connect; used by feature detection and logged.
      if (edt_len >= 2) {
        this->feature_mask_[0] = edt[0];
        this->feature_mask_[1] = edt[1];
        this->feature_mask_known_ = true;
        ESP_LOGI(TAG, "Unit feature mask 0xEE: %02X %02X", edt[0], edt[1]);
      }
      break;
    case ToshibaCommandType::FAULT_STATUS:
      this->handle_fault_status_(value);
      break;
    case ToshibaCommandType::ERROR_DETAIL:
      this->publish_error_code_(edt, edt_len);
      break;
    case ToshibaCommandType::POWER_STATE: {
      auto climateState = static_cast<STATE>(value);
      ESP_LOGD(TAG, "Received AC unit power state: %s", LOG_STR_ARG(climate_state_to_string(climateState)));
      if (climateState == STATE::OFF) {
        // AC unit was just powered off, set mode to OFF
        this->mode = climate::CLIMATE_MODE_OFF;
        this->set_self_clean_running_(false);
      } else if (this->self_clean_running_) {
        if (this->self_clean_sensor_ != nullptr) {
          ESP_LOGD(TAG, "Refreshing self-clean status after receiving AC ON state");
          this->requestData(ToshibaCommandType::SELF_CLEAN);
        }
      } else if (this->mode == climate::CLIMATE_MODE_OFF && climateState == STATE::ON) {
        // Unit reports ON while we believe it is off, e.g. powered on via IR
        // remote, or the start of a post-shutdown self-clean cycle. When
        // self-clean reporting is configured, query it first: the SELF_CLEAN
        // response is processed before the MODE response below, so a running
        // cycle keeps the entity OFF (the MODE response is ignored while
        // self-clean is running).
        if (this->self_clean_sensor_ != nullptr) {
          this->requestData(ToshibaCommandType::SELF_CLEAN);
        }
        this->requestData(ToshibaCommandType::MODE);
      }
      this->power_state_ = climateState;
      break;
    }
    case ToshibaCommandType::SELF_CLEAN: {
      auto self_clean_state = static_cast<SELF_CLEAN_STATE>(value);
      bool was_running = this->self_clean_running_;
      if (self_clean_state == SELF_CLEAN_STATE::RUNNING) {
        ESP_LOGI(TAG, "Self-clean is running");
        this->set_self_clean_running_(true);
        this->mode = climate::CLIMATE_MODE_OFF;
      } else if (self_clean_state == SELF_CLEAN_STATE::OFF) {
        ESP_LOGI(TAG, "Self-clean is stopped");
        this->set_self_clean_running_(false);
        if (was_running) {
          // The cycle ended: either it finished (unit now off) or it was
          // interrupted by switching the unit back on. The self-clean register
          // doesn't distinguish these, and the unit only reports power state on
          // change/query (not continuously), so the cached value may be stale.
          // Query the power state to resync; the POWER_STATE handler refreshes
          // the mode when the unit turns out to be on.
          this->requestData(ToshibaCommandType::POWER_STATE);
        }
      } else {
        ESP_LOGW(TAG, "Received unknown self-clean state: %d", value);
      }
      break;
    }
    case ToshibaCommandType::SPECIAL_MODE: {
      ESP_LOGD(TAG, "Received special mode: %s (0x%02X)", SpecialModeToPreset(static_cast<SPECIAL_MODE>(value)),
               value);
      this->raw_merit_ = value;
      this->publish_special_mode_preset_(value);
      // The meaning of the stored setpoint depends on the merit (+16 in 8 °C mode).
      this->update_target_temperature_();
      break;
    }
    case ToshibaCommandType::ODU_STATUS: {
      // EDT: Td, Ts, Te, compressor load, ..., Iac (byte 6).
      // 127 / 254+ mean "not measured" (e.g. while the unit is off).
      if (edt_len < 7) {
        ESP_LOGW(TAG, "ODU status EDT too short (%u)", static_cast<unsigned>(edt_len));
        break;
      }
      ESP_LOGD(TAG, "Received ODU status");
      if (cdu_td_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[0]);
        if (val != 127) {
          cdu_td_temp_sensor_->publish_state(val);
        }
      }
      if (cdu_ts_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[1]);
        if (val != 127) {
          cdu_ts_temp_sensor_->publish_state(val);
        }
      }
      if (cdu_te_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[2]);
        if (val != 127) {
          cdu_te_temp_sensor_->publish_state(val);
        }
      }
      if (cdu_load_sensor_ != nullptr) {
        // Published as the historical "load" percentage (raw / 1.7).
        uint8_t raw_val = edt[3];
        if (raw_val < 254) {
          cdu_load_sensor_->publish_state(raw_val / 1.7f);
        }
      }
      if (cdu_iac_sensor_ != nullptr) {
        uint8_t raw_val = edt[6];
        if (raw_val < 254) {
          cdu_iac_sensor_->publish_state(raw_val);
        }
      }
      break;
    }
    case ToshibaCommandType::MODEL_INFO:
      this->handle_model_info_(edt, edt_len);
      break;
    case ToshibaCommandType::AIR_PURIFIER:
      ESP_LOGD(TAG, "Received ionizer state: 0x%02X", value);
      if (this->ionizer_switch_ != nullptr)
        this->ionizer_switch_->publish_state((value & 0x08) != 0);
      break;
    case ToshibaCommandType::IDU_STATUS: {
      // EDT: Tc, Tcj, fan. Needs 3 bytes.
      if (edt_len < 3) {
        ESP_LOGW(TAG, "IDU status EDT too short (%u)", static_cast<unsigned>(edt_len));
        break;
      }
      ESP_LOGD(TAG, "Received IDU status");
      if (fcu_tc_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[0]);
        if (val != 127) {
          fcu_tc_temp_sensor_->publish_state(val);
        }
      }
      if (fcu_tcj_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[1]);
        if (val != 127) {
          fcu_tcj_temp_sensor_->publish_state(val);
        }
      }
      if (fcu_fan_rpm_sensor_ != nullptr) {
        fcu_fan_rpm_sensor_->publish_state(edt[2]);
      }
      break;
    }
    default:
      ESP_LOGD(TAG, "Unhandled EPC 0x%02X (%u bytes, first 0x%02X)", prop.epc, static_cast<unsigned>(edt_len), value);
      break;
  }
  auto same = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
  if (this->climate_changed_ || this->mode != old_mode || !same(this->target_temperature, old_target) ||
      !same(this->current_temperature, old_current) || this->fan_mode != old_fan || this->swing_mode != old_swing ||
      this->preset != old_preset) {
    this->publish_state();
  }
}

void ToshibaClimateUart::dump_config() {
  ESP_LOGCONFIG(TAG, "ToshibaClimate:");
  LOG_CLIMATE("", "Thermostat", this);
  if (!this->fcu_model_.empty()) {
    ESP_LOGCONFIG(TAG, "  FCU Model: '%s'", this->fcu_model_.c_str());
  }
  if (!this->fcu_serial_.empty()) {
    ESP_LOGCONFIG(TAG, "  FCU Serial: '%s'", this->fcu_serial_.c_str());
  }
  if (!this->fcu_info_.firmware.empty()) {
    ESP_LOGCONFIG(TAG, "  FCU Firmware: '%s'", this->fcu_info_.firmware.c_str());
  }
  if (!this->cdu_info_.model.empty() || !this->cdu_info_.serial.empty()) {
    ESP_LOGCONFIG(TAG, "  CDU Model: '%s' Serial: '%s' Firmware: '%s'", this->cdu_info_.model.c_str(),
                  this->cdu_info_.serial.c_str(), this->cdu_info_.firmware.c_str());
  }
  if (this->unit_profile_ != 0) {
    ESP_LOGCONFIG(TAG, "  Unit profile %u level %u (%02X %02X %02X %02X), diagnostics %s", this->unit_profile_,
                  this->cap_level_, this->unit_id_bytes_[0], this->unit_id_bytes_[1], this->unit_id_bytes_[2],
                  this->unit_id_bytes_[3], this->poll_diagnostics_ ? "polled" : "pushed by unit");
  } else if (this->status_init_started_) {
    ESP_LOGCONFIG(TAG, "  Unit profile: no identity received, basic status only");
  } else {
    ESP_LOGCONFIG(TAG, "  Unit profile: not yet known (handshake pending)");
  }
  if (this->feature_mask_known_) {
    ESP_LOGCONFIG(TAG, "  Unit feature mask 0xEE: %02X %02X", this->feature_mask_[0], this->feature_mask_[1]);
  }
  if (this->auto_detect_) {
    if (this->caps_loaded_) {
      ESP_LOGCONFIG(TAG, "  Auto-detected capabilities: 0x%05X (profile %u, 0xEE %02X %02X), %u of %u optional entities shown",
                    static_cast<unsigned>(this->caps_.features), this->caps_.profile, this->caps_.merit_mask[0],
                    this->caps_.merit_mask[1],
                    static_cast<unsigned>(std::count_if(this->auto_entities_.begin(), this->auto_entities_.end(),
                                                        [this](const AutoEntity &e) { return this->has_feature_(e.feature); })),
                    static_cast<unsigned>(this->auto_entities_.size()));
    } else {
      ESP_LOGCONFIG(TAG, "  Auto-detected capabilities: none stored yet");
    }
  } else {
    ESP_LOGCONFIG(TAG, "  Auto-detection: disabled");
  }
  if (this->ionizer_switch_ != nullptr) {
    LOG_SWITCH("", "Ionizer", this->ionizer_switch_);
  }
  if (this->fcu_model_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "FCU Model", this->fcu_model_text_sensor_);
  }
  if (this->fcu_serial_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "FCU Serial", this->fcu_serial_text_sensor_);
  }
  if (this->fcu_firmware_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "FCU Firmware", this->fcu_firmware_text_sensor_);
  }
  if (this->cdu_model_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "CDU Model", this->cdu_model_text_sensor_);
  }
  if (this->cdu_serial_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "CDU Serial", this->cdu_serial_text_sensor_);
  }
  if (this->cdu_firmware_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "CDU Firmware", this->cdu_firmware_text_sensor_);
  }
  if (this->error_code_text_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("", "Error Code", this->error_code_text_sensor_);
  }
  if (outdoor_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "Outdoor Temp", this->outdoor_temp_sensor_);
  }
  if (cdu_td_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "CDU Td Temp", this->cdu_td_temp_sensor_);
  }
  if (cdu_ts_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "CDU Ts Temp", this->cdu_ts_temp_sensor_);
  }
  if (cdu_te_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "CDU Te Temp", this->cdu_te_temp_sensor_);
  }
  if (cdu_load_sensor_ != nullptr) {
    LOG_SENSOR("", "CDU Load", this->cdu_load_sensor_);
  }
  if (cdu_iac_sensor_ != nullptr) {
    LOG_SENSOR("", "CDU IAC", this->cdu_iac_sensor_);
  }
  if (fcu_tc_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "FCU Tc Temp", this->fcu_tc_temp_sensor_);
  }
  if (fcu_tcj_temp_sensor_ != nullptr) {
    LOG_SENSOR("", "FCU Tcj Temp", this->fcu_tcj_temp_sensor_);
  }
  if (fcu_fan_rpm_sensor_ != nullptr) {
    LOG_SENSOR("", "FCU Fan RPM", this->fcu_fan_rpm_sensor_);
  }
  if (energy_sensor_ != nullptr) {
    LOG_SENSOR("", "Energy", this->energy_sensor_);
  }
  if (power_sensor_ != nullptr) {
    LOG_SENSOR("", "Power", this->power_sensor_);
  }
  if (pwr_select_ != nullptr) {
    LOG_SELECT("", "Power selector", this->pwr_select_);
  }
  if (vertical_air_direction_select_ != nullptr) {
    LOG_SELECT("", "Vertical air direction", this->vertical_air_direction_select_);
  }
  if (self_clean_sensor_ != nullptr) {
    LOG_BINARY_SENSOR("", "Self Clean", this->self_clean_sensor_);
  }
  if (off_timer_sensor_ != nullptr) {
    LOG_BINARY_SENSOR("", "OFF Timer", this->off_timer_sensor_);
  }
  if (off_timer_remaining_sensor_ != nullptr) {
    LOG_SENSOR("", "OFF Timer Remaining", this->off_timer_remaining_sensor_);
  }
  if (!supported_presets_.empty()) {
    ESP_LOGCONFIG(TAG, "Supported presets:");
    for (const char* &preset : supported_presets_) {
      ESP_LOGCONFIG(TAG, "  - %s", preset);
    }
  }
  ESP_LOGCONFIG(TAG, "  Min Temp: %d", this->min_temp_);
}

/**
 * Periodic link check: power status 0x80,
 * or 0xCA on profile 3 with feature level >= 3. Room temperature (and outdoor
 * temperature when that sensor is configured) is read on the same interval so
 * Home Assistant can record a history. Diagnostic EPCs are included only after
 * the identity profile says the unit expects the adapter to read them.
 */
void ToshibaClimateUart::update() {
  if (!this->status_init_started_) {
    // Identity normally starts the first read. If that frame never came and
    // the handshake queue is idle, load the basic climate status anyway.
    if (this->command_queue_.empty()) {
      ESP_LOGW(TAG, "No unit identity; loading basic status without diagnostics");
      this->start_status_reads_();
    }
    return;
  }
  this->request_keepalive_();
  this->requestData(ToshibaCommandType::ROOM_TEMP);
  // Periodic reads skip what detection found unsupported; the reads queued at
  // connect (getInitData) still go out and act as probes.
  if (this->poll_enabled_(this->outdoor_temp_sensor_)) {
    this->requestData(ToshibaCommandType::OUTDOOR_TEMP);
  }
  if (this->self_clean_running_) {
    this->requestData(ToshibaCommandType::SELF_CLEAN);
  }
  if (this->poll_enabled_(this->off_timer_sensor_) || this->poll_enabled_(this->off_timer_remaining_sensor_)) {
    // The unit doesn't push the remaining time, so poll it while a timer runs.
    this->requestData(ToshibaCommandType::COMFORT_SLEEP);
    if (this->off_timer_active_ && this->off_timer_remaining_sensor_ != nullptr) {
      this->requestData(ToshibaCommandType::OFF_TIMER_REMAINING);
    }
  }
  if (this->poll_enabled_(this->error_code_text_sensor_)) {
    this->requestData(ToshibaCommandType::FAULT_STATUS);
  }
  this->request_diagnostic_reads_(false);

  uint32_t now = millis();

  // Periodic Energy Sync (Runs every 60s if energy or power sensors are configured)
  if (this->energy_enabled_() && (now - this->last_energy_sync_ > 60000)) {
    this->sync_energy_();
  }
}

void ToshibaClimateUart::control(const climate::ClimateCall &call) {
  // Mode, fan, setpoint and merit are collected first and sent together at the
  // end: as one 0xF8 bundle on profile 2/3, otherwise
  // as single Sets. Power and swing are separate EPCs and are sent right away.
  optional<uint8_t> new_mode;
  optional<uint8_t> new_fan;

  if (call.get_mode().has_value()) {
    ClimateMode mode = *call.get_mode();
    ESP_LOGD(TAG, "Setting mode to %s", LOG_STR_ARG(climate_mode_to_string(mode)));
    if (mode != CLIMATE_MODE_OFF) {
      this->set_self_clean_running_(false);
    }
    if (this->mode == CLIMATE_MODE_OFF && mode != CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Setting AC unit power state to ON.");
      this->sendCmd(ToshibaCommandType::POWER_STATE, static_cast<uint8_t>(STATE::ON));
    }
    if (mode == CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Setting AC unit power state to OFF.");
      this->sendCmd(ToshibaCommandType::POWER_STATE, static_cast<uint8_t>(STATE::OFF));
    } else {
      new_mode = static_cast<uint8_t>(ClimateModeToInt(mode));
    }
    this->mode = mode;
  }

  if (call.get_fan_mode().has_value()) {
    auto fan_mode = *call.get_fan_mode();
    ESP_LOGD(TAG, "Setting fan mode to %s", LOG_STR_ARG(climate_fan_mode_to_string(fan_mode)));
    this->set_fan_mode_(fan_mode);
    auto fan_value = ClimateFanModeToInt(fan_mode);
    if (fan_value.has_value()) {
      new_fan = static_cast<uint8_t>(fan_value.value());
    }
  }

  if (call.has_custom_fan_mode()) {
    auto fan_mode = call.get_custom_fan_mode();
    auto payload = StringToFanLevel(fan_mode.c_str());
    if (payload.has_value()) {
      ESP_LOGD(TAG, "Setting fan mode to custom: %s", fan_mode.c_str());
      this->set_custom_fan_mode_(fan_mode);
      new_fan = static_cast<uint8_t>(payload.value());
    }
  }

  if (call.get_swing_mode().has_value()) {
    auto swing_mode = *call.get_swing_mode();
    auto function_value = ClimateSwingModeToInt(swing_mode);
    ESP_LOGD(TAG, "Setting swing mode to %s", LOG_STR_ARG(climate_swing_mode_to_string(swing_mode)));
    this->swing_mode = swing_mode;
    this->sendCmd(ToshibaCommandType::SWING, static_cast<uint8_t>(function_value));
    this->publish_vertical_air_direction_(function_value);
  }

  // Requested merit (special mode) from a standard or custom preset.
  optional<uint8_t> new_merit;
  if (call.get_preset().has_value()) {
    auto preset_string = ClimatePresetToString(*call.get_preset());
    ESP_LOGD(TAG, "Setting preset to %s", preset_string);
    auto special_mode = PresetToSpecialMode(preset_string);
    if (special_mode.has_value()) {
      new_merit = static_cast<uint8_t>(special_mode.value());
    } else {
      ESP_LOGW(TAG, "Unknown preset: %s", preset_string);
    }
  }
  if (call.has_custom_preset()) {
    auto custom_preset = call.get_custom_preset();
    ESP_LOGD(TAG, "Setting custom preset to %s", custom_preset.c_str());
    auto special_mode = PresetToSpecialMode(custom_preset.c_str());
    if (special_mode.has_value()) {
      new_merit = static_cast<uint8_t>(special_mode.value());
    } else {
      ESP_LOGW(TAG, "Unknown custom preset: %s", custom_preset.c_str());
    }
  }

  // Keep the setpoint and the 8 °C mode consistent. In 8 °C mode the unit takes
  // 5..16 °C, sent as +16; outside it 17..30 °C. A setpoint below 17 °C turns
  // 8 °C mode on and one of 17 °C or more turns it off (as Haori units do from
  // their own remote). Choosing the preset without a setpoint moves the
  // setpoint into the range of the new mode.
  const bool was_eight_deg = this->eight_deg_active_();
  uint8_t merit = new_merit.value_or(this->raw_merit_.value_or(SPECIAL_MODE::STANDARD));
  bool eight_deg = (merit & 0x0F) == SPECIAL_MODE::EIGHT_DEG;
  optional<uint8_t> new_temp;
  if (call.get_target_temperature().has_value()) {
    int temp = static_cast<int>(lroundf(*call.get_target_temperature()));
    if (!new_merit.has_value()) {
      if (temp < MIN_TEMP_STANDARD && !eight_deg) {
        ESP_LOGD(TAG, "Setpoint %d °C: switching to \"%s\"", temp, SPECIAL_MODE_EIGHT_DEG);
        merit = SPECIAL_MODE::EIGHT_DEG;
        new_merit = merit;
      } else if (temp >= MIN_TEMP_STANDARD && eight_deg) {
        ESP_LOGD(TAG, "Setpoint %d °C: leaving \"%s\"", temp, SPECIAL_MODE_EIGHT_DEG);
        merit = SPECIAL_MODE::STANDARD;
        new_merit = merit;
      }
      eight_deg = (merit & 0x0F) == SPECIAL_MODE::EIGHT_DEG;
    }
    if (eight_deg) {
      temp = clamp<int>(temp, SPECIAL_MODE_EIGHT_DEG_MIN_TEMP, SPECIAL_MODE_EIGHT_DEG_MAX_TEMP);
    } else {
      temp = clamp<int>(temp, MIN_TEMP_STANDARD, MAX_TEMP);
    }
    new_temp = static_cast<uint8_t>(temp);
  } else if (eight_deg != was_eight_deg) {
    int temp = static_cast<int>(lroundf(this->target_temperature));
    if (eight_deg && temp >= MIN_TEMP_STANDARD) {
      temp = SPECIAL_MODE_EIGHT_DEG_DEF_TEMP;
    } else if (!eight_deg && temp < MIN_TEMP_STANDARD) {
      temp = NORMAL_MODE_DEF_TEMP;
    }
    // Sent even when unchanged: the raw value differs by 16 between the modes.
    new_temp = static_cast<uint8_t>(temp);
  }
  if (eight_deg && !was_eight_deg && !new_mode.has_value()) {
    // 8 °C mode heats only, so switch the mode to Heat as well.
    new_mode = static_cast<uint8_t>(MODE::HEAT);
    if (this->mode != CLIMATE_MODE_OFF)
      this->mode = CLIMATE_MODE_HEAT;
  }
  optional<uint8_t> new_raw_temp;
  if (new_temp.has_value()) {
    new_raw_temp = static_cast<uint8_t>(new_temp.value() + (eight_deg ? SPECIAL_TEMP_OFFSET : 0));
    ESP_LOGD(TAG, "Setting target temp to %d (setpoint %d)", new_temp.value(), new_raw_temp.value());
  }

  if (new_mode.has_value() || new_fan.has_value() || new_merit.has_value() || new_raw_temp.has_value()) {
    if (this->use_bundle_()) {
      this->send_bundle_(new_mode.value_or(this->raw_mode_.value()), new_raw_temp.value_or(this->raw_target_temp_.value()),
                         new_fan.value_or(this->raw_fan_.value()), merit);
    } else {
      if (this->unit_profile_ == 2 || this->unit_profile_ == 3) {
        ESP_LOGD(TAG, "Unit state not fully known yet, sending single Sets instead of the bundle");
      }
      if (new_mode.has_value())
        this->sendCmd(ToshibaCommandType::MODE, new_mode.value());
      if (new_fan.has_value())
        this->sendCmd(ToshibaCommandType::FAN, new_fan.value());
      // Merit first, so the unit reads the setpoint in the right mode.
      if (new_merit.has_value())
        this->sendCmd(ToshibaCommandType::SPECIAL_MODE, merit);
      if (new_raw_temp.has_value())
        this->sendCmd(ToshibaCommandType::TARGET_TEMP, new_raw_temp.value());
    }
  }

  // Assume the unit applies the change; Get replies and notifies correct it if not.
  if (new_mode.has_value())
    this->raw_mode_ = new_mode;
  if (new_fan.has_value())
    this->raw_fan_ = new_fan;
  if (new_merit.has_value()) {
    this->raw_merit_ = merit;
    this->publish_special_mode_preset_(merit);
  }
  if (new_raw_temp.has_value()) {
    this->raw_target_temp_ = new_raw_temp;
    this->target_temperature = new_temp.value();
  }

  this->publish_state();
}

ClimateTraits ToshibaClimateUart::traits() {
  auto traits = climate::ClimateTraits();

  if (this->heat_only_) {
    // 0xEE byte 1 bits 1..0 = 01: heating-only unit.
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  } else if (this -> heat_mode_disabled_) {
    traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT_COOL,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY
    });
  } else {
    traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT_COOL,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY
    });
  }

  if (this -> horizontal_swing_) {
    traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
      climate::CLIMATE_SWING_HORIZONTAL,
      climate::CLIMATE_SWING_BOTH
    });
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

  traits.set_visual_temperature_step(1);
  traits.set_visual_min_temperature(this->min_temp_);
  traits.set_visual_max_temperature(MAX_TEMP);

  // Add supported standard presets based on configuration (custom presets are set in setup())
  if (!this->supported_presets_.empty()) {
    for (const char *preset_string : this->supported_presets_) {
      auto climate_preset = StringToClimatePreset(preset_string);
      if (climate_preset.has_value()) {
        // preset is supported by the climate component
        traits.add_supported_preset(*climate_preset);
      }
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

void ToshibaClimateUart::on_set_vertical_air_direction(const std::string &value) {
  auto position = StringToVerticalAirDirection(value);
  if (!position.has_value()) {
    ESP_LOGW(TAG, "Unknown vertical air direction: %s", value.c_str());
    return;
  }

  ESP_LOGD(TAG, "Setting vertical air direction to %s", value.c_str());
  this->sendCmd(ToshibaCommandType::SWING, static_cast<uint8_t>(position.value()));
  this->publish_vertical_air_direction_(position.value());
  if (position.value() == SWING::VERTICAL || position.value() == SWING::BOTH) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }
  this->publish_state();
}

void ToshibaClimateUart::publish_vertical_air_direction_(SWING swing_mode) {
  if (vertical_air_direction_select_ == nullptr) {
    return;
  }
  auto position = SwingToVerticalAirDirection(swing_mode);
  if (position != nullptr) {
    vertical_air_direction_select_->publish_state(position);
  }
}

void ToshibaVerticalAirDirectionSelect::control(const std::string &value) { parent_->on_set_vertical_air_direction(value); }

/**
 * Control the indoor-unit Wi-Fi LED.
 * ON:  DE=0x05, DF=0x00
 * OFF: DE=0x00, DF=0x80
 * Units use one of the two registers; writing both covers either.
 * BLINK (DE=0x01) exists but is not exposed here.
 */
void ToshibaClimateUart::set_wifi_led(bool enabled) {
  if (enabled) {
    ESP_LOGD(TAG, "Turning ON Wi-Fi LED");
    this->sendCmd(ToshibaCommandType::WIFI_LED_1, static_cast<uint8_t>(WIFI_LED::DE_ON));
    this->sendCmd(ToshibaCommandType::WIFI_LED_2, static_cast<uint8_t>(WIFI_LED::DF_ON));
  } else {
    ESP_LOGD(TAG, "Turning OFF Wi-Fi LED");
    this->sendCmd(ToshibaCommandType::WIFI_LED_1, static_cast<uint8_t>(WIFI_LED::DE_OFF));
    this->sendCmd(ToshibaCommandType::WIFI_LED_2, static_cast<uint8_t>(WIFI_LED::DF_OFF));
  }
}

#ifdef USE_TIME
/**
 * Unit clock synchronization.
 *
 * Two frames can set the clock, both carrying the same date/time encoding
 * (year-1900, month 0-11, day, hour, minute, second, weekday with Sunday = 0):
 *   0x99  weekly-schedule frame with an empty schedule (233 data bytes).
 *         Accepted by most units; it also clears any schedule stored in the unit.
 *   0x98  plain clock set (7 data bytes).
 * Some units ignore one of them completely (no ACK, no NAK). Carrier 40WHP /
 * Toshiba Haori ignores 0x99 and needs 0x98; with the wrong frame the clock
 * stays unset and the energy registers 0xD8..0xDB read zero.
 *
 * Which frame a unit needs is not known upfront, so this
 * component starts with 0x99 and alternates formats until one is ACKed. The
 * first retries follow each other after the ACK timeout, later ones every
 * TIME_SYNC_SLOW_RETRY. The working format is kept for periodic syncs.
 */
static const uint32_t TIME_SYNC_ACK_TIMEOUT = 15000;
static const uint8_t TIME_SYNC_FAST_RETRIES = 4;  // 0x99, 0x98, 0x99, 0x98
static const uint32_t TIME_SYNC_SLOW_RETRY = 300000;
static const uint32_t TIME_SYNC_CLOCK_WAIT = 10000;  // re-check while the time source is not valid

void ToshibaClimateUart::check_time_sync_(uint32_t now) {
  if (this->time_sync_pending_) {
    if (now - this->last_time_sync_ > TIME_SYNC_ACK_TIMEOUT) {
      const auto epc = this->time_sync_use_clock_epc_ ? ToshibaCommandType::CLOCK_SET
                                                      : ToshibaCommandType::SCHEDULE_PROGRAM;
      if (this->time_sync_attempts_ < TIME_SYNC_FAST_RETRIES) {
        ESP_LOGW(TAG, "No ACK for time sync EPC 0x%02X, trying the other format", static_cast<uint8_t>(epc));
      } else {
        // Units without a clock never answer; keep later retries quiet.
        ESP_LOGD(TAG, "No ACK for time sync EPC 0x%02X", static_cast<uint8_t>(epc));
      }
      this->on_time_sync_result_(static_cast<uint8_t>(epc), false);
    }
    return;
  }
  if (this->time_synced_ && this->time_sync_interval_ == 0)
    return;  // synced once at startup, periodic sync disabled
  if (static_cast<int32_t>(now - this->next_time_sync_at_) < 0)
    return;
  this->sync_time_();
}

void ToshibaClimateUart::on_time_sync_result_(uint8_t epc, bool ok) {
  const uint32_t now = millis();
  this->time_sync_pending_ = false;
  if (ok) {
    ESP_LOGI(TAG, "AC unit acknowledged time synchronization (EPC 0x%02X)", epc);
    this->time_synced_ = true;
    this->time_sync_use_clock_epc_ = epc == static_cast<uint8_t>(ToshibaCommandType::CLOCK_SET);
    this->time_sync_attempts_ = 0;
    this->next_time_sync_at_ = now + this->time_sync_interval_;
    return;
  }
  // No ACK or rejected: try the other format.
  if (this->time_sync_attempts_ < 255)
    this->time_sync_attempts_++;
  this->time_sync_use_clock_epc_ = !this->time_sync_use_clock_epc_;
  this->next_time_sync_at_ = this->time_sync_attempts_ < TIME_SYNC_FAST_RETRIES ? now : now + TIME_SYNC_SLOW_RETRY;
}

void ToshibaClimateUart::sync_time_() {
  const uint32_t ms = millis();
  if (!this->time_->now().is_valid()) {
    ESP_LOGD(TAG, "Time sync postponed: time source is not valid yet.");
    this->next_time_sync_at_ = ms + TIME_SYNC_CLOCK_WAIT;
    return;
  }
  auto now = this->time_->now();

  std::vector<uint8_t> edt;
  edt.reserve(9 + 224);
  edt.push_back((now.year - 2000) + 100);  // years since 1900
  edt.push_back(now.month - 1);            // 0..11
  edt.push_back(now.day_of_month);
  edt.push_back(now.hour);
  edt.push_back(now.minute);
  edt.push_back(now.second);
  edt.push_back(now.day_of_week - 1);  // Sunday=0

  ToshibaCommandType epc;
  if (this->time_sync_use_clock_epc_) {
    // Plain clock set: 01 30 01 00 08 98 <7 bytes>
    epc = ToshibaCommandType::CLOCK_SET;
  } else {
    // Weekly schedule: two zero bytes, then 7 days x 4 empty programs x 8 bytes.
    epc = ToshibaCommandType::SCHEDULE_PROGRAM;
    edt.push_back(0x00);
    edt.push_back(0x00);
    edt.insert(edt.end(), 224, 0xFF);
  }

  ESP_LOGI(TAG, "Syncing time to AC unit (EPC 0x%02X, attempt %u): %04d-%02d-%02d %02d:%02d:%02d",
           static_cast<uint8_t>(epc), this->time_sync_attempts_ + 1, now.year, now.month, now.day_of_month, now.hour,
           now.minute, now.second);

  auto payload = this->encode_frame_(make_request_fd_(static_cast<uint8_t>(epc), edt));
  // Queue the frame and a 5-second pause to prevent collisions with other traffic.
  this->enqueue_command_(ToshibaCommand{.cmd = epc, .payload = std::move(payload)});
  this->enqueue_command_(ToshibaCommand{.cmd = ToshibaCommandType::DELAY, .delay = 5000});
  this->last_time_sync_ = ms;
  this->time_sync_pending_ = true;
}
#endif

/**
 * EPC 0x88 fault status. The detailed code (0x86) is only read while a fault
 * is reported (0x41).
 */
void ToshibaClimateUart::handle_fault_status_(uint8_t fault) {
  ESP_LOGD(TAG, "Received fault status: 0x%02X", fault);
  if (this->error_code_text_sensor_ == nullptr)
    return;
  if (fault == 0x41) {
    this->requestData(ToshibaCommandType::ERROR_DETAIL);
  } else if (this->error_code_text_sensor_->get_state() != "none") {
    this->error_code_text_sensor_->publish_state("none");
  }
}

/**
 * EPC 0x86 (manufacturer's fault code): 9 bytes, byte 8 is the error code
 * (0 is treated as "no code"). Published as a hex string, e.g. "0x1C".
 */
void ToshibaClimateUart::publish_error_code_(const uint8_t *edt, size_t len) {
  if (this->error_code_text_sensor_ == nullptr)
    return;
  if (len < 9) {
    ESP_LOGW(TAG, "Error detail EDT too short (%u)", static_cast<unsigned>(len));
    return;
  }
  ESP_LOGW(TAG, "Unit reports a fault, detail [%s]", format_hex_pretty(edt, len).c_str());
  if (edt[8] == 0x00) {
    this->error_code_text_sensor_->publish_state("unknown");
    return;
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02X", edt[8]);
  this->error_code_text_sensor_->publish_state(buf);
}

void ToshibaClimateUart::sync_energy_() {
  ESP_LOGV(TAG, "Syncing energy data");
  this->requestData(ToshibaCommandType::ENERGY_DAILY);
  this->last_energy_sync_ = millis();
}

void ToshibaClimateUart::estimate_wattage_(uint32_t current_energy) {
  uint32_t now = millis();
  if (this->last_energy_update_ms_ == 0 || current_energy < this->last_total_daily_energy_) {
    this->last_total_daily_energy_ = current_energy;
    this->last_energy_update_ms_ = now;
    return;
  }

  uint32_t energy_diff = current_energy - this->last_total_daily_energy_;
  uint32_t time_diff = now - this->last_energy_update_ms_;

  if (time_diff > 0 && energy_diff > 0) {
    float wattage = (energy_diff * 3600000.0f) / time_diff;
    if (this->power_sensor_ != nullptr) {
      this->power_sensor_->publish_state(wattage);
    }
  } else if (energy_diff == 0) {
    if (this->power_sensor_ != nullptr) {
      this->power_sensor_->publish_state(0);
    }
  }

  this->last_total_daily_energy_ = current_energy;
  this->last_energy_update_ms_ = now;
}

}  // namespace toshiba_suzumi
}  // namespace esphome
