#include "rtc_adapter/thread/ProcessThreadMock.h"

#include <string>
#include "rtc_base/checks.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/logging.h"

namespace rtc_adapter {

namespace {

// We use this constant internally to signal that a module has requested
// a callback right away.  When this is set, no call to TimeUntilNextProcess
// should be made, but Process() should be called directly.
const int64_t kCallProcessImmediately = -1;

int64_t GetNextCallbackTime(webrtc::Module* module, int64_t time_now) {
  int64_t interval = module->TimeUntilNextProcess();
  if (interval < 0) {
    // Falling behind, we should call the callback now.
    return time_now;
  }
  return time_now + interval;
}
}  // namespace


ProcessThreadMock::ProcessThreadMock(rtc::TaskQueue* task_queue)
  : impl_{task_queue} {
}

// Implements ProcessThread
void ProcessThreadMock::WakeUp(webrtc::Module* module) {
  RTC_DCHECK(thread_checker_.IsCurrent());

  auto found = modules_.find(module);
  if (found == modules_.end()) {
    RTC_DLOG(LS_ERROR) << "WakeUp module not found " << module;
    return;
  }

  found->second.next_callback = kCallProcessImmediately;
  impl_->PostTask([this, module]() { this->Process_now(module); });
}

// Implements ProcessThread
void ProcessThreadMock::PostTask(std::unique_ptr<webrtc::QueuedTask> task) {
  RTC_DCHECK(thread_checker_.IsCurrent());
  impl_->PostTask(std::move(task));
}

// Implements ProcessThread
void ProcessThreadMock::RegisterModule(webrtc::Module* module, 
                                       const rtc::Location& from) {
  RTC_DCHECK(module) << from.ToString();
  RTC_DCHECK(thread_checker_.IsCurrent());

  auto insert_result = modules_.emplace(module, ModuleCallback(module, from));
  RTC_DCHECK(insert_result.second)
        << "Already registered here: " << 
           insert_result.first->second.location.ToString() << "\n"
        << "Now attempting from here: " << from.ToString();
  
  module->ProcessThreadAttached(this);

  if (!running_) {
    impl_->PostTask([this]() { this->Process(); });
    running_ = true;
  }
}

// Implements ProcessThread
void ProcessThreadMock::DeRegisterModule(webrtc::Module* module) {
  RTC_DCHECK(module);
  RTC_DCHECK(thread_checker_.IsCurrent());
  
  // Notify the module that it's been detached.
  module->ProcessThreadAttached(nullptr);

  modules_.erase(module);
}

void ProcessThreadMock::Process_now(webrtc::Module* module) {
  // DeRegisterModule after WakeUp won't call module::Process
  auto found = modules_.find(module);
  if (found == modules_.end()) {
    return;
  }

  // Module had been run in Process() maybe.
  if (found->second.next_callback == kCallProcessImmediately) {
    module->Process();
    found->second.next_callback = GetNextCallbackTime(module, rtc::TimeMillis());
  }
}

void ProcessThreadMock::Process() {
  if (modules_.empty()) {
    running_ = false;
    return;
  }

  int64_t now = rtc::TimeMillis();
  int64_t next_checkpoint = now + (rtc::kNumMillisecsPerSec * 60);

  for (auto& i : modules_) {
      ModuleCallback& m = i.second;
    // TODO(tommi): Would be good to measure the time TimeUntilNextProcess
    // takes and dcheck if it takes too long (e.g. >=10ms).  Ideally this
    // operation should not require taking a lock, so querying all modules
    // should run in a matter of nanoseconds.
    if (m.next_callback == 0)
      m.next_callback = GetNextCallbackTime(m.module, now);

    if (m.next_callback <= now ||
        m.next_callback == kCallProcessImmediately) {
      m.module->Process();
      // Use a new 'now' reference to calculate when the next callback
      // should occur.  We'll continue to use 'now' above for the baseline
      // of calculating how long we should wait, to reduce variance.
      int64_t new_now = rtc::TimeMillis();
      m.next_callback = GetNextCallbackTime(m.module, new_now);
    }

    if (m.next_callback < next_checkpoint) {
      next_checkpoint = m.next_callback;
    }
  }

  int64_t time_to_wait = next_checkpoint - rtc::TimeMillis();
  if (time_to_wait > 0) {
    impl_->PostDelayedTask([this]() {
      this->Process();
    }, time_to_wait);
  } else {
    impl_->PostTask([this]() { this->Process(); });
  }
}

} // namespace rtc_adapter

