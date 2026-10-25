// SPDX-License-Identifier: GPL-3.0-or-later
// Hagrid protocol: see root NOTICE for upstream openScale attribution.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <limits>
#include "esp_log.h"
#include "esp_random.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/time.h"

#include "mbedtls/md.h"
#include "mbedtls/aes.h"

namespace huawei_scale {

static const char *const TAG = "huawei_scale";

static const uint8_t FRAME_WRITE_PLAIN = 0xDB;
static const uint8_t FRAME_WRITE_ENCRYPTED = 0xDC;
static const uint8_t FRAME_NOTIFY_PLAIN = 0xBD;
static const uint8_t FRAME_NOTIFY_ENCRYPTED = 0xCD;

static const size_t SIMPLE_CHUNK_BYTES = 15;
static const size_t USER_INFO_PAYLOAD_BYTES = 69;
static const size_t MANAGER_HUID_BYTES = 30;
static constexpr size_t MAX_WSP_BYTES = SIMPLE_CHUNK_BYTES * 16;
static constexpr size_t MAX_PENDING_FRAMES = 16;
static constexpr uint32_t TX_TIMEOUT_MS = 5000;
static constexpr uint32_t SESSION_TIMEOUT_MS = 15000;
static constexpr uint32_t RX_TIMEOUT_MS = 15000;

static const uint16_t CRC16_TABLE[256] = {
    0, 4129, 8258, 12387, 16516, 20645, 24774, 28903, 33032, 37161, 41290, 45419, 49548, 53677, 57806, 61935,
    4657, 528, 12915, 8786, 21173, 17044, 29431, 25302, 37689, 33560, 45947, 41818, 54205, 50076, 62463, 58334,
    9314, 13379, 1056, 5121, 25830, 29895, 17572, 21637, 42346, 46411, 34088, 38153, 58862, 62927, 50604, 54669,
    13907, 9842, 5649, 1584, 30423, 26358, 22165, 18100, 46939, 42874, 38681, 34616, 63455, 59390, 55197, 51132,
    18628, 22757, 26758, 30887, 2112, 6241, 10242, 14371, 51660, 55789, 59790, 63919, 35144, 39273, 43274, 47403,
    23285, 19156, 31415, 27286, 6769, 2640, 14899, 10770, 56317, 52188, 64447, 60318, 39801, 35672, 47931, 43802,
    27814, 31879, 19684, 23749, 11298, 15363, 3168, 7233, 60846, 64911, 52716, 56781, 44330, 48395, 36200, 40265,
    32407, 28342, 24277, 20212, 15891, 11826, 7761, 3696, 65439, 61374, 57309, 53244, 48923, 44858, 40793, 36728,
    37256, 33193, 45514, 41451, 53516, 49453, 61774, 57711, 4224, 97, 12482, 8419, 20484, 16421, 28742, 24679,
    33721, 37784, 41979, 46042, 49981, 54044, 58239, 62302, 689, 4752, 8947, 13010, 16949, 21012, 25207, 29270,
    46570, 42443, 38312, 34185, 62830, 58703, 54572, 50445, 13538, 9411, 5280, 1153, 29798, 25671, 21540, 17413,
    42971, 47098, 34713, 38840, 59231, 63358, 50973, 55100, 9939, 14066, 1681, 5808, 26199, 30326, 17941, 22068,
    55628, 51565, 63758, 59695, 39368, 35305, 47498, 43435, 22596, 18533, 30726, 26663, 6336, 2273, 14466, 10403,
    52093, 56156, 60223, 64286, 35833, 39896, 43963, 48026, 19061, 23124, 27191, 31254, 2801, 6864, 10931, 14994,
    64814, 60687, 56684, 52557, 48554, 44427, 40424, 36297, 31782, 27655, 23652, 19525, 15522, 11395, 7392, 3265,
    61215, 65342, 53085, 57212, 44955, 49082, 36825, 40952, 28183, 32310, 20053, 24180, 11923, 16050, 3793, 7920
};

inline uint16_t crc16_xmodem(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc = ((crc << 8) ^ CRC16_TABLE[((data[i] ^ (crc >> 8)) & 0xFF)]) & 0xFFFF;
  }
  return crc;
}

inline uint16_t crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if ((crc & 0x0001) != 0) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

inline bool hmac_sha256(const uint8_t *msg, size_t msg_len, const uint8_t *key, size_t key_len, uint8_t *out) {
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return false;
  return mbedtls_md_hmac(md, key, key_len, msg, msg_len, out) == 0;
}

inline bool sha256_first16(const uint8_t *data, size_t len, uint8_t *out16) {
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return false;
  uint8_t full[32];
  if (mbedtls_md(md, data, len, full) != 0) return false;
  memcpy(out16, full, 16);
  return true;
}

inline bool aes_ctr_crypt(const uint8_t *in, size_t len, const uint8_t *key16, const uint8_t *iv16, uint8_t *out) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_enc(&ctx, key16, 128) != 0) {
    mbedtls_aes_free(&ctx);
    return false;
  }
  size_t nc_off = 0;
  uint8_t nonce[16];
  uint8_t stream_block[16] = {0};
  memcpy(nonce, iv16, 16);
  int ret = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce, stream_block, in, out);
  mbedtls_aes_free(&ctx);
  return ret == 0;
}

inline std::vector<uint8_t> hagrid_auth_token(const uint8_t *randA, const uint8_t *randB, const uint8_t *cak, const uint8_t *suffix, size_t suffix_len) {
  uint8_t randAB[32];
  memcpy(randAB, randA, 16);
  memcpy(randAB + 16, randB, 16);

  std::vector<uint8_t> cak_suffix(16 + suffix_len);
  memcpy(cak_suffix.data(), cak, 16);
  memcpy(cak_suffix.data() + 16, suffix, suffix_len);

  uint8_t inner_key[32];
  if (!hmac_sha256(randAB, 32, cak_suffix.data(), cak_suffix.size(), inner_key)) return {};

  std::vector<uint8_t> out(32);
  if (!hmac_sha256(randAB, 32, inner_key, 32, out.data())) return {};
  return out;
}

