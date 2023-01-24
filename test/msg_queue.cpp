#include <iostream>

#include "utils/media_msg_queue.h"
#include "utils/media_thread.h"
#include "common/media_kernel_error.h"
#include "h/media_return_code.h"
#include "h/media_server_api.h"
#include "utils/media_timer_helper.h"
#include <log4cxx/logger.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>

using namespace ma;

class TestMsg : public MediaTimerHelpSink {
 public:
  srs_error_t TestMainThread() {
    srs_error_t err = srs_success;
    std::cout << "TestMainThread 1" << std::endl;
    auto work = MediaThreadManager::Instance()->CurrentThread();
    err = work->MsgQueue()->Post([]()->srs_error_t {
      std::cout << "TestMainThread 2" << std::endl;
      return srs_error_new(ERROR_SUCCESS, "success, ok.");
    });
  
    if (err != srs_success) {
      std::cout << " error occur, desc:" << srs_error_desc(err) << std::endl;
    }
    std::cout << "TestMainThread 3" << std::endl;

    if (srs_success != (err = timer_once_.Schedule(this, 10.0, 1))) {
      std::cout << "schedule timer once in main failed desc:" << srs_error_desc(err) << std::endl;
      delete err;
    }

    if (srs_success != (err = timer_infinity_.Schedule(this, 2.0))) {
      std::cout << "schedule timer infinity in main failed desc:" << srs_error_desc(err) << std::endl;
      delete err;
    }
  
    return err;
  }

  srs_error_t TestTaskThread() {
    srs_error_t err = srs_success;
    std::cout << "TestTaskThread main thread 1" << std::endl;
    worker_ = MediaThreadManager::Instance()->CreateTaskThread("task");
    err = worker_->MsgQueue()->Post([this]()->srs_error_t {
      std::cout << "TestTaskThread task thread 2" << std::endl;

      srs_error_t err = srs_success;
      if (srs_success != (err = task_timer_once_.Schedule(this, 5.0, 1))) {
        std::cout << "schedule timer once in task failed desc:" << srs_error_desc(err) << std::endl;
        delete err;
      }

      if (srs_success != (err = task_timer_infinity_.Schedule(this, 2.0))) {
        std::cout << "schedule timer infinity in task failed desc:" << srs_error_desc(err) << std::endl;
        delete err;
      }

      return srs_error_new(ERROR_SUCCESS, "success, ok.");
    });
  
    if (err != srs_success) {
      std::cout << " error occur in task thread, desc:" << srs_error_desc(err) << std::endl;
    }
    std::cout << "TestTaskThread main thread 3" << std::endl;

    return err;
  }

  srs_error_t TestNetThread() {
    srs_error_t err = srs_success;
    std::cout << "TestNetThread main thread 1" << std::endl;
    worker_ = MediaThreadManager::Instance()->CreateNetThread("reactor");

    err = worker_->MsgQueue()->Post([this]()->srs_error_t {
      std::cout << "TestNetThread net thread 2" << std::endl;

      srs_error_t err = srs_success;
      if (srs_success != (err = task_timer_once_.Schedule(this, 5.0, 1))) {
        std::cout << "schedule timer once in net failed desc:" << srs_error_desc(err) << std::endl;
        delete err;
      }

      if (srs_success != (err = task_timer_infinity_.Schedule(this, 2.0))) {
        std::cout << "schedule timer infinity in net failed desc:" << srs_error_desc(err) << std::endl;
        delete err;
      }

      return srs_error_new(ERROR_SUCCESS, "success, ok.");
    });
  
    if (err != srs_success) {
      std::cout << " error occur in net thread, desc:" << srs_error_desc(err) << std::endl;
    }
    std::cout << "TestNetThread main thread 3" << std::endl;

    return err;
  }

  void OnTimer(MediaTimerHelp* id) override {
    static int s_count = 0;
    if (id == &timer_once_) {
      std::cout << "in main OnTimer once, stop worker" << std::endl;
      worker_->Stop();
      worker_->Join();
      worker_->Destroy();
      worker_ = nullptr;
      return ;
    }

    if (id == &timer_infinity_) {
      std::cout << "in main " << ++s_count << ".OnTimer infinity" << std::endl;
    }

    if (id == &task_timer_once_) {
         std::cout << "in task OnTimer once" << std::endl;
    }

    if (id == &task_timer_infinity_) {
      std::cout << "in task " << ++s_count << ".OnTimer infinity" << std::endl;
    }
  }
 private:
  MediaTimerHelp timer_once_;
  MediaTimerHelp timer_infinity_;

  MediaTimerHelp task_timer_once_;
  MediaTimerHelp task_timer_infinity_;
  MediaThread* worker_ = nullptr;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "argc < 2" << std::endl;
    return -1;
  }

  log4cxx::PropertyConfigurator::configureAndWatch(argv[1]); 

  MediaThread* thread = MediaThreadManager::FetchOrCreateMainThread();

  TestMsg test_case;
  test_case.TestMainThread();
  //test_case.TestTaskThread();
  test_case.TestNetThread();

  thread->OnThreadRun();
  thread->Destroy();

  return 0;
}
