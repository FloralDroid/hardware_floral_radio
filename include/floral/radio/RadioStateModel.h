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

#include "floral/device/simulation/RandomProcess.h"
#include "floral/radio/RadioTypes.h"

#include <cstdint>
#include <string>

namespace floral::radio {

class RadioStateModel final {
public:
  explicit RadioStateModel(
      uint64_t seed = floral::device::simulation::GenerateSessionSeed());

  static bool ValidateProfile(const RadioProfile &profile, std::string *error);

  bool SetProfile(const RadioProfile &profile);
  bool SetRegistration(const RegistrationControl &control,
                       int64_t timestamp_ns);
  bool SetSignal(const SignalState &signal, int64_t lease_duration_ms,
                 int64_t timestamp_ns);
  bool ReplaceCells(const std::vector<CellState> &cells,
                    int64_t lease_duration_ms, int64_t timestamp_ns);
  bool SetSimState(SimState state, int64_t lease_duration_ms,
                   int64_t timestamp_ns);
  bool SetRadioPower(bool enabled);
  void ReleaseExternalControl();

  uint64_t InjectIncomingCall(const std::string &number);
  bool Dial(const std::string &number);
  bool SetCallState(uint64_t id, CallState state);
  bool Hangup(uint64_t id);
  uint64_t InjectSms(const std::string &address, const std::string &body,
                     int64_t timestamp_ns);
  void RecordOutgoingSms(const std::string &address, const std::string &body,
                         int64_t timestamp_ns);

  RadioSnapshot Advance(int64_t timestamp_ns);
  const RadioProfile &profile() const { return profile_; }
  bool profile_configured() const { return profile_configured_; }

private:
  void ActivateConfiguredProfile();
  bool ValidateSignal(const SignalState &signal) const;
  bool ValidateCell(const CellState &cell) const;
  void AdvanceAutonomousState(float elapsed_seconds, int64_t timestamp_ns);
  void IncrementGeneration();

  RadioProfile profile_;
  RadioSnapshot state_;
  floral::device::simulation::RandomStream random_;
  floral::device::simulation::GaussMarkovProcess rsrp_noise_;
  floral::device::simulation::GaussMarkovProcess rsrq_noise_;
  floral::device::simulation::GaussMarkovProcess snr_noise_;
  int64_t last_timestamp_ns_ = 0;
  int64_t external_lease_expiry_ns_ = 0;
  int64_t next_handover_timestamp_ns_ = 0;
  uint64_t next_call_id_ = 1;
  uint64_t next_sms_sequence_ = 1;
  bool profile_configured_ = false;
};

} // namespace floral::radio
