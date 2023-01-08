#include "media_thread.h"

#include <sys/prctl.h>
#include <sys/syscall.h>

#include "common/media_log.h"
#include "common/media_kernel_error.h"
#include "media_msg_queue.h"
#include "media_reactor.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

// class MediaNetThread
class MediaNetThread final : public MediaThread
 public:
	MediaNetThread();
	~MediaNetThread() override;

	// interface MediaThread
  srs_error_t Stop() override;
	void OnThreadInit() override;
	void OnThreadRun() override;

  MediaReactor* Reactor() override;
  MediaMsgQueue* MsgQueue() override;
	MediaTimerQueue* TimerQueue() override;
 protected:
	MediaMsgQueueWithMutex msg_queue_;
  CalendarTQ timer_queue_;
  std::unique_ptr<MediaReactor> reactor_ = nullptr;
};

// class MediaTaskThread
class MediaTaskThread final : public MediaThread
 public:
  MediaTaskThread() = default;
  ~MediaTaskThread() override  = default;

  // interface MediaThread
  srs_error_t Stop() override;
  void OnThreadInit() override;
  void OnThreadRun() override;

  MediaReactor* Reactor() override { return nullptr; }
  MediaMsgQueue* MsgQueue() override;
  MediaTimerQueue* TimerQueue() override;
 protected:
  void SetStopFlag() {
    stop_loop_ = true;
  }

  void PopOrWaitPendingMsg(MediaMsgQueueImp::MsgType &msgs, 
      MediaTimeValue *time_out, uint32_t max);

  MediaMsgQueueWithMutex msg_queue_;
	OrderedListTQ timer_queue_;
  bool stop_loop_ = false;
};


pthread_t CurrentThreadRef() {
  return pthread_self();
}

bool IsThreadRefEqual(const pthread_t& a, const pthread_t& b) {
  return pthread_equal(a, b);
}

// class MediaThreadEvent
void MediaThreadEvent::Wait(int* ms) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (!ms) {
    cv_.wait(lk, []{return ready_;});
  } else {
    cv_.wait_for(lk, *ms * 1ms, []{return ready_;});
  }
}

void MediaThreadEvent::Signal() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    ready_ = true;
  }
  cv_.notify_one();
}

// MediaStopMsgT
template <class QueueType>
class MediaStopMsgT : public MediaMsg {
 public:
	MediaStopMsgT(QueueType *q)
		: queue_(q) { }
	
	srs_error_t OnFire() override {
		if (queue_)
			queue_->SetStopFlag();
		return srs_success;
	}

	static srs_error_t PostStopMsg(QueueType *q) {
		MediaStopMsgT<QueueType> msg = new MediaStopMsgT<QueueType>(q);
		return q->MsgQueue()->Post(msg);
	}

private:
	QueueType *queue_;
};

// class MediaThread
void SetCurrentThreadName(const char* name) {
  prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(name));  
}

inline unsigned long int  MediaGetTid() {
	  return syscall(__NR_gettid);
}

void MediaThread::Create(const std::string& name) {
  name_ = name;
  worker_.reset(new std::thread(ThreadProc));

  // Make sure that ThreadManager is created on the main thread before
  // we start a new thread.
  MediaThreadManager::Instance();
}

void MediaThread::Destory() {
  delete this;
}

void MediaThread::Run() {
  event_.Signal();
}

void MediaThread::OnThreadInit() {
  MA_ASSERT(!IsRunning());
  tid_ = MediaGetTid();
  SetCurrentThreadName(name_.c_str());
  handler_ = pthread_self();
  MediaThreadManager::Instance()->SetCurrentThread(this);
}

void MediaThread::ThreadProc(void *param) {
  MediaThread *thread = static_cast<MediaThread*>(param);
  
  thread->event_.Wait();

  thread->OnThreadInit();
  MLOG_INFO_THIS("Thread begin this=" << thread 
    << ", tid=" << thread->tid_
    << ", thread_name=" << thread->GetName()); 

  thread->OnThreadRun();

  MediaThreadManager::Instance()->SetCurrentThread(nullptr);
  handler_ = 0;

  MLOG_INFO_THIS("thread quit ... this="<<thread
    << ", tid=" << thread->tid_ 
    << ", thread_name=" << pThread->GetName());
}

void MediaThread::Join() {
  worker_->join();
}

bool MediaThread::IsStopped() const {
  return stopped_;
}

pthread_t MediaThread::GetThreadHandle() {
  return handler_;
}

unsigned long MediaThread::GetTid() { 
  return tid_; 
}

bool MediaThread::IsRunning() {
  return handler_ != 0;
}

