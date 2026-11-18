#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "toshiba_climate.h"
#include "toshiba_climate_mode.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/core/macros.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome {
namespace toshiba_suzumi {

ESPHOME_LOG_TAG(TAG, "toshiba_suzumi");

using namespace esphome::climate;

static const uint32_t RECEIVE_TIMEOUT = 200;
static const uint32_t COMMAND_DELAY = 100;
// How long to wait for the keepalive Get reply.
static const uint32_t KEEPALIVE_REPLY_TIMEOUT = 9000;
// Counter is compared with 4; the fifth missing reply restarts the link.
static const uint8_t KEEPALIVE_FAIL_LIMIT = 4;
static const uint16_t KEEPALIVE_RECOVERY_DELAY = 1000;

namespace {

// Frame hex dumps in the log show at most this many bytes.
constexpr size_t LOG_HEX_MAX_BYTES = 64;

// Handshake frames, sent in this order with a 2 s pause before the last two.
constexpr uint8_t HANDSHAKE_0[] = {2, 255, 255, 0, 0, 0, 0, 2};
constexpr uint8_t HANDSHAKE_1[] = {2, 255, 255, 1, 0, 0, 1, 2, 254};
constexpr uint8_t HANDSHAKE_2[] = {2, 0, 0, 0, 0, 0, 2, 2, 2, 250};
constexpr uint8_t HANDSHAKE_3[] = {2, 0, 1, 129, 1, 0, 2, 0, 0, 123};
constexpr uint8_t HANDSHAKE_4[] = {2, 0, 1, 2, 0, 0, 2, 0, 0, 254};
constexpr uint8_t HANDSHAKE_5[] = {2, 0, 2, 0, 0, 0, 0, 254};  // request unit identity
constexpr uint8_t AFTER_HANDSHAKE_0[] = {2, 0, 2, 1, 0, 0, 2, 0, 0, 251};
constexpr uint8_t AFTER_HANDSHAKE_1[] = {2, 0, 2, 2, 0, 0, 2, 0, 0, 250};
// Notify ACK FD: status OK + EOJ, captured on the wire.
constexpr uint8_t NOTIFY_ACK_FD[] = {0x00, 0x00, 0x01, 0x30, 0x01};
// Request FD header: EOJ 01 30 01, then LEN(u16 BE), EPC, EDT.
constexpr uint8_t REQUEST_EOJ[] = {0x01, 0x30, 0x01};
constexpr size_t REQUEST_FD_HEADER_LEN = 6;

template<size_t N> ToshibaCommand handshake_frame(const uint8_t (&frame)[N]) {
  static_assert(N <= TX_FRAME_MAX, "handshake frame too long");
  ToshibaCommand command;
  command.kind = CommandKind::RAW;
  command.raw = frame;
  command.raw_len = N;
  return command;
}

ToshibaCommand delay_command(uint16_t delay) {
  ToshibaCommand command;
  command.cmd = ToshibaCommandType::DELAY;
  command.kind = CommandKind::DELAY;
  command.delay = delay;
  return command;
}

ToshibaCommand get_command(ToshibaCommandType cmd) {
  ToshibaCommand command;
  command.cmd = cmd;
  command.kind = CommandKind::GET;
  return command;
}

ToshibaCommand set_command(ToshibaCommandType cmd, const uint8_t *edt, size_t len) {
  ToshibaCommand command;
  command.cmd = cmd;
  command.kind = CommandKind::SET;
  command.edt_len = static_cast<uint8_t>(std::min(len, COMMAND_EDT_MAX));
  std::memcpy(command.edt, edt, command.edt_len);
  return command;
}

/**
 * FCC checksum: two's complement of the sum of every byte except STX.
 * `length` is the index of the FCC byte (i.e. total frame size - 1).
 */
uint8_t checksum(const uint8_t *data, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 1; i < length; i++) {
    sum += data[i];
  }
  return static_cast<uint8_t>(0 - sum);
}

/**
 * Build the frame of a queued command into `out` (TX_FRAME_MAX bytes) and
 * return its length:
 *   STX | FT=0x0003 | CN | FN | DL | FD | FCC
 * Requests use CN=0x10 and FN=0x00; the unit echoes FN in the reply, and
 * replies are matched by EPC.
 */
size_t encode_command(const ToshibaCommand &command, uint8_t *out) {
  if (command.kind == CommandKind::RAW) {
    std::memcpy(out, command.raw, command.raw_len);
    return command.raw_len;
  }
  uint8_t cn = FRAME_CN_PROPERTY;
  uint8_t fn = 0x00;
  uint8_t *fd = out + FRAME_HEADER_LEN;
  size_t dl;
  if (command.kind == CommandKind::NOTIFY_ACK) {
    // The unit's FN is echoed unchanged.
    cn = FRAME_CN_NOTIFY_ACK;
    fn = command.fn;
    std::memcpy(fd, NOTIFY_ACK_FD, sizeof(NOTIFY_ACK_FD));
    dl = sizeof(NOTIFY_ACK_FD);
  } else {
    // Get: 01 30 01 00 01 <EPC>. Set: 01 30 01 <LEN> <EPC> <EDT>, LEN = 1 + EDT size.
    size_t edt_len = command.kind == CommandKind::SET ? command.edt_len : 0;
    uint8_t *edt = fd + REQUEST_FD_HEADER_LEN;
    std::memcpy(edt, command.edt, edt_len);
    if (command.kind == CommandKind::SET && command.cmd == ToshibaCommandType::SCHEDULE_PROGRAM) {
      // Weekly schedule after the clock: two zero bytes, then 7 days x 4 empty programs.
      edt[edt_len++] = 0x00;
      edt[edt_len++] = 0x00;
      std::memset(edt + edt_len, 0xFF, SCHEDULE_EDT_LEN - edt_len);
      edt_len = SCHEDULE_EDT_LEN;
    }
    const size_t len = 1 + edt_len;
    std::memcpy(fd, REQUEST_EOJ, sizeof(REQUEST_EOJ));
    fd[3] = static_cast<uint8_t>(len >> 8);
    fd[4] = static_cast<uint8_t>(len & 0xFF);
    fd[5] = static_cast<uint8_t>(command.cmd);
    dl = REQUEST_FD_HEADER_LEN + edt_len;
  }
  out[0] = FRAME_STX;
  out[1] = static_cast<uint8_t>(FRAME_FT_PROPERTY >> 8);
  out[2] = static_cast<uint8_t>(FRAME_FT_PROPERTY & 0xFF);
  out[3] = cn;
  out[4] = fn;
  out[5] = static_cast<uint8_t>(dl >> 8);
  out[6] = static_cast<uint8_t>(dl & 0xFF);
  const size_t fcc_index = FRAME_HEADER_LEN + dl;
  out[fcc_index] = checksum(out, fcc_index);
  return fcc_index + 1;
}

/**
 * Copy a NUL/space-padded ASCII field from the unit-info data into `out`
 * (NUL-terminated, trailing spaces removed).
 */
void extract_unit_field(const uint8_t *data, size_t size, size_t offset, size_t max_len, char *out, size_t out_size) {
  size_t len = 0;
  for (size_t i = 0; i < max_len && offset + i < size && len + 1 < out_size; i++) {
    uint8_t c = data[offset + i];
    if (c < 0x20 || c > 0x7E)
      break;
    out[len++] = static_cast<char>(c);
  }
  while (len > 0 && out[len - 1] == ' ')
    len--;
  out[len] = '\0';
}

template<size_t N> void copy_string(char (&dst)[N], const char *src) { snprintf(dst, N, "%s", src); }

// FNV-1 over several strings, same result as fnv1_hash() of their concatenation.
uint32_t fnv1_extend(uint32_t hash, const char *str) {
  while (*str) {
    hash *= FNV1_PRIME;
    hash ^= *str++;
  }
  return hash;
}

}  // namespace

/**
 * Send the command to UART interface.
 */
void ToshibaClimateUart::send_to_uart_(const ToshibaCommand &command, uint32_t now) {
  this->tx_len_ = encode_command(command, this->tx_buf_.data());
  this->tx_sent_ = 0;
  this->last_command_timestamp_ = now;
  if (command.keepalive) {
    this->keepalive_pending_ = true;
    this->keepalive_epc_ = static_cast<uint8_t>(command.cmd);
    this->keepalive_fn_ = this->tx_buf_[4];
    this->keepalive_sent_at_ = now;
  }
  ESP_LOGV_HEX(TAG, this->tx_buf_.data(), this->tx_len_, LOG_HEX_MAX_BYTES, "Sending: [%s]");
  this->write_pending_tx_(now);
}

/**
 * Write as much of the pending frame as the UART takes without blocking.
 * Short frames go out at once; the rest of a long one in later loop() passes.
 */
