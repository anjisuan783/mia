// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef __WA_SRC_ERIZO_THREAD_IOWORKER_H__
#define __WA_SRC_ERIZO_THREAD_IOWORKER_H__

#include <atomic>
#include <memory>
#include <future>  // NOLINT
#include <thread>  // NOLINT
#include <vector>
#include <glib.h>

namespace wa {

class IOWorker : public std::enable_shared_from_this<IOWorker> {
 public:
  IOWorker();
  ~IOWorker();

  virtual void start();
  virtual void start(std::shared_ptr<std::promise<void>> start_promise);
  virtual void close();

  GMainContext* getMainContext() { return context_; }
  GMainLoop* getMainLoop() { return loop_; }

 private:
  std::atomic<bool> started_{false};
  std::atomic<bool> closed_{false};
  std::unique_ptr<std::thread> thread_;

  GMainContext* context_{nullptr};
  GMainLoop* loop_{nullptr};
};

class IOThreadPool {
 public:
  explicit IOThreadPool(unsigned int num_workers);
  ~IOThreadPool();

  std::shared_ptr<IOWorker> getLessUsedIOWorker();
  std::shared_ptr<IOWorker> getIOWorker(int);
  void start();
  void close();

 private:
  std::vector<std::shared_ptr<IOWorker>> io_workers_;
};

}  // namespace wa

#endif  // __WA_SRC_ERIZO_THREAD_IOWORKER_H__