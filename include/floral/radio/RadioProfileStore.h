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

class RadioProfileStore final {
public:
  explicit RadioProfileStore(std::string persistent_path);

  bool LoadFile(const std::string &path, RadioProfile *profile,
                std::string *error) const;
  bool LoadPersistent(RadioProfile *profile, std::string *error) const;
  bool SavePersistent(const RadioProfile &profile, std::string *error) const;

private:
  std::string persistent_path_;
};

} // namespace floral::radio
