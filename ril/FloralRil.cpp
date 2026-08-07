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

#define LOG_TAG "FloralRIL"

#include "floral/radio/GsmPduCodec.h"
#include "floral/radio/RadioStateService.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <log/log.h>
#include <telephony/ril_mnc.h>

#include <guest/hals/ril/reference-libril/ril.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace floral::radio {
namespace {

constexpr char kRadioStateServiceName[] =
    "floral.device.radio.IRadioState/default";
constexpr char kUsimAid[] = "A0000000871002";
constexpr char kUsimLabel[] = "Floral USIM";
constexpr char kLogicalModemUuid[] = "floral.modem0";

const RIL_Env *g_environment = nullptr;
std::shared_ptr<RadioStateService> g_service;
int g_preferred_network_type = PREF_NET_TYPE_LTE_GSM_WCDMA;
int g_allowed_network_types_bitmap =
    RAF_GSM | RAF_GPRS | RAF_EDGE | RAF_UMTS | RAF_LTE;
int g_muted = 0;
int g_uicc_enabled = 1;
int g_next_sms_reference = 1;

void Complete(RIL_Token token, RIL_Errno error, void *response = nullptr,
              size_t response_length = 0) {
  g_environment->OnRequestComplete(token, error, response, response_length);
}

void CompleteString(RIL_Token token, const std::string &value) {
  Complete(token, RIL_E_SUCCESS, const_cast<char *>(value.c_str()),
           value.size() + 1);
}

template <size_t Size>
void CompleteStrings(RIL_Token token,
                     const std::array<std::string, Size> &values) {
  std::array<char *, Size> pointers = {};
  for (size_t index = 0; index < Size; ++index) {
    pointers[index] = const_cast<char *>(values[index].c_str());
  }
  Complete(token, RIL_E_SUCCESS, pointers.data(), sizeof(pointers));
}

RIL_RadioTechnology ToRilTechnology(RadioTechnology technology) {
  switch (technology) {
  case RadioTechnology::kGsm:
    return RADIO_TECH_GSM;
  case RadioTechnology::kWcdma:
    return RADIO_TECH_UMTS;
  case RadioTechnology::kLte:
    return RADIO_TECH_LTE;
  case RadioTechnology::kUnknown:
    return RADIO_TECH_UNKNOWN;
  }
  return RADIO_TECH_UNKNOWN;
}

RIL_RegState ToRilRegistration(RegistrationState state) {
  return static_cast<RIL_RegState>(state);
}

RIL_CallState ToRilCallState(CallState state) {
  return static_cast<RIL_CallState>(state);
}

RIL_RadioState CurrentRadioState() {
  if (g_service == nullptr) {
    return RADIO_STATE_UNAVAILABLE;
  }
  return g_service->GetSnapshot().radio_on ? RADIO_STATE_ON : RADIO_STATE_OFF;
}

CellState ServingCell(const RadioSnapshot &snapshot) {
  const auto serving =
      std::find_if(snapshot.cells.begin(), snapshot.cells.end(),
                   [](const CellState &cell) { return cell.registered; });
  return serving == snapshot.cells.end() ? CellState{} : *serving;
}

int GsmSignalStrength(int rssi_dbm) {
  if (rssi_dbm < -113 || rssi_dbm > -51) {
    return 99;
  }
  return std::clamp((rssi_dbm + 113) / 2, 0, 31);
}

RIL_SignalStrength_v12 MakeSignalStrength(const SignalState &signal) {
  RIL_SignalStrength_v12 result = {};
  result.GW_SignalStrength.signalStrength = GsmSignalStrength(signal.rssi_dbm);
  result.GW_SignalStrength.bitErrorRate = 0;
  result.CDMA_SignalStrength.dbm = INT_MAX;
  result.CDMA_SignalStrength.ecio = INT_MAX;
  result.EVDO_SignalStrength.dbm = INT_MAX;
  result.EVDO_SignalStrength.ecio = INT_MAX;
  result.EVDO_SignalStrength.signalNoiseRatio = INT_MAX;
  result.LTE_SignalStrength.signalStrength = GsmSignalStrength(signal.rssi_dbm);
  result.LTE_SignalStrength.rsrp = -signal.rsrp_dbm;
  result.LTE_SignalStrength.rsrq = -signal.rsrq_db;
  result.LTE_SignalStrength.rssnr = signal.rssnr_tenth_db;
  result.LTE_SignalStrength.cqi = signal.cqi;
  result.LTE_SignalStrength.timingAdvance = signal.timing_advance;
  result.TD_SCDMA_SignalStrength.rscp = INT_MAX;
  result.WCDMA_SignalStrength.signalStrength =
      GsmSignalStrength(signal.rssi_dbm);
  result.WCDMA_SignalStrength.bitErrorRate = 0;
  result.NR_SignalStrength = {INT_MAX, INT_MAX, INT_MAX,
                              INT_MAX, INT_MAX, INT_MAX};
  return result;
}

void FillLteIdentity(const RadioProfile &profile, const CellState &cell,
                     RIL_CellIdentityLte_v12 *identity) {
  identity->mcc = std::stoi(profile.mcc);
  identity->mnc = ril::util::mnc::encode(profile.mnc);
  identity->ci = static_cast<int>(cell.ci);
  identity->pci = cell.pci;
  identity->tac = cell.tac;
  identity->earfcn = cell.earfcn;
}

void RespondCardStatus(RIL_Token token) {
  const RadioProfile profile = g_service->GetProfile();
  const RadioSnapshot snapshot = g_service->GetSnapshot();
  RIL_CardStatus_v1_5 status = {};
  RIL_CardStatus_v6 &base = status.base.base.base;
  base.card_state = snapshot.sim_state == SimState::kAbsent
                        ? RIL_CARDSTATE_ABSENT
                        : RIL_CARDSTATE_PRESENT;
  base.universal_pin_state = RIL_PINSTATE_DISABLED;
  base.gsm_umts_subscription_app_index =
      base.card_state == RIL_CARDSTATE_PRESENT ? 0 : -1;
  base.cdma_subscription_app_index = -1;
  base.ims_subscription_app_index = -1;
  base.num_applications = base.card_state == RIL_CARDSTATE_PRESENT ? 1 : 0;
  if (base.num_applications == 1) {
    RIL_AppStatus &app = base.applications[0];
    app.app_type = RIL_APPTYPE_USIM;
    app.app_state = snapshot.sim_state == SimState::kReady ? RIL_APPSTATE_READY
                    : snapshot.sim_state == SimState::kPinRequired
                        ? RIL_APPSTATE_PIN
                        : RIL_APPSTATE_PUK;
    app.perso_substate = RIL_PERSOSUBSTATE_READY;
    app.aid_ptr = const_cast<char *>(kUsimAid);
    app.app_label_ptr = const_cast<char *>(kUsimLabel);
    app.pin1_replaced = 0;
    app.pin1 = snapshot.sim_state == SimState::kReady
                   ? RIL_PINSTATE_ENABLED_VERIFIED
               : snapshot.sim_state == SimState::kPinRequired
                   ? RIL_PINSTATE_ENABLED_NOT_VERIFIED
                   : RIL_PINSTATE_ENABLED_BLOCKED;
    app.pin2 = RIL_PINSTATE_DISABLED;
  }
  status.base.base.physicalSlotId = 0;
  status.base.base.atr = nullptr;
  status.base.base.iccid = const_cast<char *>(profile.iccid.c_str());
  status.base.eid = nullptr;
  Complete(token, RIL_E_SUCCESS, &status, sizeof(status));
}

void RespondCurrentCalls(RIL_Token token) {
  const RadioSnapshot snapshot = g_service->GetSnapshot();
  std::vector<RIL_Call> calls(snapshot.calls.size());
  std::vector<RIL_Call *> pointers(snapshot.calls.size());
  for (size_t index = 0; index < snapshot.calls.size(); ++index) {
    const CallRecord &source = snapshot.calls[index];
    RIL_Call &target = calls[index];
    target.state = ToRilCallState(source.state);
    target.index = static_cast<int>(source.id);
    target.toa =
        !source.number.empty() && source.number.front() == '+' ? 145 : 129;
    target.isMpty = source.multiparty;
    target.isMT = source.incoming;
    target.als = 0;
    target.isVoice = 1;
    target.isVoicePrivacy = 0;
    target.number = const_cast<char *>(source.number.c_str());
    target.numberPresentation = 0;
    target.name = nullptr;
    target.namePresentation = 2;
    target.uusInfo = nullptr;
    pointers[index] = &target;
  }
  Complete(token, RIL_E_SUCCESS, pointers.data(),
           pointers.size() * sizeof(RIL_Call *));
}

void RespondVoiceRegistration(RIL_Token token) {
  const RadioProfile profile = g_service->GetProfile();
  const RadioSnapshot snapshot = g_service->GetSnapshot();
  const CellState cell = ServingCell(snapshot);
  std::array<std::string, 18> response = {};
  response[0] = std::to_string(ToRilRegistration(snapshot.voice_registration));
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%x", cell.tac);
  response[1] = buffer;
  std::snprintf(buffer, sizeof(buffer), "%llx",
                static_cast<unsigned long long>(cell.ci));
  response[2] = buffer;
  response[3] = std::to_string(ToRilTechnology(snapshot.technology));
  response[13] = "0";
  response[14] = std::to_string(cell.pci);
  response[15] = profile.mcc;
  response[16] = profile.mnc;
  response[17] = profile.mcc + profile.mnc;
  CompleteStrings(token, response);
}

void RespondDataRegistration(RIL_Token token) {
  const RadioProfile profile = g_service->GetProfile();
  const RadioSnapshot snapshot = g_service->GetSnapshot();
  const CellState cell = ServingCell(snapshot);
  std::array<std::string, 14> response = {};
  response[0] = std::to_string(ToRilRegistration(snapshot.data_registration));
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%x", cell.tac);
  response[1] = buffer;
  std::snprintf(buffer, sizeof(buffer), "%llx",
                static_cast<unsigned long long>(cell.ci));
  response[2] = buffer;
  response[3] = std::to_string(ToRilTechnology(snapshot.technology));
  response[4] = "0";
  response[5] = "0";
  response[11] = profile.mcc;
  response[12] = profile.mnc;
  response[13] = profile.mcc + profile.mnc;
  CompleteStrings(token, response);
}

void RespondCellInfo(RIL_Token token) {
  const RadioProfile profile = g_service->GetProfile();
  const RadioSnapshot snapshot = g_service->GetSnapshot();
  std::vector<RIL_CellInfo_v12> response(snapshot.cells.size());
  for (size_t index = 0; index < snapshot.cells.size(); ++index) {
    const CellState &source = snapshot.cells[index];
    RIL_CellInfo_v12 &target = response[index];
    target.cellInfoType = RIL_CELL_INFO_TYPE_LTE;
    target.registered = source.registered;
    target.timeStampType = RIL_TIMESTAMP_TYPE_MODEM;
    target.timeStamp = static_cast<uint64_t>(snapshot.timestamp_ns);
    FillLteIdentity(profile, source, &target.CellInfo.lte.cellIdentityLte);
    target.CellInfo.lte.signalStrengthLte =
        MakeSignalStrength(source.signal).LTE_SignalStrength;
  }
  Complete(token, RIL_E_SUCCESS, response.data(),
           response.size() * sizeof(RIL_CellInfo_v12));
}

void RespondRadioCapability(RIL_Token token) {
  RIL_RadioCapability capability = {};
  capability.version = RIL_RADIO_CAPABILITY_VERSION;
  capability.phase = RC_PHASE_CONFIGURED;
  capability.rat = RAF_GSM | RAF_GPRS | RAF_EDGE | RAF_UMTS | RAF_LTE;
  std::strncpy(capability.logicalModemUuid, kLogicalModemUuid,
               sizeof(capability.logicalModemUuid) - 1);
  capability.status = RC_STATUS_SUCCESS;
  Complete(token, RIL_E_SUCCESS, &capability, sizeof(capability));
}

void RespondSlotStatus(RIL_Token token) {
  const RadioProfile profile = g_service->GetProfile();
  const RadioSnapshot snapshot = g_service->GetSnapshot();
  RIL_SimSlotStatus_V1_2 status = {};
  status.base.cardState = snapshot.sim_state == SimState::kAbsent
                              ? RIL_CARDSTATE_ABSENT
                              : RIL_CARDSTATE_PRESENT;
  status.base.slotState = SLOT_STATE_ACTIVE;
  status.base.atr = nullptr;
  status.base.logicalSlotId = 0;
  status.base.iccid = const_cast<char *>(profile.iccid.c_str());
  status.eid = nullptr;
  Complete(token, RIL_E_SUCCESS, &status, sizeof(status));
}

void NotifyChanges(uint32_t changes) {
  if (g_environment == nullptr || g_service == nullptr) {
    return;
  }
  if ((changes & (kRadioChangePower | kRadioChangeNetwork)) != 0) {
    g_environment->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
                                         nullptr, 0);
  }
  if ((changes & (kRadioChangeNetwork | kRadioChangeCells)) != 0) {
    g_environment->OnUnsolicitedResponse(
        RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, nullptr, 0);
  }
  if ((changes & kRadioChangeSignal) != 0) {
    const RIL_SignalStrength_v12 signal =
        MakeSignalStrength(g_service->GetSnapshot().signal);
    g_environment->OnUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH, &signal,
                                         sizeof(signal));
  }
  if ((changes & kRadioChangeCalls) != 0) {
    g_environment->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
                                         nullptr, 0);
  }
  if ((changes & kRadioChangeSim) != 0) {
    g_environment->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED,
                                         nullptr, 0);
  }
  if ((changes & kRadioChangeSms) != 0) {
    const RadioSnapshot snapshot = g_service->GetSnapshot();
    if (!snapshot.sms_events.empty() && snapshot.sms_events.back().incoming) {
      const SmsEvent &event = snapshot.sms_events.back();
      std::string pdu;
      std::string error;
      if (EncodeSmsDeliverPdu(event.address, event.body,
                              event.timestamp_ns / 1'000'000'000, &pdu,
                              &error)) {
        g_environment->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_SMS,
                                             pdu.data(), pdu.size());
      } else {
        ALOGW("cannot encode injected SMS: %s", error.c_str());
      }
    }
  }
}