void ToshibaClimateUart::write_pending_tx_(uint32_t now) {
  if (!this->tx_busy_())
    return;
  const size_t room = this->parent_->paced_write_room(this->last_tx_write_);
  const size_t len = std::min(room, this->tx_len_ - this->tx_sent_);
  if (len == 0)
    return;
  this->write_array(this->tx_buf_.data() + this->tx_sent_, len);
  this->tx_sent_ += len;
  this->last_tx_write_ = now;
  // The pause before the next command counts from the end of the frame.
  this->last_command_timestamp_ = now;
}

/**
 * Send starting handshake to initialize communication with the unit.
 */
void ToshibaClimateUart::start_handshake_() {
  ESP_LOGCONFIG(TAG, "Sending handshake...");
  this->enqueue_command_(handshake_frame(HANDSHAKE_0));
  this->enqueue_command_(handshake_frame(HANDSHAKE_1));
  this->enqueue_command_(handshake_frame(HANDSHAKE_2));
  this->enqueue_command_(handshake_frame(HANDSHAKE_3));
  this->enqueue_command_(handshake_frame(HANDSHAKE_4));
  this->enqueue_command_(handshake_frame(HANDSHAKE_5));
  this->enqueue_command_(delay_command(2000));
  this->enqueue_command_(handshake_frame(AFTER_HANDSHAKE_0));
  this->enqueue_command_(handshake_frame(AFTER_HANDSHAKE_1));
}

/**
 * Handle data in RX buffer, validate message for content and checksum.
 * All frames use STX|FT|CN|FN|DL|FD|FCC with 16-bit big-endian DL (payload
 * length). Incomplete garbage is dropped via RECEIVE timeout.
 */
bool ToshibaClimateUart::validate_message_() {
  size_t at = this->rx_len_ - 1;
  const uint8_t *data = this->rx_buf_.data();
  uint8_t new_byte = data[at];

  // Byte 0: HEADER (always 0x02)
  if (at == 0)
    return new_byte == FRAME_STX;

  // Need STX..DL_lo before the frame length is known.
  if (at < 6)
    return true;

  // Header is 7 bytes (0..6), then DL payload bytes, then FCC. Index of FCC = 7 + DL.
  uint16_t dl = (static_cast<uint16_t>(data[5]) << 8) | data[6];
  size_t last_index = FRAME_HEADER_LEN + static_cast<size_t>(dl);

  // Guard against corrupt DL that would overrun the buffer.
  if (last_index >= RX_FRAME_MAX) {
    ESP_LOGW(TAG, "Received frame with oversized DL=%u, dropping", dl);
    return false;
  }

  // wait until all data is read
  if (at < last_index)
    return true;

  // last byte: CHECKSUM
  uint8_t rx_checksum = new_byte;
  uint8_t calc_checksum = checksum(data, at);

  if (rx_checksum != calc_checksum) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_WARN
    // There is no ESP_LOGW_HEX; same stack buffer as the other *_HEX macros.
    char hex_buf[format_hex_pretty_size(LOG_HEX_MAX_BYTES)];
    ESP_LOGW(TAG, "Received invalid message checksum %02X!=%02X len=%u DATA=[%s]", rx_checksum, calc_checksum,
             static_cast<unsigned>(at + 1), format_hex_pretty_to(hex_buf, data, at + 1));
#endif
    return false;
  }

  const size_t len = at + 1;
  ESP_LOGV_HEX(TAG, data, len, LOG_HEX_MAX_BYTES, "Received: DATA=[%s]");
  // Mark the buffer free before parsing, so commands queued while parsing can
  // be sent right away. Nothing writes to the buffer until the next byte is read.
  this->rx_len_ = 0;
  this->parse_response_(data, len);

  // return false to reset rx buffer
  return false;
}

void ToshibaClimateUart::enqueue_command_(const ToshibaCommand &command) {
  if (!this->command_queue_.push_back(command)) {
    ESP_LOGW(TAG, "Command queue full, dropping EPC 0x%02X", static_cast<uint8_t>(command.cmd));
    return;
  }
  this->process_command_queue_();
}

/**
 * ACK an unsolicited status frame (CN=0x11).
 * Queued at the front so it is not stuck behind a pending Get/Set.
 */
