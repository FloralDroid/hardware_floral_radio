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

#include "floral/radio/GsmPduCodec.h"
#include "floral/radio/RadioProfileStore.h"
#include "floral/radio/RadioStateModel.h"

#include <android-base/file.h>
#include <gtest/gtest.h>

namespace floral::radio {
namespace {

TEST(RadioProfileStoreTest, RepositoryExampleIsValid) {
  RadioProfile profile;
  std::string error;
  const std::string path =
      android::base::GetExecutableDirectory() + "/examples/radio.json";
  RadioProfileStore store("/unused/floral-radio-profile.json");

  ASSERT_TRUE(store.LoadFile(path, &profile, &error)) << error;
  EXPECT_EQ(profile.operator_long_name, "Floral Mobile");
  EXPECT_EQ(profile.mcc, "001");
  EXPECT_EQ(profile.mnc, "01");
  EXPECT_EQ(profile.imsi.substr(0, 5), "00101");
  EXPECT_TRUE(RadioStateModel::ValidateProfile(profile, &error)) << error;
}

TEST(RadioStateModelTest, DefaultIdentityIsStableAndValid) {
  const RadioProfile first = RadioStateModel::CreateDefaultProfile(31);
  const RadioProfile second = RadioStateModel::CreateDefaultProfile(31);
  EXPECT_EQ(first.imei, second.imei);
  EXPECT_EQ(first.iccid, second.iccid);
  EXPECT_TRUE(RadioStateModel::ValidateProfile(first, nullptr));
  EXPECT_EQ(first.imsi.substr(0, 5), "00101");
}

TEST(RadioStateModelTest, InvalidProfileIsRejectedAsAUnit) {
  RadioStateModel model(37);
  const RadioProfile before = model.profile();
  RadioProfile invalid = before;
  invalid.mcc = "460";
  EXPECT_FALSE(model.SetProfile(invalid));
  EXPECT_EQ(model.profile().mcc, before.mcc);
  EXPECT_EQ(model.profile().imsi, before.imsi);
}

TEST(RadioStateModelTest, ExternalRegistrationExpiresToAutonomousMode) {
  RadioStateModel model(41);
  RegistrationControl control;
  control.voice = RegistrationState::kRoaming;
  control.data = RegistrationState::kRoaming;
  control.lease_duration_ms = 1'000;
  ASSERT_TRUE(model.SetRegistration(control, 1'000'000'000));
  EXPECT_TRUE(model.Advance(1'500'000'000).externally_controlled);

  const RadioSnapshot resumed = model.Advance(2'100'000'000);
  EXPECT_FALSE(resumed.externally_controlled);
  EXPECT_EQ(resumed.voice_registration, RegistrationState::kHome);
  EXPECT_EQ(resumed.data_registration, RegistrationState::kNotRegistered);
}

TEST(RadioStateModelTest, CallAndSmsEventsRemainInspectable) {
  RadioStateModel model(43);
  const uint64_t call_id = model.InjectIncomingCall("+80012345678");
  ASSERT_NE(call_id, 0U);
  EXPECT_TRUE(model.SetCallState(call_id, CallState::kActive));
  model.InjectSms("+80012345678", "hello", 2'000'000'000);
  const RadioSnapshot snapshot = model.Advance(3'000'000'000);
  ASSERT_EQ(snapshot.calls.size(), 1U);
  EXPECT_EQ(snapshot.calls.front().state, CallState::kActive);
  ASSERT_EQ(snapshot.sms_events.size(), 1U);
  EXPECT_TRUE(snapshot.sms_events.front().incoming);
}

TEST(RadioStateModelTest, FullSmsHistoryStillAcceptsANewEvent) {
  RadioStateModel model(47);
  for (size_t index = 0; index < 64; ++index) {
    ASSERT_NE(model.InjectSms("+80012345678", "hello", index + 1), 0U);
  }
  const uint64_t newest_sequence =
      model.InjectSms("+80012345678", "newest", 65);
  const RadioSnapshot snapshot = model.Advance(1'000'000'000);
  ASSERT_EQ(snapshot.sms_events.size(), 64U);
  EXPECT_EQ(snapshot.sms_events.front().sequence, 2U);
  EXPECT_EQ(snapshot.sms_events.back().sequence, newest_sequence);
}

TEST(GsmPduCodecTest, EncodesUnicodeSmsDeliverPdu) {
  std::string pdu;
  std::string error;
  ASSERT_TRUE(EncodeSmsDeliverPdu("+80012345678",
                                  "Floral \xE6\xB5\x8B\xE8\xAF\x95",
                                  1'700'000'000, &pdu, &error))
      << error;
  EXPECT_EQ(pdu.substr(0, 4), "0004");
  EXPECT_NE(pdu.find("0046006C006F00720061006C"), std::string::npos);
}

TEST(GsmPduCodecTest, RejectsBodyLargerThanOneUcs2Segment) {
  std::string pdu;
  std::string error;
  EXPECT_FALSE(EncodeSmsDeliverPdu("+80012345678", std::string(71, 'a'),
                                   1'700'000'000, &pdu, &error));
}

} // namespace
} // namespace floral::radio
