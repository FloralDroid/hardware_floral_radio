/*
 * Copyright 2026 FloralDroid
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "floral/radio/RadioStateModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace floral::radio {
namespace {

constexpr int64_t kMaximumLeaseMs = 60'000;
constexpr size_t kMaximumCalls = 8;
constexpr size_t kMaximumSmsEvents = 64;
constexpr size_t kMaximumCells = 32;

bool AllDigits(const std::string &value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return character >= '0' && character <= '9';
         });
}

int LuhnCheckDigit(const std::string &prefix) {
  int sum = 0;
  bool double_digit = true;
  for (auto iterator = prefix.rbegin(); iterator != prefix.rend(); ++iterator) {
    int digit = *iterator - '0';
    if (double_digit) {
      digit *= 2;
      if (digit > 9) {
        digit -= 9;
      }
    }
    sum += digit;
    double_digit = !double_digit;
  }
  return (10 - sum % 10) % 10;
}

bool HasValidLuhn(const std::string &value) {
  return value.size() >= 2 && AllDigits(value) &&
         LuhnCheckDigit(value.substr(0, value.size() - 1)) ==
             value.back() - '0';
}

bool ValidLease(int64_t duration_ms) {
  return duration_ms > 0 && duration_ms <= kMaximumLeaseMs;
}

bool ValidPhoneNumber(const std::string &value) {
  if (value.empty() || value.size() > 20) {
    return false;
  }
  const size_t start = value.front() == '+' ? 1 : 0;
  return start < value.size() && AllDigits(value.substr(start));
}

SignalState UnavailableSignal() {
  const int32_t unavailable = std::numeric_limits<int32_t>::max();
  return {
      .rssi_dbm = unavailable,
      .rsrp_dbm = unavailable,
      .rsrq_db = unavailable,
      .rssnr_tenth_db = unavailable,
      .cqi = unavailable,
      .timing_advance = unavailable,
  };
}

} // namespace

RadioStateModel::RadioStateModel(uint64_t seed)
    : random_(floral::device::simulation::DeriveSeed(seed, 1)),
      rsrp_noise_(floral::device::simulation::DeriveSeed(seed, 2), 25.0f, 3.0f),
      rsrq_noise_(floral::device::simulation::DeriveSeed(seed, 3), 20.0f, 1.2f),
      snr_noise_(floral::device::simulation::DeriveSeed(seed, 4), 18.0f,
                 15.0f) {
  // Keep the HAL registered so Android observes an absent SIM instead of a
  // crashing or repeatedly restarting radio service.
  state_.radio_on = false;
  state_.sim_state = SimState::kAbsent;
  state_.voice_registration = RegistrationState::kNotRegistered;
  state_.data_registration = RegistrationState::kNotRegistered;
  state_.technology = RadioTechnology::kUnknown;
  state_.signal = UnavailableSignal();
  state_.cells.clear();
}

void RadioStateModel::ActivateConfiguredProfile() {
  const uint64_t generation = state_.generation;
  state_ = RadioSnapshot{};
  state_.generation = generation;
  CellState serving;
  serving.identity = 1;
  state_.cells.push_back(serving);
  CellState neighbor = serving;
  neighbor.identity = 2;
  neighbor.registered = false;
  neighbor.ci += 1;
  neighbor.pci += 17;
  neighbor.earfcn += 25;
  neighbor.signal.rsrp_dbm -= 9;
  state_.cells.push_back(neighbor);
  state_.signal = serving.signal;
  external_lease_expiry_ns_ = 0;
  next_handover_timestamp_ns_ = 0;
}

bool RadioStateModel::ValidateProfile(const RadioProfile &profile,
                                      std::string *error) {
  const bool valid =
      profile.version == 1 && !profile.operator_long_name.empty() &&
      profile.operator_long_name.size() <= 64 &&
      !profile.operator_short_name.empty() &&
      profile.operator_short_name.size() <= 32 && profile.mcc.size() == 3 &&
      AllDigits(profile.mcc) &&
      (profile.mnc.size() == 2 || profile.mnc.size() == 3) &&
      AllDigits(profile.mnc) && profile.imei.size() == 15 &&
      HasValidLuhn(profile.imei) && profile.imeisv.size() == 2 &&
      AllDigits(profile.imeisv) && profile.imsi.size() >= 14 &&
      profile.imsi.size() <= 15 && AllDigits(profile.imsi) &&
      profile.imsi.rfind(profile.mcc + profile.mnc, 0) == 0 &&
      profile.iccid.size() >= 19 && profile.iccid.size() <= 20 &&
      HasValidLuhn(profile.iccid) && ValidPhoneNumber(profile.msisdn) &&
      !profile.baseband_version.empty() &&
      profile.baseband_version.size() <= 64 && profile.sim_pin.size() >= 4 &&
      profile.sim_pin.size() <= 8 && AllDigits(profile.sim_pin);
  if (!valid && error != nullptr) {
    *error = "radio profile fields are inconsistent or invalid";
  }
  return valid;
}

bool RadioStateModel::SetProfile(const RadioProfile &profile) {
  if (!ValidateProfile(profile, nullptr)) {
    return false;
  }
  profile_ = profile;
  profile_configured_ = true;
  ActivateConfiguredProfile();
  IncrementGeneration();
  return true;
}

bool RadioStateModel::SetRegistration(const RegistrationControl &control,
                                      int64_t timestamp_ns) {
  if (!profile_configured_ || !ValidLease(control.lease_duration_ms) ||
      control.voice < RegistrationState::kNotRegistered ||
      control.voice > RegistrationState::kRoaming ||
      control.data < RegistrationState::kNotRegistered ||
      control.data > RegistrationState::kRoaming ||
      control.technology < RadioTechnology::kUnknown ||
      control.technology > RadioTechnology::kLte) {
    return false;
  }
  state_.voice_registration = control.voice;
  state_.data_registration = control.data;
  state_.technology = control.technology;
  external_lease_expiry_ns_ =
      timestamp_ns + control.lease_duration_ms * 1'000'000;
  state_.externally_controlled = true;
  IncrementGeneration();
  return true;
}

bool RadioStateModel::ValidateSignal(const SignalState &signal) const {
  return signal.rssi_dbm >= -120 && signal.rssi_dbm <= -20 &&
         signal.rsrp_dbm >= -140 && signal.rsrp_dbm <= -40 &&
         signal.rsrq_db >= -30 && signal.rsrq_db <= 0 &&
         signal.rssnr_tenth_db >= -200 && signal.rssnr_tenth_db <= 300 &&
         signal.cqi >= 0 && signal.cqi <= 15 && signal.timing_advance >= 0 &&
         signal.timing_advance <= 1282;
}

bool RadioStateModel::SetSignal(const SignalState &signal,
                                int64_t lease_duration_ms,
                                int64_t timestamp_ns) {
  if (!profile_configured_ || !ValidateSignal(signal) ||
      !ValidLease(lease_duration_ms)) {
    return false;
  }
  state_.signal = signal;
  if (!state_.cells.empty()) {
    state_.cells.front().signal = signal;
  }
  external_lease_expiry_ns_ = timestamp_ns + lease_duration_ms * 1'000'000;
  state_.externally_controlled = true;
  IncrementGeneration();
  return true;
}

bool RadioStateModel::ValidateCell(const CellState &cell) const {
  return cell.identity != 0 && cell.tac >= 0 && cell.tac <= 65'535 &&
         cell.ci >= 0 && cell.ci <= 268'435'455 && cell.pci >= 0 &&
         cell.pci <= 503 && cell.earfcn >= 0 && cell.earfcn <= 262'143 &&
         cell.bandwidth_khz >= 1'400 && cell.bandwidth_khz <= 20'000 &&
         ValidateSignal(cell.signal);
}

bool RadioStateModel::ReplaceCells(const std::vector<CellState> &cells,
                                   int64_t lease_duration_ms,
                                   int64_t timestamp_ns) {
  if (!profile_configured_ || cells.empty() ||
      cells.size() > kMaximumCells ||
      !ValidLease(lease_duration_ms) ||
      std::count_if(cells.begin(), cells.end(),
                    [](const CellState &cell) { return cell.registered; }) !=
          1 ||
      !std::all_of(cells.begin(), cells.end(), [this](const CellState &cell) {
        return ValidateCell(cell);
      })) {
    return false;
  }
  state_.cells = cells;
  const auto serving =
      std::find_if(state_.cells.begin(), state_.cells.end(),
                   [](const CellState &cell) { return cell.registered; });
  state_.signal = serving->signal;
  external_lease_expiry_ns_ = timestamp_ns + lease_duration_ms * 1'000'000;
  state_.externally_controlled = true;
  IncrementGeneration();
  return true;
}

bool RadioStateModel::SetSimState(SimState state, int64_t lease_duration_ms,
                                  int64_t timestamp_ns) {
  if (!profile_configured_ || state < SimState::kAbsent ||
      state > SimState::kPukRequired ||
      !ValidLease(lease_duration_ms)) {
    return false;
  }
  state_.sim_state = state;
  if (state != SimState::kReady) {
    state_.voice_registration = RegistrationState::kNotRegistered;
    state_.data_registration = RegistrationState::kNotRegistered;
  }
  external_lease_expiry_ns_ = timestamp_ns + lease_duration_ms * 1'000'000;
  state_.externally_controlled = true;
  IncrementGeneration();
  return true;
}

bool RadioStateModel::SetRadioPower(bool enabled) {
  if (!profile_configured_ || state_.radio_on == enabled) {
    return false;
  }
  state_.radio_on = enabled;
  state_.voice_registration = enabled && state_.sim_state == SimState::kReady
                                  ? RegistrationState::kHome
                                  : RegistrationState::kNotRegistered;
  state_.data_registration = RegistrationState::kNotRegistered;
  state_.technology =
      enabled ? RadioTechnology::kLte : RadioTechnology::kUnknown;
  if (!enabled) {
    state_.calls.clear();
  }
  IncrementGeneration();
  return true;
}

void RadioStateModel::ReleaseExternalControl() {
  if (!profile_configured_ || external_lease_expiry_ns_ == 0) {
    return;
  }
  external_lease_expiry_ns_ = 0;
  state_.externally_controlled = false;
  state_.voice_registration = RegistrationState::kHome;
  state_.data_registration = RegistrationState::kNotRegistered;
  state_.technology = RadioTechnology::kLte;
  state_.sim_state = SimState::kReady;
  IncrementGeneration();
}

uint64_t RadioStateModel::InjectIncomingCall(const std::string &number) {
  if (!profile_configured_ || !state_.radio_on ||
      state_.sim_state != SimState::kReady || !ValidPhoneNumber(number) ||
      state_.calls.size() >= kMaximumCalls) {
    return 0;
  }
  CallRecord call;
  call.id = next_call_id_++;
  call.state =
      state_.calls.empty() ? CallState::kIncoming : CallState::kWaiting;
  call.incoming = true;
  call.number = number;
  state_.calls.push_back(std::move(call));
  IncrementGeneration();
  return state_.calls.back().id;
}

bool RadioStateModel::Dial(const std::string &number) {
  if (!profile_configured_ || !ValidPhoneNumber(number) ||
      state_.sim_state != SimState::kReady || !state_.radio_on ||
      state_.calls.size() >= kMaximumCalls) {
    return false;
  }
  CallRecord call;
  call.id = next_call_id_++;
  call.state = CallState::kDialing;
  call.number = number;
  state_.calls.push_back(std::move(call));
  IncrementGeneration();
  return true;
}

bool RadioStateModel::SetCallState(uint64_t id, CallState state) {
  const auto call =
      std::find_if(state_.calls.begin(), state_.calls.end(),
                   [id](const CallRecord &item) { return item.id == id; });
  if (call == state_.calls.end() || state < CallState::kActive ||
      state > CallState::kWaiting) {
    return false;
  }
  call->state = state;
  IncrementGeneration();
  return true;
}

bool RadioStateModel::Hangup(uint64_t id) {
  const auto call =
      std::find_if(state_.calls.begin(), state_.calls.end(),
                   [id](const CallRecord &item) { return item.id == id; });
  if (call == state_.calls.end()) {
    return false;
  }
  state_.calls.erase(call);
  IncrementGeneration();
  return true;
}

uint64_t RadioStateModel::InjectSms(const std::string &address,
                                    const std::string &body,
                                    int64_t timestamp_ns) {
  if (!profile_configured_ || !state_.radio_on ||
      state_.sim_state != SimState::kReady || !ValidPhoneNumber(address) ||
      body.empty() || body.size() > 1'024) {
    return 0;
  }
  SmsEvent event;
  event.sequence = next_sms_sequence_++;
  event.incoming = true;
  event.timestamp_ns = timestamp_ns;
  event.address = address;
  event.body = body;
  state_.sms_events.push_back(std::move(event));
  if (state_.sms_events.size() > kMaximumSmsEvents) {
    state_.sms_events.erase(state_.sms_events.begin());
  }
  IncrementGeneration();
  return state_.sms_events.back().sequence;
}

void RadioStateModel::RecordOutgoingSms(const std::string &address,
                                        const std::string &body,
                                        int64_t timestamp_ns) {
  if (!profile_configured_ || !state_.radio_on ||
      state_.sim_state != SimState::kReady || !ValidPhoneNumber(address) ||
      body.size() > 1'024) {
    return;
  }
  SmsEvent event;
  event.sequence = next_sms_sequence_++;
  event.timestamp_ns = timestamp_ns;
  event.address = address;
  event.body = body;
  state_.sms_events.push_back(std::move(event));
  if (state_.sms_events.size() > kMaximumSmsEvents) {
    state_.sms_events.erase(state_.sms_events.begin());
  }
  IncrementGeneration();
}

void RadioStateModel::IncrementGeneration() {
  state_.generation = state_.generation == std::numeric_limits<uint64_t>::max()
                          ? 1
                          : state_.generation + 1;
}

void RadioStateModel::AdvanceAutonomousState(float elapsed_seconds,
                                             int64_t timestamp_ns) {
  SignalState next = state_.signal;
  next.rsrp_dbm = static_cast<int32_t>(std::lround(std::clamp(
      -95.0f + rsrp_noise_.Advance(elapsed_seconds), -115.0f, -65.0f)));
  next.rsrq_db = static_cast<int32_t>(std::lround(std::clamp(
      -10.0f + rsrq_noise_.Advance(elapsed_seconds), -20.0f, -4.0f)));
  next.rssnr_tenth_db = static_cast<int32_t>(std::lround(std::clamp(
      180.0f + snr_noise_.Advance(elapsed_seconds), -50.0f, 280.0f)));
  next.rssi_dbm = std::clamp(next.rsrp_dbm + 20, -110, -45);
  next.cqi = std::clamp((next.rssnr_tenth_db + 100) / 25, 0, 15);
  state_.signal = next;
  if (!state_.cells.empty()) {
    state_.cells.front().signal = next;
  }

  if (next_handover_timestamp_ns_ == 0) {
    next_handover_timestamp_ns_ =
        timestamp_ns +
        static_cast<int64_t>(random_.Uniform(300.0f, 900.0f) * 1.0e9f);
  } else if (timestamp_ns >= next_handover_timestamp_ns_ &&
             state_.cells.size() >= 2) {
    state_.cells[0].registered = false;
    state_.cells[1].registered = true;
    std::rotate(state_.cells.begin(), state_.cells.begin() + 1,
                state_.cells.end());
    state_.cells[0].signal = state_.signal;
    next_handover_timestamp_ns_ =
        timestamp_ns +
        static_cast<int64_t>(random_.Uniform(300.0f, 900.0f) * 1.0e9f);
    IncrementGeneration();
  }
}

RadioSnapshot RadioStateModel::Advance(int64_t timestamp_ns) {
  const float elapsed_seconds =
      last_timestamp_ns_ == 0
          ? 0.1f
          : std::clamp((timestamp_ns - last_timestamp_ns_) / 1.0e9f, 0.0f,
                       60.0f);
  last_timestamp_ns_ = timestamp_ns;
  if (external_lease_expiry_ns_ != 0 &&
      timestamp_ns >= external_lease_expiry_ns_) {
    ReleaseExternalControl();
  }
  if (profile_configured_ && state_.radio_on &&
      state_.sim_state == SimState::kReady && !state_.externally_controlled) {
    AdvanceAutonomousState(elapsed_seconds, timestamp_ns);
  }
  state_.timestamp_ns = timestamp_ns;
  return state_;
}

} // namespace floral::radio
