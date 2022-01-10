// Copyright (C) <2020> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include "StaticTaskQueueFactory.h"
#include "rtc_base/logging.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/to_queued_task.h"
#include "api/task_queue_base.h"
#include "api/default_task_queue_factory.h"

namespace rtc_adapter {

// TaskQueueDummy never execute tasks
class TaskQueueDummy final : public webrtc::TaskQueueBase {
public:
  TaskQueueDummy() {}
  ~TaskQueueDummy() override = default;

  // Implements webrtc::TaskQueueBase
  void Delete() override {}
  void PostTask(std::unique_ptr<webrtc::QueuedTask> task) override {}
  void PostDelayedTask(std::unique_ptr<webrtc::QueuedTask> task,
                       uint32_t milliseconds) override {}
};

// QueuedTaskProxy only execute when the owner shared_ptr exists
class QueuedTaskProxy : public webrtc::QueuedTask {
public:
  QueuedTaskProxy(std::unique_ptr<webrtc::QueuedTask> task, 
                  std::shared_ptr<int> owner)
      : m_task(std::move(task)), m_owner(owner) {
  }

  // Implements webrtc::QueuedTask
  bool Run() override {
    if (auto owner = m_owner.lock()) {
        // Only run when owner exists
        return m_task->Run();
    }
    return true;
  }
private:
  std::unique_ptr<webrtc::QueuedTask> m_task;
  std::weak_ptr<int> m_owner;
};

// TaskQueueProxy holds a TaskQueueBase* and proxy its method without Delete
class TaskQueueProxy : public webrtc::TaskQueueBase {
public:
  TaskQueueProxy(webrtc::TaskQueueBase* taskQueue)
      : m_taskQueue(taskQueue), 
        m_sp(std::make_shared<int>(1)) {
    RTC_CHECK(m_taskQueue);
  }
  
  ~TaskQueueProxy() override = default;

  // Implements webrtc::TaskQueueBase
  inline void Delete() override {
    m_sp.reset();
  }
  // Implements webrtc::TaskQueueBase
  inline void PostTask(std::unique_ptr<webrtc::QueuedTask> task) override {
    m_taskQueue->PostTask(
        std::make_unique<QueuedTaskProxy>(std::move(task), m_sp));
  }
  // Implements webrtc::TaskQueueBase
  inline void PostDelayedTask(std::unique_ptr<webrtc::QueuedTask> task,
                       uint32_t milliseconds) override {
    m_taskQueue->PostDelayedTask(
        std::make_unique<QueuedTaskProxy>(std::move(task), m_sp), milliseconds);
  }
  inline bool IsCurrent() const override {
    return m_taskQueue->IsCurrent();
  }
private:
  webrtc::TaskQueueBase* m_taskQueue;
  // Use shared_ptr to track its tasks
  std::shared_ptr<int> m_sp;
};

// Provide static TaskQueues
class DummyTaskQueueFactory final : public webrtc::TaskQueueFactory {
public:
  DummyTaskQueueFactory(webrtc::TaskQueueBase* p)
    : task_queue_base_(p) {
  }

  // Implements webrtc::TaskQueueFactory
  std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> 
  CreateTaskQueue(std::string_view name, 
                  webrtc::TaskQueueFactory::Priority) const override {
    if (name == std::string_view("CallTaskQueue") ||
        name == std::string_view("DecodingQueue") ||
        name == std::string_view("rtp_send_controller") ||
        name == std::string_view("deliver_frame")) {
      return std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>(
        new TaskQueueProxy(task_queue_base_));
    } else {
      assert(false);
      // Return dummy task queue for other names like "IncomingVideoStream"
      RTC_DLOG(LS_INFO) << "Dummy TaskQueue for " << name;
      return std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>(
        new TaskQueueDummy());
    }
  }
  
 private:
  webrtc::TaskQueueBase* task_queue_base_;
};

std::unique_ptr<webrtc::TaskQueueFactory> 
createDummyTaskQueueFactory(webrtc::TaskQueueBase* pQueue) {
  return std::unique_ptr<webrtc::TaskQueueFactory>(new DummyTaskQueueFactory(pQueue));
}

} // namespace rtc_adapter