inline std::vector<uint8_t> hagrid_c3_from_mac(const std::string &mac_str) {
  std::string s;
  for (char c : mac_str) {
    if (c != ':' && c != '-') {
      s += (char) toupper((unsigned char) c);
    }
  }
  while (s.length() < 12) s += '0';
  s += "0000";
  return std::vector<uint8_t>(s.begin(), s.end());
}

inline std::vector<uint8_t> derive_root_key(const uint8_t *c1, const uint8_t *c2, const uint8_t *c3) {
  uint8_t mixed[16];
  for (size_t i = 0; i < 16; i++) {
    mixed[i] = (uint8_t) ((((c1[i] << 4) ^ c2[i])) & 0xFF);
  }
  uint8_t key_data[16];
  if (!sha256_first16(mixed, 16, key_data)) return {};

  uint8_t folded[16];
  for (size_t i = 0; i < 16; i++) {
    folded[i] = (uint8_t) (((key_data[i] >> 6) ^ c3[i]) & 0xFF);
  }
  std::vector<uint8_t> root_key(16);
  if (!sha256_first16(folded, 16, root_key.data())) return {};
  return root_key;
}

inline std::vector<std::vector<uint8_t>> build_wsp_write_frames(const std::vector<uint8_t> &payload, bool encrypted) {
  if (payload.size() > MAX_WSP_BYTES) return {};
  size_t total = (payload.size() + SIMPLE_CHUNK_BYTES - 1) / SIMPLE_CHUNK_BYTES;
  if (total == 0) total = 1;
  std::vector<std::vector<uint8_t>> frames;
  size_t offset = 0;
  for (size_t idx = 0; idx < total; idx++) {
    size_t chunk_len = std::min(SIMPLE_CHUNK_BYTES, payload.size() - offset);
    std::vector<uint8_t> f(chunk_len + 5);
    f[0] = encrypted ? FRAME_WRITE_ENCRYPTED : FRAME_WRITE_PLAIN;
    f[1] = (uint8_t) (chunk_len + 3);
    f[2] = (uint8_t) ((((total - 1) & 0x0F) << 4) | (idx & 0x0F));
    if (chunk_len > 0) {
      memcpy(f.data() + 3, payload.data() + offset, chunk_len);
      offset += chunk_len;
    }
    f[f.size() - 2] = 0;
    f[f.size() - 1] = 0;
    uint16_t crc = crc16_xmodem(f.data(), f.size());
    f[f.size() - 2] = (uint8_t) (crc & 0xFF);
    f[f.size() - 1] = (uint8_t) ((crc >> 8) & 0xFF);
    frames.push_back(f);
  }
  return frames;
}

inline std::vector<std::vector<uint8_t>> build_encrypted_wsp_frames(const std::vector<uint8_t> &plaintext, const uint8_t *key, const uint8_t *iv) {
  if (plaintext.size() > MAX_WSP_BYTES - 16) return {};
  std::vector<uint8_t> ciphertext(plaintext.size());
  if (!plaintext.empty()) {
    if (!aes_ctr_crypt(plaintext.data(), plaintext.size(), key, iv, ciphertext.data())) return {};
  }
  std::vector<uint8_t> cipher_with_iv(16 + ciphertext.size());
  memcpy(cipher_with_iv.data(), iv, 16);
  if (!ciphertext.empty()) {
    memcpy(cipher_with_iv.data() + 16, ciphertext.data(), ciphertext.size());
  }
  return build_wsp_write_frames(cipher_with_iv, true);
}

class HuaweiScaleClient : public esphome::ble_client::BLEClientNode, public esphome::Component {
 public:
  enum class State {
    IDLE,
    SUBSCRIBING,
    WAIT_CHALLENGE,
    AUTH_SENT,
    WORK_KEY_SENT,
    MANAGER_INFO_WAIT,
    USER_INFO_SENT,
    READY,
    AUTH_FAILED
  };

  void set_weight_sensor(esphome::sensor::Sensor *s) { this->weight_sensor_ = s; }
  void set_impedance_sensor(esphome::sensor::Sensor *s) { this->impedance_sensor_ = s; }
  void set_heart_rate_sensor(esphome::sensor::Sensor *s) { this->heart_rate_sensor_ = s; }
  void set_status_sensor(esphome::text_sensor::TextSensor *s) { this->status_sensor_ = s; }
  void set_measurement_sensor(esphome::text_sensor::TextSensor *s) { this->measurement_sensor_ = s; }

  void set_keys(const std::string &cak_hex, const std::string &c1_hex, const std::string &c2_hex) {
    this->keys_valid_ = cak_hex.size() == 32 && c1_hex.size() == 32 && c2_hex.size() == 32 &&
        esphome::parse_hex(cak_hex.c_str(), this->cak_, 16) &&
        esphome::parse_hex(c1_hex.c_str(), this->c1_, 16) &&
        esphome::parse_hex(c2_hex.c_str(), this->c2_, 16);
  }

  void set_user_profile(uint8_t gender, uint8_t age, uint16_t height_cm) {
    this->user_gender_ = gender;
    this->user_age_ = age;
    this->user_height_cm_ = height_cm;
  }

