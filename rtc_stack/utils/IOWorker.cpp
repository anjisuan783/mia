#include "./IOWorker.h"

#include <sys/prctl.h>


namespace wa {

static const char* const thraed_name = "nice";

IOWorker::IOWorker() = default;

IOWorker::~IOWorker() {
  close();
}

void IOWorker::start() {
  auto promise = std::make_shared<std::promise<void>>();
  start(promise);
}

void IOWorker::start(std::shared_ptr<std::promise<void>> start_promise) {
  if (started_.exchange(true)) {
    return;
  }

  if (!context_ && !loop_) {
    context_ = g_main_context_new();
    loop_ = g_main_loop_new(context_, FALSE);
    thread_ = std::unique_ptr<std::thread>(new std::thread([this, start_promise] {
      prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(thraed_name));
      start_promise->set_value();
      if (!this->closed_ && this->loop_) {
        g_main_loop_run(this->loop_);
      }
    }));
  }
}

void IOWorker::close() {
  if (!closed_.exchange(true)) {
    if (context_ && loop_) {
      g_main_loop_quit(loop_);
      g_main_loop_unref(loop_);
      g_main_context_unref(context_);
      loop_ = NULL;
      context_ = NULL;
    }
    if (thread_ != nullptr) {
      thread_->join();
      thread_ = nullptr;
    }
  }
}

IOThreadPool::IOThreadPool(unsigned int num_io_workers)
    : io_workers_{} {
  for (unsigned int index = 0; index < num_io_workers; index++) {
    io_workers_.push_back(std::make_shared<IOWorker>());
  }
}

IOThreadPool::~IOThreadPool() {
  close();
}

std::shared_ptr<IOWorker> IOThreadPool::getLessUsedIOWorker() {
  std::shared_ptr<IOWorker> chosen_io_worker = io_workers_.front();
  for (auto io_worker : io_workers_) {
    if (chosen_io_worker.use_count() > io_worker.use_count()) {
      chosen_io_worker = io_worker;
    }
  }
  return chosen_io_worker;
}

void IOThreadPool::start() {
  std::vector<std::shared_ptr<std::promise<void>>> promises(io_workers_.size());
  int index = 0;
  for (auto io_worker : io_workers_) {
    promises[index] = std::make_shared<std::promise<void>>();
    io_worker->start(promises[index++]);
  }
  for (auto promise : promises) {
    promise->get_future().wait();
  }
}

void IOThreadPool::close() {
  for (auto io_worker : io_workers_) {
    io_worker->close();
  }
}

} //namespace wa

