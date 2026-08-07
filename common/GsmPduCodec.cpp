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

#include <algorithm>
#include <array>
#include <ctime>
#include <vector>

namespace floral::radio {
namespace {

bool SetError(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

void AppendHexByte(uint8_t value, std::string *output) {
  constexpr char kHex[] = "0123456789ABCDEF";
  output->push_back(kHex[value >> 4]);
  output->push_back(kHex[value & 0x0f]);
}

uint8_t SwappedBcd(int value) {
  return static_cast<uint8_t>(((value % 10) << 4) | ((value / 10) % 10));
}

bool DecodeUtf8(const std::string &input, std::vector<uint16_t> *output) {
  for (size_t offset = 0; offset < input.size();) {
    const uint8_t first = static_cast<uint8_t>(input[offset]);
    uint32_t code_point = 0;
    size_t length = 0;
    if (first < 0x80) {
      code_point = first;
      length = 1;
    } else if ((first & 0xe0) == 0xc0) {
      code_point = first & 0x1f;
      length = 2;
    } else if ((first & 0xf0) == 0xe0) {
      code_point = first & 0x0f;
      length = 3;
    } else if ((first & 0xf8) == 0xf0) {
      code_point = first & 0x07;
      length = 4;
    } else {
      return false;
    }
    if (offset + length > input.size()) {
      return false;
    }
    for (size_t index = 1; index < length; ++index) {
      const uint8_t continuation = static_cast<uint8_t>(input[offset + index]);
      if ((continuation & 0xc0) != 0x80) {
        return false;
      }
      code_point = (code_point << 6) | (continuation & 0x3f);
    }
    if ((length == 2 && code_point < 0x80) ||
        (length == 3 && code_point < 0x800) ||
        (length == 4 && code_point < 0x10000) || code_point > 0x10ffff ||
        (code_point >= 0xd800 && code_point <= 0xdfff)) {
      return false;
    }
    if (code_point <= 0xffff) {
      output->push_back(static_cast<uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      output->push_back(static_cast<uint16_t>(0xd800 | (code_point >> 10)));
      output->push_back(static_cast<uint16_t>(0xdc00 | (code_point & 0x3ff)));
    }
    offset += length;
  }
  return true;
}

} // namespace

bool EncodeSmsDeliverPdu(const std::string &address, const std::string &body,
                         int64_t unix_timestamp_seconds, std::string *pdu,
                         std::string *error) {
  if (pdu == nullptr || body.empty()) {
    return SetError(error, "SMS PDU output or body is invalid");
  }
  const bool international = !address.empty() && address.front() == '+';
  const std::string digits = international ? address.substr(1) : address;
  if (digits.empty() || digits.size() > 20 ||
      !std::all_of(digits.begin(), digits.end(), [](char character) {
        return character >= '0' && character <= '9';
      })) {
    return SetError(error, "SMS address is invalid");
  }
  std::vector<uint16_t> text;
  if (!DecodeUtf8(body, &text) || text.empty() || text.size() > 70) {
    return SetError(error,
                    "SMS body is not valid UTF-8 or exceeds one UCS-2 segment");
  }

  std::time_t raw_time = static_cast<std::time_t>(unix_timestamp_seconds);
  std::tm utc = {};
  if (gmtime_r(&raw_time, &utc) == nullptr) {
    return SetError(error, "SMS timestamp is invalid");
  }

  pdu->clear();
  pdu->reserve(64 + text.size() * 4);
  AppendHexByte(0, pdu);    // Use the default SMSC.
  AppendHexByte(0x04, pdu); // SMS-DELIVER without a user-data header.
  AppendHexByte(static_cast<uint8_t>(digits.size()), pdu);
  AppendHexByte(international ? 0x91 : 0x81, pdu);
  for (size_t index = 0; index < digits.size(); index += 2) {
    const uint8_t low = static_cast<uint8_t>(digits[index] - '0');
    const uint8_t high = index + 1 < digits.size()
                             ? static_cast<uint8_t>(digits[index + 1] - '0')
                             : 0x0f;
    AppendHexByte(static_cast<uint8_t>((high << 4) | low), pdu);
  }
  AppendHexByte(0, pdu);    // Protocol identifier.
  AppendHexByte(0x08, pdu); // UCS-2 data coding scheme.
  const std::array<int, 7> timestamp = {(utc.tm_year + 1900) % 100,
                                        utc.tm_mon + 1,
                                        utc.tm_mday,
                                        utc.tm_hour,
                                        utc.tm_min,
                                        utc.tm_sec,
                                        0};
  for (int value : timestamp) {
    AppendHexByte(SwappedBcd(value), pdu);
  }
  AppendHexByte(static_cast<uint8_t>(text.size() * 2), pdu);
  for (uint16_t code_unit : text) {
    AppendHexByte(static_cast<uint8_t>(code_unit >> 8), pdu);
    AppendHexByte(static_cast<uint8_t>(code_unit), pdu);
  }
  return true;
}

} // namespace floral::radio
