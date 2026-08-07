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

#include "floral/radio/RadioProfileStore.h"
#include "floral/radio/RadioStateModel.h"

#include <aidl/floral/device/radio/BnRadioState.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace floral::radio {

enum RadioChange : uint32_t {
  kRadioChangeNone = 0,
  kRadioChangeNetwork = 1U << 0,
  kRadioChangeSignal = 1U << 1,
  kRadioChangeCells = 1U << 2,
  kRadioChangeSim = 1U << 3,
  kRadioChangeCalls = 1U << 4,
  kRadioChangeSms = 1U << 5,
  kRadioChangePower = 1U << 6,
};

class RadioStateService final
    : public aidl::floral::device::radio::BnRadioState {
public:
  RadioStateService();

  ndk::ScopedAStatus
  getProfile(aidl::floral::device::radio::RadioProfile *result) override;
  ndk::ScopedAStatus
  getSnapshot(aidl::floral::device::radio::RadioSnapshot *result) override;
  ndk::ScopedAStatus setRegistration(
      const aidl::floral::device::radio::RadioRegistrationControl &control,
      aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus
  setSignal(const aidl::floral::device::radio::RadioSignal &signal,
            int64_t lease_duration_ms,
            aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus replaceCells(
      const std::vector<aidl::floral::device::radio::RadioCell> &cells,
      int64_t lease_duration_ms,
      aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus
  setSimState(int32_t state, int64_t lease_duration_ms,
              aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus injectIncomingCall(
      const std::string &number,
      aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus setCallState(
      int64_t call_id, int32_t state,
      aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus injectIncomingSms(
      const std::string &address, const std::string &body,
      aidl::floral::device::radio::RadioControlResult *result) override;
  ndk::ScopedAStatus releaseExternalControl(
      aidl::floral::device::radio::RadioControlResult *result) override;

  RadioProfile GetProfile();
  RadioSnapshot GetSnapshot();
  bool SetRadioPower(bool enabled);
  bool Dial(const std::string &number);
  bool Answer();
  bool Hangup(uint64_t call_id);
  void RecordOutgoingSms(const std::string &address, const std::string &body);
  void SetChangeCallback(std::function<void(uint32_t)> callback);

private:
  static int64_t BootTimeNs();
  static int64_t RealtimeNs();
  static aidl::floral::device::radio::RadioProfile
  ToAidlProfile(const RadioProfile &profile);
  static aidl::floral::device::radio::RadioSnapshot
  ToAidlSnapshot(const RadioSnapshot &snapshot);
  static SignalState
  FromAidlSignal(const aidl::floral::device::radio::RadioSignal &signal);
  static CellState
  FromAidlCell(const aidl::floral::device::radio::RadioCell &cell);
  void
  FillResultLocked(bool applied, uint64_t object_id,
                   aidl::floral::device::radio::RadioControlResult *result);
  void Notify(uint32_t changes);

  std::mutex mutex_;
  RadioStateModel model_;
  RadioProfileStore store_;
  std::function<void(uint32_t)> change_callback_;
};

} // namespace floral::radio
