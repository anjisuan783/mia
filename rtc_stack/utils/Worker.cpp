#include "./Worker.h"

#include <algorithm>
#include <memory>

#include "myrtc/api/default_task_queue_factory.h"
#include "myrtc/rtc_base/to_queued_task.h"

namespace wa {

bool ScheduledTaskReference::isCancelled() {
  return cancelled_;
}

void ScheduledTaskReference::cancel() {
  cancelled_ = true;
}

////////////////////////////////////////////////////////////////////////////////
//Worker
////////////////////////////////////////////////////////////////////////////////
Worker::Worker(webrtc::TaskQueueFactory* factory, int id, std::shared_ptr<Clock> the_clock) 
    : factory_(factory),
      id_(id),
      clock_{the_clock} 
{ }

void Worker::task(Task t) {
  task_queue_->PostTask(webrtc::ToQueuedTask(std::forward<Task>(t)));
}

void Worker::task(Task t, const rtc::Location& r) {
  task_queue_->PostTask(webrtc::ToQueuedTask(std::forward<Task>(t), r));
}

void Worker::start(const std::string& name) {
  auto promise = std::make_shared<std::promise<void>>();
  start(promise, name);
  promise->get_future().wait();
}

void Worker::start(std::shared_ptr<std::promise<void>> start_promise,
                   const std::string& name) {
  auto pQueue = factory_->CreateTaskQueue(name,
      webrtc::TaskQueueFactory::Priority::NORMAL);
  task_queue_base_ = pQueue.get();
  task_queue_ = std::move(std::make_unique<rtc::TaskQueue>(std::move(pQueue))); 


  task_queue_->PostTask([start_promise] {
    start_promise->set_value();
  });
}

void Worker::close() {
  closed_ = true;
  task_queue_ = nullptr;
  task_queue_base_ = nullptr;
}

std::shared_ptr<ScheduledTaskReference> 
    Worker::scheduleFromNow(Task t, duration delta) {
  rtc::Location l;
  return scheduleFromNow(std::forward<Task>(t), delta, l);
}

std::shared_ptr<ScheduledTaskReference> 
    Worker::scheduleFromNow(Task t, duration delta, const rtc::Location& l) {
  auto id = std::make_shared<ScheduledTaskReference>();
  task_queue_->PostDelayedTask(
      webrtc::ToQueuedTask(std::forward<std::function<void()>>(
          [f = std::forward<Task>(t), id]() {
            if (!id->isCancelled()) {
              f();
            }
          }), l),
      ClockUtils::durationToMs(delta));
  return id;
}

void Worker::scheduleEvery(ScheduledTask f, duration period) {
  rtc::Location l;
  scheduleEvery(std::forward<ScheduledTask>(f), period, period, l);
}

void Worker::scheduleEvery(ScheduledTask f,  
    duration period, const rtc::Location& l) {
  scheduleEvery(std::forward<ScheduledTask>(f), period, period, l);
}

void Worker::scheduleEvery(ScheduledTask&& t, 
    duration period, duration next_delay, const rtc::Location& location) {
  time_point start = clock_->now();

  scheduleFromNow([this_ptr = shared_from_this(), 
      start, period, next_delay, 
      f = std::forward<ScheduledTask>(t), clock = clock_, location]() {
    if (f()) {
      duration clock_skew = clock->now() - start - next_delay;
      duration delay = std::max(period - clock_skew, duration(0));
      this_ptr->scheduleEvery(
          std::forward<ScheduledTask>(const_cast<ScheduledTask&>(f)),
          period, delay, location);
    }
  }, next_delay, location);
}

void Worker::unschedule(std::shared_ptr<ScheduledTaskReference> id) {
  id->cancel();
}

////////////////////////////////////////////////////////////////////////////////
//ThreadPool
////////////////////////////////////////////////////////////////////////////////
static std::unique_ptr<webrtc::TaskQueueFactory> g_task_queue_factory = 
    webrtc::CreateDefaultTaskQueueFactory();

ThreadPool::ThreadPool(unsigned int num_workers) {
  workers_.reserve(num_workers);
  for (unsigned int index = 0; index < num_workers; ++index) {
    workers_.push_back( 
        std::make_shared<Worker>(g_task_queue_factory.get(), index));
  }
}

ThreadPool::~ThreadPool() {
  close();
}

std::shared_ptr<Worker> ThreadPool::getLessUsedWorker() {
  std::shared_ptr<Worker> chosen_worker = workers_.front();
  for (auto worker : workers_) {
    if (chosen_worker.use_count() > worker.use_count()) {
      chosen_worker = worker;
    }
  }
  return chosen_worker;
}

void ThreadPool::start(const std::string& name) {
  std::vector<std::shared_ptr<std::promise<void>>> promises(workers_.size());
  int index = 0;
  for (auto worker : workers_) {
    promises[index] = std::make_shared<std::promise<void>>();
    worker->start(promises[index++], name);
  }
  for (auto promise : promises) {
    promise->get_future().wait();
  }
}

void ThreadPool::close() {
  for (auto worker : workers_) {
    worker->close();
  }
}


} //namespace wa

