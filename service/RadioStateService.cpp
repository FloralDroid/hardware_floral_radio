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

#include "floral/radio/RadioStateService.h"

#include "floral/radio/GsmPduCodec.h"

#include <android-base/logging.h>
#include <utils/Timers.h>

#include <algorithm>
#include <ctime>

namespace floral::radio {
namespace {

constexpr char kMountedProfilePath[] = "/mnt/vendor/floral_stream/radio.json";
constexpr char kPersistentProfilePath[] =
    "/data/vendor/floral/radio/profile.json";

aidl::floral::device::radio::RadioSignal
ToAidlSignal(const SignalState &signal) {
  aidl::floral::device::radio::RadioSignal result;
  result.rssiDbm = signal.rssi_dbm;
  result.rsrpDbm = signal.rsrp_dbm;
  result.rsrqDb = signal.rsrq_db;
  result.rssnrTenthDb = signal.rssnr_tenth_db;
  result.cqi = signal.cqi;
  result.timingAdvance = signal.timing_advance;
  return result;
}

} // namespace

RadioStateService::RadioStateService() : store_(kPersistentProfilePath) {
  RadioProfile profile;
  std::string mounted_error;
  std::string persistent_error;
  if (store_.LoadFile(kMountedProfilePath, &profile, &mounted_error)) {
    model_.SetProfile(profile);
    std::string save_error;
    if (!store_.SavePersistent(profile, &save_error)) {
      LOG(WARNING) << "failed to persist mounted Floral radio profile: "
                   << save_error;
    }
    LOG(INFO) << "using validated mounted Floral radio profile";
  } else if (store_.LoadPersistent(&profile, &persistent_error)) {
    model_.SetProfile(profile);
    LOG(INFO) << "using persisted Floral radio profile";
  } else {
    std::string save_error;
    if (!store_.SavePersistent(model_.profile(), &save_error)) {
      LOG(WARNING) << "failed to persist generated Floral radio profile: "
                   << save_error;
    }
    LOG(INFO) << "using generated Floral radio profile; mounted="
              << mounted_error << "; persistent=" << persistent_error;
  }
}

int64_t RadioStateService::BootTimeNs() {
  return systemTime(SYSTEM_TIME_BOOTTIME);
}

int64_t RadioStateService::RealtimeNs() {
  return systemTime(SYSTEM_TIME_REALTIME);
}

aidl::floral::device::radio::RadioProfile
RadioStateService::ToAidlProfile(const RadioProfile &profile) {
  aidl::floral::device::radio::RadioProfile result;
  result.operatorLongName = profile.operator_long_name;
  result.operatorShortName = profile.operator_short_name;
  result.mcc = profile.mcc;
  result.mnc = profile.mnc;
  result.imei = profile.imei;
  result.imeisv = profile.imeisv;
  result.imsi = profile.imsi;
  result.iccid = profile.iccid;
  result.msisdn = profile.msisdn;
  result.basebandVersion = profile.baseband_version;
  return result;
}

aidl::floral::device::radio::RadioSnapshot
RadioStateService::ToAidlSnapshot(const RadioSnapshot &snapshot) {
  aidl::floral::device::radio::RadioSnapshot result;
  result.generation = static_cast<int64_t>(snapshot.generation);
  result.timestampNs = snapshot.timestamp_ns;
  result.externallyControlled = snapshot.externally_controlled;
  result.radioOn = snapshot.radio_on;
  result.simState = static_cast<int32_t>(snapshot.sim_state);
  result.voiceRegistration = static_cast<int32_t>(snapshot.voice_registration);
  result.dataRegistration = static_cast<int32_t>(snapshot.data_registration);
  result.technology = static_cast<int32_t>(snapshot.technology);
  result.signal = ToAidlSignal(snapshot.signal);
  result.cells.reserve(snapshot.cells.size());
  for (const CellState &cell : snapshot.cells) {
    aidl::floral::device::radio::RadioCell converted;
    converted.identity = static_cast<int64_t>(cell.identity);
    converted.registered = cell.registered;
    converted.tac = cell.tac;
    converted.ci = cell.ci;
    converted.pci = cell.pci;
    converted.earfcn = cell.earfcn;
    converted.bandwidthKhz = cell.bandwidth_khz;
    converted.signal = ToAidlSignal(cell.signal);
    result.cells.push_back(std::move(converted));
  }
  result.calls.reserve(snapshot.calls.size());
  for (const CallRecord &call : snapshot.calls) {
    aidl::floral::device::radio::RadioCall converted;
    converted.id = static_cast<int64_t>(call.id);
    converted.state = static_cast<int32_t>(call.state);
    converted.incoming = call.incoming;
    converted.multiparty = call.multiparty;
    converted.number = call.number;
    result.calls.push_back(std::move(converted));
  }
  result.smsEvents.reserve(snapshot.sms_events.size());
  for (const SmsEvent &event : snapshot.sms_events) {
    aidl::floral::device::radio::RadioSmsEvent converted;
    converted.sequence = static_cast<int64_t>(event.sequence);
    converted.incoming = event.incoming;
    converted.timestampNs = event.timestamp_ns;
    converted.address = event.address;
    converted.body = event.body;
    result.smsEvents.push_back(std::move(converted));
  }
  return result;
}

SignalState RadioStateService::FromAidlSignal(
    const aidl::floral::device::radio::RadioSignal &signal) {
  return {
      .rssi_dbm = signal.rssiDbm,
      .rsrp_dbm = signal.rsrpDbm,
      .rsrq_db = signal.rsrqDb,
      .rssnr_tenth_db = signal.rssnrTenthDb,
      .cqi = signal.cqi,
      .timing_advance = signal.timingAdvance,
  };
}

CellState RadioStateService::FromAidlCell(
    const aidl::floral::device::radio::RadioCell &cell) {
  return {
      .identity = static_cast<uint64_t>(cell.identity),
      .registered = cell.registered,
      .tac = cell.tac,
      .ci = cell.ci,
      .pci = cell.pci,
      .earfcn = cell.earfcn,
      .bandwidth_khz = cell.bandwidthKhz,
      .signal = FromAidlSignal(cell.signal),
  };
}

void RadioStateService::FillResultLocked(
    bool applied, uint64_t object_id,
    aidl::floral::device::radio::RadioControlResult *result) {
  result->result = applied ? 0 : 2;
  result->generation =
      static_cast<int64_t>(model_.Advance(BootTimeNs()).generation);
  result->objectId = static_cast<int64_t>(object_id);
}

void RadioStateService::Notify(uint32_t changes) {
  std::function<void(uint32_t)> callback;
  {
    std::lock_guard lock(mutex_);
    callback = change_callback_;
  }
  if (callback != nullptr) {
    callback(changes);
  }
}

ndk::ScopedAStatus RadioStateService::getProfile(
    aidl::floral::device::radio::RadioProfile *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  std::lock_guard lock(mutex_);
  *result = ToAidlProfile(model_.profile());
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::getSnapshot(
    aidl::floral::device::radio::RadioSnapshot *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  std::lock_guard lock(mutex_);
  *result = ToAidlSnapshot(model_.Advance(BootTimeNs()));
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::setRegistration(
    const aidl::floral::device::radio::RadioRegistrationControl &control,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  bool applied;
  {
    std::lock_guard lock(mutex_);
    RegistrationControl parsed;
    parsed.voice = static_cast<RegistrationState>(control.voiceRegistration);
    parsed.data = static_cast<RegistrationState>(control.dataRegistration);
    parsed.technology = static_cast<RadioTechnology>(control.technology);
    parsed.lease_duration_ms = control.leaseDurationMs;
    applied = model_.SetRegistration(parsed, BootTimeNs());
    FillResultLocked(applied, 0, result);
  }
  if (applied) {
    Notify(kRadioChangeNetwork | kRadioChangeCells);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::setSignal(
    const aidl::floral::device::radio::RadioSignal &signal,
    int64_t lease_duration_ms,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  bool applied;
  {
    std::lock_guard lock(mutex_);
    applied = model_.SetSignal(FromAidlSignal(signal), lease_duration_ms,
                               BootTimeNs());
    FillResultLocked(applied, 0, result);
  }
  if (applied) {
    Notify(kRadioChangeSignal | kRadioChangeCells);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::replaceCells(
    const std::vector<aidl::floral::device::radio::RadioCell> &cells,
    int64_t lease_duration_ms,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  std::vector<CellState> parsed;
  parsed.reserve(cells.size());
  std::transform(cells.begin(), cells.end(), std::back_inserter(parsed),
                 FromAidlCell);
  bool applied;
  {
    std::lock_guard lock(mutex_);
    applied = model_.ReplaceCells(parsed, lease_duration_ms, BootTimeNs());
    FillResultLocked(applied, 0, result);
  }
  if (applied) {
    Notify(kRadioChangeSignal | kRadioChangeCells | kRadioChangeNetwork);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::setSimState(
    int32_t state, int64_t lease_duration_ms,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  bool applied;
  {
    std::lock_guard lock(mutex_);
    applied = model_.SetSimState(static_cast<SimState>(state),
                                 lease_duration_ms, BootTimeNs());
    FillResultLocked(applied, 0, result);
  }
  if (applied) {
    Notify(kRadioChangeSim | kRadioChangeNetwork);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::injectIncomingCall(
    const std::string &number,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  uint64_t call_id;
  {
    std::lock_guard lock(mutex_);
    call_id = model_.InjectIncomingCall(number);
    FillResultLocked(call_id != 0, call_id, result);
  }
  if (call_id != 0) {
    Notify(kRadioChangeCalls);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::setCallState(
    int64_t call_id, int32_t state,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  if (call_id <= 0) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }
  bool applied;
  {
    std::lock_guard lock(mutex_);
    applied = model_.SetCallState(static_cast<uint64_t>(call_id),
                                  static_cast<CallState>(state));
    FillResultLocked(applied, static_cast<uint64_t>(call_id), result);
  }
  if (applied) {
    Notify(kRadioChangeCalls);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::injectIncomingSms(
    const std::string &address, const std::string &body,
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  const int64_t timestamp_ns = RealtimeNs();
  std::string ignored_pdu;
  if (!EncodeSmsDeliverPdu(address, body, timestamp_ns / 1'000'000'000,
                           &ignored_pdu, nullptr)) {
    std::lock_guard lock(mutex_);
    FillResultLocked(false, 0, result);
    return ndk::ScopedAStatus::ok();
  }

  uint64_t sequence = 0;
  {
    std::lock_guard lock(mutex_);
    sequence = model_.InjectSms(address, body, timestamp_ns);
    FillResultLocked(sequence != 0, sequence, result);
  }
  if (sequence != 0) {
    Notify(kRadioChangeSms);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioStateService::releaseExternalControl(
    aidl::floral::device::radio::RadioControlResult *result) {
  if (result == nullptr) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
  }
  {
    std::lock_guard lock(mutex_);
    const bool was_external =
        model_.Advance(BootTimeNs()).externally_controlled;
    model_.ReleaseExternalControl();
    FillResultLocked(was_external, 0, result);
    if (!was_external) {
      result->result = 1;
    }
  }
  Notify(kRadioChangeNetwork | kRadioChangeSignal | kRadioChangeCells |
         kRadioChangeSim);
  return ndk::ScopedAStatus::ok();
}

RadioProfile RadioStateService::GetProfile() {
  std::lock_guard lock(mutex_);
  return model_.profile();
}

RadioSnapshot RadioStateService::GetSnapshot() {
  std::lock_guard lock(mutex_);
  return model_.Advance(BootTimeNs());
}

bool RadioStateService::SetRadioPower(bool enabled) {
  bool changed;
  {
    std::lock_guard lock(mutex_);
    changed = model_.SetRadioPower(enabled);
  }
  if (changed) {
    Notify(kRadioChangePower | kRadioChangeNetwork);
  }
  return true;
}

bool RadioStateService::Dial(const std::string &number) {
  bool applied;
  {
    std::lock_guard lock(mutex_);
    applied = model_.Dial(number);
  }
  if (applied) {
    Notify(kRadioChangeCalls);
  }
  return applied;
}

bool RadioStateService::Answer() {
  bool applied = false;
  {
    std::lock_guard lock(mutex_);
    const RadioSnapshot snapshot = model_.Advance(BootTimeNs());
    const auto call =
        std::find_if(snapshot.calls.begin(), snapshot.calls.end(),
                     [](const CallRecord &item) {
                       return item.state == CallState::kIncoming ||
                              item.state == CallState::kWaiting;
                     });
    if (call != snapshot.calls.end()) {
      applied = model_.SetCallState(call->id, CallState::kActive);
    }
  }
  if (applied) {
    Notify(kRadioChangeCalls);
  }
  return applied;
}

bool RadioStateService::Hangup(uint64_t call_id) {
  bool applied;
  {
    std::lock_guard lock(mutex_);
    applied = model_.Hangup(call_id);
  }
  if (applied) {
    Notify(kRadioChangeCalls);
  }
  return applied;
}

void RadioStateService::RecordOutgoingSms(const std::string &address,
                                          const std::string &body) {
  {
    std::lock_guard lock(mutex_);
    model_.RecordOutgoingSms(address, body, RealtimeNs());
  }
  Notify(kRadioChangeSms);
}

void RadioStateService::SetChangeCallback(
    std::function<void(uint32_t)> callback) {
  std::lock_guard lock(mutex_);
  change_callback_ = std::move(callback);
}

} // namespace floral::radio
