/*
 *  Copyright 2019 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_TASK_UTILS_TO_QUEUED_TASK_H_
#define RTC_BASE_TASK_UTILS_TO_QUEUED_TASK_H_

#include <memory>
#include <type_traits>
#include <utility>

#include "api/queued_task.h"

namespace webrtc {
namespace webrtc_new_closure_impl {
// Simple implementation of QueuedTask for use with rtc::Bind and lambdas.
template <typename Closure>
class ClosureTask : public QueuedTask {
 public:
  explicit ClosureTask(Closure&& closure)
      : closure_(std::forward<Closure>(closure)) {}

  explicit ClosureTask(Closure&& closure, const rtc::Location& location)
      : closure_(std::forward<Closure>(closure)) {
    location_ = location;
  }
 private:
  bool Run() override {
    closure_();
    return true;
  }

  typename std::decay<Closure>::type closure_;
};

// Extends ClosureTask to also allow specifying cleanup code.
// This is useful when using lambdas if guaranteeing cleanup, even if a task
// was dropped (queue is too full), is required.
template <typename Closure, typename Cleanup>
class ClosureTaskWithCleanup : public ClosureTask<Closure> {
 public:
  ClosureTaskWithCleanup(Closure&& closure, Cleanup&& cleanup)
      : ClosureTask<Closure>(std::forward<Closure>(closure)),
        cleanup_(std::forward<Cleanup>(cleanup)) {}
  ~ClosureTaskWithCleanup() override { cleanup_(); }

 private:
  typename std::decay<Cleanup>::type cleanup_;
};
}  // namespace webrtc_new_closure_impl

// Convenience function to construct closures that can be passed directly
// to methods that support std::unique_ptr<QueuedTask> but not template
// based parameters.
template <typename Closure>
std::unique_ptr<QueuedTask> ToQueuedTask(Closure&& closure) {
  return std::make_unique<webrtc_new_closure_impl::ClosureTask<Closure>>(
      std::forward<Closure>(closure));
}

template <typename Closure>
std::unique_ptr<QueuedTask> ToQueuedTask(Closure&& closure, 
    const rtc::Location& l) {
  return std::make_unique<webrtc_new_closure_impl::ClosureTask<Closure>>(
      std::forward<Closure>(closure), l);
}

template <typename Closure, typename Cleanup>
std::unique_ptr<QueuedTask> ToQueuedTask(Closure&& closure, Cleanup&& cleanup) {
  return std::make_unique<
      webrtc_new_closure_impl::ClosureTaskWithCleanup<Closure, Cleanup>>(
      std::forward<Closure>(closure), std::forward<Cleanup>(cleanup));
}

}  // namespace webrtc

#endif  // RTC_BASE_TASK_UTILS_TO_QUEUED_TASK_H_