void ToshibaClimateUart::send_notify_ack_(uint8_t fn) {
  ToshibaCommand command;
  command.kind = CommandKind::NOTIFY_ACK;
  command.fn = fn;
  ESP_LOGV(TAG, "Sending notify ACK FN=0x%02X", fn);
  if (!this->command_queue_.push_front(command))
    ESP_LOGW(TAG, "Command queue full, dropping notify ACK");
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
bool find_property(uint8_t cn, const uint8_t *fd, size_t dl, PropertyRecord *out, bool *rejected) {
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

void ToshibaClimateUart::send_cmd_(ToshibaCommandType cmd, uint8_t value) {
  ESP_LOGV(TAG, "Sending Set EPC=0x%02X value=0x%02X", static_cast<uint8_t>(cmd), value);
  this->enqueue_command_(set_command(cmd, &value, 1));
}

void ToshibaClimateUart::request_data_(ToshibaCommandType cmd) {
  for (size_t i = 0; i < this->command_queue_.size(); i++) {
    const auto &queued = this->command_queue_[i];
    if (queued.cmd == cmd && queued.kind == CommandKind::GET && !queued.keepalive) {
      ESP_LOGV(TAG, "Get EPC=0x%02X already queued", static_cast<uint8_t>(cmd));
      return;
    }
  }
  ESP_LOGV(TAG, "Requesting Get EPC=0x%02X", static_cast<uint8_t>(cmd));
  this->enqueue_command_(get_command(cmd));
}

/**
 * 0xF8 bundle Set: 01 30 01 00 05 F8 <mode> <temp> <fan> <merit>.
 * Used for any of these four on units with a feature mask (profile 2/3), so the
 * setpoint and the special mode always reach the unit together.
 */
void ToshibaClimateUart::send_bundle_(uint8_t mode, uint8_t temp, uint8_t fan, uint8_t merit) {
  const uint8_t edt[] = {mode, temp, fan, merit};
  ESP_LOGV(TAG, "Sending bundle mode=0x%02X temp=%d fan=0x%02X merit=0x%02X", mode, temp, fan, merit);
  this->enqueue_command_(set_command(ToshibaCommandType::BUNDLE, edt, sizeof(edt)));
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
  return this->raw_merit_.has_value() && (this->raw_merit_.value() & 0x0F) == SpecialMode::EIGHT_DEG;
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
    ESP_LOGV(TAG, "Special mode \"%s\" active, setpoint %d is %d °C", SPECIAL_MODE_EIGHT_DEG,
             this->raw_target_temp_.value(), value);
  }
  this->target_temperature = value;
}

void ToshibaClimateUart::publish_special_mode_preset_(uint8_t merit) {
  auto preset_string = special_mode_to_preset(static_cast<SpecialMode>(merit));
  if (std::find(this->supported_presets_.begin(), this->supported_presets_.end(), preset_string) ==
      this->supported_presets_.end()) {
    // Not offered in Home Assistant (e.g. "Standard" left out of the list):
    // show no preset rather than keeping a stale one such as "8 degrees".
    if (!this->supported_presets_.empty())
      this->climate_changed_ |= this->set_preset_(climate::CLIMATE_PRESET_NONE);
    return;
  }
  auto climate_preset = special_mode_to_standard_preset(static_cast<SpecialMode>(merit));
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
  if (this->fcu_model_[0] == '\0' && this->fcu_serial_[0] == '\0')
    return 0;
  // Same value as fnv1_hash(model + "|" + serial), which older versions stored.
  return fnv1_extend(fnv1_extend(fnv1_extend(FNV1_OFFSET_BASIS, this->fcu_model_), "|"), this->fcu_serial_);
}

void ToshibaClimateUart::load_capabilities_() {
  this->caps_pref_ = this->make_entity_preference<UnitCapabilities>(CAPS_MAGIC);
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
  if (this->fcu_model_[0] != '\0')
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
  // The app ignores bit 6 on profile 2, but profile 2 units with a horizontal
  // louver (RAS-10PKVPG, 0xEE 7C) set it and those without (RAS-10PKVSG, 0C) do not.
  if (b1 & 0x40)
    f |= FEATURE_LR_LOUVER;
  if (profile == 3) {
    if (b2 & 0x01)
      f |= FEATURE_ENERGY;
    if (b2 & 0x02)
      f |= FEATURE_LOUVER_FIXED;
  }
  if (b2 & 0x04)
    f |= FEATURE_HADA;
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
  const bool same_unit =
      this->caps_loaded_ && (hash == 0 || this->caps_.unit_hash == 0 || hash == this->caps_.unit_hash);
  if (same_unit)
    features |= this->caps_.features;

  ESP_LOGI(TAG, "Unit capabilities 0x%05X (applied 0x%05X)%s", static_cast<unsigned>(features),
           static_cast<unsigned>(this->caps_.features), same_unit ? "" : ", new unit");
  // Without an identity reply this boot, keep the stored profile.
  const uint8_t profile = this->unit_profile_ != 0 ? this->unit_profile_ : this->caps_.profile;
  if (this->caps_loaded_ && features == this->caps_.features && this->caps_.profile == profile) {
    return;
  }
  // Entities can be shown or hidden only during setup(), and Home Assistant reads
  // the climate traits only when it connects, so any change restarts the device
  // once. For the same unit features are only added (same_unit merged the stored
  // ones in), so a late notify restarts at most once per new feature.
  this->store_capabilities_and_restart_(
      features, this->caps_loaded_ ? (same_unit ? "new features found" : "different unit") : "first detection");
}

uint32_t ToshibaClimateUart::store_capabilities_(uint32_t features) {
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
  const uint32_t previous = this->caps_.features;
  this->caps_ = caps;
  this->caps_loaded_ = true;
  return previous;
}

void ToshibaClimateUart::store_capabilities_and_restart_(uint32_t features, const char *reason) {
  const uint32_t previous = this->store_capabilities_(features);
  this->caps_reboot_pending_ = true;
  ESP_LOGW(TAG, "Unit capabilities changed (%s): 0x%05X -> 0x%05X. Restarting to update Home Assistant entities",
           reason, static_cast<unsigned>(previous), static_cast<unsigned>(features));
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
  ESP_LOGV(TAG, "Setting ionizer %s", on ? "ON" : "OFF");
  this->send_cmd_(ToshibaCommandType::AIR_PURIFIER, on ? 0x18 : 0x10);
  if (this->ionizer_switch_ != nullptr)
    this->ionizer_switch_->publish_state(on);
}

/**
 * HADA Care Flow is a value of the swing property (EPC 0xA3 = 0x60). Off sets
 * swing off (0x31), as the Toshiba app does.
 */
void ToshibaClimateUart::set_hada_care(bool on) {
  const ToshibaSwing swing = on ? ToshibaSwing::HADA_CARE : ToshibaSwing::OFF;
  ESP_LOGV(TAG, "Setting HADA Care Flow %s", on ? "ON" : "OFF");
  this->send_cmd_(ToshibaCommandType::SWING, static_cast<uint8_t>(swing));
  this->publish_hada_care_(on);
  if (!on)
    this->publish_vertical_air_direction_(swing);
  if (this->swing_mode != climate::CLIMATE_SWING_OFF) {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->publish_state();
  }
}

void ToshibaClimateUart::publish_hada_care_(bool on) {
  if (this->hada_care_switch_ != nullptr && this->hada_care_switch_->state != on)
    this->hada_care_switch_->publish_state(on);
}

bool ToshibaClimateUart::has_idu_diagnostics_(bool probe) const {
  return this->poll_enabled_(this->fcu_tc_temp_sensor_, probe) ||
         this->poll_enabled_(this->fcu_tcj_temp_sensor_, probe) ||
         this->poll_enabled_(this->fcu_fan_rpm_sensor_, probe);
}

bool ToshibaClimateUart::has_odu_diagnostics_(bool probe) const {
  return this->poll_enabled_(this->cdu_td_temp_sensor_, probe) ||
         this->poll_enabled_(this->cdu_ts_temp_sensor_, probe) ||
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
    this->request_data_(ToshibaCommandType::IDU_STATUS);
  if (this->has_odu_diagnostics_(probe))
    this->request_data_(ToshibaCommandType::ODU_STATUS);
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
  for (size_t i = 0; i < this->command_queue_.size(); i++) {
    if (this->command_queue_[i].keepalive)
      return;
  }
  const bool extended = this->unit_profile_ == 3 && static_cast<int8_t>(this->cap_level_) >= 3;
  const auto cmd = extended ? ToshibaCommandType::FILTER_STATUS : ToshibaCommandType::POWER_STATE;
  ESP_LOGV(TAG, "Keepalive Get EPC=0x%02X", static_cast<uint8_t>(cmd));
  ToshibaCommand command = get_command(cmd);
  command.keepalive = true;
  this->enqueue_command_(command);
}

bool ToshibaClimateUart::keepalive_reply_matches_(uint8_t cn, uint8_t fn, uint8_t epc) const {
  return this->keepalive_pending_ && cn == FRAME_CN_PROPERTY_REPLY && epc == this->keepalive_epc_ &&
         fn == this->keepalive_fn_;
}

bool ToshibaClimateUart::note_keepalive_result_(bool ok) {
  this->keepalive_pending_ = false;
  if (ok) {
    this->keepalive_fails_ = 0;
    this->status_clear_warning();
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
  // Shown in Home Assistant until a keepalive gets a reply again.
  this->status_set_warning();
  this->command_queue_.clear();
  this->rx_len_ = 0;
  this->keepalive_pending_ = false;
  this->status_init_started_ = false;
  // The unit may have lost power (and its clock); sync again once the link is up.
  this->time_synced_ = false;
  this->time_sync_pending_ = false;
  this->time_sync_attempts_ = 0;
  const uint32_t now = App.get_loop_component_start_time();
  this->next_time_sync_at_ = now;
  this->last_command_timestamp_ = now;
  this->enqueue_command_(delay_command(KEEPALIVE_RECOVERY_DELAY));
  this->start_handshake_();
  this->set_wifi_led(!this->wifi_led_disabled_);
}

void ToshibaClimateUart::start_status_reads_() {
  if (this->status_init_started_)
    return;
  this->status_init_started_ = true;
  this->request_init_data_();
  if (this->auto_detect_) {
    // Gets above double as probes: the unit does not answer EPCs it lacks.
    this->set_timeout("toshiba_caps", CAPS_EVALUATE_DELAY, [this]() { this->evaluate_capabilities_(); });
  }
  if (this->unit_profile_ == 2 || this->unit_profile_ == 3) {
    // The feature mask 0xEE is read once at init on profile 2/3 units.
    this->request_data_(ToshibaCommandType::MERIT_SUPPORT);
  }
  this->request_diagnostic_reads_(true);
}

void ToshibaClimateUart::request_init_data_() {
  ESP_LOGV(TAG, "Requesting initial data from AC unit");
  this->request_data_(ToshibaCommandType::POWER_STATE);
  if (this->self_clean_sensor_ != nullptr) {
    this->request_data_(ToshibaCommandType::SELF_CLEAN);
  }
  this->request_data_(ToshibaCommandType::MODE);
  // Merit before setpoint: 0xB3 is +16 while 8 °C mode is active.
  this->request_data_(ToshibaCommandType::SPECIAL_MODE);
  this->request_data_(ToshibaCommandType::TARGET_TEMP);
  this->request_data_(ToshibaCommandType::FAN);
  this->request_data_(ToshibaCommandType::POWER_SEL);
  if (this->ionizer_switch_ != nullptr && this->unit_profile_ != 1) {
    // 0xC7 is read on profile 2/3 units only.
    this->request_data_(ToshibaCommandType::AIR_PURIFIER);
  }
  this->request_data_(ToshibaCommandType::SWING);
  this->request_data_(ToshibaCommandType::ROOM_TEMP);
  if (this->outdoor_temp_sensor_ != nullptr) {
    this->request_data_(ToshibaCommandType::OUTDOOR_TEMP);
  }
  if (this->energy_enabled_()) {
    this->request_data_(ToshibaCommandType::ENERGY_DAILY);
  }
  if (this->has_off_timer_sensors_()) {
    this->request_data_(ToshibaCommandType::COMFORT_SLEEP);
  }
  if (this->error_code_text_sensor_ != nullptr) {
    // 0x88 first, 0x86 only on a fault.
    this->request_data_(ToshibaCommandType::FAULT_STATUS);
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
 * This is needed because the Toshiba AC has more fan levels and presets than the standard climate component supports.
 * see https://developers.esphome.io/blog/2026/04/09/climate-and-fan-custom-mode-vectors-moved-to-entity/
 */
void ToshibaClimateUart::configure_supported_custom_modes_() {
  // Toshiba AC has more fan levels than the standard climate component supports.
  this->set_supported_custom_fan_modes({CUSTOM_FAN_LEVEL_2, CUSTOM_FAN_LEVEL_4});

  // go over all defined presets and filter out the ones that are supported by the climate component
  if (!this->supported_presets_.empty()) {
    std::vector<const char *> custom_preset_names;
    for (const char *preset_string : this->supported_presets_) {
      if (!standard_preset_from_name(preset_string).has_value()) {
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
  this->presets_explicit_ = !this->supported_presets_.empty();
  if (this->auto_detect_) {
    this->load_capabilities_();
    this->apply_capabilities_();
    if (this->caps_loaded_) {
      ESP_LOGI(TAG, "Applying stored unit capabilities 0x%05X (profile %u)",
               static_cast<unsigned>(this->caps_.features), this->caps_.profile);
    } else {
      ESP_LOGI(TAG, "No stored unit capabilities yet; detecting them (the device restarts once afterwards)");
    }
  }
  this->configure_supported_custom_modes_();
  // Handshake first. Status Gets are queued once the identity reply is
  // classified (or from update() if that reply never arrives).
  this->start_handshake_();
  // Set Wi-Fi LED initial state
  this->set_wifi_led(!this->wifi_led_disabled_);
}

/**
 * Detect RX timeout and send next command in the queue to the unit.
 */
void ToshibaClimateUart::process_command_queue_() {
  // One time base for all timestamps: mixing it with millis() could make
  // `now` older than a stored timestamp.
  const uint32_t now = App.get_loop_component_start_time();

  uint32_t since_last_command = now - this->last_command_timestamp_;

  // when we have not processed message and timeout since last received byte has expired,
  // we likely won't receive any more data and there is nothing we can do with the message as it's
  // format is was not recognized by validate_message_ function.
  // Nothing to do - drop the message to free up communication and allow to send next command.
  if (now - this->last_rx_char_timestamp_ > RECEIVE_TIMEOUT) {
    this->rx_len_ = 0;
  }

  if (this->keepalive_pending_ && now - this->keepalive_sent_at_ > KEEPALIVE_REPLY_TIMEOUT) {
    if (this->note_keepalive_result_(false))
      return;
  }

  // when there is no RX message and there is a command to send
  if (since_last_command > COMMAND_DELAY && !this->command_queue_.empty() && this->rx_len_ == 0 && !this->tx_busy_()) {
    const ToshibaCommand next_command = this->command_queue_.front();
    if (next_command.kind == CommandKind::DELAY && since_last_command < next_command.delay) {
      // delay command did not finished yet
      return;
    }
    this->command_queue_.pop_front();
    // DELAY commands don't send data over UART, just remove them from queue
    if (next_command.kind == CommandKind::DELAY)
      return;
    this->send_to_uart_(next_command, now);
  }
}

/**
 * Handle received byte from UART
 */
void ToshibaClimateUart::handle_rx_byte_(uint8_t c) {
  // validate_message_() drops a frame before it can outgrow the buffer.
  if (this->rx_len_ >= RX_FRAME_MAX)
    this->rx_len_ = 0;
  this->rx_buf_[this->rx_len_++] = c;
  if (!this->validate_message_()) {
    this->rx_len_ = 0;
  } else {
    this->last_rx_char_timestamp_ = App.get_loop_component_start_time();
  }
}

void ToshibaClimateUart::loop() {
  // Reads only what is available now, so a unit sending without pause cannot keep loop() busy.
  this->read_available([this](uint8_t c) { this->handle_rx_byte_(c); });
  this->write_pending_tx_(App.get_loop_component_start_time());
  this->process_command_queue_();
#ifdef USE_TIME
  // Checked here rather than in update(): the fallback retries need a shorter
  // period than the default 120 s update interval. The check itself only
  // compares timestamps.
  if (this->time_ != nullptr && this->status_init_started_) {
    this->check_time_sync_(App.get_loop_component_start_time());
  }
#endif
}

namespace {
void parse_model_info_record(const uint8_t *record, UnitModelInfo *info) {
  const size_t n = MODEL_INFO_RECORD_LEN;
  extract_unit_field(record, n, MODEL_INFO_MODEL_OFF, MODEL_INFO_MODEL_LEN, info->model, sizeof(info->model));
  extract_unit_field(record, n, MODEL_INFO_SERIAL_OFF, MODEL_INFO_SERIAL_LEN, info->serial, sizeof(info->serial));
  extract_unit_field(record, n, MODEL_INFO_FIRMWARE_OFF, MODEL_INFO_FIRMWARE_LEN, info->firmware,
                     sizeof(info->firmware));
}
}  // namespace

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
  parse_model_info_record(fcu, &this->fcu_info_);
  parse_model_info_record(cdu, &this->cdu_info_);
  const size_t n = MODEL_INFO_RECORD_LEN;
  char fcu_eeprom[MODEL_INFO_EEPROM_LEN + 1];
  char cdu_eeprom[MODEL_INFO_EEPROM_LEN + 1];
  extract_unit_field(fcu, n, MODEL_INFO_EEPROM_OFF, MODEL_INFO_EEPROM_LEN, fcu_eeprom, sizeof(fcu_eeprom));
  extract_unit_field(cdu, n, MODEL_INFO_EEPROM_OFF, MODEL_INFO_EEPROM_LEN, cdu_eeprom, sizeof(cdu_eeprom));
  ESP_LOGI(TAG,
           "FCU model='%s' serial='%s' firmware='%s' eeprom='%s'\n"
           "CDU model='%s' serial='%s' firmware='%s' eeprom='%s'",
           this->fcu_info_.model, this->fcu_info_.serial, this->fcu_info_.firmware, fcu_eeprom, this->cdu_info_.model,
           this->cdu_info_.serial, this->cdu_info_.firmware, cdu_eeprom);

  // The handshake identity is the primary source of the FCU model and serial;
  // use 0xE0 only when the identity reply did not carry them.
  if (this->fcu_model_[0] == '\0' && this->fcu_info_.model[0] != '\0') {
    copy_string(this->fcu_model_, this->fcu_info_.model);
    if (this->fcu_model_text_sensor_ != nullptr)
      this->fcu_model_text_sensor_->publish_state(this->fcu_model_);
  }
  if (this->fcu_serial_[0] == '\0' && this->fcu_info_.serial[0] != '\0') {
    copy_string(this->fcu_serial_, this->fcu_info_.serial);
    if (this->fcu_serial_text_sensor_ != nullptr)
      this->fcu_serial_text_sensor_->publish_state(this->fcu_serial_);
  }

  if (this->fcu_firmware_text_sensor_ != nullptr && this->fcu_info_.firmware[0] != '\0')
    this->fcu_firmware_text_sensor_->publish_state(this->fcu_info_.firmware);
  if (this->cdu_model_text_sensor_ != nullptr && this->cdu_info_.model[0] != '\0')
    this->cdu_model_text_sensor_->publish_state(this->cdu_info_.model);
  if (this->cdu_serial_text_sensor_ != nullptr && this->cdu_info_.serial[0] != '\0')
    this->cdu_serial_text_sensor_->publish_state(this->cdu_info_.serial);
  if (this->cdu_firmware_text_sensor_ != nullptr && this->cdu_info_.firmware[0] != '\0')
    this->cdu_firmware_text_sensor_->publish_state(this->cdu_info_.firmware);
}

void ToshibaClimateUart::parse_unit_identity_(const uint8_t *data, size_t len) {
  // Any identity reply means the link works again.
  this->status_clear_warning();
  // FD[0xA4..0xA7] select the unit profile used for later Gets.
  if (len > UNIT_INFO_FD_VER0 + 3) {
    this->apply_unit_capabilities_(data[UNIT_INFO_FD_VER0], data[UNIT_INFO_FD_VER0 + 1], data[UNIT_INFO_FD_VER0 + 2],
                                   data[UNIT_INFO_FD_VER0 + 3]);
  }

  // FCU model at abs[181..], serial ("NO.xxxxx") follows. The byte at abs[180] does not
  // match the model length on every unit (seen one too high, swallowing the serial's 'N'),
  // so split at "NO." when present and use the length byte only as a fallback.
  this->fcu_model_[0] = '\0';
  this->fcu_serial_[0] = '\0';

  if (len > UNIT_INFO_MODEL_DATA) {
    size_t model_len = data[UNIT_INFO_MODEL_LEN_IDX];
    if (model_len > UNIT_INFO_MODEL_MAX)
      model_len = UNIT_INFO_MODEL_MAX;

    const size_t scan_end = std::min(len, UNIT_INFO_MODEL_DATA + UNIT_INFO_MODEL_MAX + 3);
    for (size_t i = UNIT_INFO_MODEL_DATA + 1; i + 3 <= scan_end; i++) {
      if (data[i] == 'N' && data[i + 1] == 'O' && data[i + 2] == '.') {
        model_len = i - UNIT_INFO_MODEL_DATA;
        break;
      }
    }

    extract_unit_field(data, len, UNIT_INFO_MODEL_DATA, model_len, this->fcu_model_, sizeof(this->fcu_model_));
    const size_t serial_off = UNIT_INFO_MODEL_DATA + model_len;
    extract_unit_field(data, len, serial_off, UNIT_INFO_SERIAL_MAX, this->fcu_serial_, sizeof(this->fcu_serial_));
  }

  ESP_LOGI(TAG, "FCU model='%s' serial='%s'", this->fcu_model_, this->fcu_serial_);

  if (this->fcu_model_text_sensor_ != nullptr && this->fcu_model_[0] != '\0') {
    this->fcu_model_text_sensor_->publish_state(this->fcu_model_);
  }
  if (this->fcu_serial_text_sensor_ != nullptr && this->fcu_serial_[0] != '\0') {
    this->fcu_serial_text_sensor_->publish_state(this->fcu_serial_);
  }
}

void ToshibaClimateUart::parse_response_(const uint8_t *data, size_t len) {
  // Header: [0]=STX [1..2]=FT [3]=CN [4]=FN [5..6]=DL, payload at [7..], FCC last.
  if (len < FRAME_HEADER_LEN + 1)
    return;

  const uint16_t ft = (static_cast<uint16_t>(data[1]) << 8) | data[2];
  const uint8_t cn = data[3];
  const uint8_t fn = data[4];
  const uint16_t dl = (static_cast<uint16_t>(data[5]) << 8) | data[6];
  if (len < FRAME_HEADER_LEN + 1 + dl)
    return;
  const uint8_t *fd = data + FRAME_HEADER_LEN;

  // Handshake identity reply: FT=0x0002, long payload with FCU model/serial.
  if (ft == FRAME_FT_IDENTITY && len >= UNIT_INFO_MIN_LEN) {
    ESP_LOGD(TAG, "Received unit identity frame (%u bytes) FN=0x%02X", static_cast<unsigned>(len), fn);
    this->parse_unit_identity_(data, len);
    return;
  }

  // Short handshake ACKs (FT != 0x0003) are expected during init.
  if (ft != FRAME_FT_PROPERTY) {
    ESP_LOGV_HEX(TAG, data, len, LOG_HEX_MAX_BYTES, "Handshake reply FT=0x%04X CN=0x%02X FN=0x%02X [%s]", ft, cn, fn);
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
  if (!find_property(cn, fd, dl, &prop, &rejected)) {
    if (rejected) {
      ESP_LOGD_HEX(TAG, data, len, LOG_HEX_MAX_BYTES, "Unit rejected request EPC=0x%02X CN=0x%02X FN=0x%02X [%s]",
                   prop.epc, cn, fn);
      if (this->keepalive_reply_matches_(cn, fn, prop.epc))
        this->note_keepalive_result_(false);
      if (prop.epc == static_cast<uint8_t>(ToshibaCommandType::BUNDLE) && !this->bundle_rejected_) {
        // Not expected on profile 2/3, but if a unit refuses the bundle, use
        // single Sets from now on and re-read the state it did not apply.
        ESP_LOGW(TAG, "Unit rejected the 0xF8 bundle; using single Sets from now on");
        this->bundle_rejected_ = true;
        this->request_data_(ToshibaCommandType::MODE);
        this->request_data_(ToshibaCommandType::SPECIAL_MODE);
        this->request_data_(ToshibaCommandType::TARGET_TEMP);
        this->request_data_(ToshibaCommandType::FAN);
      }
#ifdef USE_TIME
      if (prop.epc == static_cast<uint8_t>(ToshibaCommandType::SCHEDULE_PROGRAM) ||
          prop.epc == static_cast<uint8_t>(ToshibaCommandType::CLOCK_SET))
        this->on_time_sync_result_(prop.epc, false);
#endif
    } else {
      ESP_LOGD_HEX(TAG, data, len, LOG_HEX_MAX_BYTES, "Property frame without EPC CN=0x%02X FN=0x%02X DL=%u [%s]", cn,
                   fn, dl);
    }
    return;
  }
  if (prop.edt_len == 0) {
    // Set reply: EPC echoed without data.
    ESP_LOGV(TAG, "Property ACK EPC=0x%02X CN=0x%02X FN=0x%02X", prop.epc, cn, fn);
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
      if (static_cast<ToshibaFan>(value) == ToshibaFan::FAN_AUTO) {
        ESP_LOGD(TAG, "Received fan mode: AUTO");
        this->set_fan_mode_(CLIMATE_FAN_AUTO);
      } else if (static_cast<ToshibaFan>(value) == ToshibaFan::FAN_QUIET) {
        ESP_LOGD(TAG, "Received fan mode: QUIET");
        this->set_fan_mode_(CLIMATE_FAN_QUIET);
      } else if (static_cast<ToshibaFan>(value) == ToshibaFan::FAN_LOW) {
        ESP_LOGD(TAG, "Received fan mode: LOW");
        this->set_fan_mode_(CLIMATE_FAN_LOW);
      } else if (static_cast<ToshibaFan>(value) == ToshibaFan::FAN_MEDIUM) {
        ESP_LOGD(TAG, "Received fan mode: MEDIUM");
        this->set_fan_mode_(CLIMATE_FAN_MEDIUM);
      } else if (static_cast<ToshibaFan>(value) == ToshibaFan::FAN_HIGH) {
        ESP_LOGD(TAG, "Received fan mode: HIGH");
        this->set_fan_mode_(CLIMATE_FAN_HIGH);
      } else {
        auto fan_mode_name = toshiba_fan_to_custom_fan_mode(static_cast<ToshibaFan>(value));
        ESP_LOGD(TAG, "Received fan mode: %s", fan_mode_name);
        this->climate_changed_ |= this->set_custom_fan_mode_(fan_mode_name);
      }
      break;
    }
    case ToshibaCommandType::SWING: {
      ESP_LOGD(TAG, "Received swing mode: %d", value);
      auto swing = static_cast<ToshibaSwing>(value);
      this->publish_hada_care_(swing == ToshibaSwing::HADA_CARE);
      auto air_direction = vertical_air_direction_to_string(swing);
      if (air_direction != nullptr) {
        ESP_LOGD(TAG, "Received vertical air direction: %s", air_direction);
        this->publish_vertical_air_direction_(swing);
      }

      if (swing == ToshibaSwing::HADA_CARE) {
        // The louvers do not swing; the switch shows the mode.
        ESP_LOGD(TAG, "Received swing mode: HADA Care Flow");
        this->swing_mode = climate::CLIMATE_SWING_OFF;
      } else if (is_fixed_vertical_air_direction(swing)) {
        this->swing_mode = climate::CLIMATE_SWING_OFF;
      } else {
        auto climate_swing = from_toshiba_swing(swing);
        ESP_LOGD(TAG, "Received swing mode: %s", LOG_STR_ARG(climate_swing_mode_to_string(climate_swing)));
        this->swing_mode = climate_swing;
      }
      break;
    }
    case ToshibaCommandType::MODE: {
      this->raw_mode_ = value;
      auto mode = from_toshiba_mode(static_cast<ToshibaMode>(value));
      ESP_LOGD(TAG, "Received AC mode: %s", LOG_STR_ARG(climate_mode_to_string(mode)));
      if (this->power_state_ == PowerState::ON && !this->self_clean_running_) {
        this->mode = mode;
      }
      break;
    }
    case ToshibaCommandType::ROOM_TEMP:
      if (value != 127) {
        ESP_LOGD(TAG, "Received room temp: %d °C", value);
        this->current_temperature = value;
        if (this->indoor_temp_sensor_ != nullptr) {
          this->indoor_temp_sensor_->publish_state((int8_t) value);
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
            this->request_data_(ToshibaCommandType::OFF_TIMER_REMAINING);
          }
        } else {
          this->off_timer_remaining_sensor_->publish_state(0);
        }
      }
      break;
    }
    case ToshibaCommandType::OUTDOOR_TEMP:
      if (value != 127) {
        if (this->outdoor_temp_sensor_ != nullptr) {
          ESP_LOGD(TAG, "Received outdoor temp: %d °C", (int8_t) value);
          this->outdoor_temp_sensor_->publish_state((int8_t) value);
        }
      }
      break;
    case ToshibaCommandType::POWER_SEL: {
      auto pwr_level = power_level_to_string(static_cast<PowerLevel>(value));
      ESP_LOGD(TAG, "Received power select: %d", value);
      if (this->pwr_select_ != nullptr) {
        this->pwr_select_->publish_state(pwr_level);
      }
      break;
    }
    case ToshibaCommandType::FILTER_STATUS:
      // config, target (u16), counter (u16). Units and byte order are not
      // confirmed, so this is only logged. Used as the profile-3 keepalive.
      if (edt_len >= 5) {
        ESP_LOGV(TAG, "Filter status: config=0x%02X target=%02X%02X counter=%02X%02X", edt[0], edt[2], edt[1], edt[4],
                 edt[3]);
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
      auto power_state = static_cast<PowerState>(value);
      ESP_LOGD(TAG, "Received AC unit power state: %s", LOG_STR_ARG(power_state_to_string(power_state)));
      if (power_state == PowerState::OFF) {
        // AC unit was just powered off, set mode to OFF
        this->mode = climate::CLIMATE_MODE_OFF;
        this->set_self_clean_running_(false);
      } else if (this->self_clean_running_) {
        if (this->self_clean_sensor_ != nullptr) {
          ESP_LOGV(TAG, "Refreshing self-clean status after receiving AC ON state");
          this->request_data_(ToshibaCommandType::SELF_CLEAN);
        }
      } else if (this->mode == climate::CLIMATE_MODE_OFF && power_state == PowerState::ON) {
        // Unit reports ON while we believe it is off, e.g. powered on via IR
        // remote, or the start of a post-shutdown self-clean cycle. When
        // self-clean reporting is configured, query it first: the SELF_CLEAN
        // response is processed before the MODE response below, so a running
        // cycle keeps the entity OFF (the MODE response is ignored while
        // self-clean is running).
        if (this->self_clean_sensor_ != nullptr) {
          this->request_data_(ToshibaCommandType::SELF_CLEAN);
        }
        this->request_data_(ToshibaCommandType::MODE);
      }
      this->power_state_ = power_state;
      break;
    }
    case ToshibaCommandType::SELF_CLEAN: {
      auto self_clean_state = static_cast<SelfCleanState>(value & SELF_CLEAN_STATUS_MASK);
      bool was_running = this->self_clean_running_;
      if (self_clean_state == SelfCleanState::RUNNING) {
        ESP_LOGV(TAG, "Self-clean is running");
        this->set_self_clean_running_(true);
        this->mode = climate::CLIMATE_MODE_OFF;
      } else if (self_clean_state == SelfCleanState::OFF) {
        ESP_LOGV(TAG, "Self-clean is stopped");
        this->set_self_clean_running_(false);
        if (was_running) {
          // The cycle ended: either it finished (unit now off) or it was
          // interrupted by switching the unit back on. The self-clean register
          // doesn't distinguish these, and the unit only reports power state on
          // change/query (not continuously), so the cached value may be stale.
          // Query the power state to resync; the POWER_STATE handler refreshes
          // the mode when the unit turns out to be on.
          this->request_data_(ToshibaCommandType::POWER_STATE);
        }
      } else {
        ESP_LOGW(TAG, "Received unknown self-clean state: %d", value);
      }
      break;
    }
    case ToshibaCommandType::SPECIAL_MODE: {
      ESP_LOGD(TAG, "Received special mode: %s (0x%02X)", special_mode_to_preset(static_cast<SpecialMode>(value)),
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
      // Raw values in one line; 127 (temperatures) and 254+ (load, current) mean not measured.
      ESP_LOGD(TAG, "ODU status: Td=%d Ts=%d Te=%d load=%u (%.1f %%) Iac=%u A", static_cast<int8_t>(edt[0]),
               static_cast<int8_t>(edt[1]), static_cast<int8_t>(edt[2]), edt[3], edt[3] / 1.7f, edt[6]);
      if (this->cdu_td_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[0]);
        if (val != 127) {
          this->cdu_td_temp_sensor_->publish_state(val);
        }
      }
      if (this->cdu_ts_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[1]);
        if (val != 127) {
          this->cdu_ts_temp_sensor_->publish_state(val);
        }
      }
      if (this->cdu_te_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[2]);
        if (val != 127) {
          this->cdu_te_temp_sensor_->publish_state(val);
        }
      }
      if (this->cdu_load_sensor_ != nullptr) {
        // Published as the historical "load" percentage (raw / 1.7).
        uint8_t raw_val = edt[3];
        if (raw_val < 254) {
          this->cdu_load_sensor_->publish_state(raw_val / 1.7f);
        }
      }
      if (this->cdu_iac_sensor_ != nullptr) {
        uint8_t raw_val = edt[6];
        if (raw_val < 254) {
          this->cdu_iac_sensor_->publish_state(raw_val);
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
      // Raw values in one line; 127 (temperatures) means not measured.
      ESP_LOGD(TAG, "IDU status: Tc=%d Tcj=%d fan=%u", static_cast<int8_t>(edt[0]), static_cast<int8_t>(edt[1]),
               edt[2]);
      if (this->fcu_tc_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[0]);
        if (val != 127) {
          this->fcu_tc_temp_sensor_->publish_state(val);
        }
      }
      if (this->fcu_tcj_temp_sensor_ != nullptr) {
        int8_t val = static_cast<int8_t>(edt[1]);
        if (val != 127) {
          this->fcu_tcj_temp_sensor_->publish_state(val);
        }
      }
      if (this->fcu_fan_rpm_sensor_ != nullptr) {
        this->fcu_fan_rpm_sensor_->publish_state(edt[2]);
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
  ESP_LOGCONFIG(TAG, "Toshiba climate:");
  LOG_CLIMATE("", "Thermostat", this);
  if (this->fcu_model_[0] != '\0' || this->fcu_serial_[0] != '\0') {
    ESP_LOGCONFIG(TAG,
                  "  FCU Model: '%s'\n"
                  "  FCU Serial: '%s'\n"
                  "  FCU Firmware: '%s'",
                  this->fcu_model_, this->fcu_serial_, this->fcu_info_.firmware);
  }
  if (this->cdu_info_.model[0] != '\0' || this->cdu_info_.serial[0] != '\0') {
    ESP_LOGCONFIG(TAG, "  CDU Model: '%s' Serial: '%s' Firmware: '%s'", this->cdu_info_.model, this->cdu_info_.serial,
                  this->cdu_info_.firmware);
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
  if (!this->auto_detect_) {
    ESP_LOGCONFIG(TAG, "  Auto-detection: disabled");
  } else if (this->caps_loaded_) {
    const auto shown = std::count_if(this->auto_entities_.begin(), this->auto_entities_.end(),
                                     [this](const AutoEntity &e) { return this->has_feature_(e.feature); });
    ESP_LOGCONFIG(TAG,
                  "  Auto-detected capabilities: 0x%05X (profile %u, 0xEE %02X %02X), %u of %u optional entities "
                  "shown",
                  static_cast<unsigned>(this->caps_.features), this->caps_.profile, this->caps_.merit_mask[0],
                  this->caps_.merit_mask[1], static_cast<unsigned>(shown),
                  static_cast<unsigned>(this->auto_entities_.size()));
  } else {
    ESP_LOGCONFIG(TAG, "  Auto-detected capabilities: none stored yet");
  }
  if (this->caps_volatile_) {
    ESP_LOGW(TAG, "  Detected features are lost on a power cut (the device then restarts once to detect them "
                  "again). Set 'restore_from_flash: true' under 'esp8266:' to keep them.");
  }
  // Auto-detected entities the unit does not support are internal: list them
  // on one line instead of the full entity dump.
  auto log_hidden = [this](const char *label, EntityBase *entity) {
    if (!this->is_hidden_auto_entity_(entity))
      return false;
    ESP_LOGCONFIG(TAG, "%s '%s': hidden (feature not detected)", label, LOG_STR_ARG(entity->get_log_name()));
    return true;
  };
  if (this->ionizer_switch_ != nullptr && !log_hidden("Ionizer", this->ionizer_switch_)) {
    LOG_SWITCH("", "Ionizer", this->ionizer_switch_);
  }
  if (this->hada_care_switch_ != nullptr && !log_hidden("HADA Care Flow", this->hada_care_switch_)) {
    LOG_SWITCH("", "HADA Care Flow", this->hada_care_switch_);
  }
  if (this->fcu_model_text_sensor_ != nullptr && !log_hidden("FCU Model", this->fcu_model_text_sensor_)) {
    LOG_TEXT_SENSOR("", "FCU Model", this->fcu_model_text_sensor_);
  }
  if (this->fcu_serial_text_sensor_ != nullptr && !log_hidden("FCU Serial", this->fcu_serial_text_sensor_)) {
    LOG_TEXT_SENSOR("", "FCU Serial", this->fcu_serial_text_sensor_);
  }
  if (this->fcu_firmware_text_sensor_ != nullptr && !log_hidden("FCU Firmware", this->fcu_firmware_text_sensor_)) {
    LOG_TEXT_SENSOR("", "FCU Firmware", this->fcu_firmware_text_sensor_);
  }
  if (this->cdu_model_text_sensor_ != nullptr && !log_hidden("CDU Model", this->cdu_model_text_sensor_)) {
    LOG_TEXT_SENSOR("", "CDU Model", this->cdu_model_text_sensor_);
  }
  if (this->cdu_serial_text_sensor_ != nullptr && !log_hidden("CDU Serial", this->cdu_serial_text_sensor_)) {
    LOG_TEXT_SENSOR("", "CDU Serial", this->cdu_serial_text_sensor_);
  }
  if (this->cdu_firmware_text_sensor_ != nullptr && !log_hidden("CDU Firmware", this->cdu_firmware_text_sensor_)) {
    LOG_TEXT_SENSOR("", "CDU Firmware", this->cdu_firmware_text_sensor_);
  }
  if (this->error_code_text_sensor_ != nullptr && !log_hidden("Error Code", this->error_code_text_sensor_)) {
    LOG_TEXT_SENSOR("", "Error Code", this->error_code_text_sensor_);
  }
  if (this->outdoor_temp_sensor_ != nullptr && !log_hidden("Outdoor Temp", this->outdoor_temp_sensor_)) {
    LOG_SENSOR("", "Outdoor Temp", this->outdoor_temp_sensor_);
  }
  if (this->cdu_td_temp_sensor_ != nullptr && !log_hidden("CDU Td Temp", this->cdu_td_temp_sensor_)) {
    LOG_SENSOR("", "CDU Td Temp", this->cdu_td_temp_sensor_);
  }
  if (this->cdu_ts_temp_sensor_ != nullptr && !log_hidden("CDU Ts Temp", this->cdu_ts_temp_sensor_)) {
    LOG_SENSOR("", "CDU Ts Temp", this->cdu_ts_temp_sensor_);
  }
  if (this->cdu_te_temp_sensor_ != nullptr && !log_hidden("CDU Te Temp", this->cdu_te_temp_sensor_)) {
    LOG_SENSOR("", "CDU Te Temp", this->cdu_te_temp_sensor_);
  }
  if (this->cdu_load_sensor_ != nullptr && !log_hidden("CDU Load", this->cdu_load_sensor_)) {
    LOG_SENSOR("", "CDU Load", this->cdu_load_sensor_);
  }
  if (this->cdu_iac_sensor_ != nullptr && !log_hidden("CDU IAC", this->cdu_iac_sensor_)) {
    LOG_SENSOR("", "CDU IAC", this->cdu_iac_sensor_);
  }
  if (this->fcu_tc_temp_sensor_ != nullptr && !log_hidden("FCU Tc Temp", this->fcu_tc_temp_sensor_)) {
    LOG_SENSOR("", "FCU Tc Temp", this->fcu_tc_temp_sensor_);
  }
  if (this->fcu_tcj_temp_sensor_ != nullptr && !log_hidden("FCU Tcj Temp", this->fcu_tcj_temp_sensor_)) {
    LOG_SENSOR("", "FCU Tcj Temp", this->fcu_tcj_temp_sensor_);
  }
  if (this->fcu_fan_rpm_sensor_ != nullptr && !log_hidden("FCU Fan RPM", this->fcu_fan_rpm_sensor_)) {
    LOG_SENSOR("", "FCU Fan RPM", this->fcu_fan_rpm_sensor_);
  }
  if (this->energy_sensor_ != nullptr && !log_hidden("Energy", this->energy_sensor_)) {
    LOG_SENSOR("", "Energy", this->energy_sensor_);
  }
  if (this->power_sensor_ != nullptr && !log_hidden("Power", this->power_sensor_)) {
    LOG_SENSOR("", "Power", this->power_sensor_);
  }
  if (this->pwr_select_ != nullptr && !log_hidden("Power selector", this->pwr_select_)) {
    LOG_SELECT("", "Power selector", this->pwr_select_);
  }
  if (this->vertical_air_direction_select_ != nullptr &&
      !log_hidden("Vertical air direction", this->vertical_air_direction_select_)) {
    LOG_SELECT("", "Vertical air direction", this->vertical_air_direction_select_);
  }
  if (this->self_clean_sensor_ != nullptr && !log_hidden("Self Clean", this->self_clean_sensor_)) {
    LOG_BINARY_SENSOR("", "Self Clean", this->self_clean_sensor_);
  }
  if (this->off_timer_sensor_ != nullptr && !log_hidden("OFF Timer", this->off_timer_sensor_)) {
    LOG_BINARY_SENSOR("", "OFF Timer", this->off_timer_sensor_);
  }
  if (this->off_timer_remaining_sensor_ != nullptr &&
      !log_hidden("OFF Timer Remaining", this->off_timer_remaining_sensor_)) {
    LOG_SENSOR("", "OFF Timer Remaining", this->off_timer_remaining_sensor_);
  }
  ESP_LOGCONFIG(TAG, "  Min Temp: %u", this->min_temp_);
  if (!this->supported_presets_.empty()) {
    ESP_LOGCONFIG(TAG, "  Supported presets:");
    for (const char *preset : this->supported_presets_) {
      ESP_LOGCONFIG(TAG, "    - %s", preset);
    }
  }
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
  this->request_data_(ToshibaCommandType::ROOM_TEMP);
  // Periodic reads skip what detection found unsupported; the reads queued at
  // connect (request_init_data_) still go out and act as probes.
  if (this->poll_enabled_(this->outdoor_temp_sensor_)) {
    this->request_data_(ToshibaCommandType::OUTDOOR_TEMP);
  }
  if (this->self_clean_running_) {
    this->request_data_(ToshibaCommandType::SELF_CLEAN);
  }
  if (this->poll_enabled_(this->off_timer_sensor_) || this->poll_enabled_(this->off_timer_remaining_sensor_)) {
    // The unit doesn't push the remaining time, so poll it while a timer runs.
    this->request_data_(ToshibaCommandType::COMFORT_SLEEP);
    if (this->off_timer_active_ && this->off_timer_remaining_sensor_ != nullptr) {
      this->request_data_(ToshibaCommandType::OFF_TIMER_REMAINING);
    }
  }
  if (this->poll_enabled_(this->error_code_text_sensor_)) {
    this->request_data_(ToshibaCommandType::FAULT_STATUS);
  }
  this->request_diagnostic_reads_(false);

  const uint32_t now = App.get_loop_component_start_time();

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
    ESP_LOGV(TAG, "Setting mode to %s", LOG_STR_ARG(climate_mode_to_string(mode)));
    if (mode != CLIMATE_MODE_OFF) {
      this->set_self_clean_running_(false);
    }
    if (this->mode == CLIMATE_MODE_OFF && mode != CLIMATE_MODE_OFF) {
      ESP_LOGV(TAG, "Setting AC unit power state to ON.");
      this->send_cmd_(ToshibaCommandType::POWER_STATE, static_cast<uint8_t>(PowerState::ON));
    }
    if (mode == CLIMATE_MODE_OFF) {
      ESP_LOGV(TAG, "Setting AC unit power state to OFF.");
      this->send_cmd_(ToshibaCommandType::POWER_STATE, static_cast<uint8_t>(PowerState::OFF));
    } else {
      new_mode = static_cast<uint8_t>(to_toshiba_mode(mode));
    }
    this->mode = mode;
  }

  if (call.get_fan_mode().has_value()) {
    auto fan_mode = *call.get_fan_mode();
    ESP_LOGV(TAG, "Setting fan mode to %s", LOG_STR_ARG(climate_fan_mode_to_string(fan_mode)));
    this->set_fan_mode_(fan_mode);
    auto fan_value = to_toshiba_fan(fan_mode);
    if (fan_value.has_value()) {
      new_fan = static_cast<uint8_t>(fan_value.value());
    }
  }

  if (call.has_custom_fan_mode()) {
    auto fan_mode = call.get_custom_fan_mode();
    auto custom_fan = custom_fan_mode_to_toshiba(fan_mode.c_str());
    if (custom_fan.has_value()) {
      ESP_LOGV(TAG, "Setting fan mode to custom: %s", fan_mode.c_str());
      this->set_custom_fan_mode_(fan_mode);
      new_fan = static_cast<uint8_t>(custom_fan.value());
    }
  }

  if (call.get_swing_mode().has_value()) {
    auto swing_mode = *call.get_swing_mode();
    auto function_value = to_toshiba_swing(swing_mode);
    ESP_LOGV(TAG, "Setting swing mode to %s", LOG_STR_ARG(climate_swing_mode_to_string(swing_mode)));
    this->swing_mode = swing_mode;
    this->send_cmd_(ToshibaCommandType::SWING, static_cast<uint8_t>(function_value));
    this->publish_vertical_air_direction_(function_value);
    // Swing and HADA Care Flow share EPC 0xA3: a swing mode ends HADA.
    this->publish_hada_care_(false);
  }

  // Requested merit (special mode) from a standard or custom preset.
  optional<uint8_t> new_merit;
  if (call.get_preset().has_value()) {
    auto preset_string = standard_preset_name(*call.get_preset());
    ESP_LOGV(TAG, "Setting preset to %s", preset_string);
    auto special_mode = preset_to_special_mode(preset_string);
    if (special_mode.has_value()) {
      new_merit = static_cast<uint8_t>(special_mode.value());
    } else {
      ESP_LOGW(TAG, "Unknown preset: %s", preset_string);
    }
  }
  if (call.has_custom_preset()) {
    auto custom_preset = call.get_custom_preset();
    ESP_LOGV(TAG, "Setting custom preset to %s", custom_preset.c_str());
    auto special_mode = preset_to_special_mode(custom_preset.c_str());
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
  uint8_t merit = new_merit.value_or(this->raw_merit_.value_or(SpecialMode::STANDARD));
  bool eight_deg = (merit & 0x0F) == SpecialMode::EIGHT_DEG;
  optional<uint8_t> new_temp;
  if (call.get_target_temperature().has_value()) {
    int temp = static_cast<int>(lroundf(*call.get_target_temperature()));
    if (!new_merit.has_value()) {
      if (temp < MIN_TEMP_STANDARD && !eight_deg) {
        ESP_LOGV(TAG, "Setpoint %d °C: switching to \"%s\"", temp, SPECIAL_MODE_EIGHT_DEG);
        merit = SpecialMode::EIGHT_DEG;
        new_merit = merit;
      } else if (temp >= MIN_TEMP_STANDARD && eight_deg) {
        ESP_LOGV(TAG, "Setpoint %d °C: leaving \"%s\"", temp, SPECIAL_MODE_EIGHT_DEG);
        merit = SpecialMode::STANDARD;
        new_merit = merit;
      }
      eight_deg = (merit & 0x0F) == SpecialMode::EIGHT_DEG;
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
    new_mode = static_cast<uint8_t>(ToshibaMode::HEAT);
    if (this->mode != CLIMATE_MODE_OFF)
      this->mode = CLIMATE_MODE_HEAT;
  }
  optional<uint8_t> new_raw_temp;
  if (new_temp.has_value()) {
    new_raw_temp = static_cast<uint8_t>(new_temp.value() + (eight_deg ? SPECIAL_TEMP_OFFSET : 0));
    ESP_LOGV(TAG, "Setting target temp to %d (setpoint %d)", new_temp.value(), new_raw_temp.value());
  }

  if (new_mode.has_value() || new_fan.has_value() || new_merit.has_value() || new_raw_temp.has_value()) {
    if (this->use_bundle_()) {
      this->send_bundle_(new_mode.value_or(this->raw_mode_.value()),
                         new_raw_temp.value_or(this->raw_target_temp_.value()),
                         new_fan.value_or(this->raw_fan_.value()), merit);
    } else {
      if (this->unit_profile_ == 2 || this->unit_profile_ == 3) {
        ESP_LOGV(TAG, "Unit state not fully known yet, sending single Sets instead of the bundle");
      }
      if (new_mode.has_value())
        this->send_cmd_(ToshibaCommandType::MODE, new_mode.value());
      if (new_fan.has_value())
        this->send_cmd_(ToshibaCommandType::FAN, new_fan.value());
      // Merit first, so the unit reads the setpoint in the right mode.
      if (new_merit.has_value())
        this->send_cmd_(ToshibaCommandType::SPECIAL_MODE, merit);
      if (new_raw_temp.has_value())
        this->send_cmd_(ToshibaCommandType::TARGET_TEMP, new_raw_temp.value());
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
  } else if (this->heat_mode_disabled_) {
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_COOL,
                                climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
  } else {
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT_COOL, climate::CLIMATE_MODE_COOL,
                                climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY});
  }

  if (this->horizontal_swing_) {
    traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                                      climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH});
  } else {
    traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL});
  }
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);

  // Toshiba AC has more fan levels that standard climate component, we have to use custom.
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
      auto climate_preset = standard_preset_from_name(preset_string);
      if (climate_preset.has_value()) {
        // preset is supported by the climate component
        traits.add_supported_preset(*climate_preset);
      }
    }
  }
  return traits;
}

