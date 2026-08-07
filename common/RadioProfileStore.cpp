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

#include <cerrno>
#include <cstring>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

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

bool EnsureParentDirectories(std::string *error) {
  if (mkdir("/data/vendor/floral", 0770) != 0 && errno != EEXIST) {
    return SetError(error, "cannot create /data/vendor/floral: " +
                               std::string(strerror(errno)));
  }
  if (mkdir("/data/vendor/floral/radio", 0770) != 0 && errno != EEXIST) {
    return SetError(error, "cannot create /data/vendor/floral/radio: " +
                               std::string(strerror(errno)));
  }
  return true;
}

} // namespace

RadioProfileStore::RadioProfileStore(std::string persistent_path)
    : persistent_path_(std::move(persistent_path)) {}

bool RadioProfileStore::LoadFile(const std::string &path, RadioProfile *profile,
                                 std::string *error) const {
  if (profile == nullptr) {
    return SetError(error, "radio profile output is null");
  }
  std::string content;
  if (!android::base::ReadFileToString(path, &content)) {
    return SetError(error, "radio profile is not available at " + path);
  }
  return ParseProfile(content, profile, error);
}

bool RadioProfileStore::LoadPersistent(RadioProfile *profile,
                                       std::string *error) const {
  return LoadFile(persistent_path_, profile, error);
}

bool RadioProfileStore::SavePersistent(const RadioProfile &profile,
                                       std::string *error) const {
  if (!RadioStateModel::ValidateProfile(profile, error) ||
      !EnsureParentDirectories(error)) {
    return false;
  }
  Json::Value root(Json::objectValue);
  root["version"] = profile.version;
  root["operatorLongName"] = profile.operator_long_name;
  root["operatorShortName"] = profile.operator_short_name;
  root["mcc"] = profile.mcc;
  root["mnc"] = profile.mnc;
  root["imei"] = profile.imei;
  root["imeisv"] = profile.imeisv;
  root["imsi"] = profile.imsi;
  root["iccid"] = profile.iccid;
  root["msisdn"] = profile.msisdn;
  root["basebandVersion"] = profile.baseband_version;
  root["simPin"] = profile.sim_pin;
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string content = Json::writeString(builder, root) + "\n";
  const std::string temporary_path = persistent_path_ + ".tmp";
  if (!android::base::WriteStringToFile(content, temporary_path, 0600, getuid(),
                                        getgid(), false)) {
    return SetError(error, "cannot write temporary radio profile");
  }
  if (rename(temporary_path.c_str(), persistent_path_.c_str()) != 0) {
    return SetError(error, "cannot replace persistent radio profile: " +
                               std::string(strerror(errno)));
  }
  return true;
}

} // namespace floral::radio
