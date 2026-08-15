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

#include "floral/radio/SimFileSystem.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace floral::radio {
namespace {

constexpr int kSimStatusNormalEnding1 = 0x90;
constexpr int kSimStatusNormalEnding2 = 0x00;
constexpr int kGetResponseSize = 15;
constexpr int kMsisdnRecordSize = 32;
constexpr int kAdnFooterSize = 14;
constexpr int kReadRecordModeAbsolute = 4;

bool IsValidMsisdn(const std::string &msisdn) {
  if (msisdn.empty() || msisdn.size() > 20) {
    return false;
  }
  const size_t start = msisdn.front() == '+' ? 1 : 0;
  if (start == msisdn.size()) {
    return false;
  }
  for (size_t index = start; index < msisdn.size(); ++index) {
    if (msisdn[index] < '0' || msisdn[index] > '9') {
      return false;
    }
  }
  return true;
}

std::string BytesToHex(const uint8_t *data, size_t size) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string result(size * 2, '0');
  for (size_t index = 0; index < size; ++index) {
    result[index * 2] = kHexDigits[data[index] >> 4];
    result[index * 2 + 1] = kHexDigits[data[index] & 0x0f];
  }
  return result;
}

template <size_t Size>
std::string BytesToHex(const std::array<uint8_t, Size> &data) {
  return BytesToHex(data.data(), data.size());
}

std::string BytesToHex(const std::vector<uint8_t> &data) {
  return BytesToHex(data.data(), data.size());
}

SimFileIoResult Success(std::string response) {
  return {
      .status = SimFileIoStatus::kSuccess,
      .sw1 = kSimStatusNormalEnding1,
      .sw2 = kSimStatusNormalEnding2,
      .response = std::move(response),
  };
}

SimFileIoResult Failure(SimFileIoStatus status) {
  return {
      .status = status,
      .sw1 = 0,
      .sw2 = 0,
      .response = {},
  };
}

std::string BuildMsisdnFileResponse() {
  std::array<uint8_t, kGetResponseSize> response = {};
  response[3] = kMsisdnRecordSize;
  response[4] = static_cast<uint8_t>(kSimEfMsisdn >> 8);
  response[5] = static_cast<uint8_t>(kSimEfMsisdn);
  response[6] = 4; // Elementary file.
  response[11] = 1;
  response[12] = 2;
  response[13] = 1; // Linear fixed structure.
  response[14] = kMsisdnRecordSize;
  return BytesToHex(response);
}

std::string BuildMsisdnRecord(const std::string &msisdn) {
  std::vector<uint8_t> response(kMsisdnRecordSize, 0xff);
  const bool international = msisdn.front() == '+';
  const std::string_view digits(msisdn.data() + (international ? 1 : 0),
                                msisdn.size() - (international ? 1 : 0));
  const size_t footer_offset = response.size() - kAdnFooterSize;
  const size_t bcd_size = (digits.size() + 1) / 2;

  // Android's AdnRecord expects the length to include the TON/NPI byte.
  response[footer_offset] = static_cast<uint8_t>(bcd_size + 1);
  response[footer_offset + 1] = international ? 0x91 : 0x81;
  for (size_t index = 0; index < digits.size(); ++index) {
    const uint8_t digit = static_cast<uint8_t>(digits[index] - '0');
    uint8_t &encoded = response[footer_offset + 2 + index / 2];
    if ((index & 1) == 0) {
      encoded = static_cast<uint8_t>((encoded & 0xf0) | digit);
    } else {
      encoded = static_cast<uint8_t>((encoded & 0x0f) | (digit << 4));
    }
  }
  return BytesToHex(response);
}

} // namespace

SimFileIoResult ProcessSimFileIo(const RadioProfile &profile, int command,
                                 int file_id, int p1, int p2, int p3) {
  if (file_id != kSimEfMsisdn) {
    return Failure(SimFileIoStatus::kNotSupported);
  }
  if (command == kSimCommandGetResponse) {
    if (p1 != 0 || p2 != 0 || p3 != kGetResponseSize) {
      return Failure(SimFileIoStatus::kInvalidArguments);
    }
    return Success(BuildMsisdnFileResponse());
  }
  if (command == kSimCommandReadRecord) {
    if (p1 != 1 || p2 != kReadRecordModeAbsolute ||
        p3 != kMsisdnRecordSize || !IsValidMsisdn(profile.msisdn)) {
      return Failure(SimFileIoStatus::kInvalidArguments);
    }
    return Success(BuildMsisdnRecord(profile.msisdn));
  }
  return Failure(SimFileIoStatus::kNotSupported);
}

} // namespace floral::radio