  // Runtime configuration keeps the distributable firmware free of device keys.
  // Call only after disconnecting; never change the target during a GATT session.
  bool configure_device(const std::string &mac, const std::string &cak,
                        const std::string &c1, const std::string &c2,
                        uint8_t gender, uint8_t age, uint16_t height_cm) {
    if (this->parent_ == nullptr || this->is_session_active()) return false;
    if (mac.size() != 17 || cak.size() != 32 || c1.size() != 32 || c2.size() != 32) return false;
    std::string compact;
    for (size_t i = 0; i < mac.size(); i++) {
      if (i % 3 == 2) { if (mac[i] != ':') return false; }
      else { if (!std::isxdigit(static_cast<unsigned char>(mac[i]))) return false; compact += mac[i]; }
    }
    uint8_t address_bytes[6];
    if (!esphome::parse_hex(compact.c_str(), address_bytes, 6)) return false;
    uint64_t address = 0;
    for (auto byte : address_bytes) address = (address << 8) | byte;
    if (address == 0 || address == 0xFFFFFFFFFFFFULL) return false;
    this->set_keys(cak, c1, c2);
    if (!this->keys_valid_) return false;
    this->parent_->set_address(address);
    this->set_address(address);
    this->set_user_profile(gender, age, height_cm);
    return true;
  }

