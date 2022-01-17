#include "./Worker.h"

#include <algorithm>
#include <memory>

#include "myrtc/api/default_task_queue_factory.h"

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
Worker::Worker(webrtc::TaskQueueFactory* factory, std::shared_ptr<Clock> the_clock) 
    : factory_(factory),
      clock_{the_clock} { 
}

void Worker::task(Task f) {
  task_queue_->PostTask(f);
}

void Worker::start(const std::string& name) {
  auto promise = std::make_shared<std::promise<void>>();
  start(promise, name);
  promise->get_future().wait();
}

void Worker::start(std::shared_ptr<std::promise<void>> start_promise,
                   const std::string& name) {
  auto pQueue = factory_->CreateTaskQueue(name, webrtc::TaskQueueFactory::Priority::NORMAL);
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

std::shared_ptr<ScheduledTaskReference> Worker::scheduleFromNow(Task f, duration delta) {
  auto delta_ms = ClockUtils::durationToMs(delta);
  auto id = std::make_shared<ScheduledTaskReference>();

  task_queue_->PostDelayedTask([f, id]() {
    if (!id->isCancelled()) {
      f();
    }
  }, delta_ms);
  return id;
}

void Worker::scheduleEvery(ScheduledTask f, duration period) {
  scheduleEvery(f, period, period);
}

void Worker::scheduleEvery(ScheduledTask f, duration period, duration next_delay) {
  time_point start = clock_->now();
  std::shared_ptr<Clock> clock = clock_;

  auto this_ptr = shared_from_this();
  scheduleFromNow([this_ptr, start, period, next_delay, f, clock]() {
    if (f()) {
      duration clock_skew = clock->now() - start - next_delay;
      duration delay = std::max(period - clock_skew, duration(0));
      this_ptr->scheduleEvery(f, period, delay);
    }
  }, next_delay);
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
  for (unsigned int index = 0; index < num_workers; ++index) {
    workers_.push_back(std::make_shared<Worker>(g_task_queue_factory.get()));
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

