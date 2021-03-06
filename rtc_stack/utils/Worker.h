// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.

#ifndef __WA_SRC_THREAD_WORKER_H__
#define __WA_SRC_THREAD_WORKER_H__

#include <algorithm>
#include <memory>
#include <future>  // NOLINT
#include <vector>

#include "myrtc/rtc_base/location.h"
#include "myrtc/rtc_base/task_queue.h"
#include "myrtc/api/task_queue_factory.h"
#include "utils/Clock.h"

namespace wa {

class ScheduledTaskReference {
 public:
  ScheduledTaskReference() = default;
  bool isCancelled();
  void cancel();
 private:
  std::atomic<bool> cancelled_{false};
};

class Worker final : public std::enable_shared_from_this<Worker> {
 public:
  typedef std::function<void()> Task;
  typedef std::function<bool()> ScheduledTask;

  explicit Worker(webrtc::TaskQueueFactory*, int id = -1,
      std::shared_ptr<Clock> the_clock = std::make_shared<SteadyClock>());
  ~Worker() = default;

  void task(Task t);
  void task(Task t, const rtc::Location&);

  void start(const std::string& name);
  void start(std::shared_ptr<std::promise<void>> start_promise,
             const std::string& name);
  void close();

  std::shared_ptr<ScheduledTaskReference> 
      scheduleFromNow(Task f, duration delta);
  std::shared_ptr<ScheduledTaskReference> 
      scheduleFromNow(Task f, duration delta, const rtc::Location&);
      
  void scheduleEvery(ScheduledTask f, duration period);
  void scheduleEvery(ScheduledTask f, duration period, const rtc::Location&);
  void unschedule(std::shared_ptr<ScheduledTaskReference> id);
  
  webrtc::TaskQueueBase* getTaskQueue() {
    return task_queue_base_;
  }

  inline int id() {
    return id_;
  }

  inline bool IsCurrent() {
    return task_queue_base_->IsCurrent();
  }

 private:
  void scheduleEvery(ScheduledTask&& f, duration period, 
      duration next_delaym, const rtc::Location& l);

 protected:
  int next_scheduled_ = 0;

 private:
  webrtc::TaskQueueFactory* factory_;
  int id_{-1};
  std::shared_ptr<Clock> clock_;
  std::atomic<bool> closed_{false};
  std::unique_ptr<rtc::TaskQueue> task_queue_;
  webrtc::TaskQueueBase* task_queue_base_;
};

class ThreadPool {
 public:
  explicit ThreadPool(unsigned int num_workers);
  ~ThreadPool();

  std::shared_ptr<Worker> getLessUsedWorker();
  void start(const std::string& name);
  void close();

 private:
  std::vector<std::shared_ptr<Worker>> workers_;
};

}  // namespace wa

#endif  // __WA_SRC_THREAD_WORKER_H__