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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace floral::radio {

enum class SimState : int32_t {
  kAbsent = 0,
  kReady = 1,
  kPinRequired = 2,
  kPukRequired = 3,
};

enum class RegistrationState : int32_t {
  kNotRegistered = 0,
  kHome = 1,
  kSearching = 2,
  kDenied = 3,
  kUnknown = 4,
  kRoaming = 5,
};

enum class RadioTechnology : int32_t {
  kUnknown = 0,
  kGsm = 1,
  kWcdma = 2,
  kLte = 3,
};

enum class CallState : int32_t {
  kActive = 0,
  kHolding = 1,
  kDialing = 2,
  kAlerting = 3,
  kIncoming = 4,
  kWaiting = 5,
};

struct RadioProfile {
  uint32_t version = 1;
  std::string operator_long_name = "Floral Mobile";
  std::string operator_short_name = "Floral";
  std::string mcc = "001";
  std::string mnc = "01";
  std::string imei;
  std::string imeisv = "01";
  std::string imsi;
  std::string iccid;
  std::string msisdn;
  std::string baseband_version = "FLORAL.MODEM.1.0";
  std::string sim_pin = "1234";
};

struct SignalState {
  int32_t rssi_dbm = -75;
  int32_t rsrp_dbm = -95;
  int32_t rsrq_db = -10;
  int32_t rssnr_tenth_db = 180;
  int32_t cqi = 10;
  int32_t timing_advance = 1;
};

struct CellState {
  uint64_t identity = 1;
  bool registered = true;
  int32_t tac = 100;
  int64_t ci = 0x010001;
  int32_t pci = 10;
  int32_t earfcn = 1300;
  int32_t bandwidth_khz = 20'000;
  SignalState signal;
};

struct CallRecord {
  uint64_t id = 0;
  CallState state = CallState::kActive;
  bool incoming = false;
  bool multiparty = false;
  std::string number;
};

struct SmsEvent {
  uint64_t sequence = 0;
  bool incoming = false;
  int64_t timestamp_ns = 0;
  std::string address;
  std::string body;
};

struct RadioSnapshot {
  uint64_t generation = 1;
  int64_t timestamp_ns = 0;
  bool externally_controlled = false;
  bool radio_on = true;
  SimState sim_state = SimState::kReady;
  RegistrationState voice_registration = RegistrationState::kHome;
  RegistrationState data_registration = RegistrationState::kNotRegistered;
  RadioTechnology technology = RadioTechnology::kLte;
  SignalState signal;
  std::vector<CellState> cells;
  std::vector<CallRecord> calls;
  std::vector<SmsEvent> sms_events;
};

struct RegistrationControl {
  RegistrationState voice = RegistrationState::kHome;
  RegistrationState data = RegistrationState::kNotRegistered;
  RadioTechnology technology = RadioTechnology::kLte;
  int64_t lease_duration_ms = 30'000;
};

} // namespace floral::radio
