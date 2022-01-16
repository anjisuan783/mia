/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/task_queue_factory.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <unordered_set>
#include <memory>
#include <type_traits>
#include <utility>

#include <string_view>
#include "api/queued_task.h"
#include "api/task_queue_base.h"
#include "libevent/event.h"
#include "rtc_base/checks.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/logging.h"
#include "rtc_base/safe_conversions.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/platform_thread_types.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/object_pool.h"
#include "rtc_base/clock.h"

namespace webrtc {
namespace {
constexpr char kQuit = 1;
constexpr char kRunTask = 2;

using Priority = TaskQueueFactory::Priority;

// This ignores the SIGPIPE signal on the calling thread.
// This signal can be fired when trying to write() to a pipe that's being
// closed or while closing a pipe that's being written to.
// We can run into that situation so we ignore this signal and continue as
// normal.
// As a side note for this implementation, it would be great if we could safely
// restore the sigmask, but unfortunately the operation of restoring it, can
// itself actually cause SIGPIPE to be signaled :-| (e.g. on MacOS)
// The SIGPIPE signal by default causes the process to be terminated, so we
// don't want to risk that.
// An alternative to this approach is to ignore the signal for the whole
// process:
//   signal(SIGPIPE, SIG_IGN);
void IgnoreSigPipeSignalOnCurrentThread() {
  sigset_t sigpipe_mask;
  sigemptyset(&sigpipe_mask);
  sigaddset(&sigpipe_mask, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &sigpipe_mask, nullptr);
}

bool SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  RTC_CHECK(flags != -1);
  return (flags & O_NONBLOCK) || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// TODO(tommi): This is a hack to support two versions of libevent that we're
// compatible with.  The method we really want to call is event_assign(),
// since event_set() has been marked as deprecated (and doesn't accept
// passing event_base__ as a parameter).  However, the version of libevent
// that we have in Chromium, doesn't have event_assign(), so we need to call
// event_set() there.
void EventAssign(struct event* ev,
                 struct event_base* base,
                 int fd,
                 short events,
                 void (*callback)(int, short, void*),
                 void* arg) {
#if defined(_EVENT2_EVENT_H_)
  RTC_CHECK_EQ(0, event_assign(ev, base, fd, events, callback, arg));
#else
  event_set(ev, fd, events, callback, arg);
  RTC_CHECK_EQ(0, event_base_set(base, ev));
#endif
}

rtc::ThreadPriority TaskQueuePriorityToThreadPriority(Priority priority) {
  switch (priority) {
    case Priority::HIGH:
      return rtc::kRealtimePriority;
    case Priority::LOW:
      return rtc::kLowPriority;
    case Priority::NORMAL:
      return rtc::kNormalPriority;
    default:
      RTC_NOTREACHED();
      break;
  }
  return rtc::kNormalPriority;
}

//TaskQueueLibevent
class TaskQueueLibevent final : public TaskQueueBase {
 public:
  TaskQueueLibevent(std::string_view queue_name, rtc::ThreadPriority priority);

  void Delete() override;
  void PostTask(std::unique_ptr<QueuedTask> task) override;
  void PostDelayedTask(std::unique_ptr<QueuedTask> task,
                       uint32_t milliseconds) override;

 private:
  bool NotifyWakeup();
 
  class SetTimerTask : public QueuedTask {
   public:
    SetTimerTask(std::unique_ptr<QueuedTask> task, uint32_t milliseconds)
        : task_(std::move(task)),
          milliseconds_(milliseconds),
          posted_(rtc::Time32()) {}

   private:
    bool Run() override {
      // Compensate for the time that has passed since construction
      // and until we got here.
      uint32_t post_time = rtc::Time32() - posted_;
      TaskQueueLibevent::Current()->PostDelayedTask(
          std::move(task_),
          post_time > milliseconds_ ? 0 : milliseconds_ - post_time);
      return true;
    }

    std::unique_ptr<QueuedTask> task_;
    const uint32_t milliseconds_;
    const uint32_t posted_;
  };
  
  struct TimerEvent {
    TimerEvent(TaskQueueLibevent* task_queue, std::unique_ptr<QueuedTask> task)
        : task_queue_(task_queue), task_(std::move(task)) {}
    TimerEvent() { }
    ~TimerEvent() { event_del(&ev_); }