// class MediaTaskThread
srs_error_t MediaTaskThread::Stop() {
  MLOG_INFO_THIS("");
  srs_error_t err = MediaStopMsgT<MediaTaskThread>::PostStopMsg(this);
  if (srs_success != err) {
    return srs_error_wrap(err, "PostStopMsg failed.");
  }
  // stop event queue after post stop event.
  msg_queue_.Stop();
  stopped_ = true;
}

void MediaTaskThread::OnThreadInit() {
  MediaThread::OnThreadInit();
  stopped_ = false;
}

void MediaTaskThread::OnThreadRun() {
  MediaMsgQueueImp::MsgType msgs;
  MediaTimeValue time_out(MediaTimeValue::time_max_);
  MediaTimeValue *tv_para;
  while (!stop_loop_) {
    time_out = MediaTimeValue::time_max_;
    timer_queue_.Check(&time_out);
    if (time_out != MediaTimeValue::time_max_) {
      if (time_out.GetTimeInMsec() == 0) {
        time_out.Set(0, 1000);
      } else if(time_out < MediaTimeValue::time_zero_) {
        time_out = MediaTimeValue::time_zero_;
      }
      tv_para = &time_out;
    }
    else {
      tv_para = nullptr;
    }
    
    msgs.clear();
    PopOrWaitPendingMsg(msgs, tv_para, MediaMsgQueueImp::MAX_GET_ONCE);

    msg_queue_.Process(msgs);
  }

  msg_queue_.DestoryPendingMsgs();
}

void MediaTaskThread::PopOrWaitPendingMsg(MediaMsgQueueImp::MsgType &msgs, 
    MediaTimeValue *time_out, uint32_t max) {

  if (msg_queue_.IsEmpty()) {
      event_.Wait(time_out->GetTimeInMsec());
    }
  }
  
  msg_queue_.PopMsgs(msgs, max);
}

MediaMsgQueue* MediaTaskThread::MsgQueue() {
  return msg_queue_.get()
}

MediaTimerQueue* MediaTaskThread::TimerQueue() {
  return timer_queue_.get();
}

// class MediaNetThread
srs_error_t MediaNetThread::Stop() {
  MLOG_INFO_THIS("");
  reactor_->StopEventLoop();
  stopped_ = true;
}

void MediaNetThread::OnThreadInit() {
  MediaThread::OnThreadInit();

  reactor_.reset(CreateReactor());

  srs_error_t err = reactor_->Open();
  if (srs_success != err) {
    MLOG_ERROR_THIS("reactor open failed! desc=" << srs_error_desc(err));
    delete err;
    MA_ASSERT(false);
  }
  stopped_ = false;
}

void MediaNetThread::OnThreadRun() {
  srs_error_t err = reactor_->RunEventLoop();
  if (srs_success != err) {
    MLOG_ERROR_THIS("reactor RunEventLoop failed! desc=" << srs_error_desc(err));
    delete err;
  }
  err = reactor_->Close();
  if (srs_success != err) {
    MLOG_ERROR_THIS("reactor Close failed! desc=" << srs_error_desc(err));
    delete err;
  }
}

MediaReactor* MediaNetThread::Reactor() {
  return reactor_.get();
}

MediaMsgQueue* MediaNetThread::MsgQueue() {
  return dynamic_cast<MediaMsgQueue>(reactor_.get());
}

MediaTimerQueue* MediaNetThread::TimerQueue() {
  return dynamic_cast<MediaTimerQueue>(reactor_.get());
}

// class MediaThreadManager
MediaThreadManager::MediaThreadManager() 
  : main_thread_ref_(CurrentThreadRef()) {
  pthread_key_create(&key_, nullptr);
}

MediaThread* MediaThreadManager::CreateNetThread(const std::string& name) {
  auto p = new MediaNetThread;
  p->Create(name);
  p->Run();
  return p;
}

MediaThread* MediaThreadManager::CreateTaskThread(const std::string& name) {
  auto p = new MediaTaskThread;
  p->Create(name);
  p->Run();
  return p;
}

MediaThread* MediaThreadManager::CurrentThread() {
  return static_cast<Thread*>(pthread_getspecific(key_));
}

void MediaThreadManager::SetCurrentThread(MediaThread* thread) {
  pthread_setspecific(key_, thread);
}

bool MediaThreadManager::IsMainThread() {
  return IsThreadRefEqual(CurrentThreadRef(), main_thread_ref_);
}

bool MediaThreadManager::IsEqualCurrentThread(MediaThread* pthread) {
  return IsThreadRefEqual(CurrentThreadRef(), pthread->GetThreadHandle());
}

} //namespace ma
