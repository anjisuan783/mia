#include "utils/media_thread.h"

#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common/media_log.h"
#include "common/media_kernel_error.h"
#include "utils/media_msg_queue.h"
#include "utils/media_reactor.h"
#include "common/media_define.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

#define  gettid()   syscall(__NR_gettid)

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
		return q->MsgQueue()->Post(new MediaStopMsgT<QueueType>(q));
	}

private:
	QueueType *queue_;
};

// class MediaNetThread
class MediaNetThread final : public MediaThread {
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
  std::unique_ptr<MediaReactor> reactor_ = nullptr;
};

// class MediaTaskThread
class MediaTaskThread final : public MediaThread {
  friend class MediaStopMsgT<MediaTaskThread>;
 public:
  MediaTaskThread() = default;
  ~MediaTaskThread() override  = default;

  // interface MediaThread
  void Create(const std::string& name, bool) override;
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

NetThreadManager g_netthdmgr;

// class MediaThreadEvent
void MediaThreadEvent::Wait(int* ms) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (!ms) {
    cv_.wait(lk, [this]{return ready_;});
  } else {
    cv_.wait_for(lk, std::chrono::milliseconds(*ms), [this]{return ready_;});
  }
}

void MediaThreadEvent::Signal() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    ready_ = true;
  }
  cv_.notify_one();
}

// class MediaThread
void SetCurrentThreadName(const char* name) {
  prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(name));  
}

inline unsigned long int  MediaGetTid() {
	  return gettid();
}

void MediaThread::Create(const std::string& name, bool wait) {
  name_ = name;
  if (wait) {
    promise_ = std::make_shared<std::promise<void>>();
  }
  worker_.reset(new std::thread(MediaThread::ThreadProc, this));

  // Make sure that ThreadManager is created on the main thread before
  // we start a new thread.
  MediaThreadManager::Instance();
  if (promise_)
    promise_->get_future().wait();
}

MediaThread* MediaThread::Current() {
  return MediaThreadManager::Instance()->CurrentThread();
}

void MediaThread::Destroy() {
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
  thread->OnThreadInit();

  if (thread->promise_) {
    thread->promise_->set_value();
  }
  thread->event_.Wait();

  MLOG_INFO("Thread begin this=" << thread 
    << ", tid=" << thread->tid_
    << ", thread_name=" << thread->GetName()); 

  thread->OnThreadRun();

  MediaThreadManager::Instance()->SetCurrentThread(nullptr);
  thread->handler_ = 0;

  MLOG_INFO("thread quit ... this="<<thread
    << ", tid=" << thread->tid_ 
    << ", thread_name=" << thread->GetName());
}

void MediaThread::Join() {
  worker_->join();
}

bool MediaThread::IsStopped() const {
  return stopped_;
}

bool MediaThread::IsCurrent() {
  return MediaThreadManager::IsEqualCurrentThread(this);
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
void MediaTaskThread::Create(const std::string& name, bool wait) {
  if (name != "main") {
    MediaThread::Create(name, wait);
    return ;
  }

  name_ = name;
  MediaThreadManager::Instance();
}

srs_error_t MediaTaskThread::Stop() {
  MLOG_INFO_THIS("");
  srs_error_t err = MediaStopMsgT<MediaTaskThread>::PostStopMsg(this);
  if (srs_success != err) {
    return srs_error_wrap(err, "PostStopMsg failed.");
  }
  // stop event queue after post stop event.
  msg_queue_.Stop();
  stopped_ = true;
  return err;
}

void MediaTaskThread::OnThreadInit() {
  MediaThread::OnThreadInit();
  msg_queue_.ResetThead();
  timer_queue_.ResetThead();
  stopped_ = false;
}

void MediaTaskThread::OnThreadRun() {
  MediaMsgQueueImp::MsgType msgs;
  MediaTimeValue time_out(MediaTimeValue::time_max_);
  MediaTimeValue *tv_para = nullptr;
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
    } else {
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
    int tv = time_out ? time_out->GetTimeInMsec() : 0;
    event_.Wait(tv? & tv:nullptr);
  }
  
  msg_queue_.PopMsgs(msgs, max);
}

MediaMsgQueue* MediaTaskThread::MsgQueue() {
  return &msg_queue_;
}