    void Init(TaskQueueLibevent* task_queue, std::unique_ptr<QueuedTask> task) {
      task_queue_ = task_queue;
      task_ = std::move(task);
    }
    
    event ev_;
    TaskQueueLibevent* task_queue_{nullptr};
    std::unique_ptr<QueuedTask> task_;
  };

  ~TaskQueueLibevent() override = default;

  static void ThreadMain(void* context);
  static void OnWakeup(int socket, short flags, void* context);  // NOLINT
  static void RunTimer(int fd, short flags, void* context);      // NOLINT

  bool is_active_ = true;
  int wakeup_pipe_in_ = -1;
  int wakeup_pipe_out_ = -1;
  event_base* event_base_;
  event wakeup_event_;
  rtc::PlatformThread thread_;
  rtc::CriticalSection pending_lock_;
  std::list<std::unique_ptr<QueuedTask>> pending_ RTC_GUARDED_BY(pending_lock_);
  // Holds a list of events pending timers for cleanup when the loop exits.
  std::unordered_set<TimerEvent*> pending_timers_;

  ObjectPoolT<TimerEvent> timer_event_pool_;

  Clock* clock_;
  Timestamp last_report_ts_;
};

TaskQueueLibevent::TaskQueueLibevent(std::string_view queue_name,
                                     rtc::ThreadPriority priority)
    : event_base_(event_base_new()),
      thread_(&TaskQueueLibevent::ThreadMain, this, queue_name, priority),
      timer_event_pool_(16),
      clock_(Clock::GetRealTimeClock()),
      last_report_ts_(clock_->CurrentTime()) {
  int fds[2];
  RTC_CHECK(pipe(fds) == 0);
  SetNonBlocking(fds[0]);
  SetNonBlocking(fds[1]);
  wakeup_pipe_out_ = fds[0];
  wakeup_pipe_in_ = fds[1];

  EventAssign(&wakeup_event_, event_base_, wakeup_pipe_out_,
              EV_READ | EV_PERSIST, OnWakeup, this);
  event_add(&wakeup_event_, nullptr);
  thread_.Start();
}

void TaskQueueLibevent::Delete() {
  RTC_DCHECK(!IsCurrent());
  struct timespec ts;
  char message = kQuit;
  while (write(wakeup_pipe_in_, &message, sizeof(message)) != sizeof(message)) {
    // The queue is full, so we have no choice but to wait and retry.
    RTC_CHECK_EQ(EAGAIN, errno);
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;
    nanosleep(&ts, nullptr);
  }

  thread_.Stop();

  event_del(&wakeup_event_);

  IgnoreSigPipeSignalOnCurrentThread();

  close(wakeup_pipe_in_);
  close(wakeup_pipe_out_);
  wakeup_pipe_in_ = -1;
  wakeup_pipe_out_ = -1;

  event_base_free(event_base_);
  delete this;
}

bool TaskQueueLibevent::NotifyWakeup() {
  char message = kRunTask;
  if (write(wakeup_pipe_in_, &message, sizeof(message)) != sizeof(message)) {
    RTC_LOG(WARNING) << "Failed to NotifyWakeup.";
    return false;
  }
  return true;
}

void TaskQueueLibevent::PostTask(std::unique_ptr<QueuedTask> task) {
  static const int kWarningAppendEventCount = 100;
  static const int kReportIntervalMs = 3000;
  QueuedTask* task_id = task.get();  // Only used for comparison.
  size_t pending_event = 0;
  {
    rtc::CritScope lock(&pending_lock_);
    pending_event = pending_.size();
    pending_.emplace_back(std::move(task));
  }

  Timestamp cur_ts = clock_->CurrentTime();
  if (last_report_ts_ + TimeDelta::ms(kReportIntervalMs) > cur_ts) {
    if (pending_event > kWarningAppendEventCount)
      RTC_LOG(WARNING) << "task queue PostTask "
        ", pending:" << pending_event << 
        ", worker name:" << thread_.name();
    last_report_ts_ = cur_ts;
  }

  if (pending_event > 0)
    return;
  
  if (!NotifyWakeup()) {
    rtc::CritScope lock(&pending_lock_);
    pending_.remove_if([task_id](std::unique_ptr<QueuedTask>& t) {
      return t.get() == task_id;
    });
  }
}

void TaskQueueLibevent::PostDelayedTask(std::unique_ptr<QueuedTask> task,
                                        uint32_t milliseconds) {
  if (IsCurrent()) {
    TimerEvent* timer = timer_event_pool_.New();
    timer->Init(this, std::move(task));
    EventAssign(&timer->ev_, event_base_, -1, 0, &TaskQueueLibevent::RunTimer,
                timer);
    pending_timers_.insert(timer);
    timeval tv = {rtc::dchecked_cast<int>(milliseconds / 1000),
                  rtc::dchecked_cast<int>(milliseconds % 1000) * 1000};
    event_add(&timer->ev_, &tv);
  } else {
    PostTask(std::make_unique<SetTimerTask>(std::move(task), milliseconds));
  }
}

// static
void TaskQueueLibevent::ThreadMain(void* context) {
  TaskQueueLibevent* me = static_cast<TaskQueueLibevent*>(context);
  me->last_report_ts_ = me->clock_->CurrentTime();
  {
    CurrentTaskQueueSetter set_current(me);
    while (me->is_active_)
      event_base_loop(me->event_base_, 0);
  }

  for (TimerEvent* timer : me->pending_timers_)
    me->timer_event_pool_.Delete(timer);
}

// static
void TaskQueueLibevent::OnWakeup(int socket,
                                 short flags,  // NOLINT
                                 void* context) {
  static int const kMaxExecuteMs = 20;
  TaskQueueLibevent* me = static_cast<TaskQueueLibevent*>(context);
  RTC_DCHECK(me->wakeup_pipe_out_ == socket);
  char buf;
  RTC_CHECK(sizeof(buf) == read(socket, &buf, sizeof(buf)));
  if (kRunTask == buf) {
    static constexpr size_t max_events_once = 3;
    std::list<std::unique_ptr<QueuedTask>> task_list;
    size_t remain = 0;
    {
      rtc::CritScope lock(&me->pending_lock_);
      size_t total_event = me->pending_.size();
      RTC_CHECK(total_event > 0);
      if (total_event <= max_events_once) {
        std::swap(me->pending_, task_list);
      } else {
        for (size_t i = 0; i < max_events_once; ++i) {
          task_list.emplace_back(std::move(me->pending_.front()));
          me->pending_.pop_front();
        }
      }
      remain = me->pending_.size();
    }

    Timestamp before_ts = me->clock_->CurrentTime();
    for (auto& task : task_list) {
      if (!task->Run())
        task.release();
    }
    TimeDelta cost_ts = me->clock_->CurrentTime() - before_ts;
    if (cost_ts.ms() > kMaxExecuteMs) {
      RTC_LOG(WARNING) << "process event too long time,"
        ", cost:" << cost_ts.ms() << 
        ", worker name:" << me->thread_.name();
    }
    
    if (remain > 0) {
      me->NotifyWakeup();
    }
  } else if (kQuit == buf) {
    me->is_active_ = false;
    event_base_loopbreak(me->event_base_);
  } else {
    RTC_NOTREACHED();
  }
}

// static
void TaskQueueLibevent::RunTimer(int, short, void* context) {
  TimerEvent* timer = static_cast<TimerEvent*>(context);
  if (!timer->task_->Run())
    timer->task_.release();
  timer->task_queue_->pending_timers_.erase(timer);
  timer->task_queue_->timer_event_pool_.Delete(timer);
}

class TaskQueueLibeventFactory final : public TaskQueueFactory {
 public:
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> 
    CreateTaskQueue(std::string_view name, Priority priority) const override {
      return std::unique_ptr<TaskQueueBase, TaskQueueDeleter>(
        new TaskQueueLibevent(name, TaskQueuePriorityToThreadPriority(priority)));
  }
};

}  // namespace

std::unique_ptr<TaskQueueFactory> CreateDefaultTaskQueueFactory() {
  return std::make_unique<TaskQueueLibeventFactory>();
}


}  // namespace webrtc
