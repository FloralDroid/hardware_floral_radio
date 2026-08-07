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

#define LOG_TAG "floral-rild"

#include <android/binder_process.h>
#include <log/log.h>

#include <guest/hals/ril/reference-libril/ril.h>

extern "C" void RIL_register(const RIL_RadioFunctions *callbacks);
extern "C" void RIL_startEventLoop();
extern "C" void rilc_thread_pool();
extern "C" void RIL_onRequestComplete(RIL_Token token, RIL_Errno error,
                                      void *response, size_t response_length);
extern "C" void RIL_onRequestAck(RIL_Token token);
extern "C" void RIL_onUnsolicitedResponse(int response, const void *data,
                                          size_t data_length);
extern "C" void RIL_requestTimedCallback(RIL_TimedCallback callback,
                                         void *parameter,
                                         const timeval *relative_time);

namespace {

RIL_Env MakeEnvironment() {
  RIL_Env environment = {};
  environment.OnRequestComplete = RIL_onRequestComplete;
  environment.OnUnsolicitedResponse = RIL_onUnsolicitedResponse;
  environment.RequestTimedCallback = RIL_requestTimedCallback;
  environment.OnRequestAck = RIL_onRequestAck;
  return environment;
}

} // namespace

int main(int argc, char **argv) {
  const RIL_Env environment = MakeEnvironment();
  ABinderProcess_setThreadPoolMaxThreadCount(4);
  ABinderProcess_startThreadPool();
  RIL_startEventLoop();

  const RIL_RadioFunctions *functions = RIL_Init(&environment, argc, argv);
  if (functions == nullptr) {
    ALOGE("Floral RIL initialization failed");
    return 1;
  }
  RIL_register(functions);
  ALOGI("Floral RIL registered at version %d", functions->version);

  rilc_thread_pool();
  return 0;
}