MediaTimerQueue* MediaTaskThread::TimerQueue() {
  return &timer_queue_;
}

// class MediaNetThread
MediaNetThread::MediaNetThread()
  : reactor_(CreateReactor()) { }

MediaNetThread::~MediaNetThread() = default;

srs_error_t MediaNetThread::Stop() {
  MLOG_INFO_THIS("stop thread, tid:" << this->GetTid());
  reactor_->StopEventLoop();
  stopped_ = true;

  return srs_success;
}

void MediaNetThread::OnThreadInit() {
  MediaThread::OnThreadInit();
  g_netthdmgr.Register(this);
  srs_error_t err = reactor_->Open();
  if (srs_success != err) {
    MLOG_ERROR_THIS("reactor open failed! desc:" << srs_error_desc(err));
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

  g_netthdmgr.UnRegister(this);
}

MediaReactor* MediaNetThread::Reactor() {
  return reactor_.get();
}

MediaMsgQueue* MediaNetThread::MsgQueue() {
  return reactor_.get();
}

MediaTimerQueue* MediaNetThread::TimerQueue() {
  return reactor_.get();
}

// class MediaThreadManager
static MediaThreadManager* s_instance_ = nullptr;
MediaThread* MediaThreadManager::default_net_thread_ = nullptr;
MediaThread* MediaThreadManager::main_thread_ = nullptr;

MediaThreadManager* MediaThreadManager::Instance() {
  if (!s_instance_) {
    s_instance_ = new MediaThreadManager;
  }
  return s_instance_;
}

MediaThreadManager::MediaThreadManager() 
  : main_thread_ref_(CurrentThreadRef()) {
  pthread_key_create(&key_, nullptr);
}

MediaThread* MediaThreadManager::CreateNetThread(const std::string& name) {
  auto p = new MediaNetThread;
  p->Create(name, true);
  p->Run();
  if (!default_net_thread_)
    default_net_thread_ = p;
  return p;
}

MediaThread* MediaThreadManager::CreateTaskThread(const std::string& name) {
  auto p = new MediaTaskThread;
  p->Create(name, false);
  p->Run();
  return p;
}

MediaThread* MediaThreadManager::FetchOrCreateMainThread() {
  if (!main_thread_) {
    main_thread_ = new MediaTaskThread;
    main_thread_->Create("main", false);
    main_thread_->OnThreadInit();
  }
  return main_thread_;
}

MediaThread* MediaThreadManager::GetDefaultNetThread() {
  if (!default_net_thread_) {
    return CreateNetThread("net");
  }
  return default_net_thread_;
}

MediaThread* MediaThreadManager::CurrentThread() {
  return static_cast<MediaThread*>(pthread_getspecific(key_));
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

// NetThreadManager
struct ThreadInfo {
  ThreadInfo(MediaThread* t) : thread_(t) { }

  MediaThread* thread_;
  iovec iov_[IOV_MAX];
  char io_buffer_[MEDIA_SOCK_IOBUFFER_SIZE];

private:
  ThreadInfo(const ThreadInfo& rhs) = delete;
  ThreadInfo& operator = (const ThreadInfo& rhs) = delete;
};

int NetThreadManager::Register(MediaThread* t) {
  std::lock_guard<std::mutex> guard(mutex_);
  thread_infos_.emplace(t->GetThreadHandle(), std::make_shared<ThreadInfo>(t));
  return 0;
}

int NetThreadManager::UnRegister(MediaThread* t) {
  std::lock_guard<std::mutex> guard(mutex_);
  thread_infos_.erase(t->GetThreadHandle());
  return 0;
}

int NetThreadManager::GetIOBuffer(pthread_t handler, iovec*& iov, char*& iobuffer) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = thread_infos_.find(handler);
  if (it == thread_infos_.end())
    return ERROR_NOT_FOUND;
  
  iov = it->second->iov_;
  iobuffer = it->second->io_buffer_;
  return ERROR_SUCCESS;
}

MediaThread* NetThreadManager::GetLessUsedThread() {
  MediaThread* result = nullptr;
  if (!thread_infos_.empty()) {
    result = thread_infos_.begin()->second->thread_;
  }
  return result;
}

} //namespace ma
