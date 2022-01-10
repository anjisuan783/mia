//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef RTC_ADAPTER_THREAD_PROCESS_THREAD_MOCK_
#define RTC_ADAPTER_THREAD_PROCESS_THREAD_MOCK_

#include <unordered_map>

#include "rtc_base/location.h"
#include "rtc_base/thread_checker.h"
#include "module/module.h"
#include "utility/process_thread.h"
#include "rtc_base/task_queue.h"

namespace rtc_adapter {

// ProcessThreadMock mock a ProcessThread on TaskQueue
class ProcessThreadMock : public webrtc::ProcessThread {
 public:
  ProcessThreadMock(rtc::TaskQueue*);

  // Implements ProcessThread
  void Start() override {}

  // Implements ProcessThread
  // Stop() has no effect on proxy
  void Stop() override {}

  // Implements ProcessThread
  void WakeUp(webrtc::Module* module) override;

  // Implements ProcessThread
  void PostTask(std::unique_ptr<webrtc::QueuedTask> task) override;

  // Implements ProcessThread
  void RegisterModule(webrtc::Module* module, const rtc::Location& from) override;

  // Implements ProcessThread
  void DeRegisterModule(webrtc::Module* module) override;

 private:
  void Process();
  void Process_now(webrtc::Module* module);
 
  struct ModuleCallback {
    ModuleCallback() = delete;
    ModuleCallback(ModuleCallback&& cb) = default;
    ModuleCallback(const ModuleCallback& cb) = default;
    ModuleCallback(webrtc::Module* module, const rtc::Location& location)
        : module(module), location(location) {}
    bool operator==(const ModuleCallback& cb) const {
      return cb.module == module;
    }

    webrtc::Module* const module;
    int64_t next_callback = 0;  // Absolute timestamp.
    const rtc::Location location;

   private:
    ModuleCallback& operator=(ModuleCallback&);
  };

  using ModuleList = std::unordered_map<webrtc::Module*, ModuleCallback>;
  ModuleList modules_;

  rtc::TaskQueue* const impl_ = nullptr;

  webrtc::SequenceChecker thread_checker_;
  bool running_ = false;
};

} // namespace rtc_adapter

#endif //RTC_ADAPTER_THREAD_PROCESS_THREAD_MOCK_