void ToshibaClimateUart::on_set_pwr_level_(size_t index) {
  auto pwr_level = power_level_from_index(index);
  if (!pwr_level.has_value()) {
    ESP_LOGW(TAG, "Unknown power level option %u", static_cast<unsigned>(index));
    return;
  }
  ESP_LOGV(TAG, "Setting power level to %s", power_level_to_string(pwr_level.value()));
  this->send_cmd_(ToshibaCommandType::POWER_SEL, static_cast<uint8_t>(pwr_level.value()));
  this->pwr_select_->publish_state(index);
}

void ToshibaClimateUart::on_set_vertical_air_direction_(size_t index) {
  auto position = vertical_air_direction_from_index(index);
  if (!position.has_value()) {
    ESP_LOGW(TAG, "Unknown vertical air direction option %u", static_cast<unsigned>(index));
    return;
  }

  ESP_LOGV(TAG, "Setting vertical air direction to %s", vertical_air_direction_to_string(position.value()));
  this->send_cmd_(ToshibaCommandType::SWING, static_cast<uint8_t>(position.value()));
  this->publish_hada_care_(false);
  this->publish_vertical_air_direction_(position.value());
  if (position.value() == ToshibaSwing::VERTICAL || position.value() == ToshibaSwing::BOTH) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }
  this->publish_state();
}