void HandleSimPin(void *data, size_t data_length, RIL_Token token) {
  int retries = 3;
  if (data == nullptr || data_length < sizeof(char *)) {
    Complete(token, RIL_E_INVALID_ARGUMENTS, &retries, sizeof(retries));
    return;
  }
  const char *pin = static_cast<char **>(data)[0];
  if (pin == nullptr || pin != g_service->GetProfile().sim_pin) {
    Complete(token, RIL_E_PASSWORD_INCORRECT, &retries, sizeof(retries));
    return;
  }
  aidl::floral::device::radio::RadioControlResult ignored;
  g_service->setSimState(static_cast<int32_t>(SimState::kReady), 60'000,
                         &ignored);
  Complete(token, RIL_E_SUCCESS, &retries, sizeof(retries));
}

void HandleRequest(int request, void *data, size_t data_length,
                   RIL_Token token) {
  if (g_service == nullptr) {
    Complete(token, RIL_E_RADIO_NOT_AVAILABLE);
    return;
  }
  switch (request) {
  case RIL_REQUEST_GET_SIM_STATUS:
    RespondCardStatus(token);
    return;
  case RIL_REQUEST_ENTER_SIM_PIN:
    HandleSimPin(data, data_length, token);
    return;
  case RIL_REQUEST_GET_CURRENT_CALLS:
    RespondCurrentCalls(token);
    return;
  case RIL_REQUEST_DIAL: {
    if (data == nullptr || data_length < sizeof(RIL_Dial)) {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
      return;
    }
    const RIL_Dial &dial = *static_cast<RIL_Dial *>(data);
    Complete(token, dial.address != nullptr && g_service->Dial(dial.address)
                        ? RIL_E_SUCCESS
                        : RIL_E_INVALID_ARGUMENTS);
    return;
  }
  case RIL_REQUEST_HANGUP: {
    if (data == nullptr || data_length < sizeof(int)) {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
      return;
    }
    const int call_id = *static_cast<int *>(data);
    Complete(token, call_id > 0 && g_service->Hangup(call_id)
                        ? RIL_E_SUCCESS
                        : RIL_E_INVALID_CALL_ID);
    return;
  }
  case RIL_REQUEST_ANSWER:
    Complete(token, g_service->Answer() ? RIL_E_SUCCESS : RIL_E_INVALID_STATE);
    return;
  case RIL_REQUEST_GET_IMSI:
    CompleteString(token, g_service->GetProfile().imsi);
    return;
  case RIL_REQUEST_GET_IMEI:
    CompleteString(token, g_service->GetProfile().imei);
    return;
  case RIL_REQUEST_GET_IMEISV:
    CompleteString(token, g_service->GetProfile().imeisv);
    return;
  case RIL_REQUEST_DEVICE_IDENTITY: {
    const RadioProfile profile = g_service->GetProfile();
    std::array<std::string, 4> response = {profile.imei, profile.imeisv, "",
                                           ""};
    CompleteStrings(token, response);
    return;
  }
  case RIL_REQUEST_SIGNAL_STRENGTH: {
    RIL_SignalStrength_v12 response =
        MakeSignalStrength(g_service->GetSnapshot().signal);
    Complete(token, RIL_E_SUCCESS, &response, sizeof(response));
    return;
  }
  case RIL_REQUEST_VOICE_REGISTRATION_STATE:
    RespondVoiceRegistration(token);
    return;
  case RIL_REQUEST_DATA_REGISTRATION_STATE:
    RespondDataRegistration(token);
    return;
  case RIL_REQUEST_OPERATOR: {
    const RadioProfile profile = g_service->GetProfile();
    const std::array<std::string, 3> response = {profile.operator_long_name,
                                                 profile.operator_short_name,
                                                 profile.mcc + profile.mnc};
    CompleteStrings(token, response);
    return;
  }
  case RIL_REQUEST_RADIO_POWER: {
    if (data == nullptr || data_length < sizeof(int)) {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
      return;
    }
    g_service->SetRadioPower(*static_cast<int *>(data) != 0);
    Complete(token, RIL_E_SUCCESS);
    return;
  }
  case RIL_REQUEST_SEND_SMS:
  case RIL_REQUEST_SEND_SMS_EXPECT_MORE: {
    if (data == nullptr || data_length < 2 * sizeof(char *)) {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
      return;
    }
    char **message = static_cast<char **>(data);
    if (message[1] == nullptr) {
      Complete(token, RIL_E_INVALID_SMS_FORMAT);
      return;
    }
    g_service->RecordOutgoingSms("+80000000000", message[1]);
    RIL_SMS_Response response = {};
    response.messageRef = g_next_sms_reference++;
    response.ackPDU = nullptr;
    response.errorCode = -1;
    Complete(token, RIL_E_SUCCESS, &response, sizeof(response));
    return;
  }
  case RIL_REQUEST_SMS_ACKNOWLEDGE:
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE: {
    int automatic = 0;
    Complete(token, RIL_E_SUCCESS, &automatic, sizeof(automatic));
    return;
  }
  case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
    Complete(token, RIL_E_SUCCESS, &g_preferred_network_type,
             sizeof(g_preferred_network_type));
    return;
  case RIL_REQUEST_GET_ALLOWED_NETWORK_TYPES_BITMAP:
    Complete(token, RIL_E_SUCCESS, &g_allowed_network_types_bitmap,
             sizeof(g_allowed_network_types_bitmap));
    return;
  case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
    if (data != nullptr && data_length >= sizeof(int)) {
      g_preferred_network_type = *static_cast<int *>(data);
      Complete(token, RIL_E_SUCCESS);
    } else {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
    }
    return;
  case RIL_REQUEST_SET_ALLOWED_NETWORK_TYPES_BITMAP:
    if (data != nullptr && data_length >= sizeof(int)) {
      g_allowed_network_types_bitmap = *static_cast<int *>(data);
      Complete(token, RIL_E_SUCCESS);
    } else {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
    }
    return;
  case RIL_REQUEST_VOICE_RADIO_TECH: {
    const int technology = ToRilTechnology(g_service->GetSnapshot().technology);
    Complete(token, RIL_E_SUCCESS, const_cast<int *>(&technology),
             sizeof(technology));
    return;
  }
  case RIL_REQUEST_GET_CELL_INFO_LIST:
    RespondCellInfo(token);
    return;
  case RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE:
  case RIL_REQUEST_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA:
  case RIL_REQUEST_SET_LINK_CAPACITY_REPORTING_CRITERIA:
  case RIL_REQUEST_SET_UNSOLICITED_RESPONSE_FILTER:
  case RIL_REQUEST_SEND_DEVICE_STATE:
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_BASEBAND_VERSION:
    CompleteString(token, g_service->GetProfile().baseband_version);
    return;
  case RIL_REQUEST_GET_RADIO_CAPABILITY:
    RespondRadioCapability(token);
    return;
  case RIL_REQUEST_SET_RADIO_CAPABILITY:
    if (data == nullptr || data_length != sizeof(RIL_RadioCapability)) {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
    } else {
      Complete(token, RIL_E_SUCCESS, data, data_length);
    }
    return;
  case RIL_REQUEST_GET_MUTE:
    Complete(token, RIL_E_SUCCESS, &g_muted, sizeof(g_muted));
    return;
  case RIL_REQUEST_SET_MUTE:
    if (data != nullptr && data_length >= sizeof(int)) {
      g_muted = *static_cast<int *>(data) != 0;
      Complete(token, RIL_E_SUCCESS);
    } else {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
    }
    return;
  case RIL_REQUEST_DATA_CALL_LIST:
    Complete(token, RIL_E_SUCCESS, nullptr, 0);
    return;
  case RIL_REQUEST_SETUP_DATA_CALL:
    Complete(token, RIL_E_REQUEST_NOT_SUPPORTED);
    return;
  case RIL_REQUEST_DEACTIVATE_DATA_CALL:
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_ENABLE_UICC_APPLICATIONS:
    if (data != nullptr && data_length >= sizeof(int)) {
      g_uicc_enabled = *static_cast<int *>(data) != 0;
      Complete(token, RIL_E_SUCCESS);
    } else {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
    }
    return;
  case RIL_REQUEST_ARE_UICC_APPLICATIONS_ENABLED:
    Complete(token, RIL_E_SUCCESS, &g_uicc_enabled, sizeof(g_uicc_enabled));
    return;
  case RIL_REQUEST_GET_MODEM_STACK_STATUS: {
    const int enabled = 1;
    Complete(token, RIL_E_SUCCESS, const_cast<int *>(&enabled),
             sizeof(enabled));
    return;
  }
  case RIL_REQUEST_ENABLE_MODEM:
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_SHUTDOWN:
    g_service->SetRadioPower(false);
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_CONFIG_GET_SLOT_STATUS:
    RespondSlotStatus(token);
    return;
  case RIL_REQUEST_CONFIG_SET_SLOT_MAPPING:
  case RIL_REQUEST_CONFIG_SET_PREFER_DATA_MODEM:
    Complete(token, RIL_E_SUCCESS);
    return;
  case RIL_REQUEST_CONFIG_GET_PHONE_CAPABILITY: {
    RIL_PhoneCapability capability = {};
    capability.maxActiveData = 1;
    capability.maxActiveInternetData = 1;
    capability.isInternetLingeringSupported = 0;
    capability.logicalModemList[0].modemId = 0;
    Complete(token, RIL_E_SUCCESS, &capability, sizeof(capability));
    return;
  }
  case RIL_REQUEST_CONFIG_SET_MODEM_CONFIG:
    if (data != nullptr && data_length == sizeof(RIL_ModemConfig) &&
        static_cast<RIL_ModemConfig *>(data)->numOfLiveModems == 1) {
      Complete(token, RIL_E_SUCCESS);
    } else {
      Complete(token, RIL_E_INVALID_ARGUMENTS);
    }
    return;
  case RIL_REQUEST_CONFIG_GET_MODEM_CONFIG: {
    RIL_ModemConfig config = {};
    config.numOfLiveModems = 1;
    Complete(token, RIL_E_SUCCESS, &config, sizeof(config));
    return;
  }
  case RIL_REQUEST_CONFIG_GET_HAL_DEVICE_CAPABILITIES: {
    bool reduced_feature_set = false;
    Complete(token, RIL_E_SUCCESS, &reduced_feature_set,
             sizeof(reduced_feature_set));
    return;
  }
  default:
    ALOGV("unsupported RIL request %d", request);
    Complete(token, RIL_E_REQUEST_NOT_SUPPORTED);
    return;
  }
}

int Supports(int request) {
  return request > 0 && request <= RIL_REQUEST_RADIO_CONFIG_LAST;
}

void Cancel(RIL_Token) {}

const char *Version() { return "FloralDroid RIL 1.0"; }

const RIL_RadioFunctions kFunctions = {
    RIL_VERSION, HandleRequest, CurrentRadioState, Supports, Cancel, Version,
};

} // namespace
} // namespace floral::radio

extern "C" const RIL_RadioFunctions *RIL_Init(const RIL_Env *environment, int,
                                              char **) {
  using namespace floral::radio;
  if (environment == nullptr) {
    return nullptr;
  }
  g_environment = environment;
  g_service = ndk::SharedRefBase::make<RadioStateService>();
  g_service->SetChangeCallback(NotifyChanges);
  const binder_status_t status = AServiceManager_addService(
      g_service->asBinder().get(), kRadioStateServiceName);
  if (status != STATUS_OK) {
    ALOGE("cannot register %s: %d", kRadioStateServiceName, status);
    g_service.reset();
    return nullptr;
  }
  return &kFunctions;
}