  bool is_session_active() const {
    return this->parent_ != nullptr &&
           this->parent_->state() != esphome::esp32_ble_tracker::ClientState::IDLE;
  }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override {
    switch (event) {
      case ESP_GATTC_SEARCH_CMPL_EVT: {
        ESP_LOGI(TAG, "GATT 服务发现完成，按 UUID 解析并订阅业务特征");
        if (param->search_cmpl.status != ESP_GATT_OK) {
          this->fail_session("GATT 服务发现失败");
          break;
        }
        this->subscribe_notifications();
        break;
      }
      case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (this->state_ != State::SUBSCRIBING || this->subscription_index_ >= this->subscriptions_.size()) break;
        const auto &sub = this->subscriptions_[this->subscription_index_];
        if (param->reg_for_notify.handle != sub.handle) break;
        ESP_LOGI(TAG, "通知注册完成 role=%s handle=0x%04X status=0x%02X", sub.role, sub.handle,
                 param->reg_for_notify.status);
        if (param->reg_for_notify.status != ESP_GATT_OK) {
          this->subscription_failed("通知注册失败");
          break;
        }
        // ESPHome 2026.7.4 V1 BLEClientBase has already submitted the CCCD write.
        // Do not write it a second time from this node.
        this->notification_registered_ = true;
        this->state_started_ms_ = esphome::millis();
        this->update_status("等待通知订阅确认");
        break;
      }
      case ESP_GATTC_WRITE_DESCR_EVT: {
        if (this->state_ != State::SUBSCRIBING || this->subscription_index_ >= this->subscriptions_.size()) break;
        const auto &sub = this->subscriptions_[this->subscription_index_];
        if (param->write.handle != sub.cccd) break;
        ESP_LOGI(TAG, "通知订阅确认 role=%s cccd=0x%04X status=0x%02X", sub.role, sub.cccd,
                 param->write.status);
        if (param->write.status != ESP_GATT_OK) {
          this->subscription_failed("通知订阅失败");
          break;
        }
        if (!this->notification_registered_) break;
        this->notify_handles_.push_back(sub.handle);
        ++this->subscription_index_;
        this->subscribe_next();
        break;
      }
      case ESP_GATTC_WRITE_CHAR_EVT: {
        if (!this->tx_busy_ || param->write.handle != this->tx_handle_) break;
        this->tx_busy_ = false;
        if (param->write.status != ESP_GATT_OK) {
          this->fail_session("GATT 写入失败");
          break;
        }
        this->send_next_frame();
        break;
      }
      case ESP_GATTC_NOTIFY_EVT: {
        this->on_notify(param->notify.handle, param->notify.value, param->notify.value_len);
        break;
      }
      case ESP_GATTC_CLOSE_EVT:
      case ESP_GATTC_DISCONNECT_EVT: {
        ESP_LOGI(TAG, "体脂秤断开连接");
        this->transition_to(State::IDLE);
        this->subscriptions_.clear();
        this->subscription_index_ = 0;
        this->notification_registered_ = false;
        this->notify_handles_.clear();
        this->rx_.clear();
        this->tx_queue_.clear();
        this->tx_busy_ = false;
        this->existing_huid_.clear();
        this->status_poll_pending_ = false;
        if (this->status_sensor_ != nullptr) this->status_sensor_->publish_state("离线");
        break;
      }
      default:
        break;
    }
  }

  void loop() override {
    // 周期性轮询与超时维护
    uint32_t now = esphome::millis();
    for (auto it = this->rx_.begin(); it != this->rx_.end();) {
      if (now - it->second.last_frame_ms > RX_TIMEOUT_MS) it = this->rx_.erase(it);
      else ++it;
    }
    if (this->tx_busy_ && now - this->tx_started_ms_ > TX_TIMEOUT_MS) {
      this->fail_session("GATT 写入确认超时");
      return;
    }
    if (this->state_ != State::IDLE && this->state_ != State::READY && this->state_ != State::AUTH_FAILED &&
        now - this->state_started_ms_ > SESSION_TIMEOUT_MS) {
      if (this->state_ == State::SUBSCRIBING && this->subscription_index_ < this->subscriptions_.size()) {
        const auto &sub = this->subscriptions_[this->subscription_index_];
        ESP_LOGW(TAG, "订阅超时 role=%s handle=0x%04X cccd=0x%04X stage=%s", sub.role, sub.handle,
                 sub.cccd, this->notification_registered_ ? "CCCD_WRITE" : "REGISTER_NOTIFY");
        this->subscription_failed("通知订阅超时");
      } else {
        this->fail_session("体脂秤握手超时");
      }
      return;
    }
    if (this->state_ == State::READY && !this->tx_busy_ && this->tx_queue_.empty() &&
        this->parent() != nullptr && this->parent()->enabled && this->parent()->connected() &&
        this->handle_status_poll_ != 0 && now - this->last_status_poll_ > 3000) {
      if (this->status_poll_pending_) {
        if (now - this->last_status_poll_ <= SESSION_TIMEOUT_MS) return;
        ESP_LOGW(TAG, "测量状态回复超时，重试单个状态查询");
        this->status_poll_pending_ = false;
      }
      this->send_status_poll();
    }
  }

 protected:
  void transition_to(State state) {
    this->state_ = state;
    this->state_started_ms_ = esphome::millis();
  }

  void fail_session(const char *reason) {
    this->transition_to(State::AUTH_FAILED);
    this->tx_queue_.clear();
    this->tx_busy_ = false;
    this->rx_.clear();
    this->subscriptions_.clear();
    this->subscription_index_ = 0;
    this->notification_registered_ = false;
    this->existing_huid_.clear();
    this->status_poll_pending_ = false;
    this->update_status(reason);
    ESP_LOGW(TAG, "%s", reason);
    if (this->parent() != nullptr && this->is_session_active()) this->parent()->disconnect();
  }

  void update_status(const char *st) {
    if (this->status_sensor_ != nullptr) this->status_sensor_->publish_state(st);
  }

  bool bind_characteristic(uint16_t service, const char *uuid, const char *role, uint16_t &handle,
                           bool required, bool writable) {
    using esphome::esp32_ble_tracker::ESPBTUUID;
    handle = 0;
    auto *client = this->parent();
    auto *chr = client->get_characteristic(ESPBTUUID::from_uint16(service), ESPBTUUID::from_raw(uuid));
    if (chr == nullptr) {
      ESP_LOGW(TAG, "缺少业务特征 role=%s service=0x%04X uuid=%s required=%d", role, service, uuid, required);
      if (required) this->fail_session("缺少必要 GATT 特征");
      return !required;
    }
    ESP_LOGI(TAG, "业务特征 role=%s handle=0x%04X properties=0x%02X", role, chr->handle, chr->properties);
    if (writable && !(chr->properties & ESP_GATT_CHAR_PROP_BIT_WRITE)) {
      ESP_LOGW(TAG, "特征不支持有响应写入 role=%s", role);
      if (required) this->fail_session("必要特征不支持写入");
      return !required;
    }
    handle = chr->handle;
    const bool can_notify = chr->properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE);
    auto *cccd = can_notify ? client->get_config_descriptor(handle) : nullptr;
    if (cccd == nullptr) {
      ESP_LOGW(TAG, "无可订阅 CCCD role=%s handle=0x%04X required=%d", role, handle, required);
      if (required) this->fail_session("必要特征缺少通知描述符");
      return !required;
    }
    this->subscriptions_.push_back({role, handle, cccd->handle, required});
    return true;
  }

  void subscribe_notifications() {
    if (this->parent() == nullptr) return;
    this->subscriptions_.clear();
    this->subscription_index_ = 0;
    this->notification_registered_ = false;
    this->notify_handles_.clear();
    // Keep ESPHome's V1 discovery objects until every selected CCCD is resolved.
    this->node_state = esphome::esp32_ble_tracker::ClientState::CONNECTED;
    this->transition_to(State::SUBSCRIBING);
    this->update_status("解析业务特征");
    if (!this->bind_characteristic(0x181C, "02b2a08e-f8b0-4047-b1fd-f4e0efeee679", "request-auth", this->handle_request_auth_, true, true) ||
        !this->bind_characteristic(0x181C, "32330a04-15d9-421a-91c5-2a2d5c7525c9", "auth-token", this->handle_auth_token_, true, true) ||
        !this->bind_characteristic(0x181C, "a3d330f8-b84f-4f48-a78c-f8d1e33b597a", "work-key", this->handle_send_work_key_, true, true) ||
        !this->bind_characteristic(0x181C, "4338c65e-ed8e-4085-bbea-a25e33ca6b54", "manager-info", this->handle_get_manager_info_, true, true) ||
        !this->bind_characteristic(0x181C, "8cc61d7d-66c0-4802-89c3-38c5a163592e", "user-info", this->handle_set_user_info_, true, true) ||
        !this->bind_characteristic(0x181B, "46797c17-d639-488d-9476-4789e8472878", "realtime-weight", this->handle_realtime_, true, true)) return;
    // Only these two status channels are optional. Do not subscribe unrelated
    // device-info, history, binding or service-changed characteristics.
    this->bind_characteristic(0x1805, "bfc36f6e-4150-4a4b-9052-3d359e52962e", "status-poll", this->handle_status_poll_, false, true);
    this->bind_characteristic(0x1805, "ba216311-1787-472b-bef6-3eb29e62293e", "status-result", this->handle_status_result_, false, false);
    ESP_LOGI(TAG, "已选择 %u 项业务通知，将串行注册", static_cast<unsigned>(this->subscriptions_.size()));
    this->subscribe_next();
  }

  void subscribe_next() {
    this->notification_registered_ = false;
    if (this->subscription_index_ >= this->subscriptions_.size()) {
      this->node_state = esphome::esp32_ble_tracker::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "业务通知订阅完成，开始认证");
      this->start_auth();
      return;
    }
    const auto &sub = this->subscriptions_[this->subscription_index_];
    this->state_started_ms_ = esphome::millis();
    this->update_status("注册业务通知");
    ESP_LOGI(TAG, "提交通知注册 role=%s handle=0x%04X cccd=0x%04X required=%d", sub.role, sub.handle,
             sub.cccd, sub.required);
    auto *client = this->parent();
    auto status = esp_ble_gattc_register_for_notify(client->get_gattc_if(), client->get_remote_bda(), sub.handle);
    if (status != ESP_OK) {
      ESP_LOGW(TAG, "通知注册提交错误 role=%s status=0x%X", sub.role, status);
      this->subscription_failed("通知注册提交失败");
    }
  }

  void subscription_failed(const char *reason) {
    const auto &sub = this->subscriptions_[this->subscription_index_];
    if (sub.required) {
      this->fail_session(reason);
    } else {
      ESP_LOGW(TAG, "跳过可选订阅 role=%s reason=%s", sub.role, reason);
      ++this->subscription_index_;
      this->subscribe_next();
    }
  }

  void start_auth() {
    if (!this->keys_valid_) {
      this->fail_session("密钥格式无效，未发送认证请求");
      return;
    }
    this->transition_to(State::WAIT_CHALLENGE);
    this->update_status("等待认证挑战");
    ESP_LOGI(TAG, "向体脂秤发送认证请求...");
    std::vector<uint8_t> empty_payload;
    this->write_wsp(this->handle_request_auth_, empty_payload, false);
  }

  void on_notify(uint16_t handle, const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) return;
    if (std::find(this->notify_handles_.begin(), this->notify_handles_.end(), handle) == this->notify_handles_.end()) return;
    uint8_t prefix = data[0];
    if (prefix != FRAME_NOTIFY_PLAIN && prefix != FRAME_NOTIFY_ENCRYPTED) {
      // Hagrid also permits raw status/ACK and realtime notifications. Route only
      // their known roles; a malformed WSP frame must still pass the WSP checks.
      ESP_LOGD(TAG, "原始业务通知 handle=0x%04X len=%u", handle, static_cast<unsigned>(len));
      const bool profile_ack = this->state_ == State::USER_INFO_SENT && handle == this->handle_set_user_info_;
      const bool measurement = this->state_ == State::READY &&
          (handle == this->handle_realtime_ || handle == this->handle_status_poll_ || handle == this->handle_status_result_);
      if (profile_ack || measurement) this->handle_packet(handle, std::vector<uint8_t>(data, data + len));
      return;
    }
    if (len < 5) {
      ESP_LOGD(TAG, "WSP 通知过短 handle=0x%04X len=%u", handle, static_cast<unsigned>(len));
      return;
    }

    // CRC16-Modbus 校验
    uint16_t expected_crc = data[len - 2] | (data[len - 1] << 8);
    uint16_t actual_crc = crc16_modbus(data, len - 2);
    if (expected_crc != actual_crc) {
      ESP_LOGW(TAG, "通知帧 CRC 校验失败: exp=0x%04X act=0x%04X", expected_crc, actual_crc);
      return;
    }

    if (data[1] < 3) return;
    size_t payload_len = data[1] - 3;
    if (payload_len > SIMPLE_CHUNK_BYTES || len != payload_len + 5) {
      this->rx_.erase(handle);
      return;
    }

    // Reassemble all WSP slices before extracting IV or decrypting the message.
    const uint8_t total = (data[2] >> 4) + 1;
    const uint8_t index = data[2] & 0x0F;
    auto &rx = this->rx_[handle];
    const uint32_t now = esphome::millis();
    if (index == 0) rx = {total, 0, prefix, {}, now};
    if (now - rx.last_frame_ms > RX_TIMEOUT_MS) {
      this->rx_.erase(handle);
      return;
    }
    rx.last_frame_ms = now;
    if (rx.total != total || rx.next != index || rx.prefix != prefix) {
      this->rx_.erase(handle);
      return;
    }
    rx.bytes.insert(rx.bytes.end(), data + 3, data + 3 + payload_len);
    if (++rx.next < total) return;
    auto frame_payload = std::move(rx.bytes);
    this->rx_.erase(handle);
    bool is_encrypted = (prefix == FRAME_NOTIFY_ENCRYPTED);

    std::vector<uint8_t> plain_payload;
    if (is_encrypted) {
      if (frame_payload.size() < 16) return;
      uint8_t iv[16];
      memcpy(iv, frame_payload.data(), 16);
      plain_payload.resize(frame_payload.size() - 16);
      if (!aes_ctr_crypt(frame_payload.data() + 16, plain_payload.size(), this->work_key_, iv, plain_payload.data())) {
        ESP_LOGE(TAG, "解密通知帧失败");
        return;
      }
    } else {
      plain_payload = frame_payload;
    }

    ESP_LOGD(TAG, "WSP 通知完成 handle=0x%04X encrypted=%d len=%u", handle, is_encrypted,
             static_cast<unsigned>(plain_payload.size()));
    this->handle_packet(handle, plain_payload);
  }

  void handle_packet(uint16_t handle, const std::vector<uint8_t> &payload) {
    // Roles are resolved by UUID on this connection, not by fixed handle numbers.
    uint16_t expected_handle = 0;
    switch (this->state_) {
      case State::WAIT_CHALLENGE: expected_handle = this->handle_request_auth_; break;
      case State::AUTH_SENT: expected_handle = this->handle_auth_token_; break;
      case State::WORK_KEY_SENT: expected_handle = this->handle_send_work_key_; break;
      case State::MANAGER_INFO_WAIT: expected_handle = this->handle_get_manager_info_; break;
      case State::USER_INFO_SENT: expected_handle = this->handle_set_user_info_; break;
      case State::READY:
        if (handle != this->handle_realtime_ && handle != this->handle_status_poll_ &&
            handle != this->handle_status_result_) return;
        break;
      default: return;
    }
    if (expected_handle != 0 && handle != expected_handle) return;
    if (this->state_ == State::WAIT_CHALLENGE && payload.size() >= 16) {
      memcpy(this->randA_, payload.data(), 16);
      esp_fill_random(this->randB_, 16);
      ESP_LOGI(TAG, "收到挑战 randA，生成 randB 并发送 authToken");

      static const uint8_t SUFFIX_TO_SCALE[] = {0x31, 0x31, 0x32, 0x33}; // "1123"
      std::vector<uint8_t> token = hagrid_auth_token(this->randA_, this->randB_, this->cak_, SUFFIX_TO_SCALE, 4);

      if (token.size() != 32) {
        this->fail_session("认证 HMAC 计算失败");
        return;
      }
      std::vector<uint8_t> token_packet(16 + token.size());
      memcpy(token_packet.data(), this->randB_, 16);
      memcpy(token_packet.data() + 16, token.data(), token.size());

      this->transition_to(State::AUTH_SENT);
      this->update_status("认证令牌已发送");
      this->write_wsp(this->handle_auth_token_, token_packet, false);
      return;
    }

    if (this->state_ == State::AUTH_SENT) {
      static const uint8_t SUFFIX_FROM_SCALE[] = {0x39, 0x38, 0x35, 0x36}; // "9856"
      std::vector<uint8_t> expected = hagrid_auth_token(this->randA_, this->randB_, this->cak_, SUFFIX_FROM_SCALE, 4);
      if (expected.size() != 32) {
        this->fail_session("认证 HMAC 计算失败");
        return;
      }
      if (payload.size() >= expected.size() && memcmp(payload.data(), expected.data(), expected.size()) == 0) {
        ESP_LOGI(TAG, "体脂秤认证响应验证成功！派生 RootKey 并协商 WorkKey");
        std::string mac_str = this->parent()->address_str();
        std::vector<uint8_t> c3 = hagrid_c3_from_mac(mac_str);
        std::vector<uint8_t> root_key = derive_root_key(this->c1_, this->c2_, c3.data());
        if (root_key.size() != 16) {
          this->fail_session("RootKey 派生失败");
          return;
        }
        memcpy(this->root_key_, root_key.data(), 16);

        esp_fill_random(this->work_key_, 16);
        uint8_t iv[16];
        esp_fill_random(iv, 16);

        std::vector<uint8_t> wk(this->work_key_, this->work_key_ + 16);
        auto frames = build_encrypted_wsp_frames(wk, this->root_key_, iv);
        this->transition_to(State::WORK_KEY_SENT);
        this->update_status("工作密钥已发送");
        this->write_frames(this->handle_send_work_key_, frames);
      } else {
        ESP_LOGE(TAG, "体脂秤认证响应校验失败！");
        this->fail_session("认证失败");
      }
      return;
    }

    if (this->state_ == State::WORK_KEY_SENT) {
      if (!payload.empty() && payload[0] == 0x00) {
        ESP_LOGI(TAG, "工作密钥被秤端接受！开始读取现有 HUID（保护原有绑定）");
        this->transition_to(State::MANAGER_INFO_WAIT);
        this->update_status("读取管理者信息");
        std::vector<uint8_t> empty;
        this->write_wsp(this->handle_get_manager_info_, empty, false);
      }
      return;
    }

    if (this->state_ == State::MANAGER_INFO_WAIT) {
      if (payload.size() >= MANAGER_HUID_BYTES) {
        this->existing_huid_.assign(payload.begin(), payload.begin() + MANAGER_HUID_BYTES);
      } else {
        this->existing_huid_.clear();
      }
      if (this->existing_huid_.empty() ||
          std::all_of(this->existing_huid_.begin(), this->existing_huid_.end(), [](uint8_t b) { return b == 0; })) {
        ESP_LOGW(TAG, "没有已有 HUID，不写入用户资料，不改变绑定");
        this->fail_session("未读取到已有 HUID，未写入");
        return;
      }
      ESP_LOGI(TAG, "已读取现有 HUID，将原样透传（不记录内容）");
      this->sync_user_profile();
      return;
    }

    if (this->state_ == State::USER_INFO_SENT) {
      const int status = payload.empty() ? -1 : payload[0];
      if (status == 0x00) {
        ESP_LOGI(TAG, "用户资料同步被秤接受，进入实时测量监听阶段");
      } else {
        // Keep the existing post-auth read behavior, but do not label a failed
        // or missing ACK as success. Never attempt to clear/rebind the HUID.
        ESP_LOGW(TAG, "用户资料同步未确认 status=%d，继续读取测量，不改变绑定", status);
      }
      this->transition_to(State::READY);
      this->status_poll_pending_ = false;
      this->last_status_poll_ = esphome::millis();
      this->begin_measurement_cycle();
      this->send_realtime_request();
      return;
    }

    if (handle == this->handle_status_poll_ || handle == this->handle_status_result_) {
      this->handle_measurement_status(payload);
      return;
    }

    if (payload.size() == 26 || payload.size() == 38) {
      this->parse_measurement(payload);
    } else {
      ESP_LOGD(TAG, "尚无完整测量 handle=0x%04X len=%u", handle, static_cast<unsigned>(payload.size()));
    }
  }

  void handle_measurement_status(const std::vector<uint8_t> &payload) {
    if (payload.empty()) return;
    this->status_poll_pending_ = false;
    const uint8_t status = payload[0];
    ESP_LOGD(TAG, "测量状态 status=0x%02X len=%u", status, static_cast<unsigned>(payload.size()));
    const uint8_t previous = this->measurement_status_;
    this->measurement_status_ = status;
    if (this->measurement_cycle_complete_) {
      if (status == 0x01 && previous != status) {
        this->begin_measurement_cycle();
        this->measurement_status_ = status;
        ESP_LOGI(TAG, "完成后检测到新称重活动，启动下一轮实时读取");
        this->send_realtime_request();
      }
      return;
    }
    if (status == 0x01) {
      // A realtime request itself can produce status=1. It does not reset the
      // one-shot final read or start another request within the same cycle.
      ESP_LOGD(TAG, "本轮测量进行中，等待数据，不重复启动");
    } else if (status == 0x00) {
      if (this->measurement_received_) {
        this->finish_measurement_cycle();
      } else if (!this->final_measurement_requested_) {
        this->final_measurement_requested_ = true;
        ESP_LOGI(TAG, "测量状态就绪但尚无数据，本轮仅补读一次");
        this->send_realtime_request();
      }
    }
  }

  void begin_measurement_cycle() {
    esp_fill_random(this->measurement_id_, sizeof(this->measurement_id_));
    this->cycle_weight_ = 0.0f;
    this->cycle_impedance_ = 0.0f;
    this->cycle_heart_rate_ = 0;
    this->measurement_status_ = 0xFF;
    this->measurement_received_ = false;
    this->measurement_cycle_complete_ = false;
    this->final_measurement_requested_ = false;
    this->impedance_received_ = false;
    this->heart_rate_received_ = false;
    // Keep the established weight entity/value. Optional metrics must not show
    // the previous user's/cycle's value while the current scale sends zero.
    const float unavailable = std::numeric_limits<float>::quiet_NaN();
    if (this->impedance_sensor_ != nullptr) this->impedance_sensor_->publish_state(unavailable);
    if (this->heart_rate_sensor_ != nullptr) this->heart_rate_sensor_->publish_state(unavailable);
    this->update_status("就绪 / 监听测量");
  }

  void finish_measurement_cycle() {
    if (this->measurement_cycle_complete_) return;
    this->measurement_cycle_complete_ = true;
    ESP_LOGI(TAG, "本轮测量完成：阻抗有效=%d 心率有效=%d，停止本轮补读",
             this->impedance_received_, this->heart_rate_received_);
    if (!this->impedance_received_ || !this->heart_rate_received_) {
      ESP_LOGW(TAG, "本轮未提供全部有效体征，缺失实体保持不可用，不生成替代值");
    }
    this->update_status("测量完成");
    this->publish_measurement_snapshot();
  }

  void publish_measurement_snapshot() {
    if (this->measurement_sensor_ == nullptr || !this->measurement_received_) return;
    char impedance[24] = "null";
    char heart_rate[16] = "null";
    if (this->impedance_received_) std::snprintf(impedance, sizeof(impedance), "%.1f", this->cycle_impedance_);
    if (this->heart_rate_received_) std::snprintf(heart_rate, sizeof(heart_rate), "%u", this->cycle_heart_rate_);
    char snapshot[192];
    std::snprintf(snapshot, sizeof(snapshot),
                  "{\"id\":\"%08lx%08lx\",\"weight\":%.2f,\"impedance\":%s,\"heart_rate\":%s}",
                  static_cast<unsigned long>(this->measurement_id_[0]),
                  static_cast<unsigned long>(this->measurement_id_[1]),
                  this->cycle_weight_, impedance, heart_rate);
    // One cycle ID and one atomic payload, not a join of three independently
    // updated HA sensors. Late optional values update this same archive row.
    this->measurement_sensor_->publish_state(snapshot);
  }

  void send_realtime_request() {
    std::vector<uint8_t> empty;
    this->write_wsp(this->handle_realtime_, empty, false);
  }

  void sync_user_profile() {
    std::vector<uint8_t> user_info(USER_INFO_PAYLOAD_BYTES, 0);
    memcpy(user_info.data(), this->existing_huid_.data(), MANAGER_HUID_BYTES);

    const char *uid = "user1";
    memcpy(user_info.data() + 30, uid, strlen(uid));

    user_info[62] = this->user_gender_;
    user_info[63] = this->user_age_;
    user_info[64] = (uint8_t) (this->user_height_cm_ & 0xFF);
    user_info[65] = (uint8_t) ((this->user_height_cm_ >> 8) & 0xFF);

    uint8_t iv[16];
    esp_fill_random(iv, 16);
    auto frames = build_encrypted_wsp_frames(user_info, this->work_key_, iv);

    this->transition_to(State::USER_INFO_SENT);
    this->update_status("同步用户资料");
    ESP_LOGI(TAG, "发送加密用户 Profile: 性别=%d 年龄=%d 身高=%dcm", this->user_gender_, this->user_age_, this->user_height_cm_);
    this->write_frames(this->handle_set_user_info_, frames);
  }

  void parse_measurement(const std::vector<uint8_t> &p) {
    // openScale 8ef93b2092745ea84ddd677eac1871452c27b3d5:
    // HuaweiHagridWspLib: low[6] at 12, HR at 24 (also for 38-byte frames).
    // HuaweiHagridWspHandler: Scale 3 has model-specific 0.1-ohm units.
    uint16_t raw_weight = p[0] | (p[1] << 8);
    float weight = raw_weight / 100.0f;

    uint16_t low_raw[6];
    float impedance = 0.0f;
    int impedance_channel = -1;
    for (size_t i = 0; i < 6; ++i) {
      low_raw[i] = p[12 + i * 2] | (p[13 + i * 2] << 8);
      // Scale 3 uses tenths of an ohm, including values below 4000. Do not
      // apply the generic Hagrid "below 4000 is already ohms" heuristic.
      const float ohms = low_raw[i] / 10.0f;
      if (impedance_channel < 0 && low_raw[i] < 40000 && ohms > 10.0f) {
        impedance = ohms;
        impedance_channel = static_cast<int>(i);
      }
    }

    uint16_t heart_rate = p[24] | (p[25] << 8);
    const bool valid_heart_rate = heart_rate >= 1 && heart_rate <= 240;

    ESP_LOGD(TAG, "测量字段：low_raw=[%u,%u,%u,%u,%u,%u] selected=%d hr_raw=%u len=%u",
             low_raw[0], low_raw[1], low_raw[2], low_raw[3], low_raw[4], low_raw[5],
             impedance_channel, heart_rate, static_cast<unsigned>(p.size()));
    ESP_LOGI(TAG, "测量结果: 体重=%.2f kg, 阻抗=%.1f Ω, 心率=%d bpm", weight, impedance, heart_rate);

    if (weight > 5.0f) {
      this->measurement_received_ = true;
      this->cycle_weight_ = weight;
      if (this->weight_sensor_ != nullptr) this->weight_sensor_->publish_state(weight);
    }
    if (impedance_channel >= 0) {
      this->impedance_received_ = true;
      this->cycle_impedance_ = impedance;
      if (this->impedance_sensor_ != nullptr) this->impedance_sensor_->publish_state(impedance);
    }
    if (valid_heart_rate) {
      this->heart_rate_received_ = true;
      this->cycle_heart_rate_ = heart_rate;
      if (this->heart_rate_sensor_ != nullptr) this->heart_rate_sensor_->publish_state(heart_rate);
    }
    if (this->measurement_cycle_complete_) this->publish_measurement_snapshot();
    else if (this->measurement_received_ && this->measurement_status_ == 0x00) this->finish_measurement_cycle();
  }

  void send_status_poll() {
    this->status_poll_pending_ = true;
    this->last_status_poll_ = esphome::millis();
    std::vector<uint8_t> empty;
    this->write_wsp(this->handle_status_poll_, empty, false);
  }

  void write_wsp(uint16_t handle, const std::vector<uint8_t> &payload, bool encrypted) {
    auto frames = build_wsp_write_frames(payload, encrypted);
    this->write_frames(handle, frames);
  }

  void write_frames(uint16_t handle, const std::vector<std::vector<uint8_t>> &frames) {
    if (frames.empty() || this->tx_queue_.size() + frames.size() + (this->tx_busy_ ? 1 : 0) > MAX_PENDING_FRAMES) {
      this->fail_session("发送帧无效或队列已满");
      return;
    }
    for (const auto &f : frames) this->tx_queue_.push_back({handle, f});
    this->send_next_frame();
  }

  void send_next_frame() {
    if (this->tx_busy_ || this->tx_queue_.empty()) return;
    auto *client = this->parent();
    if (!client) return;
    auto item = std::move(this->tx_queue_.front());
    this->tx_queue_.pop_front();
    this->tx_busy_ = true;
    this->tx_handle_ = item.first;
    this->tx_started_ms_ = esphome::millis();
    auto err = esp_ble_gattc_write_char(client->get_gattc_if(), client->get_conn_id(), item.first,
                 item.second.size(), item.second.data(), ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
      this->fail_session("GATT 写入提交失败");
      ESP_LOGE(TAG, "GATT 写入提交失败: %s", esp_err_to_name(err));
    }
  }

  struct RxAssembly {
    uint8_t total{0};
    uint8_t next{0};
    uint8_t prefix{0};
    std::vector<uint8_t> bytes;
    uint32_t last_frame_ms{0};
  };
  std::map<uint16_t, RxAssembly> rx_;
  std::deque<std::pair<uint16_t, std::vector<uint8_t>>> tx_queue_;
  struct NotificationSubscription {
    const char *role;
    uint16_t handle;
    uint16_t cccd;
    bool required;
  };
  std::vector<NotificationSubscription> subscriptions_;
  size_t subscription_index_{0};
  bool notification_registered_{false};
  bool tx_busy_{false};
  uint16_t tx_handle_{0};
  uint32_t tx_started_ms_{0};
  uint32_t state_started_ms_{0};
  bool keys_valid_{false};
  std::vector<uint16_t> notify_handles_;

  uint16_t handle_request_auth_{0};
  uint16_t handle_auth_token_{0};
  uint16_t handle_send_work_key_{0};
  uint16_t handle_get_manager_info_{0};
  uint16_t handle_set_user_info_{0};
  uint16_t handle_realtime_{0};
  uint16_t handle_status_poll_{0};
  uint16_t handle_status_result_{0};

  State state_{State::IDLE};
  uint8_t measurement_status_{0xFF};
  bool measurement_received_{false};
  bool measurement_cycle_complete_{false};
  bool final_measurement_requested_{false};
  bool impedance_received_{false};
  bool heart_rate_received_{false};
  bool status_poll_pending_{false};
  uint32_t measurement_id_[2]{0, 0};
  float cycle_weight_{0.0f};
  float cycle_impedance_{0.0f};
  uint16_t cycle_heart_rate_{0};
  uint8_t cak_[16]{0};
  uint8_t c1_[16]{0};
  uint8_t c2_[16]{0};
  uint8_t randA_[16]{0};
  uint8_t randB_[16]{0};
  uint8_t root_key_[16]{0};
  uint8_t work_key_[16]{0};

  std::vector<uint8_t> existing_huid_;
  uint8_t user_gender_{1};
  uint8_t user_age_{30};
  uint16_t user_height_cm_{170};
  uint32_t last_status_poll_{0};

  esphome::sensor::Sensor *weight_sensor_{nullptr};
  esphome::sensor::Sensor *impedance_sensor_{nullptr};
  esphome::sensor::Sensor *heart_rate_sensor_{nullptr};
  esphome::text_sensor::TextSensor *status_sensor_{nullptr};
  esphome::text_sensor::TextSensor *measurement_sensor_{nullptr};
};

} // namespace huawei_scale