void ToshibaClimateUart::publish_vertical_air_direction_(ToshibaSwing swing_mode) {
  if (this->vertical_air_direction_select_ == nullptr) {
    return;
  }
  auto position = vertical_air_direction_to_string(swing_mode);
  if (position != nullptr) {
    this->vertical_air_direction_select_->publish_state(position);
  }
}

/**
 * Control the indoor-unit Wi-Fi LED.
 * ON:  DE=0x05, DF=0x00
 * OFF: DE=0x00, DF=0x80
 * Units use one of the two registers; writing both covers either.
 * BLINK (DE=0x01) exists but is not exposed here.
 */
void ToshibaClimateUart::set_wifi_led(bool enabled) {
  if (enabled) {
    ESP_LOGV(TAG, "Turning ON Wi-Fi LED");
    this->send_cmd_(ToshibaCommandType::WIFI_LED_1, static_cast<uint8_t>(WifiLed::DE_ON));
    this->send_cmd_(ToshibaCommandType::WIFI_LED_2, static_cast<uint8_t>(WifiLed::DF_ON));
  } else {
    ESP_LOGV(TAG, "Turning OFF Wi-Fi LED");
    this->send_cmd_(ToshibaCommandType::WIFI_LED_1, static_cast<uint8_t>(WifiLed::DE_OFF));
    this->send_cmd_(ToshibaCommandType::WIFI_LED_2, static_cast<uint8_t>(WifiLed::DF_OFF));
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
      const auto epc =
          this->time_sync_use_clock_epc_ ? ToshibaCommandType::CLOCK_SET : ToshibaCommandType::SCHEDULE_PROGRAM;
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
  const uint32_t now = App.get_loop_component_start_time();
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
  const uint32_t ms = App.get_loop_component_start_time();
  auto now = this->time_->now();
  if (!now.is_valid()) {
    ESP_LOGD(TAG, "Time sync postponed: time source is not valid yet.");
    this->next_time_sync_at_ = ms + TIME_SYNC_CLOCK_WAIT;
    return;
  }

  const uint8_t clock[] = {
      static_cast<uint8_t>((now.year - 2000) + 100),  // years since 1900
      static_cast<uint8_t>(now.month - 1),            // 0..11
      now.day_of_month,
      now.hour,
      now.minute,
      now.second,
      static_cast<uint8_t>(now.day_of_week - 1),  // Sunday=0
  };
  // 0x98: plain clock set, 01 30 01 00 08 98 <7 bytes>. 0x99: the same clock
  // followed by an empty weekly schedule, added when the frame is built.
  const ToshibaCommandType epc =
      this->time_sync_use_clock_epc_ ? ToshibaCommandType::CLOCK_SET : ToshibaCommandType::SCHEDULE_PROGRAM;

  ESP_LOGI(TAG, "Syncing time to AC unit (EPC 0x%02X, attempt %u): %04d-%02d-%02d %02d:%02d:%02d",
           static_cast<uint8_t>(epc), this->time_sync_attempts_ + 1, now.year, now.month, now.day_of_month, now.hour,
           now.minute, now.second);

  // Queue the frame and a 5-second pause to prevent collisions with other traffic.
  this->enqueue_command_(set_command(epc, clock, sizeof(clock)));
  this->enqueue_command_(delay_command(5000));
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
    this->request_data_(ToshibaCommandType::ERROR_DETAIL);
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
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_WARN
  {
    char hex_buf[format_hex_pretty_size(LOG_HEX_MAX_BYTES)];
    ESP_LOGW(TAG, "Unit reports a fault, detail [%s]", format_hex_pretty_to(hex_buf, edt, len));
  }
#endif
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
  this->request_data_(ToshibaCommandType::ENERGY_DAILY);
  this->last_energy_sync_ = App.get_loop_component_start_time();
}

void ToshibaClimateUart::estimate_wattage_(uint32_t current_energy) {
  const uint32_t now = App.get_loop_component_start_time();
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
