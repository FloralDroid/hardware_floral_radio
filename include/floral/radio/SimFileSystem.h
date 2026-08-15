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

#include "floral/radio/RadioTypes.h"

#include <string>

namespace floral::radio {

inline constexpr int kSimCommandReadRecord = 0xb2;
inline constexpr int kSimCommandGetResponse = 0xc0;
inline constexpr int kSimEfMsisdn = 0x6f40;

enum class SimFileIoStatus {
  kSuccess,
  kInvalidArguments,
  kNotSupported,
};

struct SimFileIoResult {
  SimFileIoStatus status = SimFileIoStatus::kNotSupported;
  int sw1 = 0;
  int sw2 = 0;
  std::string response;
};

SimFileIoResult ProcessSimFileIo(const RadioProfile &profile, int command,
                                 int file_id, int p1, int p2, int p3);

} // namespace floral::radio
