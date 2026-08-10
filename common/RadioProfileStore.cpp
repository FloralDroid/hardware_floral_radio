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

#include "floral/radio/RadioProfileStore.h"

#include "floral/radio/RadioStateModel.h"

#include <android-base/file.h>
#include <json/json.h>

#include <sstream>
#include <utility>

namespace floral::radio {
namespace {

bool SetError(std::string *error, const std::string &message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool ReadString(const Json::Value &root, const char *name,
                std::string *output) {
  if (!root.isMember(name) || !root[name].isString()) {
    return false;
  }
  *output = root[name].asString();
  return true;
}

bool ParseProfile(const std::string &content, RadioProfile *profile,
                  std::string *error) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string parse_errors;
  std::istringstream stream(content);
  if (!Json::parseFromStream(builder, stream, &root, &parse_errors) ||
      !root.isObject()) {
    return SetError(error, "radio profile JSON is invalid: " + parse_errors);
  }
  RadioProfile parsed;
  if (!root.isMember("version") || !root["version"].isUInt() ||
      !ReadString(root, "operatorLongName", &parsed.operator_long_name) ||
      !ReadString(root, "operatorShortName", &parsed.operator_short_name) ||
      !ReadString(root, "mcc", &parsed.mcc) ||
      !ReadString(root, "mnc", &parsed.mnc) ||
      !ReadString(root, "imei", &parsed.imei) ||
      !ReadString(root, "imeisv", &parsed.imeisv) ||
      !ReadString(root, "imsi", &parsed.imsi) ||
      !ReadString(root, "iccid", &parsed.iccid) ||
      !ReadString(root, "msisdn", &parsed.msisdn) ||
      !ReadString(root, "basebandVersion", &parsed.baseband_version) ||
      !ReadString(root, "simPin", &parsed.sim_pin)) {
    return SetError(error, "radio profile is missing a required field");
  }
  parsed.version = root["version"].asUInt();
  if (!RadioStateModel::ValidateProfile(parsed, error)) {
    return false;
  }
  *profile = std::move(parsed);
  return true;
}

} // namespace

bool RadioProfileStore::LoadFile(const std::string &path, RadioProfile *profile,
                                 std::string *error) {
  if (profile == nullptr) {
    return SetError(error, "radio profile output is null");
  }
  std::string content;
  if (!android::base::ReadFileToString(path, &content)) {
    return SetError(error, "radio profile is not available at " + path);
  }
  return ParseProfile(content, profile, error);
}

} // namespace floral::radio
